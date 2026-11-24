#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include "TextEditor.h"

// ---------------------------------------------------------------------------
// CodeEditor
//
// Wrapper em torno do TextEditor (ImGuiColorTextEdit) especializado para C#.
// Fornece:
//   - Language definition C# com keywords, tipos e preprocessor completos
//   - Paleta escura calibrada para C#
//   - Diagnóstico estático via regex (erros e avisos marcados por linha)
//   - Dirty flag para controle de salvamento
//   - Auto-save opcional por intervalo de tempo
//   - API de alto nível: Open, Save, Render
// ---------------------------------------------------------------------------
class CodeEditor
{
public:
    CodeEditor();

    // Abre um arquivo .cs; carrega conteúdo, reseta estado e roda diagnóstico.
    bool Open(const std::string& filePath);

    // Salva o conteúdo atual no arquivo aberto. Retorna true em sucesso.
    bool Save();

    // Renderiza a janela completa do editor (barra de ferramentas + editor + painel de erros).
    // Deve ser chamado dentro de um ImGui::Begin/End existente OU autônomo.
    // aTitle: título da janela ImGui (único por instância).
    // aSize: tamanho usado em ImGui::SetNextWindowSize (passado como ImVec2).
    void RenderWindow(const char* aTitle, bool* pOpen);
    void SetCompileCallback(std::function<std::string()> callback) { m_CompileCallback = std::move(callback); }

    // Retorna true se há alterações não salvas.
    bool IsDirty() const { return m_Dirty; }

    // Fecha o arquivo atual sem salvar (reseta estado).
    void Close() { m_FilePath.clear(); m_Dirty = false; m_Markers.clear(); m_DiagEntries.clear(); m_ErrorCount = 0; m_WarningCount = 0; }

    // Retorna o caminho do arquivo atualmente aberto.
    const std::string& GetFilePath() const { return m_FilePath; }

    // Retorna true se um arquivo está aberto.
    bool IsOpen() const { return !m_FilePath.empty(); }

    // Força nova análise de diagnóstico no conteúdo atual.
    void RunDiagnostics();

    // Número de erros encontrados no último diagnóstico.
    int GetErrorCount() const { return m_ErrorCount; }
    int GetWarningCount() const { return m_WarningCount; }

    // Insere um template/snippet de C# na posição atual do cursor
    void InsertTemplate(const std::string& templateName);

    // Retorna lista de templates disponíveis
    static std::vector<std::string> GetAvailableTemplates();

private:
    // Configura a LanguageDefinition personalizada para C#.
    static TextEditor::LanguageDefinition BuildCSharpLanguage();

    // Configura a paleta de cores do editor.
    static TextEditor::Palette BuildPalette();

    // Renderiza a barra de ferramentas interna (Save, botões de edição, status).
    void RenderToolbar();

    // Renderiza o painel de erros/avisos na parte inferior.
    void RenderDiagnosticsPanel();

    TextEditor           m_Editor;
    std::string          m_FilePath;
    bool                 m_Dirty       = false;
    int                  m_ErrorCount  = 0;
    int                  m_WarningCount = 0;

    // Diagnóstico: mapa linha (1-based) -> mensagem de erro/aviso
    TextEditor::ErrorMarkers m_Markers;

    // Estrutura para o painel de diagnóstico
    struct DiagEntry
    {
        int         line;
        std::string message;
        bool        isError; // false = aviso
    };
    std::vector<DiagEntry> m_DiagEntries;
    std::function<std::string()> m_CompileCallback;
    std::string m_CompilerOutput;
    std::vector<char> m_CompilerOutputBuffer;
    bool m_ShowScriptGuide = false;
};
