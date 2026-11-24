#pragma once
#include <vector>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "../Renderer/Framebuffer/Framebuffer.h"
#include "../Window/Window.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/VulkanImGuiRenderer.h"
#include "../Scene/Scene/Scene.h"
#include "../Scene/Camera/Camera.h"
#include "../Camera/CameraController.h"
#include "../Editor/Snap/SnapSettings.h"
#include "../Editor/UndoRedo/UndoRedoManager.h"
#include "../Editor/Panels/AssetBrowser/AssetBrowserPanel.h"
#include "../Engine/Lighting/LightingManager.h"
#include "../Engine/Systems/AnimationSystem.h"
#include "../Engine/DemoGames/RaceGame.h"
#include "../Engine/DemoGames/SurvivalGame.h"
#include "../Engine/Audio/AudioSystem.h"
#include "../Engine/Audio/AudioSceneSync.h"
#include "../Editor/CodeEditor/CodeEditor.h"
#include "../Hub/ProjectManager.h"
#include "../Scene/SceneManager/SceneManager.h"  // GraphicsQuality, ProjectInfo
#include "../Engine/UI/UIInteraction.h"
#include "../Engine/UI/UIInputBridge.h"

class Entity;
class InspectorPanel;
class HierarchyPanel;

// ---------------------------------------------------------------------------
// Configuração passada pelo ProjectHub ao criar o Application
// ---------------------------------------------------------------------------
struct ProjectConfig
{
    std::string     ProjectPath;  // pasta raiz do projeto
    std::string     ScenePath;    // arquivo .scene inicial (pode ser vazio)
    GraphicsQuality Quality = GraphicsQuality::Balanced;
    ProjectWorldType WorldType = ProjectWorldType::World3D;
    GraphicsAPI Graphics = GraphicsAPI::OpenGL;
    bool PersistEditorSettings = true;
    bool RuntimeGame = false;
    std::string GameTitle;
    std::string GameVersion;
    std::string GameIconPath;
    int RuntimeWidth = 1280;
    int RuntimeHeight = 720;
    int RuntimeWindowMode = 0; // 0 windowed, 1 borderless, 2 fullscreen
};

enum class ProjectSessionAction
{
    Quit,
    ReturnToHub,
    OpenAnotherProject,
    RestartWithGraphicsAPI
};

struct ProjectSessionResult
{
    ProjectSessionAction Action = ProjectSessionAction::Quit;
    ProjectConfig NextProject;
};

class Application
{
    friend class SceneManager;
public:

    Window& GetWindow();

    // Construtor padrão (legado — usado em testes sem hub)
    Application();

    // Construtor principal — recebe configuração do ProjectHub
    explicit Application(const ProjectConfig& config);

    ~Application();

    enum class GizmoMode
    {
        Translate,
        Rotate,
        Scale
    };

    enum class WorldMode
    {
        World3D,
        World2D
    };

    void SetGizmoMode(
        GizmoMode mode
    );

    GizmoMode GetGizmoMode() const;

    WorldMode GetWorldMode() const { return m_WorldMode; }
    bool Is2DWorldMode() const { return m_WorldMode == WorldMode::World2D; }
    void SetWorldMode(WorldMode mode);

