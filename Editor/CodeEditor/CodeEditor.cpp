#include "../../Engine/Localization/Localization.h"
#include "CodeEditor.h"
#include "ScriptGuide.h"
#include "../../Engine/Localization/Localization.h"

#include <imgui.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Helpers internos
// ---------------------------------------------------------------------------

static const ImVec4 COLOR_ERROR   = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
static const ImVec4 COLOR_WARNING = ImVec4(1.0f, 0.85f, 0.2f,  1.0f);
static const ImVec4 COLOR_OK      = ImVec4(0.4f, 1.0f,  0.4f,  1.0f);
static const ImVec4 COLOR_INFO    = ImVec4(0.6f, 0.8f,  1.0f,  1.0f);

// ---------------------------------------------------------------------------
// Construtor
// ---------------------------------------------------------------------------
CodeEditor::CodeEditor()
{
    m_Editor.SetLanguageDefinition(BuildCSharpLanguage());
    m_Editor.SetPalette(BuildPalette());
    m_Editor.SetTabSize(4);
    m_Editor.SetShowWhitespaces(false);
}

// ---------------------------------------------------------------------------
// Open
// ---------------------------------------------------------------------------
bool CodeEditor::Open(const std::string& filePath)
{
    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open())
        return false;

    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

    m_FilePath = filePath;
    m_Editor.SetText(content);
    m_Dirty = false;
    RunDiagnostics();
    return true;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------
bool CodeEditor::Save()
{
    if (m_FilePath.empty())
        return false;

    std::ofstream out(m_FilePath, std::ios::binary);
    if (!out.is_open())
        return false;

    out << m_Editor.GetText();
    m_Dirty = false;
    RunDiagnostics();
    return true;
}

// ---------------------------------------------------------------------------
// RenderWindow — janela completa flutuante
// ---------------------------------------------------------------------------
void CodeEditor::RenderWindow(const char* aTitle, bool* pOpen)
{
    // ImGui precisa receber Begin/End de cada janela dockada em todo frame,
    // mesmo quando sua aba não está selecionada. A documentação é uma janela
    // irmã do editor; renderize-a antes do Begin do editor para que o retorno
    // antecipado de uma aba inativa não a remova do DockSpace.
    ScriptGuide::Render(&m_ShowScriptGuide);

    ImGui::SetNextWindowSize(ImVec2(900.0f, 650.0f), ImGuiCond_Appearing);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (m_Dirty)
        flags |= ImGuiWindowFlags_UnsavedDocument; // asterisco no título

    if (!ImGui::Begin(aTitle, pOpen, flags))
    {
        ImGui::End();
        return;
    }

    RenderToolbar();
    ImGui::Separator();

    // Captura Ctrl+S para salvar
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
        ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_S))
    {
        Save();
    }

    // Área do editor — reserva espaço para o painel de diagnóstico embaixo
    const float diagHeight = (m_DiagEntries.empty() ? 28.0f : 140.0f);
    const float editorHeight = ImGui::GetContentRegionAvail().y - diagHeight - 6.0f;

    m_Editor.Render("##code", ImVec2(-1.0f, editorHeight));

    // Atualiza dirty flag quando o usuário edita
    if (m_Editor.IsTextChanged())
        m_Dirty = true;

    ImGui::Separator();
    RenderDiagnosticsPanel();

    ImGui::End();
}

