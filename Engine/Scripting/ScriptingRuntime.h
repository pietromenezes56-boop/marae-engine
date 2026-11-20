#pragma once

#include "ScriptingAPI.h"
#include "../Navigation/NavigationSystem.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <vector>

class Scene;
class Entity;

class ScriptingRuntime
{
public:
    static ScriptingRuntime& Instance();

    ScriptingRuntime(const ScriptingRuntime&) = delete;
    ScriptingRuntime& operator=(const ScriptingRuntime&) = delete;

    bool Initialize(const std::string& projectRoot);
    void Shutdown();

    void OnEnterPlay(Scene* scene);
    void OnExitPlay();
    void BeginFrame(float dt);
    void OnFixedUpdate(float fixedDt);
    void OnUpdate(float dt);

    void ReloadAll();
    // Compila o projeto e retorna os diagnósticos estruturados do Roslyn.
    std::string CompileProjectScripts();
    std::string FindNavigationPath(float sx, float sy, float sz, float dx, float dy, float dz);

    // Executa o controller C# da entidade indicada com estados de teclado
    // sintetizados, validando o caminho Input -> script -> Transform.
    int RunWASDInputRegressionTests(Scene& scene, const std::string& entityName);
    // Exercita o script CameraFlyController com teclado e mouse sintetizados.
    int RunCameraFlyRegressionTests(Scene& scene);
    // Compila e executa scripts de regressão para a API C# 2D/3D e componentes.
    int RunComponentApiRegressionTests(Scene& scene, bool world2D);

    void FireCollisionEvent(uint64_t entityA, uint64_t entityB, int eventType,
                            float cx = 0.0f, float cy = 0.0f, float cz = 0.0f,
                            float nx = 0.0f, float ny = 0.0f, float nz = 0.0f,
                            float impulse = 0.0f);

    // Eventos de UI 2D disparados pelo sistema de interação nativo.
    void FireUIClicked(uint64_t entityId);
    void FireUIValueChanged(uint64_t entityId, float value);
    void FireUITextChanged(uint64_t entityId, const std::string& text);

    // Inspector: retorna JSON de campos públicos de uma instância ativa.
    // Formato: {"Speed":{"type":"float","value":"60"},"Active":{"type":"bool","value":"true"},...}
    // Retorna "" se a instância não existe ou não está em play.
    std::string GetPublicFieldsJson(uint64_t entityId) const;

    // Inspector: aplica um novo valor JSON a um campo específico.
    void ApplyFieldValue(uint64_t entityId, const std::string& fieldName, const std::string& jsonValue);

    bool IsInitialized()   const { return m_Initialized;   }
    bool IsHostFxrLoaded() const { return m_HostFxrLoaded; }

    Scene* GetCurrentScene() const { return m_CurrentScene; }

private:
    ScriptingRuntime() = default;
    ~ScriptingRuntime() = default;

    void PopulateNativeAPI();
    bool InitializeHostFxr(const std::string& projectRoot);
    bool LoadManagedAssembly(const std::string& projectRoot);
    void CreateScriptInstancesForScene();
    void DestroyAllScriptInstances();
    void CallOnStartForNewInstances();

    bool m_Initialized = false;
    bool m_HostFxrLoaded = false;
    Scene* m_CurrentScene = nullptr;

    NativeAPI m_NativeAPI{};
    ManagedAPI m_ManagedAPI{};
    InitializeBootstrapFn m_InitializeBootstrap = nullptr;

    std::unordered_map<uint64_t, uint64_t> m_EntityToInstance;
    std::unordered_set<uint64_t>           m_StartedInstances;
    std::unordered_map<uint64_t, bool>     m_EnabledLastFrame;  // rastreia mudanças OnEnable/OnDisable
    std::vector<uint64_t> m_UpdateEntityIDsScratch;

    std::string m_ProjectRoot;
    NavigationSystem m_NavigationSystem;

    void* m_NetHostModule = nullptr;
    void* m_HostFxrContext = nullptr;
};
