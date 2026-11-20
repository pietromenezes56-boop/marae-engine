#pragma once

#include "ProjectManager.h"
#include <string>
#include <vector>

struct GLFWwindow;
struct ImFont;

// ---------------------------------------------------------------------------
// Resultado retornado pelo ProjectHub para o Main
// ---------------------------------------------------------------------------
struct HubResult
{
    bool     ShouldLaunch  = false;  // true = abrir o editor
    bool     OpenPlatform  = false;  // abrir a plataforma local de jogos
    std::string ProjectPath;          // pasta raiz do projeto escolhido
    std::string ScenePath;            // arquivo .scene inicial
    GraphicsQuality Quality = GraphicsQuality::Balanced;
    ProjectWorldType WorldType = ProjectWorldType::World3D;
};

// ---------------------------------------------------------------------------
// ProjectHub
// Tela inicial da engine: lista projetos, cria novos, configurações globais.
// Roda seu próprio loop GLFW + ImGui e retorna um HubResult para o Main.
// ---------------------------------------------------------------------------
class ProjectHub
{
public:
    ProjectHub();
    ~ProjectHub();

    // Executa o loop da tela inicial e retorna a decisão do usuário
    HubResult Run();

private:
    // Inicialização / shutdown
    bool Init();
    void Shutdown();

    // Loop de renderização
    void BeginFrame();
    void EndFrame();

    // Render da UI
    void RenderMainWindow();
    void RenderProjectList();
    void RenderNewProjectModal();
    void RenderRenameProjectModal();
    void RenderDeleteProjectModal();
    void RenderSettingsPanel();
    void RenderTermsPanel();
    void RenderEditorSettings();

    // Helpers
    void ApplyTheme();
    void LoadFonts(float dpiScale);
    void OpenProject(const ProjectInfo& project);
    void LaunchProject(const ProjectInfo& project);
    void RenderHardwareWarningModal();

    // Fontes do sistema
    ImFont*                   m_FontRegular     = nullptr;
    ImFont*                   m_FontBold        = nullptr;
    ImFont*                   m_FontTitle       = nullptr;
    ImFont*                   m_FontSmall       = nullptr;

    // Filtro de busca na lista de projetos
    char                      m_SearchFilter[128] = "";
    bool                      m_SettingsSavedFeedback = false;
    std::string BrowseFolder();   // abre dialog nativo de seleção de pasta

    // Estado
    GLFWwindow*               m_Window        = nullptr;
    std::vector<ProjectInfo>  m_Projects;
    unsigned int              m_LogoTexture   = 0;
    HubResult                 m_Result;
    bool                      m_Running        = true;
    bool                      m_ShowNewModal   = false;
    bool                      m_ShowSettings   = false;
    bool                      m_ShowTerms      = false;
    int                       m_SelectedProject = -1;
    bool                      m_ShowRenameModal = false;
    bool                      m_ShowDeleteModal = false;
    bool                      m_ShowHardwareWarning = false;
    bool                      m_HardwareDriverRisk = false;
    ProjectInfo               m_PendingHardwareProject;
    std::string               m_HardwareWarningDetails;
    bool                      m_DeleteProjectFiles = false;
    char                      m_RenameBuffer[256] = "";

    // Estado do modal "Novo Projeto"
    char     m_NewName[256]      = "";
    char     m_NewDirectory[512] = "";
    GraphicsQuality m_NewQuality = GraphicsQuality::Balanced;
    ProjectWorldType m_NewWorldType = ProjectWorldType::World3D;
    std::string     m_NewModalError;

    // Configurações persistentes do editor (não do projeto)
    struct EditorSettings
    {
        bool  AutoSave          = false;
        int   AutoSaveIntervalS = 300;   // segundos
        float FontScale         = 1.0f;
        bool  ShowFPS           = true;
        bool  VSync             = true;
        int   ThemeIndex        = 0;     // 0 = Dark Blue (default)
    };
    EditorSettings m_Settings;

    void LoadEditorSettings();
    void SaveEditorSettings();
    std::string GetEditorSettingsPath();
};