    void Run();
    // Troca o backend do editor reiniciando a sessão com a API solicitada.
    void RequestGraphicsAPISwitch(GraphicsAPI api);
    // Executa testes determinísticos do solver sem iniciar o loop do editor.
    int RunPhysicsRegressionTests();
    int RunUIRegressionTests();
    int RunRecoveryRegressionTests();
    int RunSceneRegressionTests();
    int RunAnimationRegressionTests();
    int RunMaterialRegressionTests();
    int RunAcousticRegressionTests();
    const ProjectSessionResult& GetSessionResult() const { return m_SessionResult; }
    Framebuffer& GetFramebuffer();
    void CreateCubeEntity();
    void CreateSphereEntity();
    void CreateCylinderEntity();
    void CreateConeEntity();
    void CreateEmptyEntity();
    void CreateFolderEntity();
    void CreateModelEntity();
    void ImportModelEntity(const std::string& filepath = "");
    void CreateTriangleEntity();
    void CreateQuadEntity();
    void Create2DShapeEntity(const std::string& primitiveType);
    void CreatePlaneEntity();
    void CreateTerrainEntity();
    void CreateWedgeEntity();
    void CreateTorusEntity();
    void CreateCapsuleEntity();
    void CreateCameraEntity();
    void CreateScriptForSelectedEntity();
    void OpenSelectedScriptInVSCode();
    void OpenScriptsFolderInVSCode();
    // Internal script editor support
    void OpenScriptInInternalEditor(const std::string& path);
    void ReloadScriptsFromAI();
    bool IsInternalEditorOpen() const;
    void CloseInternalEditor();
    void RenderInternalEditorUI();
    void DeleteSelectedEntity();
    void DuplicateSelectedEntity();
    void CopySelectedEntities();
    void PasteCopiedEntities();
    Scene& GetScene();
    int GetSelectedEntity() const;
    Entity& GetSelectedEntity();
    uint64_t GetSelectedID() const;
    void SetSelectedID(uint64_t id);
    Renderer& GetRenderer();
    Camera& GetCamera();
    Camera GetViewportCamera() const;
    const Entity* GetActiveSceneCamera() const;
    AudioSystem& GetAudioSystem();
    void RenderCameraPreview(Entity& cameraEntity, int width = 320, int height = 180);
    uint32_t GetCameraPreviewTexture() const { return m_CameraPreviewFramebuffer.GetTexture(); }
    int GetCameraPreviewWidth() const { return m_CameraPreviewFramebuffer.GetWidth(); }
    int GetCameraPreviewHeight() const { return m_CameraPreviewFramebuffer.GetHeight(); }

    void SetViewportSize(
        int width,
        int height
    );

    bool& GetShowGrid();
    bool& GetShowAxis();
    float& GetGridRenderDistance();
    float& GetShadowIntensity();
    float& GetAmbientLight();
    GraphicsQuality GetGraphicsQuality() const { return m_GraphicsQuality; }
    void SetGraphicsQuality(GraphicsQuality quality);
    void ToggleSettings();
    void SetShowTimeline(bool show);
    void SetShowPixelArtEditor(bool show) { m_ShowPixelArtEditor = show; }
    bool& GetShowPixelArtEditor()         { return m_ShowPixelArtEditor; }
    bool& GetShowAssetBrowser();
    void RestoreDefaultLayout();
    bool& GetShowHierarchy();
    bool& GetShowInspector();
    bool& GetShowAIPanel();
    const ProjectConfig& GetProjectConfig() const { return m_ProjectConfig; }

    void SetHierarchyWidth(float width);
    void SetInspectorWidth(float width);

    float GetHierarchyWidth() const;
    float GetInspectorWidth() const;

    int GetViewportWidth() const;
    int GetViewportHeight() const;
    uint32_t GetViewportTexture() const;
    uint32_t GetMobilePreviewTexture() const { return m_MobilePreviewFramebuffer.GetTexture(); }
    bool& GetShowMobilePreview() { return m_ShowMobilePreview; }
    // Liga/desliga a UI 2D (ScreenGui) desenhada sobre a cena no modo editor,
    // para que os widgets nao atrapalhem a edicao. No Play a UI sempre aparece.
    bool& GetShowUIInEditor() { return m_ShowUIInEditor; }
    bool IsRenderUIInEditorActive() const
    {
        return m_EditorState != EditorState::Edit || m_ShowUIInEditor;
    }
    bool IsShowFPS() const { return m_GlobalEditorSettings.ShowFPS; }
    void ResizeViewport(uint32_t width, uint32_t height) { SetViewportSize((int)width, (int)height); }