// ---------------------------------------------------------------------------
// RenderToolbar
// ---------------------------------------------------------------------------
void CodeEditor::RenderToolbar()
{
    if (m_CompileCallback)
    {
        if (ImGui::Button(UI("Compilar scripts C#")))
        {
            m_CompilerOutput = m_CompileCallback();
            m_CompilerOutputBuffer.assign(m_CompilerOutput.begin(), m_CompilerOutput.end());
            m_CompilerOutputBuffer.push_back('\0');
            RunDiagnostics();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(UI("Executa a compilação real do projeto com Roslyn e mostra o resultado abaixo."));
        ImGui::SameLine();
    }

    if (ImGui::Button(UI("Guia C#")))
        m_ShowScriptGuide = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(UI("Referência completa da API de scripts da Marae Engine."));
    ImGui::SameLine();
    // Botão Salvar
    const bool canSave = m_Dirty && !m_FilePath.empty();
    if (!canSave)
        ImGui::BeginDisabled();

    if (ImGui::Button((std::string("  ") + L("common.save") + "  ").c_str()) || (canSave && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)))
        Save();

    if (!canSave)
        ImGui::EndDisabled();

    ImGui::SameLine();

    // Abrir no VS Code
    if (ImGui::Button(L("component.open_vscode")))
    {
        if (!m_FilePath.empty())
        {
            std::string cmd = "code --reuse-window \"" + m_FilePath + "\"";
            std::system(cmd.c_str());
        }
    }

    ImGui::SameLine();

    // Undo / Redo
    if (!m_Editor.CanUndo()) ImGui::BeginDisabled();
    if (ImGui::Button((std::string(" ") + L("common.undo") + " ").c_str())) m_Editor.Undo();
    if (!m_Editor.CanUndo()) ImGui::EndDisabled();

    ImGui::SameLine();

    if (!m_Editor.CanRedo()) ImGui::BeginDisabled();
    if (ImGui::Button((std::string(" ") + L("common.redo") + " ").c_str())) m_Editor.Redo();
    if (!m_Editor.CanRedo()) ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    // Dropdown de Templates
    if (ImGui::BeginCombo("  Template  ", "Inserir..."))
    {
        auto templates = GetAvailableTemplates();
        for (const auto& templ : templates)
        {
            if (ImGui::Selectable(templ.c_str()))
            {
                InsertTemplate(templ);
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();

    // Diagnóstico rápido
    if (m_ErrorCount > 0)
        ImGui::TextColored(COLOR_ERROR, "  %d erro%s", m_ErrorCount, m_ErrorCount > 1 ? "s" : "");
    else if (m_WarningCount > 0)
        ImGui::TextColored(COLOR_WARNING, "  %d aviso%s", m_WarningCount, m_WarningCount > 1 ? "s" : "");
    else
        ImGui::TextColored(COLOR_OK, "  %s", L("code.no_errors"));

    ImGui::SameLine();

    // Nome do arquivo e cursor
    if (!m_FilePath.empty())
    {
        std::string fname = std::filesystem::path(m_FilePath).filename().string();
        if (m_Dirty) fname += " *";
        ImGui::SetNextItemWidth(200.0f);
        ImGui::TextColored(COLOR_INFO, "  %s", fname.c_str());

        ImGui::SameLine();
        auto pos = m_Editor.GetCursorPosition();
        ImGui::TextDisabled("  %s %d, Col %d", L("code.line"), pos.mLine + 1, pos.mColumn + 1);
    }
}

// ---------------------------------------------------------------------------
// RenderDiagnosticsPanel
// ---------------------------------------------------------------------------
void CodeEditor::RenderDiagnosticsPanel()
{
    if (!m_CompilerOutput.empty())
    {
        const bool failed = m_CompilerOutput.rfind("ERROS DE COMPILAÇÃO:", 0) == 0;
        ImGui::TextDisabled(UI("Saída da compilação — selecione o texto ou copie tudo:"));
        ImGui::SameLine();
        if (ImGui::SmallButton(UI("Copiar saída##CompilerOutput")))
            ImGui::SetClipboardText(m_CompilerOutput.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, failed ? COLOR_ERROR : COLOR_INFO);
        if (m_CompilerOutputBuffer.empty())
        {
            m_CompilerOutputBuffer.assign(m_CompilerOutput.begin(), m_CompilerOutput.end());
            m_CompilerOutputBuffer.push_back('\0');
        }
        ImGui::InputTextMultiline("##CompilerOutputText", m_CompilerOutputBuffer.data(),
            m_CompilerOutputBuffer.size(),
            ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * 6.0f),
            ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
        ImGui::Separator();
    }
    if (m_DiagEntries.empty())
    {
        ImGui::TextColored(COLOR_OK, "  %s", L("code.no_issues"));
        return;
    }

    ImGui::Text(UI("%s (%d):"), L("code.problems"), (int)m_DiagEntries.size());

    ImGui::BeginChild("##diag", ImVec2(-1.0f, 0.0f), true,
                      ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& entry : m_DiagEntries)
    {
        const ImVec4& col = entry.isError ? COLOR_ERROR : COLOR_WARNING;
        const char*  icon = entry.isError ? "[ERRO]"    : "[AVISO]";

        ImGui::TextColored(col, "%s  Linha %-4d  %s", icon, entry.line, entry.message.c_str());

        // Clica na linha → navega até ela no editor
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        {
            int col0 = 0;
            m_Editor.SetCursorPosition({ entry.line - 1, col0 });
        }
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// RunDiagnostics — análise estática leve via regex
// ---------------------------------------------------------------------------
void CodeEditor::RunDiagnostics()
{
    m_Markers.clear();
    m_DiagEntries.clear();
    m_ErrorCount   = 0;
    m_WarningCount = 0;

    const std::string text = m_Editor.GetText();
    std::istringstream stream(text);
    std::string line;
    int lineNum = 1;

    // ------------------------------------------------------------------
    // Padrões de ERROS
    // ------------------------------------------------------------------
    // 1. Ponto-e-vírgula faltando após statement (heurística: linha
    //    termina com identificador/parêntese/colchete mas sem ; { } // /*
    //    e não é uma declaração de tipo/namespace/using/atributo)
    static const std::regex reNeedsSemicolon(
        R"(^\s*(return|throw|break|continue)\s+[^;{}/\n]+\s*$)"
    );

    // 2. Uso de var sem atribuição (var x;)
    static const std::regex reVarNoInit(
        R"(\bvar\s+\w+\s*;)"
    );

    // 3. Parênteses não fechados na linha (simplificado: conta abertos vs fechados)
    // (tratado no loop abaixo)

    // 4. String não terminada (aspas abertas sem fechar na linha)
    //    Heurística: linha que contém número ímpar de aspas não-escapadas
    //    (implementado no loop de balanceamento abaixo — sem regex para evitar
    //     lookbehind que o MSVC não suporta em todos os modos)

    // 5. Uso de == com null em vez de is null
    static const std::regex reEqualNull(
        R"(\=\=\s*null)"
    );

    // 6. Casting estilo C em C# (ex.: (int)x — aviso informativo)
    static const std::regex reCCast(
        R"(\([a-zA-Z]+\)\s*[a-zA-Z_]\w*)"
    );

    // 7. Método público sem tipo de retorno explícito antes do nome
    //    (palavra isolada que parece um método mas sem void/tipo anterior)
    //    ex.: "public MyMethod()" → falta tipo de retorno
    static const std::regex reMethodNoReturn(
        R"(^\s*(?:public|private|protected|internal|static)\s+(\w+)\s*\()"
    );
    // nomes de tipo conhecidos (não geram aviso)
    static const std::unordered_set<std::string> knownReturnTypes = {
        "void","int","float","double","string","bool","byte","char",
        "long","uint","ulong","short","ushort","object","dynamic",
        "var","async","override","virtual","abstract","sealed",
        "List","Dictionary","IEnumerable","Task","Action","Func",
        "Vector2","Vector3","Vector4","Quaternion","Transform",
        "string[]","int[]","float[]","bool[]"
    };

    // 8. Missing using statement for common namespaces
    static const std::regex reMissingUsing(
        R"(DateTime|List<|Dictionary<|HashSet<|IEnumerable<)"
    );

    // 9. Static method called on instance
    static const std::regex reStaticOnInstance(
        R"(\w+\.\w+\s*\()"
    );

    // ------------------------------------------------------------------
    // Padrões de AVISOS
    // ------------------------------------------------------------------
    // 1. TODO / FIXME / HACK
    static const std::regex reTodo(R"(//.*\b(TODO|FIXME|HACK|XXX)\b)");

    // 2. Variável possivelmente não usada: declaração local simples
    //    (heurística muito leve — apenas para tipos primitivos)
    // omitido para evitar falsos positivos

    // 3. Comparação de float com == (deve usar Mathf.Approximately)
    static const std::regex reFloatEq(
        R"(\bfloat\b.*==)"
    );

    // 4. Empty catch block
    static const std::regex reEmptyCatch(
        R"(catch\s*(\(.*\))?\s*\{\s*\})"
    );

    // 5. Debug.Log / Console.WriteLine deixado no código
    static const std::regex reDebugLog(
        R"(\b(Debug\.Log|Console\.Write(Line)?)\s*\()"
    );

    // ------------------------------------------------------------------
    // Análise de chaves/parênteses globais
    // ------------------------------------------------------------------
    int braceBalance = 0;   // { }
    int parenBalance = 0;   // ( )
    int firstBraceUnder = -1;
    int firstParenUnder = -1;
    bool inBlockComment = false;

    std::string fullText = text;

    // reset stream
    stream.clear();
    stream.seekg(0);
    lineNum = 1;

    while (std::getline(stream, line))
    {
        const std::string trimmed = [&]() {
            size_t s = line.find_first_not_of(" \t\r");
            return (s == std::string::npos) ? std::string("") : line.substr(s);
        }();

        bool inString    = false;
        bool inChar      = false;
        bool inLineComment = false;

        // Conta balanceamento de chaves e parênteses nesta linha
        // (respeitando strings e comentários de forma simplificada)
        for (size_t i = 0; i < line.size(); ++i)
        {
            char c  = line[i];
            char nc = (i + 1 < line.size()) ? line[i + 1] : '\0';

            if (inBlockComment)
            {
                if (c == '*' && nc == '/') { inBlockComment = false; ++i; }
                continue;
            }
            if (inLineComment) break;
            if (inString)
            {
                if (c == '\\') { ++i; continue; }
                if (c == '"')  inString = false;
                continue;
            }
            if (inChar)
            {
                if (c == '\\') { ++i; continue; }
                if (c == '\'') inChar = false;
                continue;
            }

            if (c == '/' && nc == '/') { inLineComment = true; continue; }
            if (c == '/' && nc == '*') { inBlockComment = true; ++i; continue; }
            if (c == '"')  { inString = true;  continue; }
            if (c == '\'') { inChar   = true;  continue; }

            if (c == '{') { ++braceBalance; }
            if (c == '}') {
                --braceBalance;
                if (braceBalance < 0 && firstBraceUnder < 0)
                    firstBraceUnder = lineNum;
            }
            if (c == '(') { ++parenBalance; }
            if (c == ')') {
                --parenBalance;
                if (parenBalance < 0 && firstParenUnder < 0)
                    firstParenUnder = lineNum;
            }
        }

        // Linha não vazia e não é só um comentário
        const bool isComment = trimmed.rfind("//", 0) == 0
                            || trimmed.rfind("/*", 0) == 0
                            || trimmed.rfind("*",  0) == 0;

        if (!trimmed.empty() && !isComment)
        {
            std::smatch m;

            // Erro: return/throw sem ponto-e-vírgula (heurística)
            if (std::regex_match(line, m, reNeedsSemicolon))
            {
                std::string msg = "Possível ';' faltando após statement";
                m_Markers[lineNum] = msg;
                m_DiagEntries.push_back({ lineNum, msg, true });
                ++m_ErrorCount;
            }

            // Erro: var sem inicialização
            if (std::regex_search(line, m, reVarNoInit))
            {
                std::string msg = "'var' requer atribuição na declaração";
                m_Markers[lineNum] = msg;
                m_DiagEntries.push_back({ lineNum, msg, true });
                ++m_ErrorCount;
            }

            // Aviso: método público sem tipo de retorno explícito
            if (std::regex_search(line, m, reMethodNoReturn))
            {
                const std::string word = m[1].str();
                if (knownReturnTypes.find(word) == knownReturnTypes.end())
                {
                    std::string msg = "'" + word + "': possível tipo de retorno ausente";
                    if (m_Markers.find(lineNum) == m_Markers.end())
                        m_Markers[lineNum] = msg;
                    m_DiagEntries.push_back({ lineNum, msg, true });
                    ++m_ErrorCount;
                }
            }

            // Aviso: == null (prefira is null)
            if (std::regex_search(line, m, reEqualNull))
            {
                std::string msg = "Prefira 'is null' em vez de '== null'";
                if (m_Markers.find(lineNum) == m_Markers.end())
                    m_Markers[lineNum] = msg;
                m_DiagEntries.push_back({ lineNum, msg, false });
                ++m_WarningCount;
            }

            // Aviso: comparação de float com ==
            if (std::regex_search(line, m, reFloatEq))
            {
                std::string msg = "Comparação de float com '==' pode ser imprecisa";
                if (m_Markers.find(lineNum) == m_Markers.end())
                    m_Markers[lineNum] = msg;
                m_DiagEntries.push_back({ lineNum, msg, false });
                ++m_WarningCount;
            }

            // Aviso: catch vazio
            if (std::regex_search(line, m, reEmptyCatch))
            {
                std::string msg = "Bloco catch vazio suprime exceções silenciosamente";
                if (m_Markers.find(lineNum) == m_Markers.end())
                    m_Markers[lineNum] = msg;
                m_DiagEntries.push_back({ lineNum, msg, false });
                ++m_WarningCount;
            }

            // Aviso: Debug.Log / Console.WriteLine
            if (std::regex_search(line, m, reDebugLog))
            {
                std::string msg = "Remova logs de debug antes de publicar";
                if (m_Markers.find(lineNum) == m_Markers.end())
                    m_Markers[lineNum] = msg;
                m_DiagEntries.push_back({ lineNum, msg, false });
                ++m_WarningCount;
            }

            // Aviso: Casting estilo C
            if (std::regex_search(line, m, reCCast))
            {
                std::string msg = "Prefira 'as' ou conversões seguras em vez de cast estilo C";
                if (m_Markers.find(lineNum) == m_Markers.end())
                    m_Markers[lineNum] = msg;
                m_DiagEntries.push_back({ lineNum, msg, false });
                ++m_WarningCount;
            }

            // Aviso: Possible missing using statement
            if (std::regex_search(line, m, reMissingUsing))
            {
                std::string msg = "Verifique se as diretivas using necessárias estão presentes";
                if (m_Markers.find(lineNum) == m_Markers.end())
                    m_Markers[lineNum] = msg;
                m_DiagEntries.push_back({ lineNum, msg, false });
                ++m_WarningCount;
            }
        }

        // TODO/FIXME (dentro de comentários)
        {
            std::smatch m;
            if (std::regex_search(line, m, reTodo))
            {
                std::string msg = m[1].str() + ": " + line.substr(line.find("//") + 2);
                // Trim
                size_t s = msg.find_first_not_of(" \t");
                if (s != std::string::npos) msg = msg.substr(s);
                if (m_Markers.find(lineNum) == m_Markers.end())
                    m_Markers[lineNum] = msg;
                m_DiagEntries.push_back({ lineNum, msg, false });
                ++m_WarningCount;
            }
        }

        ++lineNum;
    }

    // Balanceamento global de chaves
    if (braceBalance > 0)
    {
        int reportLine = lineNum - 1;
        std::string msg = "Chave '{' não fechada (" + std::to_string(braceBalance) + " aberta(s))";
        m_Markers[reportLine] = msg;
        m_DiagEntries.push_back({ reportLine, msg, true });
        ++m_ErrorCount;
    }
    else if (firstBraceUnder >= 0)
    {
        std::string msg = "'}' sem '{' correspondente";
        if (m_Markers.find(firstBraceUnder) == m_Markers.end())
            m_Markers[firstBraceUnder] = msg;
        m_DiagEntries.push_back({ firstBraceUnder, msg, true });
        ++m_ErrorCount;
    }

    if (parenBalance > 0)
    {
        int reportLine = lineNum - 1;
        std::string msg = "Parêntese '(' não fechado (" + std::to_string(parenBalance) + " aberto(s))";
        if (m_Markers.find(reportLine) == m_Markers.end())
            m_Markers[reportLine] = msg;
        m_DiagEntries.push_back({ reportLine, msg, true });
        ++m_ErrorCount;
    }
    else if (firstParenUnder >= 0)
    {
        std::string msg = "')' sem '(' correspondente";
        if (m_Markers.find(firstParenUnder) == m_Markers.end())
            m_Markers[firstParenUnder] = msg;
        m_DiagEntries.push_back({ firstParenUnder, msg, true });
        ++m_ErrorCount;
    }

    m_Editor.SetErrorMarkers(m_Markers);
}

// ---------------------------------------------------------------------------
// BuildCSharpLanguage — LanguageDefinition completa para C#
// ---------------------------------------------------------------------------
TextEditor::LanguageDefinition CodeEditor::BuildCSharpLanguage()
{
    TextEditor::LanguageDefinition lang;
    lang.mName = "C#";
    lang.mSingleLineComment   = "//";
    lang.mCommentStart        = "/*";
    lang.mCommentEnd          = "*/";
    lang.mPreprocChar         = '#';
    lang.mAutoIndentation     = true;
    lang.mCaseSensitive       = true;

    // ---- Keywords C# ----
    static const char* keywords[] = {
        "abstract","as","base","bool","break","byte","case","catch",
        "char","checked","class","const","continue","decimal","default",
        "delegate","do","double","else","enum","event","explicit","extern",
        "false","finally","fixed","float","for","foreach","goto","if",
        "implicit","in","int","interface","internal","is","lock","long",
        "namespace","new","null","object","operator","out","override",
        "params","private","protected","public","readonly","ref","return",
        "sbyte","sealed","short","sizeof","stackalloc","static","string",
        "struct","switch","this","throw","true","try","typeof","uint",
        "ulong","unchecked","unsafe","ushort","using","virtual","void",
        "volatile","while",
        // C# 8+
        "await","async","yield","var","dynamic","record","init","with",
        "global","file","required","scoped","nint","nuint","not","and","or"
    };
    for (auto kw : keywords)
        lang.mKeywords.insert(kw);

    // ---- Identificadores conhecidos (tipos/métodos comuns) ----
    static const char* knownIds[] = {
        // System
        "Console","Math","Convert","Environment","GC","Exception",
        "ArgumentException","NullReferenceException","InvalidOperationException",
        "NotImplementedException","IndexOutOfRangeException",
        "string","String","int","float","double","bool","byte","char","object",
        "List","Dictionary","HashSet","Queue","Stack","Array","Span","Memory",
        "IEnumerable","IList","IDictionary","ICollection","IReadOnlyList",
        "Task","ValueTask","CancellationToken","Func","Action","Predicate",
        "StringBuilder","DateTime","TimeSpan","Guid","Random",
        "Nullable","Tuple","ValueTuple",
        // System.IO
        "File","Directory","Path","FileStream","StreamReader","StreamWriter",
        "System.IO","System.Text","System.Linq","System.Collections.Generic",
        // System.Threading
        "Thread","Task","ThreadPool","CancellationTokenSource","async","await",
        // System.Text.RegularExpressions
        "Regex","Match","Group","RegexOptions",
        // Unity/Engine comuns (úteis para contexto)
        "Vector2","Vector3","Vector4","Quaternion","Matrix4x4",
        "Transform","GameObject","Component","MonoBehaviour","ScriptableObject",
        "Debug","Time","Input","Physics","Camera","Mathf",
        "Rigidbody","Collider","Renderer","Animator","AudioSource",
        "OnStart","OnUpdate","OnDestroy","Start","Update","FixedUpdate",
        "Awake","OnEnable","OnDisable","LateUpdate","OnCollisionEnter",
        "ScriptComponent","Entity","Scene","Application",
        "OnAwake","OnUpdate","OnEnabled","OnDisabled",
        // Marae Engine específicos
        "TransformComponent","MeshComponent","MaterialComponent",
        "LightComponent","AudioComponent","AnimationComponent",
        "Vector2","Vector3","Color"
    };
    for (auto id : knownIds)
    {
        TextEditor::Identifier ident;
        ident.mDeclaration = id;
        lang.mIdentifiers.insert_or_assign(id, ident);
    }

    // ---- Token regex para colorização ----
    // Ordem importa: mais específicos primeiro

    // Strings verbatim @"..."
    lang.mTokenRegexStrings.push_back(
        { R"(@"(?:[^"]|"")*")", TextEditor::PaletteIndex::String });

    // Strings interpoladas $"..."  (simplificado)
    lang.mTokenRegexStrings.push_back(
        { R"(\$"(?:[^"\\]|\\.)*")", TextEditor::PaletteIndex::String });

    // Strings normais "..."
    lang.mTokenRegexStrings.push_back(
        { R"("(?:[^"\\]|\\.)*")", TextEditor::PaletteIndex::String });

    // Char literal '.'
    lang.mTokenRegexStrings.push_back(
        { R"('(?:[^'\\]|\\.)')", TextEditor::PaletteIndex::CharLiteral });

    // Números: hex, float, int
    lang.mTokenRegexStrings.push_back(
        { R"(0[xX][0-9a-fA-F]+[uUlL]*)", TextEditor::PaletteIndex::Number });
    lang.mTokenRegexStrings.push_back(
        { R"([0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?[fFdDmMuUlL]*)", TextEditor::PaletteIndex::Number });

    // Atributos [Attribute]
    lang.mTokenRegexStrings.push_back(
        { R"(\[[A-Za-z_]\w*(?:\(.*?\))?\])", TextEditor::PaletteIndex::Preprocessor });

    // Diretivas de preprocessor
    lang.mTokenRegexStrings.push_back(
        { R"(#\s*(?:if|else|elif|endif|define|undef|region|endregion|pragma|warning|error|nullable|line)\b.*)",
          TextEditor::PaletteIndex::Preprocessor });

    // Identificadores (capturado automaticamente pela lógica interna de keywords/knownIds)
    lang.mTokenRegexStrings.push_back(
        { R"([a-zA-Z_]\w*)", TextEditor::PaletteIndex::Identifier });

    // Pontuação
    lang.mTokenRegexStrings.push_back(
        { R"([{}()\[\];,.<>=!+\-*/%&|^~?:])", TextEditor::PaletteIndex::Punctuation });

    return lang;
}

// ---------------------------------------------------------------------------
// BuildPalette — paleta escura otimizada para C#
// ---------------------------------------------------------------------------
TextEditor::Palette CodeEditor::BuildPalette()
{
    // Parte da paleta dark padrão e personaliza
    auto p = TextEditor::GetDarkPalette();

    // Background ligeiramente mais escuro que o ImGui default
    p[(int)TextEditor::PaletteIndex::Background]        = 0xFF1A1A1A;

    // Palavras-chave: azul claro (estilo VS Code C#)
    p[(int)TextEditor::PaletteIndex::Keyword]           = 0xFF569CD6;

    // Números: verde claro
    p[(int)TextEditor::PaletteIndex::Number]            = 0xFFB5CEA8;

    // Strings: laranja/salmão
    p[(int)TextEditor::PaletteIndex::String]            = 0xFFCE9178;
    p[(int)TextEditor::PaletteIndex::CharLiteral]       = 0xFFCE9178;

    // Comentários: verde acinzentado
    p[(int)TextEditor::PaletteIndex::Comment]           = 0xFF6A9955;
    p[(int)TextEditor::PaletteIndex::MultiLineComment]  = 0xFF6A9955;

    // Preprocessor / atributos: cinza/lilás
    p[(int)TextEditor::PaletteIndex::Preprocessor]      = 0xFFBD9FD8;
    p[(int)TextEditor::PaletteIndex::PreprocIdentifier] = 0xFFBD9FD8;

    // Identificadores conhecidos (tipos/métodos): ciano
    p[(int)TextEditor::PaletteIndex::KnownIdentifier]   = 0xFF4EC9B0;

    // Texto normal: branco suave
    p[(int)TextEditor::PaletteIndex::Default]           = 0xFFD4D4D4;

    // Pontuação: branco mais brilhante
    p[(int)TextEditor::PaletteIndex::Punctuation]       = 0xFFD4D4D4;

    // Número de linha: cinza escuro
    p[(int)TextEditor::PaletteIndex::LineNumber]        = 0xFF555555;

    // Erro: fundo vermelho escuro
    p[(int)TextEditor::PaletteIndex::ErrorMarker]       = 0xAA3A1010;

    // Linha atual: destaque sutil
    p[(int)TextEditor::PaletteIndex::CurrentLineFill]         = 0x18FFFFFF;
    p[(int)TextEditor::PaletteIndex::CurrentLineFillInactive] = 0x0CFFFFFF;
    p[(int)TextEditor::PaletteIndex::CurrentLineEdge]         = 0x30FFFFFF;

    // Seleção: azul semitransparente
    p[(int)TextEditor::PaletteIndex::Selection]         = 0x80264F78;

    // Cursor: branco
    p[(int)TextEditor::PaletteIndex::Cursor]            = 0xFFFFFFFF;

    return p;
}

// ---------------------------------------------------------------------------
// Sistema de Templates/Snippets
// ---------------------------------------------------------------------------

static const std::unordered_map<std::string, std::string> TEMPLATES = {
    {
        "MonoBehaviour",
R"(using MaraeEngine;

public class $NAME$ : MonoBehaviour
{
    void Start()
    {
        // Código de inicialização
    }

    void Update()
    {
        // Código executado a cada frame
    }
})"
    },
    {
        "ScriptComponent",
R"(using MaraeEngine;

public class $NAME$ : ScriptComponent
{
    public override void OnAwake()
    {
        // Inicialização
    }

    public override void OnUpdate(float deltaTime)
    {
        // Atualização por frame
    }
})"
    },
    {
        "EmptyClass",
R"(public class $NAME$
{
    public $NAME$()
    {
        // Construtor
    }
})"
    },
    {
        "Property",
R"(public $TYPE$ $NAME$
{
    get { return _$NAME$; }
    set { _$NAME$ = value; }
}
private $TYPE$ _$NAME$;)"
    },
    {
        "ForLoop",
R"(for (int i = 0; i < $COUNT$; i++)
{
    // loop body
})"
    },
    {
        "ForeachLoop",
R"(foreach (var item in $COLLECTION$)
{
    // loop body
})"
    },
    {
        "IfStatement",
R"(if ($CONDITION$)
{
    // true branch
}
else
{
    // false branch
})"
    },
    {
        "TryCatch",
R"(try
{
    // código que pode lançar exceção
}
catch (System.Exception ex)
{
    // tratamento de erro
})"
    },
    {
        "Coroutine",
R"(public System.Collections.IEnumerator $NAME$Coroutine()
{
    yield return null;
})"
    },
    {
        "Singleton",
R"(public class $NAME$
{
    private static $NAME$ _instance;
    public static $NAME$ Instance
    {
        get
        {
            if (_instance == null)
                _instance = new $NAME$();
            return _instance;
        }
    }

    private $NAME$() { }
})"
    }
};

// ---------------------------------------------------------------------------
// GetAvailableTemplates
// ---------------------------------------------------------------------------
std::vector<std::string> CodeEditor::GetAvailableTemplates()
{
    std::vector<std::string> names;
    for (const auto& pair : TEMPLATES)
        names.push_back(pair.first);
    return names;
}

// ---------------------------------------------------------------------------
// InsertTemplate
// ---------------------------------------------------------------------------
void CodeEditor::InsertTemplate(const std::string& templateName)
{
    auto it = TEMPLATES.find(templateName);
    if (it == TEMPLATES.end())
        return;

    std::string templateContent = it->second;
    
    // Substitui placeholders
    size_t pos = 0;
    while ((pos = templateContent.find("$NAME$", pos)) != std::string::npos)
    {
        templateContent.replace(pos, 6, "MyScript");
        pos += 8;
    }
    
    pos = 0;
    while ((pos = templateContent.find("$TYPE$", pos)) != std::string::npos)
    {
        templateContent.replace(pos, 6, "int");
        pos += 8;
    }
    
    pos = 0;
    while ((pos = templateContent.find("$COUNT$", pos)) != std::string::npos)
    {
        templateContent.replace(pos, 7, "10");
        pos += 9;
    }
    
    pos = 0;
    while ((pos = templateContent.find("$COLLECTION$", pos)) != std::string::npos)
    {
        templateContent.replace(pos, 12, "items");
        pos += 14;
    }
    
    pos = 0;
    while ((pos = templateContent.find("$CONDITION$", pos)) != std::string::npos)
    {
        templateContent.replace(pos, 12, "true");
        pos += 14;
    }

    // Insere na posição do cursor
    auto cursorPos = m_Editor.GetCursorPosition();
    m_Editor.InsertText(templateContent);
    m_Dirty = true;
}
