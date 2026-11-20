using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using MaraeEngine.Serialization;
using StubsCollision = MaraeEngine.Collision.Stubs.Collision;
using StubsCollider = MaraeEngine.Collision.Stubs.Collider;

namespace MaraeEngine.Internal
{
    /// <summary>
    /// Classe estática de invocação segura dos métodos de ciclo de vida de <see cref="ScriptBehaviour"/>.
    /// Todas as chamadas são envolvidas em try/catch para evitar que um script com erro crashe a engine.
    /// Mantém um cache GCHandle para evitar que o GC colete instâncias ainda ativas no C++.
    /// </summary>
    public static class ScriptInvoker
    {
        private static readonly Dictionary<IntPtr, GCHandle> s_instances = new();
        private static readonly Dictionary<Type, ConstructorInfo?> s_ctorCache = new();
        private static readonly HashSet<IntPtr> s_failedInstances = new();
        private sealed class ScheduledCall
        {
            public IntPtr Instance = IntPtr.Zero;
            public string MethodName = string.Empty;
            public float Remaining;
            public float RepeatRate;
        }
        private static readonly List<ScheduledCall> s_scheduledCalls = new();

        /// <summary>
        /// Cria uma instância do tipo especificado, injeta a entidade correspondente e retorna
        /// um ponteiro estável (GCHandle) para uso pelo lado C++.
        /// </summary>
        public static unsafe IntPtr CreateScriptInstance(ulong entityHandle, [MarshalAs(UnmanagedType.LPWStr)] string typeName)
        {
            if (string.IsNullOrEmpty(typeName)) return IntPtr.Zero;
            try
            {
                const string fileBindingPrefix = "@marae-script-file:";
                Type? type = typeName.StartsWith(fileBindingPrefix, StringComparison.Ordinal)
                    ? AssemblyManager.ResolveScriptTypeForFile(typeName[fileBindingPrefix.Length..])
                    : ResolveType(typeName);
                if (type == null)
                {
                    NativeDelegates.Log?.Invoke(2,
                        $"[ScriptInvoker] Não encontrei uma classe que herda de ScriptBehaviour no script '{typeName}'. " +
                        "Se o arquivo tiver mais de uma classe de script, deixe o nome de uma delas igual ao arquivo.");
                    return IntPtr.Zero;
                }
                if (!typeof(ScriptBehaviour).IsAssignableFrom(type))
                {
                    NativeDelegates.Log?.Invoke(2, $"[ScriptInvoker] Tipo {typeName} não herda de ScriptBehaviour.");
                    return IntPtr.Zero;
                }

                ConstructorInfo? ctor = GetDefaultCtor(type);
                ScriptBehaviour? instance = ctor == null
                    ? Activator.CreateInstance(type) as ScriptBehaviour
                    : ctor.Invoke(Array.Empty<object?>()) as ScriptBehaviour;

                if (instance == null) return IntPtr.Zero;

                instance.gameObject = new Entity(entityHandle);

                GCHandle gch = GCHandle.Alloc(instance, GCHandleType.Normal);
                IntPtr ptr = GCHandle.ToIntPtr(gch);
                s_instances[ptr] = gch;
                return ptr;
            }
            catch (Exception ex)
            {
                NativeDelegates.Log?.Invoke(3,
                    $"[ScriptInvoker] CreateScriptInstance({typeName}) falhou: {ex}");
                return IntPtr.Zero;
            }
        }

        /// <summary>Libera uma instância de script e chama OnDestroy antes de liberar.</summary>
        public static void DestroyScriptInstance(IntPtr instancePtr)
        {
            if (instancePtr == IntPtr.Zero) return;
            ScriptBehaviour? sb = GetInstance(instancePtr);
            try
            {
                if (sb != null)
                {
                    SafeCallNoReturn(sb, "OnDestroy", static b => b.OnDestroy());
                }
            }
            finally
            {
                s_failedInstances.Remove(instancePtr);
                if (s_instances.Remove(instancePtr, out GCHandle gch))
                {
                    if (gch.IsAllocated) gch.Free();
                }
                s_scheduledCalls.RemoveAll(x => x.Instance == instancePtr);
            }
        }

        /// <summary>Chama <see cref="ScriptBehaviour.OnStart"/> e <see cref="ScriptBehaviour.Start"/> de forma segura.</summary>
        public static int CallOnStart(IntPtr instancePtr)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            return SafeCall(sb, "OnStart", static b => {
                b.OnStart();
                b.Start();
                return 0;
            });
        }