    bool IsViewportHovered() const { return m_ViewportHovered; }
    void SetViewportHovered(bool hovered) { m_ViewportHovered = hovered; }
    bool IsViewportFocused() const { return m_ViewportFocused; }
    void SetViewportFocused(bool focused) { m_ViewportFocused = focused; }
    bool IsViewportImageHovered() const { return m_ViewportImageHovered; }
    void SetViewportImageHovered(bool hovered) { m_ViewportImageHovered = hovered; }
    bool IsCameraControlling() const { return m_CameraController.IsControlling(); }
    float GetCameraFlySpeedMultiplier() const
    {
        return m_CameraController.GetFlySpeedMultiplier();
    }
    void SetCameraFlySpeedMultiplier(float multiplier)
    {
        m_CameraController.SetFlySpeedMultiplier(multiplier);
    }
    void SetViewportPointer(float x, float y, bool valid)
    {
        m_ViewportPointerX = x;
        m_ViewportPointerY = y;
        m_ViewportPointerValid = valid;
    }
    bool HasViewportPointer() const { return m_ViewportPointerValid; }
    float GetViewportPointerX() const { return m_ViewportPointerX; }
    float GetViewportPointerY() const { return m_ViewportPointerY; }

    // Resultado da interação da UI 2D do último frame (hover/press/clique/foco).
    const MaraeUI::UIInteractionResult& GetUIInteraction() const { return m_UIInteraction; }

    // Multi-selection support (indices into scene entities)
    void SetMultiSelectedEntities(const std::vector<int>& indices);
    void ClearMultiSelection() { m_MultiSelectedEntities.clear(); }
    const std::vector<int>& GetMultiSelectedEntities() const;
    bool IsMultiSelectedEntity(int index) const;

    enum class EditorState
    {
        Edit = 0,
        Play,
        Pause
    };

    void SetEditorState(
        EditorState state
    );

    EditorState GetEditorState() const;
    bool GetEditDuringPlay() const { return m_EditDuringPlay; }
    void SetEditDuringPlay(bool enabled) { m_EditDuringPlay = enabled; }

    const std::vector<Entity>& GetEntities() const;

    void SetSelectedEntity(
        int index
    );

    int GetSelectedEntityIndex() const;

    enum class GizmoSpace
    {
        Local,
        World
    };

    GizmoSpace GetGizmoSpace() const;

    bool GetFaceScaleHandlesEnabled() const { return m_FaceScaleHandlesEnabled; }
    void SetFaceScaleHandlesEnabled(bool enabled) { m_FaceScaleHandlesEnabled = enabled; }

    SnapSettings& GetSnapSettings();

    UndoRedoManager& GetUndoRedoManager();

    Transform m_BeforeTransform;
    bool m_WasUsingGizmo = false;
    bool m_ShowTermsModal = false;
    void RenderTermsModal();

    void SetSelectedAsset(const AssetInfo* asset);
    const AssetInfo* GetSelectedAsset() const;

    void AddKeyframeAtCurrentTime();

    // Mark dirty when user changes scene content
    void MarkDirty();
    bool IsDirty() const { return m_SceneDirty; }
    void ClearDirty() { m_SceneDirty = false; }

    // Audio preview controls for Inspector
    void PreviewAudioForSelectedEntity();
    void StopAudioPreview();

    // Mouse coordinates (passthrough to Window)
    double GetMouseX() const;
    double GetMouseY() const;

    // Gizmo usage flag (for undo-redo sync with panels)
    bool WasUsingGizmo() const { return m_WasUsingGizmo; }
    void SetWasUsingGizmo(bool value) { m_WasUsingGizmo = value; }

private:
    Window m_Window;
    Renderer m_Renderer;
    VulkanImGuiRenderer m_VulkanUIRenderer;
    Scene m_Scene;
    Camera m_Camera;
    CameraController m_CameraController;
    Framebuffer m_Framebuffer;
    Framebuffer m_CameraPreviewFramebuffer;
    Framebuffer m_MobilePreviewFramebuffer;
    SnapSettings m_SnapSettings;
    UndoRedoManager m_UndoRedoManager;
    AssetBrowserPanel m_AssetBrowserPanel;
    AnimationSystem m_AnimationSystem;
    // AudioSystem é singleton — acesso via AudioSystem::Get()
    // m_AudioSystem mantido como referência para compatibilidade de código existente
    AudioSystem& m_AudioSystem = AudioSystem::Get();
    CodeEditor m_CodeEditor;

    ProjectConfig m_ProjectConfig;
    ProjectSessionResult m_SessionResult;

    unsigned int m_LogoTexture = 0;
    bool m_LoadingSplashActive = false;
    float m_LoadingSplashProgress = 0.0f;

