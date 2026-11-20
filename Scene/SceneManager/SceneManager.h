#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <cstdint>
#include <unordered_map>

class Application;
class Scene;

// ---------------------------------------------------------------------------
// SceneManager — Sistema de Cenas, Níveis, Transições e Carregamento Assíncrono
// ---------------------------------------------------------------------------

enum class SceneTransitionType
{
    None = 0,
    FadeBlack = 1,
    FadeWhite = 2,
    CrossFade = 3
};

enum class SceneTransitionState
{
    Idle,
    FadingOut,      // Tela escurecendo / opacando
    Loading,        // No ponto escuro: executa a carga da nova cena
    FadingIn        // Tela clareando / revelando a nova cena
};

struct SceneLevelInfo
{
    std::string Name;           // Ex: "Main", "Level_01", "Dungeon"
    std::string RelativePath;   // Ex: "Assets/Scenes/Main.scene"
    std::string AbsolutePath;   // Caminho absoluto no disco
    int OrderIndex = 0;         // Ordem de progressão de fases
};

class SceneManager
{
public:
    using SceneLoadedCallback = std::function<void(const std::string& scenePath, bool success)>;

    SceneManager();
    ~SceneManager();

    // ---- Carregamento de Cenas e Níveis ----
    // Carrega imediatamente de forma síncrona
    bool LoadScene(Application& app, const std::string& path);

    // Carrega de forma assíncrona em thread separada com transição visual
    void LoadSceneAsync(Application& app,
                        const std::string& path,
                        SceneTransitionType transition = SceneTransitionType::FadeBlack,
                        float transitionDuration = 0.5f,
                        SceneLoadedCallback onComplete = nullptr);

    // Carrega cena aditiva (adiciona entidades sem apagar o mundo existente)
    bool LoadSceneAdditive(Application& app, const std::string& path);

    // Limpa a cena e inicializa uma nova cena padrão
    void NewScene(Application& app);

    // Salva a cena atual no caminho ativo ou especificado
    bool SaveActiveScene(Application& app, const std::string& customPath = "");

    // Transição manual direta (fade)
    void TriggerTransition(SceneTransitionType type, float duration = 0.5f);

    // Atualização chamada a cada frame no loop principal do Application
    void Update(Application& app, float deltaTime);

    // Renderiza o overlay visual de transição e barra de carregamento assíncrono
    void RenderTransitionOverlay(float screenWidth, float screenHeight);

    // ---- Gerenciamento de Níveis do Projeto ----
    // Descobre e cataloga todas as cenas .scene presentes no projeto
    void RefreshProjectLevels(const std::string& projectRootPath);

    const std::vector<SceneLevelInfo>& GetProjectLevels() const { return m_ProjectLevels; }

    // Carrega próximo nível na ordem catalogada
    bool LoadNextLevel(Application& app, SceneTransitionType transition = SceneTransitionType::FadeBlack);

    // ---- Status e Consultas ----
    const std::string& GetActiveScenePath() const { return m_ActiveScenePath; }
    std::string GetActiveSceneName() const;

    bool IsLoading() const { return m_IsAsyncLoading.load(); }
    float GetLoadingProgress() const { return m_LoadingProgress.load(); }
    std::string GetLoadingStatus() const;

    bool IsInTransition() const { return m_TransitionState != SceneTransitionState::Idle; }
    SceneTransitionState GetTransitionState() const { return m_TransitionState; }

    // Registra nomes de cenas aditivas atualmente carregadas
    const std::vector<std::string>& GetAdditiveScenes() const { return m_LoadedAdditiveScenes; }
    void UnloadAdditiveScene(Application& app, const std::string& scenePath);

private:
    bool ExecuteLoadSceneInternal(Application& app, const std::string& path, bool isAdditive);

    std::string m_ActiveScenePath;
    std::vector<std::string> m_LoadedAdditiveScenes;
    std::unordered_map<std::string, std::vector<uint64_t>> m_AdditiveEntityIDs;
    std::vector<SceneLevelInfo> m_ProjectLevels;

    // Estado da Transição
    SceneTransitionType  m_TransitionType = SceneTransitionType::None;
    SceneTransitionState m_TransitionState = SceneTransitionState::Idle;
    float m_TransitionDuration = 0.5f;
    float m_TransitionTimer = 0.0f;
    float m_TransitionAlpha = 0.0f;

    // Estado do Carregamento Assíncrono
    std::atomic<bool>  m_IsAsyncLoading{ false };
    std::atomic<float> m_LoadingProgress{ 0.0f };
    std::string        m_LoadingStatus;
    mutable std::mutex m_StatusMutex;

    std::string         m_PendingScenePath;
    SceneLoadedCallback m_PendingCallback = nullptr;
    std::unique_ptr<std::thread> m_AsyncThread;

    // Buffer de dados da cena lida em background
    std::string m_AsyncFileContent;
    std::atomic<bool> m_AsyncReadDone{ false };
    std::atomic<bool> m_AsyncReadSuccess{ false };
};
