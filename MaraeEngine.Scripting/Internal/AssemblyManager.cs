using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Emit;
using MaraeEngine.Serialization;

namespace MaraeEngine.Internal
{
    /// <summary>
    /// Contexto de carga de assembly colecionável usado para hot-reload. Ao invés de descarregar
    /// domínios inteiros, descarregamos apenas este ALC (mantendo a engine intacta).
    /// </summary>
    internal sealed class CollectibleLoadContext : AssemblyLoadContext
    {
        private readonly AssemblyDependencyResolver? _resolver;

        /// <summary>Cria um novo ALC coletável.</summary>
        public CollectibleLoadContext(string? assemblyPath = null)
            : base(isCollectible: true)
        {
            if (!string.IsNullOrWhiteSpace(assemblyPath) && File.Exists(assemblyPath))
                _resolver = new AssemblyDependencyResolver(assemblyPath);
        }

        /// <inheritdoc/>
        protected override Assembly? Load(AssemblyName assemblyName)
        {
            // Os scripts precisam herdar exatamente o ScriptBehaviour já usado
            // pelo host. Retornar null dependia do probing do ALC Default, que
            // falha quando a engine carrega esta DLL por um caminho externo ao
            // diretório do executável; o assembly já carregado deve ser
            // compartilhado explicitamente com o contexto coletável.
            Assembly scriptingAssembly = typeof(ScriptBehaviour).Assembly;
            if (string.Equals(assemblyName.Name, scriptingAssembly.GetName().Name,
                              StringComparison.OrdinalIgnoreCase))
                return scriptingAssembly;
            if (_resolver == null) return null;
            string? path = _resolver.ResolveAssemblyToPath(assemblyName);
            return path != null ? LoadFromAssemblyPath(path) : null;
        }

        /// <inheritdoc/>
        protected override IntPtr LoadUnmanagedDll(string unmanagedDllName)
        {
            if (_resolver == null) return IntPtr.Zero;
            string? path = _resolver.ResolveUnmanagedDllToPath(unmanagedDllName);
            return path != null ? LoadUnmanagedDllFromPath(path) : IntPtr.Zero;
        }
    }

    /// <summary>
    /// Responsável por compilar dinamicamente todos os arquivos .cs de uma pasta usando Roslyn,
    /// carregar o assembly resultante em um <see cref="CollectibleLoadContext"/> e providenciar
    /// os hooks necessários para hot-reload.
    /// </summary>
    public static class AssemblyManager
    {
        private static CollectibleLoadContext? s_currentAlc;
        private static Assembly? s_scriptsAssembly;
        private static Dictionary<string, string[]> s_scriptTypesByPath = new(StringComparer.OrdinalIgnoreCase);
        private static readonly List<MetadataReference> s_sharedReferences = new();

        private static readonly CSharpCompilationOptions CompilationOpts =
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary)
                .WithOptimizationLevel(OptimizationLevel.Debug)
                .WithAllowUnsafe(true)
                .WithNullableContextOptions(NullableContextOptions.Enable)
                .WithOverflowChecks(false);

        /// <summary>Assembly de scripts atualmente carregado (null se nenhum).</summary>
        public static Assembly? ScriptsAssembly => s_scriptsAssembly;