    int m_SelectedEntity = -1;
    uint64_t m_SelectedID = 0;
    std::vector<int> m_MultiSelectedEntities;

    bool m_UpPressed = false;
    bool m_DownPressed = false;
    bool m_DPressed = false;
    bool m_CPressed = false;
    bool m_VPressed = false;
    bool m_FPressed = false;
    bool m_UndoPressed = false;
    bool m_RedoPressed = false;
    bool m_DeletePressed = false;
    std::vector<Entity> m_EntityClipboard;
    std::vector<uint64_t> m_EntityClipboardRoots;

    void OnUpdate();

    // Recarrega scripts C# (hot-reload básico)
    void ReloadScripts();

    void PrintHierarchy();
    void PrintInspector();

    void MoveSelectedEntity(
        float x,
        float y,
        float z
    );

    void InitImGui();
    void ShutdownImGui();

    void LoadMaraeLogo();
    void RenderLoadingSplash(float progress, const char* status);
    void ReturnToProjectHub();
    void OpenAnotherProject();

    SceneManager& GetSceneManager() { return m_SceneManager; }
    const SceneManager& GetSceneManager() const { return m_SceneManager; }
    void OpenSceneDialog();
    void NewScene();
    void OpenAdditiveSceneDialog();
    void SaveSceneAsDialog();

    bool SaveScene();
    void LoadScene();
    bool LoadScene(const std::string& path, bool additive = false);  // sobrecarga com path explícito
    std::string GetRecoverySnapshotPath() const;
    bool SaveRecoverySnapshot();
    void CheckForRecoverySnapshot();
    void RenderRecoveryDialog();

    // Preferências do editor salvas automaticamente por projeto.
    void LoadEditorSettings();
    void SaveEditorSettings();
    void AutoSaveEditorSettings();
    std::string GetEditorSettingsPath() const;
    std::string BuildEditorSettingsSnapshot() const;

    // Configurações GLOBAIS do editor (persistem entre projetos — mesmo arquivo do ProjectHub)
    struct GlobalEditorSettings
    {
        bool  AutoSave          = false;
        int   AutoSaveIntervalS = 300;
        float FontScale         = 1.0f;
        int   FontIndex          = 0;
        bool  ShowFPS           = true;
        bool  VSync             = true;
        int   ThemeIndex        = 0;
        RendererMode RequestedRendererMode = RendererMode::Automatic;
        GraphicsAPI RequestedGraphicsAPI = GraphicsAPI::OpenGL;
    };
    void LoadGlobalEditorSettings();
    void SaveGlobalEditorSettings();
    std::string GetGlobalEditorSettingsPath() const;
    void ApplyGlobalEditorSettings(bool rebuildFonts = false);
    void RenderGlobalEditorSettingsModal();
    void CompleteGraphicsAPISwitch();

    bool m_ShowGlobalEditorSettings = false;
    bool m_FontRebuildPending = false;
    GlobalEditorSettings m_GlobalEditorSettings;
    double m_GlobalAutoSaveAccumulator = 0.0;

    bool m_ShowHierarchy = true;
    bool m_ShowInspector = true;
    bool m_ShowAIPanel = true;
    bool m_ShowAssetBrowser = true;
    bool m_ShowSceneSettings = false;
    bool m_ShowTimeline = false;
    bool m_ShowPixelArtEditor = false;
    bool m_ShowLogicCircuit = true;
    bool m_ShowMobilePreview = false;

    bool m_ShowGrid = true;
    bool m_ShowAxis = true;
    bool m_ShowUIInEditor = true;
    float m_GridRenderDistance = 500.0f;
    float m_ShadowIntensity = 0.5f;
    float m_AmbientLight = 0.35f;
    GraphicsQuality m_GraphicsQuality = GraphicsQuality::Balanced;
    // Preserve the selected preset when a temporary GPU safety profile is used.
    bool m_GraphicsSafetyFallbackActive = false;
    bool m_ShowGridBeforePlay = true;
    bool m_ShowAxisBeforePlay = true;
    bool m_PlayEditorVisualsSnapshotValid = false;
    bool m_ShowUnsavedDialog = false;
    bool m_ImGuiPlatformBackendInitialized = false;
    bool m_ImGuiOpenGLBackendInitialized = false;
    bool m_PendingGraphicsAPISwitch = false;
    GraphicsAPI m_PendingGraphicsAPI = GraphicsAPI::OpenGL;
    // ---- Exportador de jogo ----
    SceneManager m_SceneManager;
    bool m_ShowExportModal = false;
    void RenderExportModal();
    void RenderMobilePreview();
    double m_LastUpdateTime = 0.0;