        /// <summary>Chama <see cref="ScriptBehaviour.OnUpdate"/> e <see cref="ScriptBehaviour.Update"/> de forma segura.</summary>
        public static int CallOnUpdate(IntPtr instancePtr, float dt)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            int result = SafeCall(sb, "OnUpdate", b => {
                b.OnUpdate(dt);
                b.Update();
                return 0;
            });
            TickScheduledCalls(dt);
            return result;
        }

        /// <summary>Executa o ciclo fixo dos scripts antes de cada passo da simulação física.</summary>
        public static int CallOnFixedUpdate(IntPtr instancePtr, float fixedDt)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            return SafeCall(sb, "OnFixedUpdate", b => {
                b.OnFixedUpdate(fixedDt);
                b.FixedUpdate();
                return 0;
            });
        }

        internal static void ScheduleInvoke(ScriptBehaviour sb, string methodName, float delay, float repeatRate)
        {
            IntPtr handle = FindHandleOf(sb);
            if (handle == IntPtr.Zero || string.IsNullOrWhiteSpace(methodName)) return;
            s_scheduledCalls.RemoveAll(x => x.Instance == handle && x.MethodName == methodName);
            s_scheduledCalls.Add(new ScheduledCall
            {
                Instance = handle,
                MethodName = methodName,
                Remaining = Math.Max(0.0f, delay),
                RepeatRate = Math.Max(0.0f, repeatRate)
            });
        }

        internal static void CancelInvoke(ScriptBehaviour sb, string? methodName)
        {
            IntPtr handle = FindHandleOf(sb);
            if (handle == IntPtr.Zero) return;
            if (string.IsNullOrWhiteSpace(methodName))
                s_scheduledCalls.RemoveAll(x => x.Instance == handle);
            else
                s_scheduledCalls.RemoveAll(x => x.Instance == handle && x.MethodName == methodName);
        }

        internal static bool IsInvoking(ScriptBehaviour sb, string? methodName)
        {
            IntPtr handle = FindHandleOf(sb);
            return handle != IntPtr.Zero && s_scheduledCalls.Exists(x =>
                x.Instance == handle && (string.IsNullOrWhiteSpace(methodName) || x.MethodName == methodName));
        }

        internal static object? InvokeMethod(ScriptBehaviour sb, string methodName, object?[]? args)
        {
            if (sb == null || string.IsNullOrWhiteSpace(methodName)) return null;
            try
            {
                BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
                object?[] actualArgs = args ?? Array.Empty<object?>();
                MethodInfo? method = sb.GetType().GetMethods(flags)
                    .Where(m => string.Equals(m.Name, methodName, StringComparison.Ordinal) &&
                                !m.IsSpecialName && m.GetParameters().Length == actualArgs.Length)
                    .FirstOrDefault(m => ParametersAccept(m.GetParameters(), actualArgs));
                if (method == null)
                {
                    NativeDelegates.Log?.Invoke(2, $"[ScriptInvoker] Método não encontrado ou argumentos incompatíveis: {sb.GetType().FullName}.{methodName}");
                    return null;
                }
                ParameterInfo[] parameters = method.GetParameters();
                object?[] converted = new object?[actualArgs.Length];
                for (int i = 0; i < actualArgs.Length; ++i)
                    converted[i] = ConvertArgument(actualArgs[i], parameters[i].ParameterType);
                return method.Invoke(sb, converted);
            }
            catch (TargetInvocationException ex)
            {
                LogException(sb, methodName, ex.InnerException ?? ex);
                return null;
            }
            catch (Exception ex)
            {
                LogException(sb, methodName, ex);
                return null;
            }
        }

        /// <summary>Chama <see cref="ScriptBehaviour.OnEnable"/> de forma segura.</summary>
        public static int CallOnEnable(IntPtr instancePtr)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            return SafeCall(sb, "OnEnable", static b => { b.OnEnable(); return 0; });
        }

        /// <summary>Chama <see cref="ScriptBehaviour.OnDisable"/> de forma segura.</summary>
        public static int CallOnDisable(IntPtr instancePtr)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            return SafeCall(sb, "OnDisable", static b => { b.OnDisable(); return 0; });
        }

        /// <summary>Chama <see cref="ScriptBehaviour.OnDestroy"/> de forma segura (sem liberar).</summary>
        public static int CallOnDestroy(IntPtr instancePtr)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            return SafeCall(sb, "OnDestroy", static b => { b.OnDestroy(); return 0; });
        }

        #region Collision safe-call wrappers

        internal static int CallOnCollisionEnter(IntPtr instancePtr, ulong otherEntity,
            Vector3 contact, Vector3 normal, float impulse)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            Entity? other = otherEntity == 0 ? null : new Entity(otherEntity);
            StubsCollision data = new(other, contact, normal, impulse);
            return SafeCall(sb, "OnCollisionEnter", b => { b.OnCollisionEnter(data); return 0; });
        }
        internal static int CallOnCollisionStay(IntPtr instancePtr, ulong otherEntity,
            Vector3 contact, Vector3 normal, float impulse)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            Entity? other = otherEntity == 0 ? null : new Entity(otherEntity);
            StubsCollision data = new(other, contact, normal, impulse);
            return SafeCall(sb, "OnCollisionStay", b => { b.OnCollisionStay(data); return 0; });
        }
        internal static int CallOnCollisionExit(IntPtr instancePtr, ulong otherEntity,
            Vector3 contact, Vector3 normal, float impulse)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            Entity? other = otherEntity == 0 ? null : new Entity(otherEntity);
            StubsCollision data = new(other, contact, normal, impulse);
            return SafeCall(sb, "OnCollisionExit", b => { b.OnCollisionExit(data); return 0; });
        }
        internal static int CallOnTriggerEnter(IntPtr instancePtr, ulong otherEntity)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            Entity? other = otherEntity == 0 ? null : new Entity(otherEntity);
            StubsCollider c = new(other);
            return SafeCall(sb, "OnTriggerEnter", b => { b.OnTriggerEnter(c); return 0; });
        }
        internal static int CallOnTriggerStay(IntPtr instancePtr, ulong otherEntity)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            Entity? other = otherEntity == 0 ? null : new Entity(otherEntity);
            StubsCollider c = new(other);
            return SafeCall(sb, "OnTriggerStay", b => { b.OnTriggerStay(c); return 0; });
        }
        internal static int CallOnTriggerExit(IntPtr instancePtr, ulong otherEntity)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            Entity? other = otherEntity == 0 ? null : new Entity(otherEntity);
            StubsCollider c = new(other);
            return SafeCall(sb, "OnTriggerExit", b => { b.OnTriggerExit(c); return 0; });
        }

        #endregion

        #region UI safe-call wrappers

        internal static int CallOnUIClicked(IntPtr instancePtr, ulong entityId)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            UIElement element = new(new Entity(entityId));
            return SafeCall(sb, "OnUIClicked", b => { b.OnUIClicked(element); return 0; });
        }

        internal static int CallOnUIValueChanged(IntPtr instancePtr, ulong entityId, float value)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            UIElement element = new(new Entity(entityId));
            return SafeCall(sb, "OnUIValueChanged", b => { b.OnUIValueChanged(element, value); return 0; });
        }

        internal static int CallOnUITextChanged(IntPtr instancePtr, ulong entityId, string? text)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            UIElement element = new(new Entity(entityId));
            string value = text ?? string.Empty;
            return SafeCall(sb, "OnUITextChanged", b => { b.OnUITextChanged(element, value); return 0; });
        }

        #endregion

        /// <summary>Extrai os valores dos campos serializáveis como JSON string.</summary>
        public static IntPtr GetPublicFieldsJson(IntPtr instancePtr)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return IntPtr.Zero;
            try
            {
                string json = TypeBinder.ExtractPublicFieldsAsJson(sb);
                return Marshal.StringToHGlobalUni(json);
            }
            catch (Exception ex)
            {
                NativeDelegates.Log?.Invoke(2,
                    $"[ScriptInvoker] GetPublicFieldsJson falhou: {ex.Message}");
                return Marshal.StringToHGlobalUni("{}");
            }
        }

        /// <summary>Aplica um valor JSON a um campo nomeado.</summary>
        public static int SetFieldJson(IntPtr instancePtr,
            [MarshalAs(UnmanagedType.LPWStr)] string fieldName,
            [MarshalAs(UnmanagedType.LPWStr)] string jsonValue)
        {
            ScriptBehaviour? sb = GetInstance(instancePtr);
            if (sb == null) return -1;
            try
            {
                bool ok = TypeBinder.ApplySingleFieldFromJson(sb, fieldName, jsonValue);
                return ok ? 0 : 1;
            }
            catch (Exception ex)
            {
                NativeDelegates.Log?.Invoke(2,
                    $"[ScriptInvoker] SetFieldJson({fieldName}) falhou: {ex.Message}");
                return -2;
            }
        }

        #region Private helpers

        private static int SafeCall(ScriptBehaviour sb, string methodName, Func<ScriptBehaviour, int> fn)
        {
            if (sb == null) return -1;
            IntPtr key = FindHandleOf(sb);
            if (key != IntPtr.Zero && s_failedInstances.Contains(key)) return 1;
            if (ShouldBreakAtDebugger(sb, methodName)) return 1;
            try { return fn(sb); }
            catch (Exception ex)
            {
                LogException(sb, methodName, ex);
                if (key != IntPtr.Zero) s_failedInstances.Add(key);
                return -99;
            }
        }

        private static void SafeCallNoReturn(ScriptBehaviour sb, string methodName, Action<ScriptBehaviour> fn)
        {
            try { fn(sb); }
            catch (Exception ex) { LogException(sb, methodName, ex); }
        }

        private static void TickScheduledCalls(float dt)
        {
            if (s_scheduledCalls.Count == 0) return;
            ScheduledCall[] snapshot = s_scheduledCalls.ToArray();
            foreach (ScheduledCall call in snapshot)
            {
                if (!s_scheduledCalls.Contains(call)) continue;
                call.Remaining -= Math.Max(0.0f, dt);
                if (call.Remaining > 0.0f) continue;
                ScriptBehaviour? sb = GetInstance(call.Instance);
                if (sb != null) InvokeMethod(sb, call.MethodName, Array.Empty<object?>());
                if (call.RepeatRate > 0.0f && sb != null)
                    call.Remaining = call.RepeatRate;
                else
                    s_scheduledCalls.Remove(call);
            }
        }

        private static bool ParametersAccept(ParameterInfo[] parameters, object?[] args)
        {
            for (int i = 0; i < parameters.Length; ++i)
            {
                object? value = args[i];
                Type target = parameters[i].ParameterType;
                if (value == null)
                {
                    if (target.IsValueType && Nullable.GetUnderlyingType(target) == null) return false;
                    continue;
                }
                if (!target.IsInstanceOfType(value) && !CanConvert(value, target)) return false;
            }
            return true;
        }

        private static bool CanConvert(object value, Type target)
        {
            try
            {
                _ = Convert.ChangeType(value, Nullable.GetUnderlyingType(target) ?? target, CultureInfo.InvariantCulture);
                return true;
            }
            catch { return false; }
        }

        private static object? ConvertArgument(object? value, Type target)
        {
            if (value == null) return null;
            if (target.IsInstanceOfType(value)) return value;
            if (target.IsEnum) return Enum.Parse(target, value.ToString() ?? string.Empty, true);
            return Convert.ChangeType(value, Nullable.GetUnderlyingType(target) ?? target, CultureInfo.InvariantCulture);
        }

        private static void LogException(ScriptBehaviour behaviour, string method, Exception ex)
        {
            string variablesJson = "{}";
            try { variablesJson = TypeBinder.ExtractPublicFieldsAsJson(behaviour); }
            catch { /* o diagnóstico não pode substituir a exceção original */ }

            try
            {
                NativeDelegates.ScriptDebugReportException?.Invoke(
                    behaviour.gameObject?.id ?? 0UL,
                    behaviour.GetType().FullName ?? behaviour.GetType().Name,
                    method,
                    ex.Message,
                    ex.StackTrace ?? string.Empty,
                    variablesJson);
            }
            catch { /* manter o isolamento do script */ }

            StringBuilder builder = new();
            builder.Append(CultureInfo.InvariantCulture,
                $"[ScriptException] {behaviour.GetType().FullName}.{method}: {ex.Message}");
            if (!string.IsNullOrWhiteSpace(ex.StackTrace))
                builder.Append(CultureInfo.InvariantCulture, $"\n{ex.StackTrace}");
            NativeDelegates.Log?.Invoke(3, builder.ToString());
        }

        private static bool ShouldBreakAtDebugger(ScriptBehaviour behaviour, string method)
        {
            try
            {
                bool paused = NativeDelegates.ScriptDebugShouldBreak?.Invoke(
                    behaviour.gameObject?.id ?? 0UL,
                    behaviour.GetType().FullName ?? behaviour.GetType().Name,
                    method) == true;
                if (!paused) return false;
                string variablesJson = "{}";
                try { variablesJson = TypeBinder.ExtractPublicFieldsAsJson(behaviour); }
                catch { /* manter o breakpoint mesmo se um campo não serializar */ }
                NativeDelegates.ScriptDebugReportBreakpoint?.Invoke(
                    behaviour.gameObject?.id ?? 0UL,
                    behaviour.GetType().FullName ?? behaviour.GetType().Name,
                    method,
                    variablesJson);
                return true;
            }
            catch
            {
                return false;
            }
        }

        private static ScriptBehaviour? GetInstance(IntPtr ptr)
        {
            if (ptr == IntPtr.Zero) return null;
            try
            {
                if (s_instances.TryGetValue(ptr, out GCHandle gch) && gch.IsAllocated)
                    return gch.Target as ScriptBehaviour;
            }
            catch { /* ignorar */ }
            return null;
        }

        private static IntPtr FindHandleOf(ScriptBehaviour sb)
        {
            foreach (KeyValuePair<IntPtr, GCHandle> kv in s_instances)
            {
                if (kv.Value.IsAllocated && ReferenceEquals(kv.Value.Target, sb)) return kv.Key;
            }
            return IntPtr.Zero;
        }

        private static ConstructorInfo? GetDefaultCtor(Type t)
        {
            if (s_ctorCache.TryGetValue(t, out ConstructorInfo? cached)) return cached;
            ConstructorInfo? ctor = t.GetConstructor(
                BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
                binder: null, types: Type.EmptyTypes, modifiers: null);
            s_ctorCache[t] = ctor;
            return ctor;
        }

        private static Type? ResolveType(string typeName)
        {
            if (string.IsNullOrWhiteSpace(typeName)) return null;

            // 1. Tentar diretamente
            Type? t = Type.GetType(typeName, throwOnError: false);
            if (t != null) return t;

            // 2. Se o assembly de scripts compilados estiver disponível, procurar nele primeiro
            if (AssemblyManager.ScriptsAssembly != null)
            {
                t = AssemblyManager.ScriptsAssembly.GetType(typeName, throwOnError: false);
                if (t != null) return t;

                try
                {
                    foreach (Type candidate in AssemblyManager.ScriptsAssembly.GetTypes())
                    {
                        if (string.Equals(candidate.Name, typeName, StringComparison.OrdinalIgnoreCase) ||
                            string.Equals(candidate.FullName, typeName, StringComparison.OrdinalIgnoreCase))
                            return candidate;
                    }
                }
                catch { /* ignorar */ }
            }

            // 3. Procurar em todos os assemblies carregados no AppDomain
            foreach (Assembly asm in AppDomain.CurrentDomain.GetAssemblies())
            {
                t = asm.GetType(typeName, throwOnError: false);
                if (t != null) return t;

                try
                {
                    foreach (Type candidate in asm.GetTypes())
                    {
                        if (string.Equals(candidate.Name, typeName, StringComparison.OrdinalIgnoreCase) ||
                            string.Equals(candidate.FullName, typeName, StringComparison.OrdinalIgnoreCase))
                            return candidate;
                    }
                }
                catch { /* ignorar assemblies sem permissão de reflexão */ }
            }
            string loadedScriptTypes = AssemblyManager.ScriptsAssembly == null
                ? "<nenhum assembly de scripts carregado>"
                : "<erro ao enumerar tipos>";
            try
            {
                if (AssemblyManager.ScriptsAssembly != null)
                    loadedScriptTypes = string.Join(", ", AssemblyManager.ScriptsAssembly.GetTypes()
                        .Where(candidate => typeof(ScriptBehaviour).IsAssignableFrom(candidate))
                        .Select(candidate => candidate.FullName));
            }
            catch (Exception ex)
            {
                loadedScriptTypes = "<" + ex.GetType().Name + ": " + ex.Message + ">";
            }
            NativeDelegates.Log?.Invoke(2,
                $"[ScriptInvoker] Tipo '{typeName}' não resolvido. Scripts carregados: {loadedScriptTypes}");
            return null;
        }

        #endregion
    }
}