        /// <summary>
        /// Compila todos os arquivos *.cs em <paramref name="scriptsFolder"/> com referências a
        /// <c>MaraeEngine.Scripting.dll</c> e às referências padrão net8.0.
        /// Retorna 0 se deu certo, 1 se aviso, != 0 se erro.
        /// </summary>
        public static int CompileScriptsFolder(
            [MarshalAs(UnmanagedType.LPWStr)] string scriptsFolder,
            [MarshalAs(UnmanagedType.LPWStr)] string? outputFolder,
            out IntPtr errors,
            out int errorCount)
        {
            errors = IntPtr.Zero;
            errorCount = 0;
            try
            {
                if (!Directory.Exists(scriptsFolder))
                {
                    errors = Marshal.StringToHGlobalUni($"Pasta de scripts não encontrada: {scriptsFolder}");
                    errorCount = 1;
                    return -1;
                }
                // Quando a entrada é uma raiz de projeto, prefira o padrão
                // Assets/**. O diretório Scripts/** na raiz é legado e só deve
                // ser usado como fallback: compilar os dois ao mesmo tempo
                // duplica classes comuns (por exemplo, CameraWASD) e invalida
                // todo o assembly do projeto.
                string assetsFolder = Path.Combine(scriptsFolder, "Assets");
                string projectScriptsFolder = Path.Combine(scriptsFolder, "Scripts");
                List<string> sourceFolders = new();
                if (Directory.Exists(assetsFolder))
                {
                    string[] assetSources = Directory
                        .EnumerateFiles(assetsFolder, "*.cs", SearchOption.AllDirectories)
                        .ToArray();
                    if (assetSources.Length > 0)
                    {
                        sourceFolders.Add(assetsFolder);
                        if (Directory.Exists(projectScriptsFolder))
                        {
                            NativeDelegates.Log?.Invoke(1,
                                $"[AssemblyManager] Scripts da raiz ignorados porque Assets já contém scripts. " +
                                $"Use apenas '{assetsFolder}' (Scripts legado: '{projectScriptsFolder}').");
                        }
                    }
                    else if (Directory.Exists(projectScriptsFolder))
                    {
                        sourceFolders.Add(projectScriptsFolder);
                    }
                    else
                    {
                        // Mantém suporte a projetos cujo diretório passado já
                        // seja Assets ou uma pasta Scripts específica.
                        sourceFolders.Add(assetsFolder);
                    }
                }
                else if (Directory.Exists(projectScriptsFolder))
                {
                    sourceFolders.Add(projectScriptsFolder);
                }
                else
                {
                    sourceFolders.Add(scriptsFolder);
                }

                string[] files = sourceFolders
                    .SelectMany(folder => Directory.EnumerateFiles(folder, "*.cs", SearchOption.AllDirectories))
                    .Distinct(StringComparer.OrdinalIgnoreCase)
                    .ToArray();
                if (files.Length == 0)
                {
                    return 0;
                }

                List<SyntaxTree> trees = new();
                foreach (string f in files)
                {
                    string text = File.ReadAllText(f, Encoding.UTF8);
                    SyntaxTree tree = CSharpSyntaxTree.ParseText(text,
                        path: f,
                        options: new CSharpParseOptions(LanguageVersion.Latest,
                            preprocessorSymbols: new[] { "DEBUG", "NET8_0_OR_GREATER" }),
                        encoding: Encoding.UTF8);
                    trees.Add(tree);
                }

                List<MetadataReference> references = CollectReferences();

                string assemblyName = $"MaraeEngine.UserScripts.{Guid.NewGuid():N}";
                CSharpCompilation compilation = CSharpCompilation.Create(
                    assemblyName,
                    trees,
                    references,
                    CompilationOpts);

                Dictionary<string, HashSet<string>> scriptTypesByPath = new(StringComparer.OrdinalIgnoreCase);
                foreach (SyntaxTree tree in trees)
                {
                    SemanticModel semanticModel = compilation.GetSemanticModel(tree);
                    foreach (ClassDeclarationSyntax declaration in tree.GetRoot()
                        .DescendantNodes().OfType<ClassDeclarationSyntax>())
                    {
                        if (semanticModel.GetDeclaredSymbol(declaration) is not INamedTypeSymbol symbol ||
                            symbol.IsAbstract || !InheritsScriptBehaviour(symbol))
                            continue;

                        string sourcePath = Path.GetFullPath(tree.FilePath);
                        if (!scriptTypesByPath.TryGetValue(sourcePath, out HashSet<string>? typeNames))
                        {
                            typeNames = new HashSet<string>(StringComparer.Ordinal);
                            scriptTypesByPath[sourcePath] = typeNames;
                        }
                        typeNames.Add(symbol.ToDisplayString(SymbolDisplayFormat.CSharpErrorMessageFormat));
                    }
                }

                using MemoryStream ms = new();
                using MemoryStream pdbMs = new();
                EmitResult emitResult = compilation.Emit(
                    peStream: ms,
                    pdbStream: pdbMs,
                    options: new EmitOptions(debugInformationFormat: DebugInformationFormat.PortablePdb));

                StringBuilder errorsBuf = new();
                int sevCount = 0;
                foreach (Diagnostic d in emitResult.Diagnostics)
                {
                    if (d.Severity == DiagnosticSeverity.Error) sevCount++;
                    if (d.Severity == DiagnosticSeverity.Error || d.Severity == DiagnosticSeverity.Warning)
                    {
                        var span = d.Location.GetLineSpan();
                        string source = span.Path ?? string.Empty;
                        int line = span.StartLinePosition.Line + 1;
                        int column = span.StartLinePosition.Character + 1;
                        errorsBuf.Append(CultureInfo.InvariantCulture,
                            $"{source}({line},{column}): {d.Severity.ToString().ToLowerInvariant()} {d.Id}: {d.GetMessage(CultureInfo.InvariantCulture)}\n");
                    }
                }

                if (!emitResult.Success)
                {
                    errors = Marshal.StringToHGlobalUni(errorsBuf.ToString());
                    errorCount = sevCount;
                    NativeDelegates.Log?.Invoke(3,
                        $"[AssemblyManager] {sevCount} erro(s) na compilação de scripts:\n{errorsBuf}");
                    return -2;
                }

                if (errorsBuf.Length > 0)
                {
                    NativeDelegates.Log?.Invoke(1,
                        $"[AssemblyManager] Warnings:\n{errorsBuf}");
                }

                ms.Position = 0;
                pdbMs.Position = 0;

                UnloadCurrent();
                s_currentAlc = new CollectibleLoadContext();
                s_scriptsAssembly = s_currentAlc.LoadFromStream(ms, pdbMs);
                s_scriptTypesByPath = scriptTypesByPath.ToDictionary(
                    entry => entry.Key,
                    entry => entry.Value.OrderBy(name => name, StringComparer.Ordinal).ToArray(),
                    StringComparer.OrdinalIgnoreCase);

                if (!string.IsNullOrWhiteSpace(outputFolder))
                {
                    try
                    {
                        Directory.CreateDirectory(outputFolder);
                        string dllPath = Path.Combine(outputFolder, assemblyName + ".dll");
                        File.WriteAllBytes(dllPath, ms.ToArray());
                    }
                    catch (Exception ex)
                    {
                        NativeDelegates.Log?.Invoke(2,
                            $"[AssemblyManager] Falhou ao gravar assembly: {ex.Message}");
                    }
                }

                return 0;
            }
            catch (Exception ex)
            {
                errors = Marshal.StringToHGlobalUni(ex.ToString());
                errorCount = 1;
                NativeDelegates.Log?.Invoke(3, $"[AssemblyManager] CompileScriptsFolder crash: {ex}");
                return -3;
            }
        }