    EditorState m_EditorState =
        EditorState::Edit;
    bool m_EditDuringPlay = true;

    int m_ViewportWidth = 1280;
    int m_ViewportHeight = 720;
    int m_PendingViewportWidth = 0;
    int m_PendingViewportHeight = 0;
    double m_ViewportResizeRequestedAt = 0.0;
    bool m_ViewportResizePending = false;
    float m_HierarchyWidth = 250.0f;
    float m_InspectorWidth = 350.0f;

    bool m_ViewportHovered = false;
    bool m_ViewportFocused = false;
    bool m_ViewportImageHovered = false;
    float m_ViewportPointerX = 0.0f;
    float m_ViewportPointerY = 0.0f;
    bool m_ViewportPointerValid = false;

    // Área onde a cena do jogo (com a UI 2D nativa) é exibida no modo runtime do
    // editor. Usada para converter o ponteiro do mouse em pixels do canvas do
    // jogo no sistema de interação da UI.
    float m_GameImageMinX = 0.0f;
    float m_GameImageMinY = 0.0f;
    float m_GameImageWidth = 0.0f;
    float m_GameImageHeight = 0.0f;
    bool m_GameImageValid = false;

    MaraeUI::UIInteractionResult m_UIInteraction;

    GizmoMode m_GizmoMode =
        GizmoMode::Translate;

    GizmoSpace m_GizmoSpace =
        GizmoSpace::World;

    bool m_FaceScaleHandlesEnabled = false;

    WorldMode m_WorldMode = WorldMode::World3D;
    Camera m_CameraBefore2D;
    bool m_HasCameraBefore2D = false;

    const AssetInfo* m_SelectedAsset = nullptr;

    std::vector<Transform> m_PlayStartTransforms;
    std::vector<Vector3> m_PlayStartVelocities;
    size_t m_PlaySnapshotEntityCount = 0;
    bool m_PlaySnapshotValid = false;
    float m_PhysicsAccumulator = 0.0f;
    // Pares em contato no último passo, usados para emitir Enter/Stay/Exit
    // de forma estável para os scripts de gameplay 2D e 3D.
    std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> m_PhysicsContactPairs;
    std::unordered_map<uint64_t, float> m_ScriptTimes;
    AudioSceneSync m_AudioSceneSync;

    // Jogos demo embutidos (corrida e sobrevivência). O estado e a lógica
    // ficam em Engine/DemoGames; aqui restam apenas os pontos de integração
    // do ciclo de vida do editor.
    RaceGame m_RaceGame;
    SurvivalGame m_SurvivalGame;
    DemoGameContext MakeDemoGameContext();

    bool m_SceneDirty = false;
    bool m_SceneHasLightingSettings = false;
    bool m_ShowRecoveryDialog = false;
    bool m_WritingRecoverySnapshot = false;
    std::string m_RecoverySnapshotPath;
    double m_RecoverySnapshotAccumulator = 0.0;
    void UpdateWindowTitle();

    bool m_EditorSettingsLoaded = false;
    bool m_EditorSettingsSavePending = false;
    double m_EditorSettingsLastChangeTime = 0.0;
    std::string m_LastEditorSettingsSnapshot;

    void SimulatePhysics(float deltaTime);
    void SimulatePhysicsStep(float fixedDeltaTime);
    void SimulatePhysics2DStep(float fixedDeltaTime);
    void ExecuteAttachedScripts(float deltaTime);
    void CapturePlaySnapshot();
    void RestorePlaySnapshot();
    // Traduz input (mouse/teclado) em estados dos widgets de UI 2D da cena.
    void UpdateUIInteraction();
};