        /// <summary>
        /// Hook de conveniência chamado pelo Runtime nativo: compila + recarrega assembly e
        /// retorna 0 em sucesso.
        /// </summary>
        public static int ReloadScripts(
            [MarshalAs(UnmanagedType.LPWStr)] string scriptsFolder,
            [MarshalAs(UnmanagedType.LPWStr)] string outputFolder)
        {
            int result = CompileScriptsFolder(scriptsFolder, outputFolder, out IntPtr errorsPtr, out _);
            if (errorsPtr != IntPtr.Zero) Marshal.FreeHGlobal(errorsPtr);
            return result;
        }

        /// <summary>
        /// Descobre todos os ScriptBehaviour no assembly atualmente carregado.
        /// </summary>
        public static IReadOnlyList<Type> DiscoverScriptBehaviourTypes()
        {
            return s_scriptsAssembly == null
                ? Array.Empty<Type>()
                : TypeBinder.DiscoverScriptBehaviourTypes(s_scriptsAssembly);
        }

        /// <summary>
        /// Resolve a ScriptBehaviour using the source file it came from. This lets users
        /// attach a .cs asset without requiring its filename to match its class name.
        /// </summary>
        public static Type? ResolveScriptTypeForFile(string scriptPath)
        {
            if (string.IsNullOrWhiteSpace(scriptPath) || s_scriptsAssembly == null)
                return null;

            string normalizedPath;
            try
            {
                normalizedPath = Path.GetFullPath(scriptPath);
            }
            catch
            {
                return null;
            }

            if (!s_scriptTypesByPath.TryGetValue(normalizedPath, out string[]? candidates) ||
                candidates.Length == 0)
                return null;

            // Most script files declare one behaviour. If a file contains several,
            // prefer the type matching the filename; otherwise report ambiguity.
            string fileStem = Path.GetFileNameWithoutExtension(normalizedPath);
            string? selectedName = candidates.Length == 1
                ? candidates[0]
                : candidates.FirstOrDefault(name =>
                    string.Equals(name[(name.LastIndexOf('.') + 1)..], fileStem,
                        StringComparison.OrdinalIgnoreCase));
            return selectedName == null
                ? null
                : s_scriptsAssembly.GetType(selectedName, throwOnError: false);
        }

        private static bool InheritsScriptBehaviour(INamedTypeSymbol type)
        {
            for (INamedTypeSymbol? current = type.BaseType; current != null; current = current.BaseType)
            {
                if (string.Equals(current.ToDisplayString(), "MaraeEngine.ScriptBehaviour",
                    StringComparison.Ordinal))
                    return true;
            }
            return false;
        }

        /// <summary>Descarrega o assembly de scripts atual (se houver), permitindo hot-reload.</summary>
        public static void UnloadCurrent()
        {
            try
            {
                s_scriptsAssembly = null;
                s_scriptTypesByPath.Clear();
                if (s_currentAlc != null)
                {
                    WeakReference alcRef = new(s_currentAlc);
                    s_currentAlc.Unload();
                    s_currentAlc = null;
                    for (int i = 0; i < 10 && alcRef.IsAlive; i++)
                    {
                        GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, true, true);
                        GC.WaitForPendingFinalizers();
                    }
                }
            }
            catch (Exception ex)
            {
                NativeDelegates.Log?.Invoke(2, $"[AssemblyManager] UnloadCurrent warning: {ex.Message}");
            }
        }

        private static List<MetadataReference> CollectReferences()
        {
            if (s_sharedReferences.Count == 0)
            {
                try
                {
                    // Roslyn não herda automaticamente as referências do host .NET quando
                    // compila em memória. Em instalações modernas, Location de CoreLib pode
                    // ser vazio; o TPA é a fonte confiável de todos os assemblies da runtime.
                    HashSet<string> paths = new(StringComparer.OrdinalIgnoreCase);
                    string? trustedPlatformAssemblies = AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") as string;
                    if (!string.IsNullOrWhiteSpace(trustedPlatformAssemblies))
                    {
                        foreach (string path in trustedPlatformAssemblies.Split(Path.PathSeparator))
                        {
                            if (File.Exists(path)) paths.Add(path);
                        }
                    }

                    string? scriptingPath = SafeLocation(typeof(ScriptBehaviour).Assembly);
                    if (scriptingPath != null) paths.Add(scriptingPath);

                    foreach (string path in paths)
                    {
                        try { s_sharedReferences.Add(MetadataReference.CreateFromFile(path)); }
                        catch (Exception ex)
                        {
                            NativeDelegates.Log?.Invoke(1,
                                $"[AssemblyManager] Referência ignorada '{path}': {ex.Message}");
                        }
                    }

                    NativeDelegates.Log?.Invoke(0,
                        $"[AssemblyManager] Referências Roslyn carregadas: {s_sharedReferences.Count}");
                }
                catch (Exception ex)
                {
                    NativeDelegates.Log?.Invoke(2,
                        $"[AssemblyManager] Algumas referências não foram resolvidas: {ex.Message}");
                }
            }

            return new List<MetadataReference>(s_sharedReferences);
        }

        private static string? SafeLocation(Assembly a)
        {
            try { return !string.IsNullOrWhiteSpace(a.Location) && File.Exists(a.Location) ? a.Location : null; }
            catch { return null; }
        }
    }
}
