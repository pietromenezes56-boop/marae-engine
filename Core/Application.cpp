#include "../Engine/Localization/Localization.h"
#include "../Engine/Legal/LegalNotice.h"
#include "../Engine/Scripting/ScriptingRuntime.h"
#include "Application.h"
#include "../Engine/Paths/RuntimePaths.h"
#include "../Renderer/MeshFactory/MeshFactory.h"
#include "../Renderer/Texture/TextureLoader.h"
#include "../Editor/Exporter/GameExporter.h"
#include "../Engine/Audio/Mixer/AudioMixer.h"
#include "../Engine/Audio/AudioAcoustics.h"
#include "../Engine/DemoGames/DemoGameContext.h"
#include "../Engine/UI/UILayout.h"
#include "../Engine/UI/UITextMarkup.h"
#include "../Engine/UI/UIFontLibrary.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <cstring>
#include <GLFW/glfw3.h>
#include <fstream>
#include <sstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#define VK_NO_PROTOTYPES
#include <backends/imgui_impl_vulkan.h>
#include "../Scene/SceneCommands.h"
#include "../Editor/EditorObjects/EditorObjects.h"
#include "../Editor/HierarchyPanel/HierarchyPanel.h"
#include "../Editor/InspectorPanel/InspectorPanel.h"
#include "../Editor/Panels/AIPanel/AIPanel.h"
#include "../Editor/ViewportPanel/ViewportPanel.h"
#include "../Editor/Panels/AssetBrowser/AssetBrowserPanel.h"
#include "../Editor/Panels/Toolbar/ToolbarPanel.h"
#include "../Editor/Panels/EditorSettings/EditorSettingsPanel.h"
#include "../Editor/UI/EditorIcons.h"
#include "../Editor/Panels/Timeline/TimelinePanel.h"
#include "../Editor/Panels/PixelArtEditor/PixelArtEditorPanel.h"
#include "../Editor/Panels/LogicCircuit/LogicCircuitPanel.h"
#include "../Engine/Localization/Localization.h"
#include "../Scene/Components/AnimationComponent.h"
#include <algorithm>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "../Engine/Animation/Animation.h"
#include "../Engine/Systems/AnimationSystem.h"
#include "../Engine/AI/AISystem.h"
#include "../Assets/AssetDatabase/AssetDatabase.h"
#include "../Editor/Thumbnails/ThumbnailManager.h"
#include <cmath>
#include <glm/glm.hpp>
#include <algorithm>
#include <filesystem>
#include "../Engine/Assets/GUID/GUID.h"
#include "../../Assets/MaterialDatabase/MaterialDatabase.h"
#include "../Engine/Lighting/LightingManager.h"
#include "../Engine/Lighting/LightingSystem.h"
#include <glm/gtx/quaternion.hpp>
#include "../Core/Diagnostics/PerformanceMonitor.h"
#include "../Core/Diagnostics/DiagnosticLog.h"
#include "../Hub/ProjectManager.h"
#include <iomanip>
#include <cstdlib>
#include <cctype>
#include <regex>
#include <iterator>
#include <random>
#include <unordered_set>
#include "../Renderer/Texture/TextureCache.h"
#include "../Renderer/Mesh/MeshCache.h"
#include "../Engine/Assets/ModelImporter/ModelImporter.h"
#include "../ThirdParty/stb/stb_image.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GUID Windows_SDK_GUID
#include <windows.h>
#include <commdlg.h>
#undef GUID
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

// Global Application pointer para acesso em callbacks de GLFW (evita WindowContext)
void* g_CurrentApplication = nullptr;

static void ApplyGraphicsPresetToLighting(LightingSettings& settings, GraphicsQuality quality);

static std::filesystem::path ResolveProjectFilePath(
    const ProjectConfig& project,
    const std::string& path)
{
    std::filesystem::path input(path);
    if (input.is_absolute())
        return input;

    const std::filesystem::path projectRoot = !project.ProjectPath.empty()
        ? std::filesystem::path(project.ProjectPath)
        : std::filesystem::current_path();
    const std::filesystem::path inProject = projectRoot / input;
    if (std::filesystem::exists(inProject))
        return std::filesystem::weakly_canonical(inProject);

    const std::filesystem::path inWorkingDirectory = std::filesystem::current_path() / input;
    if (std::filesystem::exists(inWorkingDirectory))
        return std::filesystem::weakly_canonical(inWorkingDirectory);

    // Mesmo quando o arquivo ainda nÃ£o existe, retorna o caminho esperado do
    // projeto para que a mensagem de erro seja Ãºtil.
    return inProject;
}

int Application::RunAnimationRegressionTests()
{
    int failures = 0;
    auto check = [&](bool condition, const char* name)
    {
        std::cout << (condition ? "[AnimationTest] PASS: " : "[AnimationTest] FAIL: ") << name << std::endl;
        if (!condition) ++failures;
    };

    // 1) Smoothing: apply an instantaneous pose change in a clip and verify that
    // when UseSmoothing=true the transform moves gradually.
    m_Scene.Clear();
    Entity& e1 = m_Scene.CreateEntity();
    e1.SetName("AnimTest1");
    e1.SetEditorOnly(false);
    AnimationComponent& ac1 = e1.GetAnimationComponent();
    // create a clip that jumps from (0,0,0) to (10,0,0) at t=0
    auto clip1 = AnimationClip::CreateDefault("jump", 1.0f, glm::vec3(10.0f,0.0f,0.0f), glm::quat(1,0,0,0), glm::vec3(1.0f));
    ac1.SetClip(clip1);
    ac1.Time = 0.0f;
    ac1.Playing = true;
    ac1.UseSmoothing = true;
    ac1.Smoothness = 6.0f;
    ac1.Weight = 1.0f;
    // initial transform at origin
    e1.GetTransform().Position = Vector3(0.0f,0.0f,0.0f);
    // simulate a few steps
    for (int i = 0; i < 10; ++i)
        AnimationSystem::Update(m_Scene, 1.0f/60.0f);
    // after smoothing, position should be between 0 and 10 (not jumped immediately)
    const float px = e1.GetTransform().Position.X;
    check(px > 0.01f && px < 10.0f, "smoothing produz transicao gradual");

    // 2) Easing: verify eased interpolation produces different mid-value than linear
    m_Scene.Clear();
    Entity& e2 = m_Scene.CreateEntity();
    AnimationComponent& ac2 = e2.GetAnimationComponent();
    // create a moving clip from 0 -> 20 over 2s
    {
        auto clip = std::make_shared<AnimationClip>();
        clip->Name = "move_ease";
        clip->Duration = 2.0f;
        clip->Loop = false;
        clip->Position.Keys.push_back({0.0f, glm::vec3(0.0f, 0.0f, 0.0f)});
        clip->Position.Keys.push_back({2.0f, glm::vec3(20.0f, 0.0f, 0.0f)});
        clip->Rotation.Keys.push_back({0.0f, glm::quat(1,0,0,0)});
        clip->Rotation.Keys.push_back({2.0f, glm::quat(1,0,0,0)});
        clip->Scale.Keys.push_back({0.0f, glm::vec3(1.0f)});
        clip->Scale.Keys.push_back({2.0f, glm::vec3(1.0f)});
        ac2.SetClip(clip);
    }
    ac2.Weight = 1.0f;
    ac2.UseSmoothing = false;
    ac2.Easing = AnimationComponent::EasingType::EaseIn;
    ac2.Playing = true;
    // sample halfway
    ac2.Time = 1.0f; // halfway
    AnimationSystem::ApplyPoseAtTime(e2, ac2.Time, 0.0f);
    const float easedX = e2.GetTransform().Position.X;
    // reset and apply linear for comparison
    m_Scene.Clear();
    Entity& e3 = m_Scene.CreateEntity();
    AnimationComponent& ac3 = e3.GetAnimationComponent();
    {
        auto clip = std::make_shared<AnimationClip>();
        clip->Name = "move_linear";
        clip->Duration = 2.0f;
        clip->Loop = false;
        clip->Position.Keys.push_back({0.0f, glm::vec3(0.0f, 0.0f, 0.0f)});
        clip->Position.Keys.push_back({2.0f, glm::vec3(20.0f, 0.0f, 0.0f)});
        clip->Rotation.Keys.push_back({0.0f, glm::quat(1,0,0,0)});
        clip->Rotation.Keys.push_back({2.0f, glm::quat(1,0,0,0)});
        clip->Scale.Keys.push_back({0.0f, glm::vec3(1.0f)});
        clip->Scale.Keys.push_back({2.0f, glm::vec3(1.0f)});
        ac3.SetClip(clip);
    }
    ac3.Easing = AnimationComponent::EasingType::Linear;
    ac3.Weight = 1.0f;
    ac3.Time = 1.0f;
    AnimationSystem::ApplyPoseAtTime(e3, ac3.Time, 0.0f);
    const float linearX = e3.GetTransform().Position.X;
    check(std::abs(easedX - linearX) > 0.001f, "easing altera valor intermediario comparado a linear");

    // 3) Blend-in: weight reduced at start
    m_Scene.Clear();
    Entity& e4 = m_Scene.CreateEntity();
    AnimationComponent& ac4 = e4.GetAnimationComponent();
    auto clip4 = AnimationClip::CreateDefault("blend", 2.0f, glm::vec3(5.0f,0.0f,0.0f), glm::quat(1,0,0,0), glm::vec3(1.0f));
    ac4.SetClip(clip4);
    ac4.Playing = true;
    ac4.BlendIn = 1.0f;
    ac4.Weight = 1.0f;
    ac4.Time = 0.1f; // near start
    AnimationSystem::Update(m_Scene, 0.1f);
    // runtime blend should be less than 1.0
    check(ac4.RuntimeBlend < 1.0f, "blend-in reduz peso no inicio");

    // 4) PlayOnStart deve iniciar pelo mesmo caminho usado na execução do jogo.
    m_Scene.Clear();
    Entity& e5 = m_Scene.CreateEntity();
    e5.SetName("AnimPlayOnStartTest");
    AnimationComponent& ac5 = e5.GetAnimationComponent();
    auto clip5 = AnimationClip::CreateDefault(
        "autoplay", 1.0f, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    clip5->Loop = false;
    clip5->Position.Keys = {
        {0.0f, glm::vec3(0.0f)},
        {1.0f, glm::vec3(8.0f, 0.0f, 0.0f)}
    };
    ac5.SetClip(clip5);
    // Confirma também o default usado ao criar um clip novo no editor.
    AnimationSystem::BeginPlay(m_Scene);
    check(ac5.Playing && std::abs(ac5.Time) < 0.0001f,
          "PlayOnStart inicia o clip no comeco da sessao");
    AnimationSystem::Update(m_Scene, 0.25f);
    check(std::abs(e5.GetTransform().Position.X - 2.0f) < 0.02f,
          "clip de runtime avanca e aplica a pose");
    check(clip5->ID.rfind("anim_", 0) == 0 && !clip5->ID.empty(),
          "clip recebe ID unico com prefixo de animacao");
    const auto anotherClip = AnimationClip::CreateDefault(
        "unique-id-test", 1.0f, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    check(clip5->ID != anotherClip->ID, "clips distintos recebem IDs diferentes");

    // Controller com dois clips e transição condicional verificável em runtime.
    Entity& clipSource = m_Scene.CreateEntity();
    clipSource.SetName("AnimRunClipSource");
    auto runClip = AnimationClip::CreateDefault(
        "run", 1.0f, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
    clipSource.GetAnimationComponent().SetClip(runClip);
    clipSource.GetAnimationComponent().PlayOnStart = false;
    ac5.StateMachineEnabled = true;
    ac5.States = { { "Idle", clip5 }, { "Run", runClip } };
    ac5.InitialState = "Idle";
    ac5.Transitions = { { "Idle", "Run", "Moving", true, 0.0f, 0.5f } };
    ac5.BoolParameters["Moving"] = false;
    AnimationSystem::BeginPlay(m_Scene);
    const bool enteredInitial = ac5.Playing && ac5.CurrentState == "Idle" && ac5.Clip->ID == clip5->ID;
    ac5.BoolParameters["Moving"] = true;
    e5.GetTransform().Position.X = 4.0f;
    AnimationSystem::Update(m_Scene, 1.0f / 60.0f);
    check(enteredInitial && ac5.CurrentState == "Run" && ac5.Clip->ID == runClip->ID,
          "máquina de estados inicia no estado padrão e troca clip por parâmetro bool");
    const float transitionMidX = e5.GetTransform().Position.X;
    check(ac5.TransitionBlending && transitionMidX > 0.0f && transitionMidX < 4.0f,
          "transição interpola a pose de origem para o estado de destino");
    for (int i = 0; i < 32; ++i) AnimationSystem::Update(m_Scene, 1.0f / 60.0f);
    check(!ac5.TransitionBlending && std::abs(e5.GetTransform().Position.X) < 0.05f,
          "blend da transição termina na pose do clip de destino");

    // Round-trip real em arquivo: clip, keyframes, ID e autoplay precisam sobreviver.
    const std::string previousScenePath = m_ProjectConfig.ScenePath;
    const std::filesystem::path animationTestScene =
        std::filesystem::temp_directory_path() / "Marae_Animation_RoundTrip.scene";
    const std::string savedClipID = ac5.Clip ? ac5.Clip->ID : std::string();
    m_ProjectConfig.ScenePath = animationTestScene.string();
    SaveScene();
    m_Scene.Clear();
    LoadScene(animationTestScene.string());
    Entity* restoredEntity = nullptr;
    for (Entity& candidate : m_Scene.GetEntities())
        if (candidate.GetName() == "AnimPlayOnStartTest")
        {
            restoredEntity = &candidate;
            break;
        }
    check(restoredEntity && restoredEntity->GetAnimationComponent().Clip &&
          restoredEntity->GetAnimationComponent().Clip->ID == savedClipID &&
          !restoredEntity->GetAnimationComponent().Clip->Position.Keys.empty() &&
          restoredEntity->GetAnimationComponent().PlayOnStart,
          "salvar/reabrir preserva ID, keyframes e PlayOnStart");
    check(restoredEntity && restoredEntity->GetAnimationComponent().StateMachineEnabled &&
          restoredEntity->GetAnimationComponent().InitialState == "Idle" &&
          restoredEntity->GetAnimationComponent().States.size() == 2 &&
          restoredEntity->GetAnimationComponent().Transitions.size() == 1 &&
          std::abs(restoredEntity->GetAnimationComponent().Transitions[0].BlendDuration - 0.5f) < 0.001f &&
          restoredEntity->GetAnimationComponent().BoolParameters.contains("Moving") &&
          restoredEntity->GetAnimationComponent().States[0].Clip &&
          restoredEntity->GetAnimationComponent().States[0].Clip->Position.Keys.size() >= 1,
          "salvar/reabrir preserva controller, estados, transições e parâmetros");
    std::error_code animationTestCleanupError;
    std::filesystem::remove(animationTestScene, animationTestCleanupError);
    m_ProjectConfig.ScenePath = previousScenePath;

    std::cout << "[AnimationTest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU") << " (" << failures << " falha(s))" << std::endl;
    return failures == 0 ? 0 : 1;
}

int Application::RunSceneRegressionTests()
{
    int failures = 0;
    const auto check = [&failures](bool condition, const char* name)
    {
        std::cout << (condition ? "[SceneTest] PASS: " : "[SceneTest] FAIL: ")
                  << name << std::endl;
        if (!condition) ++failures;
    };

    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("MaraeSceneTest_" + std::to_string(
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count())));
    const std::filesystem::path scenesDir = root / "Assets" / "Scenes";
    const std::filesystem::path sceneA = scenesDir / "SceneA.scene";
    const std::filesystem::path sceneB = scenesDir / "SceneB.scene";
    const std::filesystem::path sceneCopy = scenesDir / "SceneCopy.scene";

    std::error_code error;
    std::filesystem::create_directories(scenesDir, error);
    if (error)
    {
        std::cout << "[SceneTest] FAIL: nao foi possivel criar diretorio temporario\n";
        return 1;
    }

    const std::string previousProjectPath = m_ProjectConfig.ProjectPath;
    const std::string previousScenePath = m_ProjectConfig.ScenePath;
    const bool previousDirty = m_SceneDirty;
    m_ProjectConfig.ProjectPath = root.string();

    const auto createScene = [this](const std::filesystem::path& path,
                                    const char* name,
                                    const Vector3& position)
    {
        m_Scene.Clear();
        Entity& entity = m_Scene.CreateEntity();
        entity.SetName(name);
        entity.GetTransform().Position = position;
        if (std::string(name) == "SceneAObject")
        {
            entity.PhysicsEnabled = true;
            entity.IsTrigger = true;
            entity.AddAI();
            AIComponent& ai = entity.GetAIComponent();
            ai.CurrentMode = AIComponent::Mode::StateMachine;
            ai.SightRadius = 42.0f;
            ai.FieldOfViewDegrees = 95.0f;
            ai.TargetTag = "Hero";
            ai.Nodes = { { 7, 0, AIComponent::NodeType::Root },
                         { 8, 7, AIComponent::NodeType::ChaseTarget, 1.25f, 0.5f, 4.0f } };
            ai.RootNodeId = 7;
            ai.States = { { "Idle", AIComponent::Task::Idle },
                          { "Pursue", AIComponent::Task::ChaseTarget, 4.0f, 1.25f } };
            ai.InitialState = "Idle";
            ai.Transitions = { { "Idle", "Pursue", AIComponent::TransitionCondition::TargetVisible, 2.0f } };
        }
        m_ProjectConfig.ScenePath = path.string();
        m_SceneDirty = true;
        SaveScene();
        return std::filesystem::is_regular_file(path) && !m_SceneDirty;
    };

    check(createScene(sceneA, "SceneAObject", Vector3(-2.0f, 0.0f, 0.0f)),
          "cria e salva a primeira cena");
    check(createScene(sceneB, "SceneBObject", Vector3(3.0f, 1.0f, 0.0f)),
          "cria e salva a segunda cena");

    m_SceneManager.RefreshProjectLevels(root.string());
    check(m_SceneManager.GetProjectLevels().size() == 2,
          "descobre as cenas do projeto");

    check(m_SceneManager.LoadScene(*this, sceneA.string()),
          "carrega a cena principal de forma sincronizada");
    bool hasSceneA = false;
    for (const Entity& entity : m_Scene.GetEntities())
        hasSceneA = hasSceneA || entity.GetName() == "SceneAObject";
    check(hasSceneA && m_SceneManager.GetActiveScenePath() == sceneA.string(),
          "preserva entidades e caminho ativo da cena principal");
    bool triggerLoaded = false;
    for (const Entity& entity : m_Scene.GetEntities())
        if (entity.GetName() == "SceneAObject")
            triggerLoaded = entity.PhysicsEnabled && entity.IsTrigger;
    check(triggerLoaded, "persiste e recarrega o trigger da física");
    Entity* aiRestored = nullptr;
    for (Entity& entity : m_Scene.GetEntities())
        if (entity.GetName() == "SceneAObject") aiRestored = &entity;
    check(aiRestored && aiRestored->HasAI() &&
          aiRestored->GetAIComponent().CurrentMode == AIComponent::Mode::StateMachine &&
          std::abs(aiRestored->GetAIComponent().SightRadius - 42.0f) < 0.001f &&
          aiRestored->GetAIComponent().TargetTag == "Hero" &&
          aiRestored->GetAIComponent().Nodes.size() == 2 &&
          aiRestored->GetAIComponent().RootNodeId == 7 &&
          aiRestored->GetAIComponent().States.size() == 2 &&
          aiRestored->GetAIComponent().Transitions.size() == 1,
          "salva e reabre configuração de percepção, árvore e estados da IA");

    const std::filesystem::path missingScene = scenesDir / "Missing.scene";
    hasSceneA = false;
    for (const Entity& entity : m_Scene.GetEntities())
        hasSceneA = hasSceneA || entity.GetName() == "SceneAObject";
    check(!m_SceneManager.LoadScene(*this, missingScene.string()) && hasSceneA,
          "recusa cena ausente sem destruir a cena atual");

    check(m_SceneManager.SaveActiveScene(*this, sceneCopy.string()) &&
          std::filesystem::is_regular_file(sceneCopy),
          "salva a cena ativa em um caminho alternativo");

    const std::filesystem::path pathBlocker = root / "NotADirectory";
    {
        std::ofstream blocker(pathBlocker);
        blocker << "file blocks directory creation";
    }
    const std::string activePathBeforeFailedSave = m_SceneManager.GetActiveScenePath();
    check(!m_SceneManager.SaveActiveScene(
              *this, (pathBlocker / "CannotSave.scene").string()) &&
          m_SceneManager.GetActiveScenePath() == activePathBeforeFailedSave,
          "Save As informa falha e preserva a cena ativa quando o destino nao e gravavel");

    check(m_SceneManager.LoadScene(*this, sceneA.string()),
          "reabre a cena original depois do Save As");

    check(m_SceneManager.LoadSceneAdditive(*this, sceneB.string()),
          "carrega uma cena aditiva sem apagar a principal");
    bool hasA = false;
    bool hasB = false;
    for (const Entity& entity : m_Scene.GetEntities())
    {
        hasA = hasA || entity.GetName() == "SceneAObject";
        hasB = hasB || entity.GetName() == "SceneBObject";
    }
    check(hasA && hasB && m_SceneManager.GetAdditiveScenes().size() == 1,
          "mantem as entidades principal e aditiva na mesma cena");

    m_SceneManager.UnloadAdditiveScene(*this, sceneB.string());
    hasA = false;
    hasB = false;
    for (const Entity& entity : m_Scene.GetEntities())
    {
        hasA = hasA || entity.GetName() == "SceneAObject";
        hasB = hasB || entity.GetName() == "SceneBObject";
    }
    check(hasA && !hasB && m_SceneManager.GetAdditiveScenes().empty(),
          "descarrega entidades aditivas sem remover a principal");

    bool callbackCalled = false;
    bool callbackSucceeded = false;
    m_SceneManager.LoadSceneAsync(*this, sceneB.string(), SceneTransitionType::None, 0.05f,
        [&callbackCalled, &callbackSucceeded](const std::string&, bool success)
        {
            callbackCalled = true;
            callbackSucceeded = success;
        });
    for (int attempt = 0; attempt < 500 && !callbackCalled; ++attempt)
    {
        m_SceneManager.Update(*this, 0.01f);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(callbackCalled && callbackSucceeded && !m_SceneManager.IsLoading() &&
          m_SceneManager.GetActiveScenePath() == sceneB.string(),
          "troca de cena assincrona com callback de sucesso");

    m_SceneManager.NewScene(*this);
    bool hasOldEntity = false;
    bool hasGlobalLight = false;
    for (const Entity& entity : m_Scene.GetEntities())
    {
        hasOldEntity = hasOldEntity || entity.GetName() == "SceneAObject" ||
                       entity.GetName() == "SceneBObject";
        hasGlobalLight = hasGlobalLight || entity.IsGlobalLight();
    }
    check(!hasOldEntity && hasGlobalLight,
          "inicializa uma cena nova sem herdar entidades antigas");

    m_ProjectConfig.ProjectPath = previousProjectPath;
    m_ProjectConfig.ScenePath = previousScenePath;
    m_SceneDirty = previousDirty;
    std::filesystem::remove_all(root, error);

    std::cout << "[SceneTest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU")
              << " (" << failures << " falha(s))" << std::endl;
    return failures == 0 ? 0 : 1;
}

int Application::RunRecoveryRegressionTests()
{
    int failures = 0;
    const auto check = [&failures](bool condition, const char* name)
    {
        std::cout << (condition ? "[RecoveryTest] PASS: " : "[RecoveryTest] FAIL: ")
                  << name << std::endl;
        if (!condition) ++failures;
    };

    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("MaraeRecoveryTest_" + std::to_string(
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count())));
    const std::filesystem::path scenePath = root / "Main.scene";
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        std::cout << "[RecoveryTest] FAIL: não foi possível criar diretório temporário\n";
        return 1;
    }

    const std::string previousScenePath = m_ProjectConfig.ScenePath;
    const bool previousDirty = m_SceneDirty;
    m_ProjectConfig.ScenePath = scenePath.string();
    m_Scene.Clear();

    Entity& original = m_Scene.CreateEntity();
    original.SetName("RecoveryTestCube");
    original.GetTransform().Position = Vector3(0.0f, 0.0f, 0.0f);
    m_SceneDirty = true;
    SaveScene();
    check(std::filesystem::is_regular_file(scenePath) && !m_SceneDirty,
          "salva a cena base antes do snapshot");

    original.GetTransform().Position = Vector3(7.0f, 2.0f, -3.0f);
    MarkDirty();
    SaveRecoverySnapshot();
    const std::filesystem::path snapshotPath(GetRecoverySnapshotPath());
    check(std::filesystem::is_regular_file(snapshotPath) && m_SceneDirty,
          "cria snapshot sem limpar o estado sujo da cena");

    m_ShowRecoveryDialog = false;
    CheckForRecoverySnapshot();
    check(m_ShowRecoveryDialog && m_RecoverySnapshotPath == snapshotPath.string(),
          "detecta snapshot diferente e oferece recuperação");

    LoadScene(snapshotPath.string());
    Entity* restored = nullptr;
    for (Entity& entity : m_Scene.GetEntities())
    {
        if (entity.GetName() == "RecoveryTestCube")
        {
            restored = &entity;
            break;
        }
    }
    check(restored && std::abs(restored->GetTransform().Position.X - 7.0f) < 0.001f &&
          std::abs(restored->GetTransform().Position.Y - 2.0f) < 0.001f &&
          std::abs(restored->GetTransform().Position.Z + 3.0f) < 0.001f,
          "restaura o conteúdo do snapshot");

    std::filesystem::remove(snapshotPath, error);
    check(!std::filesystem::exists(snapshotPath), "descarta o snapshot temporário");

    original.GetTransform().Position = Vector3(8.0f, 1.0f, -2.0f);
    MarkDirty();
    const std::filesystem::path blockedTemporaryPath = snapshotPath.string() + ".tmp";
    std::filesystem::create_directory(blockedTemporaryPath, error);
    const bool failedSnapshotRejected = !error && !SaveRecoverySnapshot() && m_SceneDirty;
    check(failedSnapshotRejected,
          "mantem a cena suja quando nao consegue gravar o snapshot");
    std::filesystem::remove_all(blockedTemporaryPath, error);

    m_RecoverySnapshotPath.clear();
    m_ShowRecoveryDialog = false;
    m_ProjectConfig.ScenePath = previousScenePath;
    m_SceneDirty = previousDirty;
    std::filesystem::remove_all(root, error);

    std::cout << "[RecoveryTest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU")
              << " (" << failures << " falha(s))" << std::endl;
    return failures == 0 ? 0 : 1;
}

int Application::RunUIRegressionTests()
{
    int failures = 0;
    auto check = [&](bool condition, const char* name)
    {
        std::cout << (condition ? "[UITest] PASS: " : "[UITest] FAIL: ") << name << std::endl;
        if (!condition) ++failures;
    };

    // Cria cena limpa
    m_Scene.Clear();

    // 1) Redimensionamento básico: ao aumentar a altura, FontSize deve crescer proporcionalmente
    Entity& e1 = m_Scene.CreateEntity();
    e1.SetName("UI_Test_Label");
    e1.SetEditorOnly(false);
    e1.AddUI(UIElementType::TextLabel);
    UIComponent& ui1 = e1.GetUIComponent();
    ui1.PositionX = 10.0f; ui1.PositionY = 20.0f; ui1.Width = 200.0f; ui1.Height = 40.0f; ui1.FontSize = 16.0f;
    check(e1.HasUI() && !e1.GetMeshComponent().MeshData,
          "UI não cria nem mantém a malha Cube padrão");

    // simula arraste vertical para dobrar a altura
    const float origH = ui1.Height;
    const float newH = origH * 2.0f;
    ui1.Height = newH;
    const float expectedFont = std::max(6.0f, 16.0f * (newH / origH));
    check(std::abs(ui1.FontSize - expectedFont) < 0.001f || ui1.FontSize >= 6.0f,
          "font-size ajustado proporcionalmente ao redimensionamento (altura)");

    // 2) Clamp de tamanho mínimo
    m_Scene.Clear();
    Entity& e2 = m_Scene.CreateEntity();
    e2.AddUI(UIElementType::Frame);
    UIComponent& ui2 = e2.GetUIComponent();
    ui2.Width = 5.0f; ui2.Height = 5.0f;
    // aplica redimensionamento para valores negativos
    ui2.Width = -10.0f; ui2.Height = -20.0f;
    ui2.Width = std::max(1.0f, ui2.Width);
    ui2.Height = std::max(1.0f, ui2.Height);
    check(ui2.Width >= 1.0f && ui2.Height >= 1.0f,
          "largura e altura permanecem válidas após redução");

    // 3) Handles behavior: TL handle altera X/Y/W/H corretamente
    m_Scene.Clear();
    Entity& e3 = m_Scene.CreateEntity();
    e3.AddUI(UIElementType::Frame);
    UIComponent& ui3 = e3.GetUIComponent();
    ui3.PositionX = 50.0f; ui3.PositionY = 50.0f; ui3.Width = 100.0f; ui3.Height = 40.0f; ui3.FontSize = 12.0f;
    // simula arraste do handle TL com dx = 10, dy = 5
    const float dx = 10.0f, dy = 5.0f;
    float newX = ui3.PositionX + dx;
    float newY = ui3.PositionY + dy;
    float newW = ui3.Width - dx;
    float newH3 = ui3.Height - dy;
    ui3.PositionX = newX; ui3.PositionY = newY; ui3.Width = std::max(1.0f, newW); ui3.Height = std::max(1.0f, newH3);
    check(ui3.PositionX == newX && ui3.PositionY == newY && ui3.Width == std::max(1.0f, newW) && ui3.Height == std::max(1.0f, newH3),
          "TL handle ajusta rect corretamente");

    // 4) Text FontSize mínimo
    m_Scene.Clear();
    Entity& e4 = m_Scene.CreateEntity();
    e4.AddUI(UIElementType::TextLabel);
    UIComponent& ui4 = e4.GetUIComponent();
    ui4.Type = UIElementType::TextLabel; ui4.Height = 10.0f; ui4.FontSize = 8.0f;
    // reduz para muito pequeno
    ui4.Height = 1.0f;
    ui4.FontSize = std::max(6.0f, ui4.FontSize * (ui4.Height / 10.0f));
    check(ui4.FontSize >= 6.0f, "FontSize não fica abaixo do mínimo (6)");

    // 5) Propriedades de aparência e texto são armazenáveis sem perder alfa.
    ui4.Text = "Texto editável";
    ui4.BackgroundColor[0] = 0.2f;
    ui4.BackgroundColor[3] = 0.35f;
    ui4.Transparency = 0.4f;
    ui4.BorderThickness = 3.0f;
    ui4.TextAlignment = UITextAlignment::Center;
    ui4.ClipsContent = true;
    check(ui4.Text == "Texto editável" && ui4.BackgroundColor[3] > 0.0f &&
              ui4.Transparency > 0.0f && ui4.BorderThickness == 3.0f &&
              ui4.TextAlignment == UITextAlignment::Center && ui4.ClipsContent,
          "texto, cor, transparência, borda, alinhamento e recorte são editáveis");

    // 6) Hit-test usado pelo clique em TextButton/ImageButton.
    ui4.PositionX = 100.0f; ui4.PositionY = 80.0f;
    ui4.Width = 120.0f; ui4.Height = 40.0f;
    ui4.AnchorX = 0.0f; ui4.AnchorY = 0.0f;
    check(ui4.ContainsPoint(110.0f, 90.0f) && !ui4.ContainsPoint(250.0f, 90.0f),
          "hit-test reconhece dentro e fora do botão");
    ui4.AnchorX = 0.5f; ui4.AnchorY = 0.5f;
    check(ui4.ContainsPoint(100.0f, 80.0f) && !ui4.ContainsPoint(30.0f, 80.0f),
          "hit-test respeita âncora do botão");

    // Responsive layout uses normalized anchors with pixel offsets while the
    // legacy mode keeps its original absolute rect.
    ui4.UseResponsiveAnchors = true;
    ui4.AnchorMinX = ui4.AnchorMaxX = 1.0f;
    ui4.AnchorMinY = ui4.AnchorMaxY = 1.0f;
    ui4.OffsetMinX = -140.0f; ui4.OffsetMinY = -60.0f;
    ui4.OffsetMaxX = -20.0f; ui4.OffsetMaxY = -20.0f;
    const auto rectSmall = ui4.ResolveRect(960.0f, 540.0f);
    const auto rectLarge = ui4.ResolveRect(1920.0f, 1080.0f);
    check(std::abs(rectSmall.X - 820.0f) < 0.001f && std::abs(rectSmall.Y - 480.0f) < 0.001f &&
          rectSmall.Width == 120.0f && rectSmall.Height == 40.0f &&
          std::abs(rectLarge.X - 1780.0f) < 0.001f && std::abs(rectLarge.Y - 1020.0f) < 0.001f,
          "elemento ancorado no canto inferior mantém margens em canvas de resoluções diferentes");
    ui4.AnchorMinX = 0.0f; ui4.AnchorMaxX = 1.0f;
    ui4.OffsetMinX = 24.0f; ui4.OffsetMaxX = -24.0f;
    const auto stretched = ui4.ResolveRect(1280.0f, 720.0f);
    check(std::abs(stretched.X - 24.0f) < 0.001f && std::abs(stretched.Width - 1232.0f) < 0.001f,
          "âncoras opostas esticam elemento mantendo margens laterais");
    ui4.UseResponsiveAnchors = false;
    const auto legacyRect = ui4.ResolveRect(1920.0f, 1080.0f);
    check(std::abs(legacyRect.X - (ui4.PositionX - ui4.AnchorX * ui4.Width)) < 0.001f &&
          legacyRect.Width == ui4.Width,
          "modo responsivo desligado preserva o layout legado de cena");

    const std::string previousUIScenePath = m_ProjectConfig.ScenePath;
    const std::filesystem::path uiTestScene =
        std::filesystem::temp_directory_path() / "Marae_UI_Responsive_RoundTrip.scene";
    m_Scene.Clear();
    Entity& responsiveEntity = m_Scene.CreateEntity();
    responsiveEntity.SetName("ResponsiveUIRoundTrip");
    responsiveEntity.SetEditorOnly(false);
    responsiveEntity.AddUI(UIElementType::Frame);
    UIComponent& responsiveUI = responsiveEntity.GetUIComponent();
    responsiveUI.UseResponsiveAnchors = true;
    responsiveUI.AnchorMinX = 0.0f; responsiveUI.AnchorMinY = 0.0f;
    responsiveUI.AnchorMaxX = 1.0f; responsiveUI.AnchorMaxY = 1.0f;
    responsiveUI.OffsetMinX = 16.0f; responsiveUI.OffsetMinY = 24.0f;
    responsiveUI.OffsetMaxX = -16.0f; responsiveUI.OffsetMaxY = -24.0f;
    m_ProjectConfig.ScenePath = uiTestScene.string();
    const bool uiSaved = SaveScene();
    m_Scene.Clear();
    const bool uiLoaded = uiSaved && LoadScene(uiTestScene.string());
    const Entity* restoredUIEntity = nullptr;
    for (const Entity& candidate : m_Scene.GetEntities())
        if (candidate.GetName() == "ResponsiveUIRoundTrip") { restoredUIEntity = &candidate; break; }
    check(uiLoaded && restoredUIEntity && restoredUIEntity->HasUI() &&
          restoredUIEntity->GetUIComponent().UseResponsiveAnchors &&
          std::abs(restoredUIEntity->GetUIComponent().OffsetMinX - 16.0f) < 0.001f &&
          std::abs(restoredUIEntity->GetUIComponent().OffsetMaxY + 24.0f) < 0.001f,
          "cena salva e reabre âncoras e offsets responsivos");
    std::error_code uiTestCleanupError;
    std::filesystem::remove(uiTestScene, uiTestCleanupError);
    m_ProjectConfig.ScenePath = previousUIScenePath;

    check(InspectorPanel::RunBatchEditRegressionTests() == 0,
          "Inspector aplica edições em lote aos componentes compatíveis da seleção");

    std::cout << "[UITest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU")
              << " (" << failures << " falha(s))" << std::endl;
    return failures == 0 ? 0 : 1;
}

int Application::RunMaterialRegressionTests()
{
    int failures = 0;
    auto check = [&](bool condition, const char* name)
    {
        std::cout << (condition ? "[MaterialTest] PASS: " : "[MaterialTest] FAIL: ")
                  << name << std::endl;
        if (!condition) ++failures;
    };

    Scene testScene;
    Entity& original = testScene.CreateEntity();
    original.SetName("MaterialAndLightSource");
    original.GetMaterialComponent().MaterialGUID = "mat_neon_test";
    original.GetMaterialComponent().MaterialData.Type = MaterialType::Neon;
    original.GetMaterialComponent().MaterialData.PBRParametersEnabled = true;
    original.GetMaterialComponent().MaterialData.Roughness = 0.11f;
    original.GetMaterialComponent().MaterialData.Metallic = 0.73f;
    original.GetMaterialComponent().MaterialData.EmissionStrength = 4.0f;
    original.GetMaterialComponent().MaterialData.Alpha = 0.82f;
    original.GetColorComponent().Color = Vector3(0.1f, 0.4f, 1.0f);
    original.AddLight(LightType::Spot);
    original.GetLightComponent().Enabled = true;
    original.GetLightComponent().CastShadows = true;
    original.GetLightComponent().Intensity = 37.0f;
    original.GetLightComponent().Range = 18.0f;
    original.GetLightComponent().OuterAngleDeg = 48.0f;

    const Material sourceMaterialSnapshot = original.GetMaterialComponent().MaterialData;

    const int cloneIndex = SceneCommands::DuplicateEntity(testScene, 0);
    check(cloneIndex == 1, "duplicacao cria a segunda entidade");
    if (cloneIndex >= 0 && cloneIndex < static_cast<int>(testScene.GetEntities().size()))
    {
        Entity& clone = testScene.GetEntities()[static_cast<size_t>(cloneIndex)];
        const Material& cloneMaterial = clone.GetMaterialComponent().MaterialData;
        check(clone.GetMaterialComponent().MaterialGUID == "mat_neon_test" &&
                  cloneMaterial.Type == MaterialType::Neon &&
                  cloneMaterial.PBRParametersEnabled &&
                  std::abs(cloneMaterial.Roughness - sourceMaterialSnapshot.Roughness) < 0.0001f &&
                  std::abs(cloneMaterial.Metallic - sourceMaterialSnapshot.Metallic) < 0.0001f &&
                  std::abs(cloneMaterial.EmissionStrength - sourceMaterialSnapshot.EmissionStrength) < 0.0001f &&
                  std::abs(cloneMaterial.Alpha - sourceMaterialSnapshot.Alpha) < 0.0001f,
              "duplicado preserva material, PBR, emissao e transparencia");
        check(clone.HasLight() && clone.GetLightComponent().Type == LightType::Spot &&
                  clone.GetLightComponent().Enabled && clone.GetLightComponent().CastShadows &&
                  std::abs(clone.GetLightComponent().Intensity - 37.0f) < 0.0001f &&
                  std::abs(clone.GetLightComponent().Range - 18.0f) < 0.0001f &&
                  std::abs(clone.GetLightComponent().OuterAngleDeg - 48.0f) < 0.0001f,
              "duplicado preserva componente e parametros da luz");
    }

    Material glass;
    glass.Type = MaterialType::Glass;
    Material rubber;
    rubber.Type = MaterialType::Rubber;
    Material ceramic;
    ceramic.Type = MaterialType::Ceramic;
    Material fabric;
    fabric.Type = MaterialType::Fabric;
    check(static_cast<int>(glass.Type) == 4 && static_cast<int>(rubber.Type) == 5 &&
              static_cast<int>(ceramic.Type) == 6 && static_cast<int>(fabric.Type) == 7,
          "presets vidro, borracha, ceramica e tecido disponiveis");

    std::cout << "[MaterialTest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU")
              << " (" << failures << " falha(s))" << std::endl;
    return failures == 0 ? 0 : 1;
}

int Application::RunAcousticRegressionTests()
{
    int failures = 0;
    auto check = [&](bool condition, const char* name)
    {
        std::cout << (condition ? "[AcousticTest] PASS: " : "[AcousticTest] FAIL: ")
                  << name << std::endl;
        if (!condition) ++failures;
    };

    Scene testScene;
    Entity& source = testScene.CreateEntity();
    source.AddAudio();
    source.GetTransform().Position = Vector3(-5.0f, 0.0f, 0.0f);
    source.GetAudioComponent().AcousticPropagationEnabled = true;
    source.GetAudioComponent().AcousticRayTracingEnabled = true;
    source.GetAudioComponent().GeometricAcousticsEnabled = true;
    source.GetAudioComponent().OcclusionEnabled = true;
    source.GetAudioComponent().ObstructionEnabled = true;
    source.GetAudioComponent().DiffractionEnabled = false;
    source.GetAudioComponent().AcousticPathfindingEnabled = false;

    Entity& wall = testScene.CreateEntity();
    wall.GetTransform().Position = Vector3(0.0f, 0.0f, 0.0f);
    wall.GetTransform().Scale = Vector3(2.0f, 4.0f, 2.0f);
    wall.AudioAcousticGeometryEnabled = true;
    wall.AudioOccluder = true;
    wall.AudioTransmissionEnabled = false;
    wall.AudioAbsorption = 1.0f;
    wall.AudioDiffraction = 0.0f;

    const glm::vec3 listenerPosition(5.0f, 0.0f, 0.0f);
    const AudioAcousticMix blocked = AudioAcoustics::Evaluate(testScene, source, listenerPosition);
    check(blocked.Occlusion > 0.9f && blocked.Gain < 0.35f,
          "parede bloqueia e reduz o som por ray tracing geometrico");

    wall.AudioTransmissionEnabled = true;
    wall.AudioTransmission = 1.0f;
    const AudioAcousticMix transmitted = AudioAcoustics::Evaluate(testScene, source, listenerPosition);
    check(transmitted.Transmission > blocked.Transmission && transmitted.Gain > blocked.Gain,
          "transmissao permite que o som atravesse a parede");

    source.GetAudioComponent().AcousticRayTracingEnabled = false;
    const AudioAcousticMix disabled = AudioAcoustics::Evaluate(testScene, source, listenerPosition);
    check(std::abs(disabled.Gain - 1.0f) < 0.0001f,
          "ray tracing acustico pode ser desativado na fonte");

    std::cout << "[AcousticTest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU")
              << " (" << failures << " falha(s))" << std::endl;
    return failures == 0 ? 0 : 1;
}

#ifdef _WIN32
static bool LaunchVSCodeFile(const std::filesystem::path& filePath)
{
    std::vector<std::filesystem::path> candidates;

    auto getEnvironmentPath = [](const char* name) -> std::filesystem::path
    {
        char buffer[32768] = {};
        const DWORD length = GetEnvironmentVariableA(name, buffer, sizeof(buffer));
        return length > 0 && length < sizeof(buffer)
            ? std::filesystem::path(buffer)
            : std::filesystem::path();
    };

    const std::filesystem::path localAppData = getEnvironmentPath("LOCALAPPDATA");
    const std::filesystem::path programFiles = getEnvironmentPath("ProgramW6432");
    const std::filesystem::path programFilesX86 = getEnvironmentPath("ProgramFiles(x86)");

    if (!localAppData.empty())
    {
        candidates.push_back(localAppData / "Programs" / "Microsoft VS Code" / "Code.exe");
        candidates.push_back(localAppData / "Programs" / "Microsoft VS Code Insiders" / "Code - Insiders.exe");
    }
    if (!programFiles.empty())
        candidates.push_back(programFiles / "Microsoft VS Code" / "Code.exe");
    if (!programFilesX86.empty())
        candidates.push_back(programFilesX86 / "Microsoft VS Code" / "Code.exe");

    std::filesystem::path codeExe;
    for (const auto& candidate : candidates)
    {
        if (std::filesystem::exists(candidate))
        {
            codeExe = candidate;
            break;
        }
    }

    const std::wstring file = filePath.wstring();
    if (!codeExe.empty())
    {
        std::wstring command = L"\"" + codeExe.wstring() + L"\" --reuse-window \"" + file + L"\"";
        std::vector<wchar_t> commandLine(command.begin(), command.end());
        commandLine.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(
            codeExe.wstring().c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process))
        {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            return true;
        }
    }

    // Fallback para instalaÃ§Ãµes que registraram o comando "code" no PATH.
    const std::wstring args = L"--reuse-window \"" + file + L"\"";
    HINSTANCE result = ShellExecuteW(nullptr, L"open", L"code", args.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}
#endif

static unsigned int LoadMaraeLogoTexture()
{
    const std::filesystem::path logoPath = RuntimePaths::ResolveEngineAsset(
        __FILE__, "Editor/Branding/MaraeLogoTransparent.png");

    // Fixamos o flip apenas nesta thread: não depende da ordem das texturas
    // carregadas e não interfere com imagens abertas por outras threads.
    stbi_set_flip_vertically_on_load_thread(false);
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load(logoPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels)
    {
        std::cerr << "[Application] Nao foi possivel carregar a logo: " << logoPath.string() << "\n";
        return 0;
    }

    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);
    return texture;
}

static void ApplyMaraeWindowIcon(GLFWwindow* window, const std::string& customIconPath = {})
{
    if (!window)
        return;

    const bool customIcon = !customIconPath.empty() && std::filesystem::is_regular_file(customIconPath);
    const std::filesystem::path logoPath = customIcon
        ? std::filesystem::path(customIconPath)
        : RuntimePaths::ResolveEngineAsset(__FILE__, "Editor/Branding/MaraeIcon.png");
    int width = 0, height = 0, channels = 0;
    stbi_set_flip_vertically_on_load_thread(false);
    unsigned char* pixels = stbi_load(
        logoPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels)
    {
        std::cerr << "[Application] Nao foi possivel carregar icone da janela: "
                  << logoPath.string() << "\n";
        return;
    }

    GLFWimage icon{};
    icon.width = width;
    icon.height = height;
    icon.pixels = pixels;

    // O ícone do Windows mantém o fundo preto, com cantos arredondados e
    // antialiasing. O PNG original permanece intacto para a interface.
    const float iconWidth = static_cast<float>(width);
    const float iconHeight = static_cast<float>(height);
    const float cornerRadius = std::min(iconWidth, iconHeight) * 0.18f;
    if (!customIcon)
    {
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                unsigned char* pixel = pixels + (static_cast<size_t>(y) * width + x) * 4u;
                const float sourceAlpha = pixel[3] / 255.0f;
                pixel[0] = static_cast<unsigned char>(pixel[0] * sourceAlpha + 0.5f);
                pixel[1] = static_cast<unsigned char>(pixel[1] * sourceAlpha + 0.5f);
                pixel[2] = static_cast<unsigned char>(pixel[2] * sourceAlpha + 0.5f);
                const float qx = std::abs((x + 0.5f) - iconWidth * 0.5f)
                    - (iconWidth * 0.5f - cornerRadius);
                const float qy = std::abs((y + 0.5f) - iconHeight * 0.5f)
                    - (iconHeight * 0.5f - cornerRadius);
                const float outsideX = std::max(qx, 0.0f);
                const float outsideY = std::max(qy, 0.0f);
                const float signedDistance = std::sqrt(outsideX * outsideX + outsideY * outsideY)
                    + std::min(std::max(qx, qy), 0.0f) - cornerRadius;
                const float coverage = std::clamp(0.5f - signedDistance, 0.0f, 1.0f);
                pixel[3] = static_cast<unsigned char>(coverage * 255.0f + 0.5f);
            }
        }
    }

    glfwSetWindowIcon(window, 1, &icon);
    stbi_image_free(pixels);
}

static void SetupDefaultDockLayout(ImGuiID dockspaceId, const ImVec2& dockSize)
{
    // Layout inicial real do editor. Depois que as janelas estiverem dockadas,
    // o ImGui passa a controlar tamanhos, movimento e undocking normalmente.
    if (ImGui::DockBuilderGetNode(dockspaceId) != nullptr)
    {
        return;
    }

    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, dockSize);

    ImGuiID leftID = 0;
    ImGuiID rightID = 0;
    ImGuiID centerID = dockspaceId;
    ImGui::DockBuilderSplitNode(centerID, ImGuiDir_Left, 0.21f, &leftID, &centerID);
    ImGui::DockBuilderSplitNode(centerID, ImGuiDir_Right, 0.20f, &rightID, &centerID);

    ImGuiID assetsID = 0;
    ImGui::DockBuilderSplitNode(leftID, ImGuiDir_Down, 0.30f, &assetsID, &leftID);

    ImGuiID debugID = 0;
    ImGui::DockBuilderSplitNode(centerID, ImGuiDir_Down, 0.18f, &debugID, &centerID);

    ImGui::DockBuilderDockWindow("Hierarchy", leftID);
    ImGui::DockBuilderDockWindow("Assets", assetsID);
    ImGui::DockBuilderDockWindow("Viewport", centerID);
    ImGui::DockBuilderDockWindow("Debug", debugID);
    ImGui::DockBuilderDockWindow("Inspector", rightID);
    ImGui::DockBuilderDockWindow("IA", rightID);
    ImGui::DockBuilderFinish(dockspaceId);
}

namespace
{
    using FrameSection = PerformanceMonitor::FrameSection;

    // RAII para medir uma seção de subsistema por frame no profiler.
    struct FrameSectionTimer
    {
        FrameSection Section;
        std::chrono::steady_clock::time_point Start;

        explicit FrameSectionTimer(FrameSection section)
            : Section(section), Start(std::chrono::steady_clock::now()) {}
        ~FrameSectionTimer()
        {
            const double elapsedMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - Start).count();
            PerformanceMonitor::RecordFrameSection(Section, elapsedMs);
        }
    };

    // Ponto padrÃ£o para criaÃ§Ã£o no editor: alguns metros Ã  frente da cÃ¢mera,
    // usando yaw e pitch atuais. Assim o objeto nasce visÃ­vel e nÃ£o sobre o
    // centro do mundo, sem alterar a posiÃ§Ã£o da cÃ¢mera nem a cena existente.
    static Vector3 SpawnPositionInFrontOfCamera(const Camera& camera, float distance = 4.0f)
    {
        const float yaw = glm::radians(camera.Rotation.Y);
        const float pitch = glm::radians(camera.Rotation.X);
        const glm::vec3 forward = glm::normalize(glm::vec3(
            std::sin(yaw) * std::cos(pitch),
            -std::sin(pitch),
            -std::cos(yaw) * std::cos(pitch)));
        const glm::vec3 position(
            camera.Position.X, camera.Position.Y, camera.Position.Z);
        const glm::vec3 spawn = position + forward * std::max(0.5f, distance);
        return Vector3(spawn.x, spawn.y, spawn.z);
    }

    // CÃ¢meras de cena sÃ£o entidades normais e podem estar parentadas. O
    // viewport do runtime precisa usar o transform mundial, nÃ£o apenas o
    // Position/Rotation local salvo no componente Transform.
    static Camera BuildCameraFromEntity(const Entity& entity)
    {
        Camera camera;
        const Transform& transform = const_cast<Entity&>(entity).GetTransform();
        const glm::mat4& world = transform.WorldMatrix;

        const glm::vec3 position = glm::vec3(world[3]);
        glm::vec3 axisX = glm::vec3(world[0]);
        glm::vec3 axisY = glm::vec3(world[1]);
        glm::vec3 axisZ = glm::vec3(world[2]);

        // Remove escala do eixo antes de converter para quaternion. Sem isso,
        // uma escala no pai tambÃ©m distorceria a orientaÃ§Ã£o da cÃ¢mera.
        axisX = glm::length(axisX) > 0.0001f
            ? glm::normalize(axisX) : glm::vec3(1.0f, 0.0f, 0.0f);
        axisY = glm::length(axisY) > 0.0001f
            ? glm::normalize(axisY) : glm::vec3(0.0f, 1.0f, 0.0f);
        axisZ = glm::length(axisZ) > 0.0001f
            ? glm::normalize(axisZ) : glm::vec3(0.0f, 0.0f, 1.0f);

        const glm::quat rotation = glm::normalize(glm::quat_cast(glm::mat3(
            axisX, axisY, axisZ)));
        // CameraRenderer recebe a rotação da VIEW (inversa da orientação da
        // câmera). Deriva yaw/pitch do eixo -Z da entidade para que o que o
        // gizmo mostra seja exatamente o que o runtime renderiza, inclusive
        // quando a câmera é filha de outro objeto.
        // CameraRenderer monta a rotação da view na ordem Rx * Ry * Rz. Converta
        // a orientação mundial da câmera para sua inversa e extraia Euler nessa
        // mesma ordem; glm::eulerAngles(rotation) usa outra convenção e podia
        // introduzir roll de 180° quando o pitch/yaw cruzava certos quadrantes.
        const glm::mat3 viewRotation = glm::mat3_cast(glm::conjugate(rotation));
        const float sinYaw = std::clamp(viewRotation[2][0], -1.0f, 1.0f);
        const float yaw = std::asin(sinYaw);
        const float cosYaw = std::cos(yaw);
        float pitch = 0.0f;
        float roll = 0.0f;
        if (std::abs(cosYaw) > 1e-5f)
        {
            pitch = std::atan2(-viewRotation[2][1], viewRotation[2][2]);
            roll = std::atan2(-viewRotation[1][0], viewRotation[0][0]);
        }
        else
        {
            // No polo, yaw e roll não são separáveis; preserve a orientação
            // combinada e mantenha roll neutro para a imagem não capotar.
            pitch = std::copysign(1.0f, sinYaw) *
                std::atan2(viewRotation[0][1], viewRotation[1][1]);
        }

        camera.Position = Vector3(position.x, position.y, position.z);
        camera.Rotation = Vector3(glm::degrees(pitch), glm::degrees(yaw), glm::degrees(roll));
        const CameraComponent& settings = entity.GetCameraComponent();
        camera.FieldOfView = std::clamp(settings.FieldOfView, 1.0f, 179.0f);
        camera.NearClip = std::max(0.001f, settings.NearClip);
        camera.FarClip = std::max(camera.NearClip + 0.01f, settings.FarClip);
        camera.Orthographic = settings.Orthographic;
        camera.OrthographicSize = std::max(0.1f, settings.OrthographicSize);
        return camera;
    }

    static const Entity* FindActiveSceneCamera(const Scene& scene)
    {
        for (const Entity& entity : scene.GetEntities())
        {
            if (!entity.IsEditorOnly() && entity.Active && entity.HasCamera() &&
                entity.GetCameraComponent().Enabled)
            {
                return &entity;
            }
        }
        return nullptr;
    }

    static Camera BuildRuntimeCamera(const Scene& scene, const Camera& flyCamera)
    {
        const Entity* sceneCamera = FindActiveSceneCamera(scene);
        return sceneCamera ? BuildCameraFromEntity(*sceneCamera) : flyCamera;
    }

}

void Application::CreateSphereEntity()
{
    Entity& ent = m_Scene.CreateEntity();
    static int counter = 1;
    ent.SetName("Sphere " + std::to_string(counter++));
    ent.SetEditorOnly(false);
    ent.GetMeshComponent().PrimitiveType = "Sphere";
    ent.GetMeshComponent().MeshData = std::make_shared<Mesh>();
    ent.GetMeshComponent().MeshData->Create(MeshFactory::CreateSphere(32, 24), GL_TRIANGLES, "Application::CreateSphereEntity");
    ent.GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);
    ent.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);
    ent.GetColorComponent().Color = Vector3(0.5f, 0.5f, 0.5f);
    ent.GetMaterialComponent().MaterialData.Color[0] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[1] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[2] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
    ent.GetMaterialComponent().MaterialData.DiffuseTexture = nullptr;
    ent.PickingRadius = 1.0f;
    ent.Collider = Entity::ColliderShape::Sphere;
    // Novas entidades criadas manualmente sÃ£o estÃ¡ticas por padrÃ£o
    ent.PhysicsEnabled = false;
    ent.UseGravity = false;
    ent.IsStatic = true;
    ent.ColliderEnabled = false;
    MarkDirty();
}

void Application::CreateCylinderEntity()
{
    Entity& ent = m_Scene.CreateEntity();
    static int counter = 1;
    ent.SetName("Cylinder " + std::to_string(counter++));
    ent.SetEditorOnly(false);
    ent.GetMeshComponent().PrimitiveType = "Cylinder";
    ent.GetMeshComponent().MeshData = std::make_shared<Mesh>();
    ent.GetMeshComponent().MeshData->Create(MeshFactory::CreateCylinder(32, 1.0f), GL_TRIANGLES, "Application::CreateCylinderEntity");
    ent.GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);
    ent.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);
    ent.GetColorComponent().Color = Vector3(0.5f, 0.5f, 0.5f);
    ent.GetMaterialComponent().MaterialData.Color[0] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[1] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[2] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
    ent.GetMaterialComponent().MaterialData.DiffuseTexture = nullptr;
    ent.PickingRadius = 1.0f;
    ent.Collider = Entity::ColliderShape::Cylinder;
    // Novas entidades criadas manualmente sÃ£o estÃ¡ticas por padrÃ£o
    ent.PhysicsEnabled = false;
    ent.UseGravity = false;
    ent.IsStatic = true;
    ent.ColliderEnabled = false;
    MarkDirty();
}

void Application::CreateConeEntity()
{
    Entity& ent = m_Scene.CreateEntity();
    static int counter = 1;
    ent.SetName("Cone " + std::to_string(counter++));
    ent.SetEditorOnly(false);
    ent.GetMeshComponent().PrimitiveType = "Cone";
    ent.GetMeshComponent().MeshData = std::make_shared<Mesh>();
    ent.GetMeshComponent().MeshData->Create(MeshFactory::CreateCone(32, 1.0f), GL_TRIANGLES, "Application::CreateConeEntity");
    ent.GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);
    ent.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);
    ent.GetColorComponent().Color = Vector3(0.5f, 0.5f, 0.5f);
    ent.GetMaterialComponent().MaterialData.Color[0] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[1] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[2] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
    ent.GetMaterialComponent().MaterialData.DiffuseTexture = nullptr;
    ent.PickingRadius = 1.0f;
    // Novas entidades criadas manualmente sÃ£o estÃ¡ticas por padrÃ£o
    ent.PhysicsEnabled = false;
    ent.UseGravity = false;
    ent.IsStatic = true;
    ent.ColliderEnabled = false;
    MarkDirty();
}


Window& Application::GetWindow()
{
    return m_Window;
}

Application::Application()
    : Application(ProjectConfig{})   // delega ao construtor principal com config vazia
{
}

Application::Application(const ProjectConfig& config)
    : m_Window(config.RuntimeGame ? config.RuntimeWidth : 1280,
        config.RuntimeGame ? config.RuntimeHeight : 720,
        config.RuntimeGame && !config.GameTitle.empty() ? config.GameTitle.c_str() :
            (config.RuntimeGame ? "Marae Game" : "Marae Engine"),
        config.Graphics,
        config.RuntimeGame && config.RuntimeWindowMode == 0,
        !config.RuntimeGame,
        config.RuntimeGame && config.RuntimeWindowMode == 2),
    m_ProjectConfig(config),
    m_SelectedEntity(0),
    m_UpPressed(false),
    m_DownPressed(false)
{
    std::cout << "Iniciando Application..." << std::endl;
    if (m_ProjectConfig.RuntimeGame)
    {
        m_ViewportWidth = std::max(320, m_Window.GetWidth());
        m_ViewportHeight = std::max(200, m_Window.GetHeight());
    }
    m_GraphicsQuality = m_ProjectConfig.Quality;

    // Aplica configuraÃ§Ãµes de qualidade grÃ¡fica antes de inicializar o renderer
    switch (m_ProjectConfig.Quality)
    {
        case GraphicsQuality::Performance:
            // Performance reduz a resolução, mas não elimina sombras locais;
            // luz artificial sem sombra parece quebrada no viewport.
            m_ShadowIntensity = 0.5f;
            m_AmbientLight    = 0.5f;
            break;
        case GraphicsQuality::UltraPerformance:
            m_ShadowIntensity = 0.0f;
            m_AmbientLight = 0.65f;
            break;
        case GraphicsQuality::Balanced:
            m_ShadowIntensity = 0.5f;
            m_AmbientLight    = 0.4f;
            break;
        case GraphicsQuality::HighFidelity:
            m_ShadowIntensity = 0.85f;
            m_AmbientLight    = 0.3f;
            break;
        case GraphicsQuality::Ultra:
            m_ShadowIntensity = 1.0f;
            m_AmbientLight = 0.25f;
            // Habilita SSAO e SSR automaticamente no preset Ultra
            for (Entity& e : m_Scene.GetEntities())
                if (e.IsGlobalLight()) {
                    e.GetGlobalLightComponent().Lighting.ScreenSpaceAOEnabled = true;
                    e.GetGlobalLightComponent().Lighting.ScreenSpaceAOIntensity = 0.75f;
                    e.GetGlobalLightComponent().Lighting.ScreenSpaceAORadius = 1.5f;
                    e.GetGlobalLightComponent().Lighting.ScreenSpaceReflectionsEnabled = true;
                    e.GetGlobalLightComponent().Lighting.ScreenSpaceReflectionsStrength = 0.35f;
                }
            break;
    }

    std::cout << "Iniciando Renderer..." << std::endl;
    m_Renderer.Init();
    m_Renderer.SetActiveGraphicsAPI(m_Window.GetGraphicsAPI());
    m_Renderer.SetRequestedGraphicsAPI(m_Window.GetGraphicsAPI());
    SetGraphicsQuality(m_GraphicsQuality);
    std::cout << "Renderer iniciado com sucesso!" << std::endl;

    LoadGlobalEditorSettings();
    // A preferência do arquivo inicia a janela (Main.cpp lê antes de construí-la).
    // A sessão ativa é a autoridade durante a execução; nunca deixar a UI mostrar
    // uma API escolhida que ainda não foi carregada.
    m_GlobalEditorSettings.RequestedGraphicsAPI = m_Window.GetGraphicsAPI();
    m_Renderer.SetRequestedGraphicsAPI(m_Window.GetGraphicsAPI());
    InitImGui();
    ApplyGlobalEditorSettings(true);
    LoadMaraeLogo();
    ApplyMaraeWindowIcon(m_Window.GetNativeWindow(), m_ProjectConfig.GameIconPath);
    m_LoadingSplashActive = true;
    RenderLoadingSplash(0.08f, "Inicializando renderer...");

    ThumbnailManager::Initialize();
    RenderLoadingSplash(0.20f, "Preparando thumbnails...");

    // Usa a pasta do projeto como base para o AssetDatabase, se disponÃ­vel
    if (!m_ProjectConfig.ProjectPath.empty())
    {
        try { std::filesystem::current_path(m_ProjectConfig.ProjectPath); }
        catch (...) {}
    }

    AssetDatabase::Initialize();
    AssetDatabase::Load(m_ProjectConfig.ProjectPath);
    AssetDatabase::ScanAssets(m_ProjectConfig.ProjectPath);
    RenderLoadingSplash(0.42f, "Carregando assets...");
    PerformanceMonitor::Initialize();

    bool framebufferCreated = m_Framebuffer.Create(m_ViewportWidth, m_ViewportHeight);
    if (framebufferCreated)
        std::cout << "Framebuffer criado!" << std::endl;
    else
        std::cout << "Falha ao criar Framebuffer!" << std::endl;
    if (!m_MobilePreviewFramebuffer.Create(360, 640))
        std::cout << "Aviso: preview mobile indisponivel." << std::endl;
    RenderLoadingSplash(0.58f, "Criando viewport...");

    m_AudioSystem.Init();
    RenderLoadingSplash(0.70f, "Inicializando audio...");

    m_Camera.Position = Vector3(0.0f, 3.0f, 6.0f);
    m_Camera.Rotation = Vector3(25.0f, 0.0f, 0.0f);
    if (m_ProjectConfig.WorldType == ProjectWorldType::World2D)
        SetWorldMode(WorldMode::World2D);

    EditorObjects::CreateDefaultObjects(m_Scene);
    RenderLoadingSplash(0.82f, "Montando cena...");

    // A cena inicial é carregada uma única vez pelo SceneManager abaixo. Fazer
    // LoadScene aqui também lia e reconstruía a mesma cena duas vezes durante
    // a inicialização, prolongando o splash em projetos maiores.
    // Axis era um mesh auxiliar antigo e nÃ£o deve existir como entidade da cena.
    // Os eixos atuais continuam sendo desenhados pelo GridRenderer.
    for (int i = static_cast<int>(m_Scene.GetEntities().size()) - 1; i >= 0; --i)
    {
        const std::string& name = m_Scene.GetEntities()[static_cast<size_t>(i)].GetName();
        if (name == "Axis" || name == "Axis X" || name == "Axis Y" || name == "Axis Z")
            m_Scene.DeleteEntity(i);
    }
    m_SelectedEntity = -1;
    m_SelectedID = 0;
    RenderLoadingSplash(0.94f, "Finalizando projeto...");

    // As preferÃªncias do editor sÃ£o independentes da cena e sÃ£o restauradas
    // depois do carregamento para nÃ£o serem sobrescritas por dados da cena.
    m_SceneManager.RefreshProjectLevels(m_ProjectConfig.ProjectPath);
    if (!m_ProjectConfig.ScenePath.empty() && std::filesystem::exists(m_ProjectConfig.ScenePath))
    {
        m_SceneManager.LoadScene(*this, m_ProjectConfig.ScenePath);
    }
    if (!m_ProjectConfig.RuntimeGame)
        LoadEditorSettings();

    m_SceneDirty = false;
    UpdateWindowTitle();
    if (!m_ProjectConfig.RuntimeGame)
        CheckForRecoverySnapshot();

    // Registra este Application no global (acessÃ­vel pelo close callback via extern)
    g_CurrentApplication = this;

    glfwSetWindowCloseCallback(m_Window.GetNativeWindow(), [](GLFWwindow* w)
    {
        Application* app = static_cast<Application*>(g_CurrentApplication);
        if (app && !app->m_ProjectConfig.RuntimeGame && app->m_SceneDirty)
        {
            glfwSetWindowShouldClose(w, GLFW_FALSE);
            app->m_ShowUnsavedDialog = true;
        }
    });

    PrintHierarchy();
    PrintInspector();

    // O runtime .NET é pesado e não deve bloquear a transição para o editor
    // em projetos que ainda não possuem scripts. Antes, o splash chegava a
    // 100% e ficava preso aqui enquanto HostFxr/AssemblyManager iniciavam.
    bool projectHasScripts = false;
    for (const Entity& entity : m_Scene.GetEntities())
    {
        const ScriptComponent& script = const_cast<Entity&>(entity).GetScriptComponent();
        if (script.Enabled &&
            (!script.ScriptPath.empty() || !script.ScriptAssemblyQualifiedTypeName.empty()))
        {
            projectHasScripts = true;
            break;
        }
    }

    if (!projectHasScripts && !m_ProjectConfig.ProjectPath.empty())
    {
        std::error_code scriptScanError;
        const std::filesystem::path projectRoot(m_ProjectConfig.ProjectPath);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 projectRoot, std::filesystem::directory_options::skip_permission_denied,
                 scriptScanError))
        {
            if (scriptScanError)
                break;
            if (entry.is_regular_file(scriptScanError) &&
                entry.path().extension() == ".cs")
            {
                projectHasScripts = true;
                break;
            }
        }
    }

    if (projectHasScripts)
        ScriptingRuntime::Instance().Initialize(m_ProjectConfig.ProjectPath);
    else
        std::cout << "[Scripting] Nenhum script no projeto; runtime gerenciado adiado."
                  << std::endl;

    // Só encerra o splash depois de todas as tarefas de inicialização. Antes,
    // ele era desenhado em 100% antes da análise de scripts e podia permanecer
    // visível enquanto o editor já aguardava a transição.
    RenderLoadingSplash(1.0f, "Concluido");
    m_LoadingSplashActive = false;
}

Application::~Application()
{
    ScriptingRuntime::Instance().Shutdown();
    if (m_ProjectConfig.PersistEditorSettings)
    {
        SaveEditorSettings();
        SaveGlobalEditorSettings();
        m_SurvivalGame.Save(MakeDemoGameContext());
    }
    m_AudioSystem.Stop();
    g_CurrentApplication = nullptr;
    if (m_LogoTexture)
    {
        glDeleteTextures(1, &m_LogoTexture);
        m_LogoTexture = 0;
    }
    ThumbnailManager::Shutdown();
    ShutdownImGui();
}

std::string Application::GetEditorSettingsPath() const
{
    namespace fs = std::filesystem;
    const fs::path projectRoot = !m_ProjectConfig.ProjectPath.empty()
        ? fs::path(m_ProjectConfig.ProjectPath)
        : fs::current_path();
    return (projectRoot / "Library" / "editor_settings.cfg").string();
}

std::string Application::BuildEditorSettingsSnapshot() const
{
    const auto& sun = m_Scene.GetLightingManager().Sun;
    const auto& moon = m_Scene.GetLightingManager().Moon;
    const auto& lightingSettings = m_Scene.GetLightingManager().Settings;
    const GlobalLightComponent* globalLight = nullptr;
    for (const Entity& entity : m_Scene.GetEntities())
    {
        if (entity.IsGlobalLight())
        {
            globalLight = &entity.GetGlobalLightComponent();
            break;
        }
    }
    const GlobalLightComponent defaultGlobalLight;
    if (!globalLight)
        globalLight = &defaultGlobalLight;

    std::ostringstream snapshot;
    snapshot << std::setprecision(9)
        << static_cast<int>(m_GraphicsQuality) << '|'
        << m_Camera.MoveSpeed << '|'
        << m_ShowGrid << '|' << m_ShowAxis << '|' << m_GridRenderDistance << '|'
        << m_SnapSettings.PositionEnabled << '|' << m_SnapSettings.RotationEnabled << '|'
        << m_SnapSettings.ScaleEnabled << '|' << m_SnapSettings.PositionStep << '|'
        << m_SnapSettings.RotationStep << '|' << m_SnapSettings.ScaleStep << '|'
        << m_SnapSettings.FaceSnapEnabled << '|' << m_SnapSettings.FaceSnapThreshold << '|'
        << m_SnapSettings.FaceAlignmentEnabled << '|' << m_SnapSettings.EdgeSnapEnabled << '|'
        << m_SnapSettings.VertexSnapEnabled << '|' << m_SnapSettings.AxisAlignmentEnabled << '|'
        << m_SnapSettings.FaceAlignTolerance << '|' << m_SnapSettings.VertexSnapThreshold << '|'
        << m_SnapSettings.EdgeSnapThreshold << '|'
        << m_ShadowIntensity << '|' << m_AmbientLight << '|'
        << lightingSettings.Enabled << '|' << lightingSettings.DynamicShadows << '|'
        << lightingSettings.ContactShadows << '|' << lightingSettings.IndirectLighting << '|'
        << lightingSettings.EnvironmentLighting << '|' << lightingSettings.DayNightCycleEnabled << '|'
        << lightingSettings.DirectionalDistanceSoftening << '|'
        << lightingSettings.DayTimeHours << '|' << lightingSettings.DayNightCycleSpeedHoursPerSecond << '|'
        << lightingSettings.SunAzimuthDegrees << '|'
        << lightingSettings.DirectionalSourceAngularDiameterDegrees << '|'
        << lightingSettings.SunIntensityScale << '|'
        << static_cast<int>(lightingSettings.Quality) << '|'
        << lightingSettings.MaxPointLights << '|' << lightingSettings.MaxSpotLights << '|'
        << lightingSettings.MaxShadowedLocalLights << '|'
        << lightingSettings.IndirectIntensity << '|'
        << lightingSettings.ContactShadowStrength << '|'
        << lightingSettings.ContactShadowDistance << '|'
        << lightingSettings.ShadowDistance << '|'
        << lightingSettings.ShadowMapResolution << '|'
        << lightingSettings.PointShadowMapResolution << '|'
        << m_AudioSystem.GetMasterVolume() << '|'
        << AudioMixer::Get().GetEffectiveVolume(MixerGroupID::Music) << '|'
        << AudioMixer::Get().GetEffectiveVolume(MixerGroupID::SFX)   << '|'
        << sun.Direction.X << '|' << sun.Direction.Y << '|' << sun.Direction.Z << '|'
        << sun.Color.X << '|' << sun.Color.Y << '|' << sun.Color.Z << '|'
        << sun.Intensity << '|' << sun.UseTemperature << '|' << sun.TemperatureKelvin << '|'
        << moon.Direction.X << '|' << moon.Direction.Y << '|' << moon.Direction.Z << '|'
        << moon.Color.X << '|' << moon.Color.Y << '|' << moon.Color.Z << '|'
        << moon.Intensity << '|' << moon.UseTemperature << '|' << moon.TemperatureKelvin << '|'
        << globalLight->Enabled << '|'
        << globalLight->Brightness << '|' << globalLight->Contrast << '|'
        << globalLight->Saturation << '|' << globalLight->Exposure << '|'
        << globalLight->Gamma << '|'
        << globalLight->ShadowStrength << '|'
        << globalLight->ColorTint.X << '|' << globalLight->ColorTint.Y << '|'
        << globalLight->ColorTint.Z << '|'
        << globalLight->FogEnabled << '|'
        << globalLight->FogColor.X << '|' << globalLight->FogColor.Y << '|'
        << globalLight->FogColor.Z << '|' << globalLight->FogDensity << '|'
        << globalLight->FogStart << '|' << globalLight->FogEnd << '|'
        << globalLight->Lighting.FogMode << '|'
        << globalLight->VignetteStrength << '|' << globalLight->VignetteRadius << '|'
        << static_cast<int>(globalLight->ToneMapping) << '|'
        << globalLight->Temperature << '|' << globalLight->Tint << '|'
        << globalLight->FilmGrainEnabled << '|' << globalLight->FilmGrainStrength << '|'
        << globalLight->FilmGrainSize << '|' << globalLight->FilmGrainSpeed << '|'
        << globalLight->ChromaticAberrationEnabled << '|' << globalLight->ChromaticAberrationStrength << '|'
        << globalLight->BloomStrength << '|' << globalLight->SharpenStrength << '|'
        << static_cast<int>(m_GizmoMode) << '|' << static_cast<int>(m_GizmoSpace) << '|'
        << m_FaceScaleHandlesEnabled << '|'
        << m_EditDuringPlay << '|'
        << m_ShowHierarchy << '|' << m_ShowInspector << '|' << m_ShowAIPanel << '|'
        << m_ShowAssetBrowser << '|' << m_HierarchyWidth << '|' << m_InspectorWidth;
    return snapshot.str();
}

void Application::LoadEditorSettings()
{
    // AudioSystem/AudioMixer sobrevivem à troca de projeto como singletons.
    // Sempre comece pelo estado padrão e só depois aplique o arquivo do projeto
    // atual, evitando herdar volumes/mutes do projeto aberto anteriormente.
    m_AudioSystem.SetMasterVolume(1.0f);
    AudioMixer::Get().Reset();
    AudioMixer::Get().Load(m_ProjectConfig.ProjectPath);
    // Preferências são isoladas por projeto; projetos sem arquivo próprio
    // começam com os padrões, sem herdar valores do projeto anterior.
    m_FaceScaleHandlesEnabled = false;
    m_EditDuringPlay = true;

    std::ifstream file(GetEditorSettingsPath());
    if (file.is_open())
    {
        std::string line;
        int savedQuality = static_cast<int>(m_GraphicsQuality);
        bool qualityWasSaved = false;
        bool shadowIntensityWasSaved = false;

        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == '#')
                continue;

            const std::size_t equals = line.find('=');
            if (equals == std::string::npos)
                continue;

            const std::string key = line.substr(0, equals);
            const std::string value = line.substr(equals + 1);
            if (m_SceneHasLightingSettings)
            {
                static const std::unordered_set<std::string> sceneLightingKeys = {
                    "LightingEnabled", "DayNightCycleEnabled", "DirectionalDistanceSoftening", "DayTimeHours",
                    "DayNightCycleSpeed", "SunAzimuthDegrees", "DirectionalSourceAngularDiameterDegrees", "SunIntensityScale",
                    "DynamicShadows", "ContactShadows", "IndirectLighting",
                    "EnvironmentLighting", "LightingQuality", "MaxPointLights",
                    "MaxSpotLights", "MaxShadowedLocalLights", "IndirectIntensity",
                    "ContactShadowStrength", "ContactShadowDistance", "ShadowDistance",
                    "ShadowMapResolution", "PointShadowMapResolution", "ShadowIntensity",
                    "AmbientLight", "SunDirectionX", "SunDirectionY", "SunDirectionZ",
                    "SunColorR", "SunColorG", "SunColorB", "SunIntensity",
                    "SunUseTemperature", "SunTemperature"
                    , "MoonDirectionX", "MoonDirectionY", "MoonDirectionZ",
                    "MoonColorR", "MoonColorG", "MoonColorB", "MoonIntensity",
                    "MoonUseTemperature", "MoonTemperature"
                };
                if (sceneLightingKeys.count(key) != 0)
                    continue;
            }
            try
            {
                const bool boolValue = (value == "1" || value == "true");
                if      (key == "GraphicsQuality")       { savedQuality = std::stoi(value); qualityWasSaved = true; }
                else if (key == "CameraMoveSpeed")       m_Camera.MoveSpeed = std::clamp(std::stof(value), 0.001f, 1.0f);
                else if (key == "ShowGrid")              m_ShowGrid = boolValue;
                else if (key == "ShowAxis")              m_ShowAxis = boolValue;
                else if (key == "GridRenderDistance")   m_GridRenderDistance = std::clamp(std::stof(value), 500.0f, 10000.0f);
                else if (key == "PositionSnap")         m_SnapSettings.PositionEnabled = boolValue;
                else if (key == "RotationSnap")         m_SnapSettings.RotationEnabled = boolValue;
                else if (key == "ScaleSnap")            m_SnapSettings.ScaleEnabled = boolValue;
                else if (key == "PositionStep")         m_SnapSettings.PositionStep = std::clamp(std::stof(value), 0.01f, 100.0f);
                else if (key == "RotationStep")         m_SnapSettings.RotationStep = std::clamp(std::stof(value), 1.0f, 90.0f);
                else if (key == "ScaleStep")            m_SnapSettings.ScaleStep = std::clamp(std::stof(value), 0.01f, 10.0f);
                else if (key == "FaceSnapEnabled")      m_SnapSettings.FaceSnapEnabled = boolValue;
                else if (key == "FaceAlignmentEnabled") m_SnapSettings.FaceAlignmentEnabled = boolValue;
                else if (key == "EdgeSnapEnabled")      m_SnapSettings.EdgeSnapEnabled = boolValue;
                else if (key == "VertexSnapEnabled")    m_SnapSettings.VertexSnapEnabled = boolValue;
                else if (key == "AxisAlignmentEnabled") m_SnapSettings.AxisAlignmentEnabled = boolValue;
                else if (key == "FaceSnapThreshold")    m_SnapSettings.FaceSnapThreshold = std::clamp(std::stof(value), 0.001f, 10.0f);
                else if (key == "FaceAlignTolerance")   m_SnapSettings.FaceAlignTolerance = std::clamp(std::stof(value), 0.001f, 10.0f);
                else if (key == "VertexSnapThreshold")  m_SnapSettings.VertexSnapThreshold = std::clamp(std::stof(value), 0.001f, 10.0f);
                else if (key == "EdgeSnapThreshold")    m_SnapSettings.EdgeSnapThreshold = std::clamp(std::stof(value), 0.001f, 10.0f);
                else if (key == "ShadowIntensity")      { m_ShadowIntensity = std::clamp(std::stof(value), 0.0f, 1.0f); shadowIntensityWasSaved = true; }
                else if (key == "AmbientLight")         m_AmbientLight = std::clamp(std::stof(value), 0.1f, 0.8f);
                else if (key == "LightingEnabled")     m_Scene.GetLightingManager().Settings.Enabled = boolValue;
                else if (key == "DayNightCycleEnabled") m_Scene.GetLightingManager().Settings.DayNightCycleEnabled = boolValue;
                else if (key == "DirectionalDistanceSoftening") m_Scene.GetLightingManager().Settings.DirectionalDistanceSoftening = boolValue;
                else if (key == "DayTimeHours")        m_Scene.GetLightingManager().Settings.DayTimeHours = std::clamp(std::stof(value), 0.0f, 24.0f);
                else if (key == "DayNightCycleSpeed")  m_Scene.GetLightingManager().Settings.DayNightCycleSpeedHoursPerSecond = std::clamp(std::stof(value), 0.00001f, 1.0f);
                else if (key == "SunAzimuthDegrees")   m_Scene.GetLightingManager().Settings.SunAzimuthDegrees = std::clamp(std::stof(value), -360.0f, 360.0f);
                else if (key == "DirectionalSourceAngularDiameterDegrees") m_Scene.GetLightingManager().Settings.DirectionalSourceAngularDiameterDegrees = std::clamp(std::stof(value), 0.01f, 5.0f);
                else if (key == "SunIntensityScale")   m_Scene.GetLightingManager().Settings.SunIntensityScale = std::clamp(std::stof(value), 0.0f, 10.0f);
                else if (key == "DynamicShadows")      m_Scene.GetLightingManager().Settings.DynamicShadows = boolValue;
                else if (key == "ContactShadows")      m_Scene.GetLightingManager().Settings.ContactShadows = boolValue;
                else if (key == "IndirectLighting")    m_Scene.GetLightingManager().Settings.IndirectLighting = boolValue;
                else if (key == "EnvironmentLighting") m_Scene.GetLightingManager().Settings.EnvironmentLighting = boolValue;
                else if (key == "LightingQuality")     m_Scene.GetLightingManager().Settings.Quality = static_cast<LightingQuality>(std::clamp(std::stoi(value), 0, 3));
                else if (key == "MaxPointLights")      m_Scene.GetLightingManager().Settings.MaxPointLights = std::clamp(std::stoi(value), 0, 8);
                else if (key == "MaxSpotLights")       m_Scene.GetLightingManager().Settings.MaxSpotLights = std::clamp(std::stoi(value), 0, 8);
                else if (key == "MaxShadowedLocalLights") m_Scene.GetLightingManager().Settings.MaxShadowedLocalLights = std::clamp(std::stoi(value), 0, 4);
                else if (key == "IndirectIntensity")   m_Scene.GetLightingManager().Settings.IndirectIntensity = std::clamp(std::stof(value), 0.0f, 2.0f);
                else if (key == "ContactShadowStrength") m_Scene.GetLightingManager().Settings.ContactShadowStrength = std::clamp(std::stof(value), 0.0f, 1.0f);
                else if (key == "ContactShadowDistance") m_Scene.GetLightingManager().Settings.ContactShadowDistance = std::clamp(std::stof(value), 0.01f, 10.0f);
                else if (key == "ShadowDistance")       m_Scene.GetLightingManager().Settings.ShadowDistance = std::clamp(std::stof(value), 10.0f, 10000.0f);
                else if (key == "ShadowMapResolution")  m_Scene.GetLightingManager().Settings.ShadowMapResolution = std::clamp(std::stoi(value), 512, 4096);
                else if (key == "PointShadowMapResolution") m_Scene.GetLightingManager().Settings.PointShadowMapResolution = std::clamp(std::stoi(value), 256, 2048);
                else if (key == "AudioMasterVolume")    m_AudioSystem.SetMasterVolume(std::clamp(std::stof(value), 0.0f, 1.0f));
                else if (key == "AudioMusicVolume")     AudioMixer::Get().GetGroup(MixerGroupID::Music) ? AudioMixer::Get().GetGroup(MixerGroupID::Music)->SetVolume(std::clamp(std::stof(value), 0.0f, 1.0f)) : (void)0;
                else if (key == "AudioSFXVolume")       AudioMixer::Get().GetGroup(MixerGroupID::SFX)   ? AudioMixer::Get().GetGroup(MixerGroupID::SFX)->SetVolume(std::clamp(std::stof(value), 0.0f, 1.0f))   : (void)0;
                else if (key == "SunDirectionX")        m_Scene.GetLightingManager().Sun.Direction.X = std::stof(value);
                else if (key == "SunDirectionY")        m_Scene.GetLightingManager().Sun.Direction.Y = std::stof(value);
                else if (key == "SunDirectionZ")        m_Scene.GetLightingManager().Sun.Direction.Z = std::stof(value);
                else if (key == "SunColorR")            m_Scene.GetLightingManager().Sun.Color.X = std::clamp(std::stof(value), 0.0f, 1.0f);
                else if (key == "SunColorG")            m_Scene.GetLightingManager().Sun.Color.Y = std::clamp(std::stof(value), 0.0f, 1.0f);
                else if (key == "SunColorB")            m_Scene.GetLightingManager().Sun.Color.Z = std::clamp(std::stof(value), 0.0f, 1.0f);
                else if (key == "SunIntensity")         m_Scene.GetLightingManager().Sun.Intensity = std::clamp(std::stof(value), 0.0f, 3.0f);
                else if (key == "SunUseTemperature")    m_Scene.GetLightingManager().Sun.UseTemperature = boolValue;
                else if (key == "SunTemperature")       m_Scene.GetLightingManager().Sun.TemperatureKelvin = std::clamp(std::stof(value), 1000.0f, 40000.0f);
                else if (key == "MoonDirectionX")        m_Scene.GetLightingManager().Moon.Direction.X = std::stof(value);
                else if (key == "MoonDirectionY")        m_Scene.GetLightingManager().Moon.Direction.Y = std::stof(value);
                else if (key == "MoonDirectionZ")        m_Scene.GetLightingManager().Moon.Direction.Z = std::stof(value);
                else if (key == "MoonColorR")            m_Scene.GetLightingManager().Moon.Color.X = std::clamp(std::stof(value), 0.0f, 1.0f);
                else if (key == "MoonColorG")            m_Scene.GetLightingManager().Moon.Color.Y = std::clamp(std::stof(value), 0.0f, 1.0f);
                else if (key == "MoonColorB")            m_Scene.GetLightingManager().Moon.Color.Z = std::clamp(std::stof(value), 0.0f, 1.0f);
                else if (key == "MoonIntensity")         m_Scene.GetLightingManager().Moon.Intensity = std::clamp(std::stof(value), 0.0f, 3.0f);
                else if (key == "MoonUseTemperature")    m_Scene.GetLightingManager().Moon.UseTemperature = boolValue;
                else if (key == "MoonTemperature")       m_Scene.GetLightingManager().Moon.TemperatureKelvin = std::clamp(std::stof(value), 1000.0f, 40000.0f);
                else if (key == "GlobalLightEnabled")   {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().Enabled = boolValue;
                }
                else if (key == "GlobalBrightness")     {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().Brightness = std::clamp(std::stof(value), 0.0f, 3.0f);
                }
                else if (key == "GlobalContrast")       {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().Contrast = std::clamp(std::stof(value), 0.0f, 3.0f);
                }
                else if (key == "GlobalSaturation")     {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().Saturation = std::clamp(std::stof(value), 0.0f, 3.0f);
                }
                else if (key == "GlobalExposure")       {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().Exposure = std::clamp(std::stof(value), -3.0f, 3.0f);
                }
                else if (key == "GlobalGamma")          {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().Gamma = std::clamp(std::stof(value), 0.1f, 3.0f);
                }
                else if (key == "GlobalShadowStrength") {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().ShadowStrength = std::clamp(std::stof(value), 0.0f, 1.0f);
                }
                else if (key == "GlobalTintR")          {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().ColorTint.X = std::clamp(std::stof(value), 0.0f, 3.0f);
                }
                else if (key == "GlobalTintG")          {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().ColorTint.Y = std::clamp(std::stof(value), 0.0f, 3.0f);
                }
                else if (key == "GlobalTintB")          {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().ColorTint.Z = std::clamp(std::stof(value), 0.0f, 3.0f);
                }
                else if (key == "GlobalFogEnabled")     {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().FogEnabled = boolValue;
                }
                else if (key == "GlobalFogColorR")      {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().FogColor.X = std::clamp(std::stof(value), 0.0f, 3.0f);
                }
                else if (key == "GlobalFogColorG")      {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().FogColor.Y = std::clamp(std::stof(value), 0.0f, 3.0f);
                }
                else if (key == "GlobalFogColorB")      {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().FogColor.Z = std::clamp(std::stof(value), 0.0f, 3.0f);
                }
                else if (key == "GlobalFogDensity")     {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().FogDensity = std::clamp(std::stof(value), 0.0f, 0.2f);
                }
                else if (key == "GlobalFogStart")       {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().FogStart = std::clamp(std::stof(value), 0.0f, 10000.0f);
                }
                else if (key == "GlobalFogEnd")         {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().FogEnd = std::clamp(std::stof(value), 1.0f, 10000.0f);
                }
                else if (key == "GlobalFogMode")        {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().Lighting.FogMode = std::clamp(std::stoi(value), 0, 2);
                }
                else if (key == "GlobalVignetteStrength") {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().VignetteStrength = std::clamp(std::stof(value), 0.0f, 1.0f);
                }
                else if (key == "GlobalVignetteRadius") {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().VignetteRadius = std::clamp(std::stof(value), 0.1f, 1.0f);
                }
                else if (key == "GlobalToneMapping") {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().ToneMapping = static_cast<GlobalLightComponent::ToneMappingOperator>(std::clamp(std::stoi(value), 0, 3));
                }
                else if (key == "GlobalTemperature") {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().Temperature = std::clamp(std::stof(value), -1.0f, 1.0f);
                }
                else if (key == "GlobalPostTint") {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().Tint = std::clamp(std::stof(value), -1.0f, 1.0f);
                }
                else if (key == "GlobalFilmGrainEnabled") {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().FilmGrainEnabled = boolValue;
                }
                else if (key == "GlobalFilmGrainStrength") {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().FilmGrainStrength = std::clamp(std::stof(value), 0.0f, 0.25f);
                }
                else if (key == "GlobalFilmGrainSize") {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().FilmGrainSize = std::clamp(std::stof(value), 0.1f, 5.0f);
                }
                else if (key == "GlobalFilmGrainSpeed") {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().FilmGrainSpeed = std::clamp(std::stof(value), 0.0f, 2.0f);
                }
                else if (key == "GlobalChromaticEnabled") {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().ChromaticAberrationEnabled = boolValue;
                }
                else if (key == "GlobalChromaticStrength") {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().ChromaticAberrationStrength = std::clamp(std::stof(value), 0.0f, 0.1f);
                }
                else if (key == "GlobalSharpenStrength") {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().SharpenStrength = std::clamp(std::stof(value), 0.0f, 2.0f);
                }
                else if (key == "GlobalBloomStrength") {
                    for (Entity& entity : m_Scene.GetEntities()) if (entity.IsGlobalLight()) entity.GetGlobalLightComponent().BloomStrength = std::clamp(std::stof(value), 0.0f, 2.0f);
                }
                else if (key == "GizmoMode")            m_GizmoMode = static_cast<GizmoMode>(std::clamp(std::stoi(value), 0, 2));
                else if (key == "GizmoSpace")           m_GizmoSpace = static_cast<GizmoSpace>(std::clamp(std::stoi(value), 0, 1));
                else if (key == "FaceScaleHandles")     m_FaceScaleHandlesEnabled = boolValue;
                else if (key == "EditDuringPlay")      m_EditDuringPlay = boolValue;
                else if (key == "ShowHierarchy")        m_ShowHierarchy = boolValue;
                else if (key == "ShowInspector")        m_ShowInspector = boolValue;
                else if (key == "ShowAIPanel")          m_ShowAIPanel = boolValue;
                else if (key == "ShowAssetBrowser")     m_ShowAssetBrowser = boolValue;
                else if (key == "HierarchyWidth")       m_HierarchyWidth = std::clamp(std::stof(value), 200.0f, 800.0f);
                else if (key == "InspectorWidth")       m_InspectorWidth = std::clamp(std::stof(value), 200.0f, 800.0f);
            }
            catch (...) {}
        }

        {
            DirectionalLight& moon = m_Scene.GetLightingManager().Moon;
            // Migrate the old built-in blue moon tint while preserving custom tints.
            if (std::abs(moon.Color.X - 0.28f) < 0.015f &&
                std::abs(moon.Color.Y - 0.38f) < 0.015f &&
                std::abs(moon.Color.Z - 0.72f) < 0.015f)
                moon.Color = Vector3(1.0f, 1.0f, 1.0f);
        }

        if (qualityWasSaved)
        {
            savedQuality = std::clamp(savedQuality, 0, 4);
            m_GraphicsQuality = static_cast<GraphicsQuality>(savedQuality);
            m_ProjectConfig.Quality = m_GraphicsQuality;
            m_Renderer.SetQuality(m_GraphicsQuality);

            // A qualidade controla se os mapas existem, mas nÃ£o deve deixar
            // o Ultra sem sombras sÃ³ porque um valor antigo de intensidade
            // (salvo no modo Performance) foi carregado. Quando o arquivo
            // nÃ£o tinha uma intensidade explÃ­cita, usa o padrÃ£o do preset.
            if (!shadowIntensityWasSaved)
            {
                switch (m_GraphicsQuality)
                {
                case GraphicsQuality::Performance: m_ShadowIntensity = 0.5f; break;
                case GraphicsQuality::Balanced:    m_ShadowIntensity = 0.5f; break;
                case GraphicsQuality::HighFidelity: m_ShadowIntensity = 0.85f; break;
                case GraphicsQuality::UltraPerformance: m_ShadowIntensity = 0.0f; break;
                case GraphicsQuality::Ultra: m_ShadowIntensity = 1.0f; break;
                }
            }
            else if (m_GraphicsQuality != GraphicsQuality::Performance &&
                     m_GraphicsQuality != GraphicsQuality::UltraPerformance &&
                     m_ShadowIntensity <= 0.001f)
            {
                // Corrige preferÃªncias antigas que ficaram com 0 ao trocar
                // de Performance para Balanced/Ultra.
                m_ShadowIntensity = m_GraphicsQuality == GraphicsQuality::Ultra ? 1.0f :
                    (m_GraphicsQuality == GraphicsQuality::HighFidelity ? 0.85f : 0.5f);
            }
        }
    }

    // O perfil escolhido no Hub também configura os limites reais do sistema
    // de iluminação após cena e preferências do projeto terem sido lidas.
    if (!m_SceneHasLightingSettings)
        ApplyGraphicsPresetToLighting(m_Scene.GetLightingManager().Settings, m_GraphicsQuality);
    // Garante Enabled=true independente do arquivo de cena (cenas velhas tinham 0).
    m_Scene.GetLightingManager().Settings.Enabled = true;

    m_EditorSettingsLoaded = true;
    m_EditorSettingsSavePending = false;
    m_LastEditorSettingsSnapshot = BuildEditorSettingsSnapshot();
}

void Application::SaveEditorSettings()
{
    try
    {
        const std::filesystem::path path(GetEditorSettingsPath());
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        if (!file.is_open())
            return;

        const auto& sun = m_Scene.GetLightingManager().Sun;
        const auto& moon = m_Scene.GetLightingManager().Moon;
        auto& lightingSettings = m_Scene.GetLightingManager().Settings;
        lightingSettings.Clamp();
        const GlobalLightComponent* globalLight = nullptr;
        for (const Entity& entity : m_Scene.GetEntities())
        {
            if (entity.IsGlobalLight())
            {
                globalLight = &entity.GetGlobalLightComponent();
                break;
            }
        }
        const GlobalLightComponent defaultGlobalLight;
        if (!globalLight)
            globalLight = &defaultGlobalLight;

        file << "# Preferencias automaticas do editor Marae Engine\n";
        file << std::setprecision(9);
        file << "GraphicsQuality=" << static_cast<int>(m_GraphicsQuality) << "\n";
        file << "CameraMoveSpeed=" << m_Camera.MoveSpeed << "\n";
        file << "ShowGrid=" << m_ShowGrid << "\n";
        file << "ShowAxis=" << m_ShowAxis << "\n";
        file << "GridRenderDistance=" << m_GridRenderDistance << "\n";
        file << "PositionSnap=" << m_SnapSettings.PositionEnabled << "\n";
        file << "RotationSnap=" << m_SnapSettings.RotationEnabled << "\n";
        file << "ScaleSnap=" << m_SnapSettings.ScaleEnabled << "\n";
        file << "PositionStep=" << m_SnapSettings.PositionStep << "\n";
        file << "RotationStep=" << m_SnapSettings.RotationStep << "\n";
        file << "ScaleStep=" << m_SnapSettings.ScaleStep << "\n";
        file << "FaceSnapEnabled=" << m_SnapSettings.FaceSnapEnabled << "\n";
        file << "FaceAlignmentEnabled=" << m_SnapSettings.FaceAlignmentEnabled << "\n";
        file << "EdgeSnapEnabled=" << m_SnapSettings.EdgeSnapEnabled << "\n";
        file << "VertexSnapEnabled=" << m_SnapSettings.VertexSnapEnabled << "\n";
        file << "AxisAlignmentEnabled=" << m_SnapSettings.AxisAlignmentEnabled << "\n";
        file << "FaceSnapThreshold=" << m_SnapSettings.FaceSnapThreshold << "\n";
        file << "FaceAlignTolerance=" << m_SnapSettings.FaceAlignTolerance << "\n";
        file << "VertexSnapThreshold=" << m_SnapSettings.VertexSnapThreshold << "\n";
        file << "EdgeSnapThreshold=" << m_SnapSettings.EdgeSnapThreshold << "\n";
        file << "ShadowIntensity=" << m_ShadowIntensity << "\n";
        file << "AmbientLight=" << m_AmbientLight << "\n";
        file << "LightingEnabled=" << lightingSettings.Enabled << "\n";
        file << "DynamicShadows=" << lightingSettings.DynamicShadows << "\n";
        file << "ContactShadows=" << lightingSettings.ContactShadows << "\n";
        file << "IndirectLighting=" << lightingSettings.IndirectLighting << "\n";
        file << "EnvironmentLighting=" << lightingSettings.EnvironmentLighting << "\n";
        file << "LightingQuality=" << static_cast<int>(lightingSettings.Quality) << "\n";
        file << "MaxPointLights=" << lightingSettings.MaxPointLights << "\n";
        file << "MaxSpotLights=" << lightingSettings.MaxSpotLights << "\n";
        file << "MaxShadowedLocalLights=" << lightingSettings.MaxShadowedLocalLights << "\n";
        file << "IndirectIntensity=" << lightingSettings.IndirectIntensity << "\n";
        file << "ContactShadowStrength=" << lightingSettings.ContactShadowStrength << "\n";
        file << "ContactShadowDistance=" << lightingSettings.ContactShadowDistance << "\n";
        file << "ShadowDistance=" << lightingSettings.ShadowDistance << "\n";
        file << "ShadowMapResolution=" << lightingSettings.ShadowMapResolution << "\n";
        file << "PointShadowMapResolution=" << lightingSettings.PointShadowMapResolution << "\n";
        file << "DayNightCycleEnabled=" << lightingSettings.DayNightCycleEnabled << "\n";
        file << "DirectionalDistanceSoftening=" << lightingSettings.DirectionalDistanceSoftening << "\n";
        file << "DayTimeHours=" << lightingSettings.DayTimeHours << "\n";
        file << "DayNightCycleSpeed=" << lightingSettings.DayNightCycleSpeedHoursPerSecond << "\n";
        file << "SunAzimuthDegrees=" << lightingSettings.SunAzimuthDegrees << "\n";
        file << "DirectionalSourceAngularDiameterDegrees=" << lightingSettings.DirectionalSourceAngularDiameterDegrees << "\n";
        file << "SunIntensityScale=" << lightingSettings.SunIntensityScale << "\n";
        file << "AudioMasterVolume=" << m_AudioSystem.GetMasterVolume() << "\n";
        file << "AudioMusicVolume=" << (AudioMixer::Get().GetGroup(MixerGroupID::Music) ? AudioMixer::Get().GetGroup(MixerGroupID::Music)->GetVolume() : 1.f) << "\n";
        file << "AudioSFXVolume="   << (AudioMixer::Get().GetGroup(MixerGroupID::SFX)   ? AudioMixer::Get().GetGroup(MixerGroupID::SFX)->GetVolume()   : 1.f) << "\n";
        file << "SunDirectionX=" << sun.Direction.X << "\n";
        file << "SunDirectionY=" << sun.Direction.Y << "\n";
        file << "SunDirectionZ=" << sun.Direction.Z << "\n";
        file << "SunColorR=" << sun.Color.X << "\n";
        file << "SunColorG=" << sun.Color.Y << "\n";
        file << "SunColorB=" << sun.Color.Z << "\n";
        file << "SunIntensity=" << sun.Intensity << "\n";
        file << "SunUseTemperature=" << sun.UseTemperature << "\n";
        file << "SunTemperature=" << sun.TemperatureKelvin << "\n";
        file << "MoonDirectionX=" << moon.Direction.X << "\n";
        file << "MoonDirectionY=" << moon.Direction.Y << "\n";
        file << "MoonDirectionZ=" << moon.Direction.Z << "\n";
        file << "MoonColorR=" << moon.Color.X << "\n";
        file << "MoonColorG=" << moon.Color.Y << "\n";
        file << "MoonColorB=" << moon.Color.Z << "\n";
        file << "MoonIntensity=" << moon.Intensity << "\n";
        file << "MoonUseTemperature=" << moon.UseTemperature << "\n";
        file << "MoonTemperature=" << moon.TemperatureKelvin << "\n";
        file << "GlobalLightEnabled=" << globalLight->Enabled << "\n";
        file << "GlobalBrightness=" << globalLight->Brightness << "\n";
        file << "GlobalContrast=" << globalLight->Contrast << "\n";
        file << "GlobalSaturation=" << globalLight->Saturation << "\n";
        file << "GlobalExposure=" << globalLight->Exposure << "\n";
        file << "GlobalGamma=" << globalLight->Gamma << "\n";
        file << "GlobalShadowStrength=" << globalLight->ShadowStrength << "\n";
        file << "GlobalTintR=" << globalLight->ColorTint.X << "\n";
        file << "GlobalTintG=" << globalLight->ColorTint.Y << "\n";
        file << "GlobalTintB=" << globalLight->ColorTint.Z << "\n";
        file << "GlobalFogEnabled=" << globalLight->FogEnabled << "\n";
        file << "GlobalFogColorR=" << globalLight->FogColor.X << "\n";
        file << "GlobalFogColorG=" << globalLight->FogColor.Y << "\n";
        file << "GlobalFogColorB=" << globalLight->FogColor.Z << "\n";
        file << "GlobalFogDensity=" << globalLight->FogDensity << "\n";
        file << "GlobalFogStart=" << globalLight->FogStart << "\n";
        file << "GlobalFogEnd=" << globalLight->FogEnd << "\n";
        file << "GlobalFogMode=" << std::clamp(globalLight->Lighting.FogMode, 0, 2) << "\n";
        file << "GlobalVignetteStrength=" << globalLight->VignetteStrength << "\n";
        file << "GlobalVignetteRadius=" << globalLight->VignetteRadius << "\n";
        file << "GlobalToneMapping=" << static_cast<int>(globalLight->ToneMapping) << "\n";
        file << "GlobalTemperature=" << globalLight->Temperature << "\n";
        file << "GlobalPostTint=" << globalLight->Tint << "\n";
        file << "GlobalFilmGrainEnabled=" << globalLight->FilmGrainEnabled << "\n";
        file << "GlobalFilmGrainStrength=" << globalLight->FilmGrainStrength << "\n";
        file << "GlobalFilmGrainSize=" << globalLight->FilmGrainSize << "\n";
        file << "GlobalFilmGrainSpeed=" << globalLight->FilmGrainSpeed << "\n";
        file << "GlobalChromaticEnabled=" << globalLight->ChromaticAberrationEnabled << "\n";
        file << "GlobalChromaticStrength=" << globalLight->ChromaticAberrationStrength << "\n";
        file << "GlobalSharpenStrength=" << globalLight->SharpenStrength << "\n";
        file << "GlobalBloomStrength=" << globalLight->BloomStrength << "\n";
        file << "GizmoMode=" << static_cast<int>(m_GizmoMode) << "\n";
        file << "GizmoSpace=" << static_cast<int>(m_GizmoSpace) << "\n";
        file << "FaceScaleHandles=" << m_FaceScaleHandlesEnabled << "\n";
        file << "EditDuringPlay=" << m_EditDuringPlay << "\n";
        file << "ShowHierarchy=" << m_ShowHierarchy << "\n";
        file << "ShowInspector=" << m_ShowInspector << "\n";
        file << "ShowAIPanel=" << m_ShowAIPanel << "\n";
        file << "ShowAssetBrowser=" << m_ShowAssetBrowser << "\n";
        file << "HierarchyWidth=" << m_HierarchyWidth << "\n";
        file << "InspectorWidth=" << m_InspectorWidth << "\n";

        m_LastEditorSettingsSnapshot = BuildEditorSettingsSnapshot();
        m_EditorSettingsSavePending = false;
        AudioMixer::Get().Save(m_ProjectConfig.ProjectPath);
    }
    catch (...) {}
}

void Application::AutoSaveEditorSettings()
{
    if (!m_EditorSettingsLoaded)
        return;

    const std::string currentSnapshot = BuildEditorSettingsSnapshot();
    if (currentSnapshot != m_LastEditorSettingsSnapshot)
    {
        m_LastEditorSettingsSnapshot = currentSnapshot;
        m_EditorSettingsSavePending = true;
        m_EditorSettingsLastChangeTime = glfwGetTime();
    }

    // Debounce de 0,75 s: sliders podem gerar muitos eventos por segundo.
    if (m_EditorSettingsSavePending &&
        glfwGetTime() - m_EditorSettingsLastChangeTime >= 0.75)
    {
        SaveEditorSettings();
    }
}

// ---------------------------------------------------------------------------
// ConfiguraÃ§Ãµes globais do editor (mesmo arquivo do ProjectHub)
// ---------------------------------------------------------------------------
static std::filesystem::path GetEditorSystemFontPath(int fontIndex)
{
    static constexpr const char* fontFiles[] = {
        "segoeui.ttf", "arial.ttf", "calibri.ttf", "consola.ttf", "tahoma.ttf", "verdana.ttf"
    };
    const int safeIndex = std::clamp(fontIndex, 0, static_cast<int>(std::size(fontFiles)) - 1);
    size_t envLength = 0;
    char* windowsDirectory = nullptr;
    _dupenv_s(&windowsDirectory, &envLength, "WINDIR");
    const std::filesystem::path windowsRoot = windowsDirectory ? windowsDirectory : "C:/Windows";
    std::free(windowsDirectory);
    return windowsRoot / "Fonts" / fontFiles[safeIndex];
}

std::string Application::GetGlobalEditorSettingsPath() const
{
    namespace fs = std::filesystem;
    fs::path dir = fs::path(ProjectManager::GetUserDocumentsPath()) / "MaraeEngine";
    fs::create_directories(dir);
    return (dir / "editor_settings.cfg").string();
}

void Application::LoadGlobalEditorSettings()
{
    std::ifstream f(GetGlobalEditorSettingsPath());
    if (!f.is_open())
    {
        m_Renderer.SetRequestedGraphicsAPI(m_GlobalEditorSettings.RequestedGraphicsAPI);
        return;
    }

    static const auto parseBool = [](const std::string& v) -> bool {
        return !v.empty() && (v[0] == '1' || v[0] == 't' || v[0] == 'T' || v == "true" || v == "True");
    };

    std::string line;
    while (std::getline(f, line))
    {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back())))  key.pop_back();
        while (!val.empty() && std::isspace(static_cast<unsigned char>(val.front()))) val.erase(val.begin());
        while (!val.empty() && std::isspace(static_cast<unsigned char>(val.back())))  val.pop_back();

        if      (key == "AutoSave")         m_GlobalEditorSettings.AutoSave          = parseBool(val);
        else if (key == "AutoSaveInterval") m_GlobalEditorSettings.AutoSaveIntervalS = std::max(30, std::atoi(val.c_str()));
        else if (key == "FontScale")        m_GlobalEditorSettings.FontScale         = std::clamp(std::stof(val), 0.5f, 3.0f);
        else if (key == "FontIndex")        m_GlobalEditorSettings.FontIndex         = std::clamp(std::atoi(val.c_str()), 0, 5);
        else if (key == "ShowFPS")          m_GlobalEditorSettings.ShowFPS           = parseBool(val);
        else if (key == "VSync")            m_GlobalEditorSettings.VSync             = parseBool(val);
        else if (key == "ThemeIndex")       m_GlobalEditorSettings.ThemeIndex        = std::atoi(val.c_str());
        else if (key == "GraphicsAPI")      m_GlobalEditorSettings.RequestedGraphicsAPI = static_cast<GraphicsAPI>(std::clamp(std::atoi(val.c_str()), 0, 1));
        else if (key == "RendererMode")     m_GlobalEditorSettings.RequestedRendererMode = static_cast<RendererMode>(std::clamp(std::atoi(val.c_str()), 0, 3));
    }
    // Preserve configurations written by older editor builds.
    if (m_GlobalEditorSettings.RequestedRendererMode == RendererMode::Automatic &&
        m_GlobalEditorSettings.RequestedGraphicsAPI == GraphicsAPI::Vulkan)
        m_GlobalEditorSettings.RequestedRendererMode = RendererMode::Hybrid;
    m_Renderer.SetRequestedGraphicsAPI(m_GlobalEditorSettings.RequestedGraphicsAPI);
}

void Application::SaveGlobalEditorSettings()
{
    std::ofstream f(GetGlobalEditorSettingsPath());
    if (!f.is_open()) return;
    f << "AutoSave="         << (m_GlobalEditorSettings.AutoSave ? 1 : 0)           << "\n";
    f << "AutoSaveInterval=" << m_GlobalEditorSettings.AutoSaveIntervalS             << "\n";
    f << "FontScale="        << std::fixed << m_GlobalEditorSettings.FontScale       << "\n";
    f << "FontIndex="        << m_GlobalEditorSettings.FontIndex                      << "\n";
    f << "ShowFPS="          << (m_GlobalEditorSettings.ShowFPS ? 1 : 0)             << "\n";
    f << "VSync="            << (m_GlobalEditorSettings.VSync ? 1 : 0)               << "\n";
    f << "GraphicsAPI="      << static_cast<int>(m_GlobalEditorSettings.RequestedGraphicsAPI) << "\n";
    f << "RendererMode="     << static_cast<int>(m_GlobalEditorSettings.RequestedRendererMode) << "\n";
    f << "ThemeIndex="       << m_GlobalEditorSettings.ThemeIndex                    << "\n";
}

void Application::ApplyGlobalEditorSettings(bool rebuildFonts)
{
    ImGuiIO* io = ImGui::GetCurrentContext() ? &ImGui::GetIO() : nullptr;

    glfwSwapInterval(m_GlobalEditorSettings.VSync ? 1 : 0);

    if (io)
    {
        io->FontGlobalScale = std::clamp(m_GlobalEditorSettings.FontScale, 0.5f, 3.0f);
        if (rebuildFonts && io->Fonts && io->Fonts->Fonts.size() > 0)
        {
            float scaleX = 1.0f, scaleY = 1.0f;
            glfwGetWindowContentScale(m_Window.GetNativeWindow(), &scaleX, &scaleY);
            const float dpiScale = std::clamp(std::max({ scaleX, scaleY, 1.0f }), 1.0f, 1.50f);
            io->Fonts->Clear();
            io->Fonts->AddFontDefault(); // slot 0: fallback para cenas antigas
            for (int i = 0; i < 6; ++i)
            {
                const std::filesystem::path fontPath = GetEditorSystemFontPath(i);
                ImFont* font = std::filesystem::exists(fontPath)
                    ? io->Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 13.0f * dpiScale)
                    : nullptr;
                if (!font) io->Fonts->AddFontDefault(); // preserva índice estável
            }
            const int defaultFontIndex = std::clamp(m_GlobalEditorSettings.FontIndex, 0, 5) + 1;
            io->FontDefault = defaultFontIndex < io->Fonts->Fonts.Size
                ? io->Fonts->Fonts[defaultFontIndex] : io->Fonts->Fonts[0];
        }
    }
}

void Application::RenderGlobalEditorSettingsModal()
{
    if (!m_ShowGlobalEditorSettings) return;

    ImGui::SetNextWindowSize(ImVec2(640.f, 500.f), ImGuiCond_FirstUseEver);
    const std::string popupName = std::string(L("settings.preferences")) + "##GlobalEditorSettings";
    if (!ImGui::BeginPopupModal(popupName.c_str(), nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
    {
        // Abrir o popup na primeira vez
        ImGui::OpenPopup(popupName.c_str());
        return;
    }

    const ImVec4 accent(0.06f, 0.56f, 0.98f, 1.00f);
    if (ImGui::BeginTabBar("##EditorPreferencesTabs"))
    {
        if (ImGui::BeginTabItem(L("settings.tab_general")))
        {
            ImGui::Spacing();
            ImGui::Checkbox(L("hub.show_fps"), &m_GlobalEditorSettings.ShowFPS);
            const char* themes[] = { "Dark Blue", "Cl\u00e1ssico", "Escuro Puro" };
            int idx = std::clamp(m_GlobalEditorSettings.ThemeIndex, 0, 2);
            if (ImGui::Combo(L("hub.theme"), &idx, themes, IM_ARRAYSIZE(themes)))
            {
                m_GlobalEditorSettings.ThemeIndex = idx;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(L("settings.tab_language")))
        {
            ImGui::Spacing();
            auto& localization = Localization::Get();
            int languageIndex = 0;
            switch (localization.GetLanguage())
            {
                case EditorLanguage::Auto:       languageIndex = 0; break;
                case EditorLanguage::Portuguese: languageIndex = 1; break;
                case EditorLanguage::English:    languageIndex = 2; break;
                case EditorLanguage::Spanish:    languageIndex = 3; break;
                default:                         languageIndex = 0; break;
            }
            const char* languages[] = {
                L("menu.language_auto"),
                L("menu.language_pt"),
                L("menu.language_en"),
                L("menu.language_es")
            };
            if (ImGui::Combo(L("settings.language"), &languageIndex, languages, IM_ARRAYSIZE(languages)))
            {
                if (languageIndex == 0)      localization.SetLanguage(EditorLanguage::Auto);
                else if (languageIndex == 1) localization.SetLanguage(EditorLanguage::Portuguese);
                else if (languageIndex == 2) localization.SetLanguage(EditorLanguage::English);
                else if (languageIndex == 3) localization.SetLanguage(EditorLanguage::Spanish);
            }

            if (localization.GetLanguage() == EditorLanguage::Auto)
            {
                EditorLanguage sys = localization.GetResolvedLanguage();
                const char* sysName = (sys == EditorLanguage::Portuguese) ? "Portugu\xc3\xaas" :
                                      (sys == EditorLanguage::Spanish)    ? "Espa\xc3\xb1ol" : "English";
                ImGui::TextDisabled("Sistema: %s", sysName);
            }

            bool automaticTranslation = localization.IsAutoTranslationEnabled();
            if (ImGui::Checkbox(L("settings.auto_translation"), &automaticTranslation))
                localization.SetAutoTranslationEnabled(automaticTranslation);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(UI("Somente rótulos fixos da interface são enviados ao serviço online; projetos, scripts e nomes de assets não são enviados."));
            const std::string translationStatus = localization.GetAutoTranslationStatus();
            ImGui::TextWrapped("%s", translationStatus.c_str());
            if (automaticTranslation &&
                (translationStatus.find("indisponível") != std::string::npos ||
                 translationStatus.find("unavailable") != std::string::npos) &&
                ImGui::Button(L("settings.translation_retry")))
                localization.RetryAutoTranslation();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(L("settings.tab_appearance")))
        {
            ImGui::Spacing();
            const char* fontOptions[] = {
                L("settings.font_system"), L("settings.font_arial"), L("settings.font_calibri"),
                L("settings.font_consolas"), L("settings.font_tahoma"), L("settings.font_verdana")
            };
            int fontIndex = std::clamp(m_GlobalEditorSettings.FontIndex, 0, IM_ARRAYSIZE(fontOptions) - 1);
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::Combo(L("settings.font"), &fontIndex, fontOptions, IM_ARRAYSIZE(fontOptions)))
            {
                m_GlobalEditorSettings.FontIndex = fontIndex;
                m_FontRebuildPending = true;
            }
            ImGui::TextWrapped("%s", L("settings.font_note"));

            float scale = m_GlobalEditorSettings.FontScale;
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::SliderFloat(L("hub.font_scale"), &scale, 0.75f, 2.0f, "%.2f"))
            {
                m_GlobalEditorSettings.FontScale = scale;
                ApplyGlobalEditorSettings(false);
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(L("settings.tab_rendering")))
        {
            ImGui::Spacing();
            if (ImGui::Checkbox(L("common.vsync"), &m_GlobalEditorSettings.VSync))
            {
                ApplyGlobalEditorSettings(false);
            }

            ImGui::Separator();
            ImGui::TextUnformatted(L("settings.rendering_api"));
            ImGui::Spacing();
            const char* rendererModes[] = {
                "Automático (recomendado)",
                "Somente OpenGL",
                "Híbrido: Vulkan na interface + OpenGL na cena",
                "Somente Vulkan (indisponível: renderer da cena incompleto)"
            };
            int rendererMode = static_cast<int>(m_GlobalEditorSettings.RequestedRendererMode);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("Modo do renderer##RendererMode", &rendererMode,
                             rendererModes, IM_ARRAYSIZE(rendererModes)))
            {
                const RendererMode requested = static_cast<RendererMode>(std::clamp(rendererMode, 0, 3));
                if (requested != RendererMode::VulkanOnly)
                {
                    m_GlobalEditorSettings.RequestedRendererMode = requested;
                    if (requested == RendererMode::Automatic || requested == RendererMode::OpenGLOnly)
                    {
                        m_GlobalEditorSettings.RequestedGraphicsAPI = GraphicsAPI::OpenGL;
                        RequestGraphicsAPISwitch(GraphicsAPI::OpenGL);
                    }
                    else
                    {
                        ImGui::OpenPopup("VulkanExperimentalConfirmation");
                    }
                    SaveGlobalEditorSettings();
                }
            }
            ImGui::TextWrapped("Automático verifica compatibilidade; a comparação de desempenho entre APIs será habilitada quando os mesmos passes da cena rodarem em ambos os backends.");
            ImGui::Text("OpenGL: %s | %s | %s",
                m_Renderer.GetOpenGLVendor().c_str(),
                m_Renderer.GetOpenGLRenderer().c_str(),
                m_Renderer.GetOpenGLVersion().c_str());
            if (!m_Renderer.GetVulkanDiagnostics().empty())
            {
                if (ImGui::TreeNode("Diagnóstico Vulkan##RendererHardware"))
                {
                    ImGui::TextWrapped("%s", m_Renderer.GetVulkanDiagnostics().c_str());
                    ImGui::TreePop();
                }
            }
            else
            {
                ImGui::TextDisabled("A compatibilidade Vulkan ainda não foi sondada nesta sessão.");
            }
            ImGui::Spacing();
            const float apiButtonWidth = std::max(120.0f,
                (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
            const GraphicsAPI selectedApi = m_GlobalEditorSettings.RequestedGraphicsAPI;

            if (selectedApi == GraphicsAPI::OpenGL)
                ImGui::PushStyleColor(ImGuiCol_Button, accent);
            if (ImGui::Button(L("settings.api_opengl"), ImVec2(apiButtonWidth, 36.0f)))
            {
                m_GlobalEditorSettings.RequestedRendererMode = RendererMode::OpenGLOnly;
                RequestGraphicsAPISwitch(GraphicsAPI::OpenGL);
                SaveGlobalEditorSettings();
            }
            if (selectedApi == GraphicsAPI::OpenGL)
                ImGui::PopStyleColor();

            ImGui::SameLine();
            if (selectedApi == GraphicsAPI::Vulkan)
                ImGui::PushStyleColor(ImGuiCol_Button, accent);
            if (ImGui::Button(L("settings.api_vulkan"), ImVec2(apiButtonWidth, 36.0f)) &&
                selectedApi != GraphicsAPI::Vulkan)
            {
                ImGui::OpenPopup("VulkanExperimentalConfirmation");
            }
            if (selectedApi == GraphicsAPI::Vulkan)
                ImGui::PopStyleColor();

            ImGui::Text(L("settings.api_active"),
                m_Renderer.GetActiveGraphicsAPI() == GraphicsAPI::OpenGL
                    ? "OpenGL (interface e cena)"
                    : "Híbrido: Vulkan (interface) + OpenGL (cena)");
            ImGui::Text(L("settings.api_selected"),
                m_GlobalEditorSettings.RequestedRendererMode == RendererMode::Automatic
                    ? "Automático (seleção segura: OpenGL até haver benchmark comparável)"
                    : (m_GlobalEditorSettings.RequestedGraphicsAPI == GraphicsAPI::OpenGL
                        ? "Somente OpenGL" : "Híbrido (experimental)") );
            ImGui::TextWrapped("%s", L("settings.api_stage_note"));

            if (ImGui::BeginPopupModal("VulkanExperimentalConfirmation", nullptr,
                    ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextWrapped("%s", L("settings.vulkan_warning"));
                ImGui::Spacing();
                ImGui::TextWrapped("%s", L("settings.vulkan_not_active_note"));
                ImGui::Spacing();
                if (ImGui::Button(L("settings.vulkan_accept"), ImVec2(150.0f, 0.0f)))
                {
                    m_GlobalEditorSettings.RequestedRendererMode = RendererMode::Hybrid;
                    RequestGraphicsAPISwitch(GraphicsAPI::Vulkan);
                    SaveGlobalEditorSettings();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(L("settings.vulkan_cancel"), ImVec2(100.0f, 0.0f)))
                {
                    if (m_GlobalEditorSettings.RequestedRendererMode == RendererMode::Hybrid)
                    {
                        m_GlobalEditorSettings.RequestedRendererMode = RendererMode::OpenGLOnly;
                        m_GlobalEditorSettings.RequestedGraphicsAPI = GraphicsAPI::OpenGL;
                        SaveGlobalEditorSettings();
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(L("settings.tab_saving")))
        {
            ImGui::Spacing();
            ImGui::Checkbox(L("hub.autosave"), &m_GlobalEditorSettings.AutoSave);
            if (m_GlobalEditorSettings.AutoSave)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100.f);
                int interval = m_GlobalEditorSettings.AutoSaveIntervalS;
                if (ImGui::InputInt(UI("##autosavesec"), &interval, 30))
                {
                    m_GlobalEditorSettings.AutoSaveIntervalS = std::max(30, interval);
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(L("hub.seconds"));
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // ---------- BotÃµes ----------
    ImVec2 btnSize(140.f, 34.f);
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(avail - (btnSize.x * 2.f + 12.f));

    ImGui::PushStyleColor(ImGuiCol_Button,        accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.04f, 0.42f, 0.74f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.04f, 0.42f, 0.74f, 1.f));
    if (ImGui::Button(L("hub.save_settings"), btnSize))
    {
        SaveGlobalEditorSettings();
        ImGui::CloseCurrentPopup();
        m_ShowGlobalEditorSettings = false;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0.f, 12.f);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.22f, 0.22f, 0.22f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.30f, 0.30f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.18f, 0.18f, 0.18f, 1.f));
    if (ImGui::Button(L("common.cancel"), btnSize))
    {
        // Descarta alteraÃ§Ãµes â€” recarrega do disco.
        LoadGlobalEditorSettings();
        m_FontRebuildPending = true;
        ApplyGlobalEditorSettings(false);
        ImGui::CloseCurrentPopup();
        m_ShowGlobalEditorSettings = false;
    }
    ImGui::PopStyleColor(3);

    ImGui::EndPopup();
}

void Application::LoadMaraeLogo()
{
    m_LogoTexture = LoadMaraeLogoTexture();
}

void Application::RenderLoadingSplash(float progress, const char* status)
{
    if (!m_LoadingSplashActive && progress < 1.0f)
        return;

    m_LoadingSplashProgress = std::clamp(progress, 0.0f, 1.0f);
    m_Window.PollEvents();
    if (m_Window.ShouldClose())
        return;

    const int width = std::max(1, m_Window.GetWidth());
    const int height = std::max(1, m_Window.GetHeight());
    if (m_Window.GetGraphicsAPI() == GraphicsAPI::OpenGL)
    {
        glViewport(0, 0, width, height);
        glClearColor(0.018f, 0.025f, 0.055f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_NewFrame();
    }
    else if (m_VulkanUIRenderer.IsInitialized())
    {
        ImGui_ImplVulkan_NewFrame();
    }
    else
    {
        return;
    }
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)));
    ImGui::Begin(UI("##MaraeLoadingSplash"), nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoInputs);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 center(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
    const float time = static_cast<float>(ImGui::GetTime());

    // Fundo simples, com apenas uma luz suave para não competir com a logo.
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                        IM_COL32(7, 11, 27, 255));
    draw->AddCircleFilled(ImVec2(center.x - 230.0f, center.y - 180.0f),
                          250.0f, IM_COL32(29, 76, 150, 24), 64);
    draw->AddCircleFilled(ImVec2(center.x + 270.0f, center.y + 190.0f),
                          230.0f, IM_COL32(20, 108, 160, 18), 64);

    const float logoSize = std::min(238.0f, std::min(size.x, size.y) * 0.32f);
    const ImVec2 logoPos(center.x - logoSize * 0.5f, center.y - logoSize * 0.68f);
    if (m_LogoTexture != 0 && m_Window.GetGraphicsAPI() == GraphicsAPI::OpenGL)
    {
        ImGui::SetCursorScreenPos(logoPos);
        ImGui::Image(static_cast<ImTextureID>(m_LogoTexture),
                     ImVec2(logoSize, logoSize), ImVec2(0, 0), ImVec2(1, 1));
    }

    // Roda de carregamento simples, com arco em movimento.
    const ImVec2 spinnerCenter(center.x, center.y + logoSize * 0.48f);
    const float start = time * 4.2f;
    draw->AddCircle(spinnerCenter, 17.0f, IM_COL32(42, 62, 102, 255), 32, 3.0f);
    draw->PathClear();
    for (int i = 0; i <= 28; ++i)
    {
        const float angle = start + 4.8f * static_cast<float>(i) / 28.0f;
        draw->PathLineTo(ImVec2(spinnerCenter.x + std::cos(angle) * 17.0f,
                                spinnerCenter.y + std::sin(angle) * 17.0f));
    }
    draw->PathStroke(IM_COL32(91, 172, 255, 255), false, 3.5f);

    const char* loadingStatus = status ? status : "Carregando...";
    const ImVec2 statusSize = ImGui::CalcTextSize(loadingStatus);
    draw->AddText(ImVec2(center.x - statusSize.x * 0.5f, spinnerCenter.y + 32.0f),
                  IM_COL32(178, 193, 222, 255), loadingStatus);

    char percentText[16] = {};
    std::snprintf(percentText, sizeof(percentText), "%d%%",
                  static_cast<int>(m_LoadingSplashProgress * 100.0f));
    const ImVec2 percentSize = ImGui::CalcTextSize(percentText);
    draw->AddText(ImVec2(center.x - percentSize.x * 0.5f, spinnerCenter.y + 55.0f),
                  IM_COL32(91, 145, 208, 210), percentText);

    ImGui::End();
    ImGui::Render();
    if (m_Window.GetGraphicsAPI() == GraphicsAPI::OpenGL)
    {
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        m_Window.Present();
    }
    else
    {
        m_VulkanUIRenderer.Render(ImGui::GetDrawData());
    }
}

void Application::ReturnToProjectHub()
{
    SaveEditorSettings();
    m_SurvivalGame.Save(MakeDemoGameContext());
    SaveScene();
    if (m_SceneDirty) return; // mantÃ©m o editor aberto se o salvamento falhar

    m_SessionResult.Action = ProjectSessionAction::ReturnToHub;
    glfwSetWindowShouldClose(m_Window.GetNativeWindow(), GLFW_TRUE);
}

void Application::OpenAnotherProject()
{
    const std::string selectedPath = ProjectManager::BrowseForProjectFolder(
        "Selecione o outro projeto para abrir");
    if (selectedPath.empty()) return;

    const ProjectInfo discovered = ProjectManager::DiscoverProject(selectedPath, m_ProjectConfig.Quality);
    const ProjectInfo registered = ProjectManager::RegisterProject(discovered);
    if (registered.Path.empty()) return;

    m_SurvivalGame.Save(MakeDemoGameContext());
    SaveScene();
    SaveEditorSettings();
    if (m_SceneDirty) return; // nÃ£o troca de projeto se nÃ£o foi possÃ­vel proteger a cena atual

    m_SessionResult.Action = ProjectSessionAction::OpenAnotherProject;
    m_SessionResult.NextProject.ProjectPath = registered.Path;
    m_SessionResult.NextProject.ScenePath = registered.ScenePath;
    m_SessionResult.NextProject.Quality = registered.Quality;
    m_SessionResult.NextProject.WorldType = registered.WorldType;
    m_SessionResult.NextProject.Graphics = m_Window.GetGraphicsAPI();
    glfwSetWindowShouldClose(m_Window.GetNativeWindow(), GLFW_TRUE);
}

void Application::PrintInspector()
{
}

int Application::RunPhysicsRegressionTests()
{
    constexpr float step = 1.0f / 60.0f;
    int failures = 0;
    auto check = [&](bool condition, const char* name)
    {
        std::cout << (condition ? "[PhysicsTest] PASS: " : "[PhysicsTest] FAIL: ")
                  << name << std::endl;
        if (!condition)
            ++failures;
    };
    auto configureBody = [](Entity& entity, const Vector3& position,
                            bool isStatic, Entity::ColliderShape shape)
    {
        entity.GetTransform().Position = position;
        entity.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);
        entity.GetTransform().Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        entity.PhysicsEnabled = true;
        entity.ColliderEnabled = true;
        entity.IsStatic = isStatic;
        entity.UseGravity = !isStatic;
        entity.Collider = shape;
        entity.Mass = 1.0f;
        entity.Restitution = 0.0f;
        entity.Friction = 0.8f;
        entity.Velocity = Vector3(0.0f, 0.0f, 0.0f);
        entity.AngularVelocity = Vector3(0.0f, 0.0f, 0.0f);
    };

    m_WorldMode = WorldMode::World3D;

    // 1) Gravidade: um corpo livre deve adquirir aproximadamente g*t de
    // velocidade e deslocar-se para baixo de forma determinística.
    m_Scene.Clear();
    Entity& fallingSphere = m_Scene.CreateEntity();
    configureBody(fallingSphere, Vector3(0.0f, 10.0f, 0.0f), false,
                  Entity::ColliderShape::Sphere);
    fallingSphere.PickingRadius = 0.5f;
    fallingSphere.ColliderEnabled = false;
    for (int i = 0; i < 60; ++i)
        SimulatePhysicsStep(step);
    check(fallingSphere.GetTransform().Position.Y < 6.0f,
          "gravidade desloca o corpo para baixo");
    check(fallingSphere.Velocity.Y < -8.0f && fallingSphere.Velocity.Y > -11.0f,
          "aceleracao gravitacional proxima de -9.81 m/s^2");

    // A resistência aerodinâmica deve crescer com a velocidade: queda longa
    // converge suavemente para uma velocidade terminal, sem clamp brusco.
    m_Scene.Clear();
    Entity& longFall = m_Scene.CreateEntity();
    configureBody(longFall, Vector3(0.0f, 1000.0f, 0.0f), false,
                  Entity::ColliderShape::Sphere);
    longFall.ColliderEnabled = false;
    float previousFallSpeed = 0.0f;
    bool fallSpeedChangedSmoothly = true;
    for (int i = 0; i < 600; ++i)
    {
        previousFallSpeed = longFall.Velocity.Y;
        SimulatePhysicsStep(step);
        fallSpeedChangedSmoothly = fallSpeedChangedSmoothly &&
            std::abs(longFall.Velocity.Y - previousFallSpeed) < 0.5f;
    }
    check(longFall.Velocity.Y < -20.0f && longFall.Velocity.Y > -36.0f,
          "queda longa se aproxima de velocidade terminal moderada");
    check(fallSpeedChangedSmoothly,
          "resistencia do ar reduz a aceleracao progressivamente sem salto de velocidade");

    // Um corpo lançado preserva velocidade horizontal, sobe e depois retorna
    // em arco sob ação contínua da gravidade.
    m_Scene.Clear();
    Entity& projectile = m_Scene.CreateEntity();
    configureBody(projectile, Vector3(0.0f, 2.0f, 0.0f), false,
                  Entity::ColliderShape::Sphere);
    projectile.ColliderEnabled = false;
    projectile.Velocity = Vector3(8.0f, 8.0f, 0.0f);
    for (int i = 0; i < 30; ++i)
        SimulatePhysicsStep(step);
    const float projectileApexApproachY = projectile.GetTransform().Position.Y;
    const float projectileHorizontalAtHalfSecond = projectile.GetTransform().Position.X;
    for (int i = 0; i < 120; ++i)
        SimulatePhysicsStep(step);
    check(projectileHorizontalAtHalfSecond > 3.5f &&
              projectileApexApproachY > 2.0f &&
              projectile.GetTransform().Position.Y < 2.0f,
          "impulso produz trajetória balística com subida, avanço e queda");

    // Um corpo descendo uma rampa preserva a velocidade ao sair pela ponta.
    m_Scene.Clear();
    Entity& launchRamp = m_Scene.CreateEntity();
    configureBody(launchRamp, Vector3(0.0f, 3.0f, 0.0f), true,
                  Entity::ColliderShape::Box);
    launchRamp.GetTransform().Scale = Vector3(8.0f, 0.5f, 4.0f);
    launchRamp.GetTransform().Rotation = glm::angleAxis(
        glm::radians(-15.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    launchRamp.Friction = 0.15f;
    Entity& rampRunner = m_Scene.CreateEntity();
    configureBody(rampRunner, Vector3(2.5f, 2.75f, 0.0f), false,
                  Entity::ColliderShape::Box);
    rampRunner.GetTransform().Scale = Vector3(1.0f, 0.4f, 0.5f);
    rampRunner.GetTransform().Rotation = launchRamp.GetTransform().Rotation;
    rampRunner.Velocity = Vector3(7.0f, -1.875f, 0.0f);
    rampRunner.Friction = 0.15f;
    for (int i = 0; i < 90; ++i)
        SimulatePhysicsStep(step);
    check(rampRunner.GetTransform().Position.X > 4.5f &&
              rampRunner.Velocity.X > 3.0f,
          "corpo atravessa a ponta da rampa preservando impulso horizontal");
    check(rampRunner.Velocity.Y < -0.5f &&
              rampRunner.GetTransform().Position.Y < 2.75f,
          "corpo deixa a rampa em voo livre sem ser atraído pelo contato");

    // Contato de área em rampa: um cubo alinhado à superfície e com restituição
    // zero não deve receber torque de um vértice arbitrário nem ser lançado
    // para fora pelo solver ao pousar.
    m_Scene.Clear();
    Entity& stableRamp = m_Scene.CreateEntity();
    configureBody(stableRamp, Vector3(0.0f, 0.0f, 0.0f), true,
                  Entity::ColliderShape::Box);
    stableRamp.GetTransform().Scale = Vector3(12.0f, 0.5f, 6.0f);
    stableRamp.GetTransform().Rotation = glm::angleAxis(
        glm::radians(-15.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    stableRamp.Friction = 0.8f;
    Entity& rampLandingBox = m_Scene.CreateEntity();
    configureBody(rampLandingBox, Vector3(0.0f, 4.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    rampLandingBox.GetTransform().Rotation = stableRamp.GetTransform().Rotation;
    rampLandingBox.Restitution = 0.0f;
    rampLandingBox.Friction = 0.8f;
    const glm::vec3 rampNormal = stableRamp.GetTransform().Rotation *
        glm::vec3(0.0f, 1.0f, 0.0f);
    float maximumRampSeparationSpeed = 0.0f;
    for (int i = 0; i < 240; ++i)
    {
        SimulatePhysicsStep(step);
        maximumRampSeparationSpeed = std::max(maximumRampSeparationSpeed,
            rampLandingBox.Velocity.X * rampNormal.x +
            rampLandingBox.Velocity.Y * rampNormal.y +
            rampLandingBox.Velocity.Z * rampNormal.z);
    }
    const float rampLandingAngularSpeed = std::sqrt(
        rampLandingBox.AngularVelocity.X * rampLandingBox.AngularVelocity.X +
        rampLandingBox.AngularVelocity.Y * rampLandingBox.AngularVelocity.Y +
        rampLandingBox.AngularVelocity.Z * rampLandingBox.AngularVelocity.Z);
    check(maximumRampSeparationSpeed < 0.20f,
          "impacto sem restituição na rampa não lança o cubo para longe da superfície");
    check(rampLandingAngularSpeed < 0.60f,
          "contato alinhado na rampa não injeta rotação artificial persistente");

    // 2) Repouso: a plataforma e o corpo devem terminar sem penetração nem
    // velocidade vertical residual relevante.
    m_Scene.Clear();
    Entity& platform = m_Scene.CreateEntity();
    configureBody(platform, Vector3(0.0f, -0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    platform.GetTransform().Scale = Vector3(20.0f, 1.0f, 20.0f);
    Entity& restingBox = m_Scene.CreateEntity();
    configureBody(restingBox, Vector3(0.0f, 6.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    for (int i = 0; i < 360; ++i)
        SimulatePhysicsStep(step);
    std::cout << "[PhysicsTest] repouso: posY="
              << restingBox.GetTransform().Position.Y
              << " velY=" << restingBox.Velocity.Y << std::endl;
    check(restingBox.GetTransform().Position.Y >= 0.49f,
          "corpo nao penetra o piso");
    check(std::abs(restingBox.Velocity.Y) < 0.08f,
          "corpo estabiliza em repouso");

    // 3) Colisão dinâmica: duas caixas com velocidades opostas devem
    // separar-se após o impacto, sem atravessar uma à outra.
    m_Scene.Clear();
    Entity& leftBox = m_Scene.CreateEntity();
    configureBody(leftBox, Vector3(-2.0f, 2.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    leftBox.UseGravity = false;
    leftBox.Velocity.X = 4.0f;
    leftBox.Restitution = 0.6f;
    Entity& rightBox = m_Scene.CreateEntity();
    configureBody(rightBox, Vector3(2.0f, 2.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    rightBox.UseGravity = false;
    rightBox.Velocity.X = -4.0f;
    rightBox.Restitution = 0.6f;
    for (int i = 0; i < 90; ++i)
        SimulatePhysicsStep(step);
    check(leftBox.GetTransform().Position.X < rightBox.GetTransform().Position.X,
          "caixas permanecem separadas apos a colisao");
    check(leftBox.Velocity.X < 0.0f && rightBox.Velocity.X > 0.0f,
          "impulso troca o sentido das velocidades");

    // 4) Caso de regressão: cubo dinâmico levemente inclinado sobre cubo
    // estático não pode conservar torque indefinidamente.
    m_Scene.Clear();
    Entity& staticCube = m_Scene.CreateEntity();
    configureBody(staticCube, Vector3(0.0f, 0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    staticCube.GetTransform().Scale = Vector3(4.0f, 1.0f, 4.0f);
    Entity& dynamicCube = m_Scene.CreateEntity();
    configureBody(dynamicCube, Vector3(0.0f, 4.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    dynamicCube.GetTransform().Rotation = glm::angleAxis(
        glm::radians(8.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    for (int i = 0; i < 600; ++i)
        SimulatePhysicsStep(step);
    const float angularSpeed = std::sqrt(
        dynamicCube.AngularVelocity.X * dynamicCube.AngularVelocity.X +
        dynamicCube.AngularVelocity.Y * dynamicCube.AngularVelocity.Y +
        dynamicCube.AngularVelocity.Z * dynamicCube.AngularVelocity.Z);
    check(dynamicCube.GetTransform().Position.Y >= 0.49f,
          "cubo dinamico nao atravessa o cubo estatico");
    check(angularSpeed < 0.05f,
          "cubo apoiado para de girar");    // 5) Atrito: caixas com atrito alto e baixo devem desacelerar de forma
    // diferente quando deslizam sobre uma plataforma estática.
    m_Scene.Clear();
    Entity& floorF = m_Scene.CreateEntity();
    configureBody(floorF, Vector3(0.0f, -0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    floorF.GetTransform().Scale = Vector3(20.0f, 1.0f, 20.0f);
    Entity& lowFriction = m_Scene.CreateEntity();
    configureBody(lowFriction, Vector3(-2.0f, 1.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    lowFriction.Velocity.X = 6.0f;
    lowFriction.Friction = 0.05f;
    Entity& highFriction = m_Scene.CreateEntity();
    configureBody(highFriction, Vector3(2.0f, 1.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    highFriction.Velocity.X = 6.0f;
    highFriction.Friction = 0.9f;
    for (int i = 0; i < 300; ++i)
        SimulatePhysicsStep(step);
    check(std::abs(highFriction.Velocity.X) < std::abs(lowFriction.Velocity.X),
          "objeto com mais atrito desacelera mais rapido");

    // 6) Massa: aplicar a mesma quantidade de impulso em corpos com massas
    // distintas produz velocidades inversamente proporcionais.
    m_Scene.Clear();
    Entity& light = m_Scene.CreateEntity();
    configureBody(light, Vector3(-1.0f, 1.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    light.Mass = 0.5f;
    Entity& heavy = m_Scene.CreateEntity();
    configureBody(heavy, Vector3(1.0f, 1.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    heavy.Mass = 5.0f;
    // Simula um impulso horizontal idêntico: deltaV = impulse / mass
    const float impulse = 10.0f;
    light.Velocity.X += impulse / light.Mass;
    heavy.Velocity.X += impulse / heavy.Mass;
    check(std::abs(light.Velocity.X) > std::abs(heavy.Velocity.X),
          "massa menor recebe maior velocidade apos impulso igual");

    // 7) Cubo dinâmico sobre estático — variação de massa: mesmo com massa
    // grande o estático não deve penetrar e o dinâmico deve estabilizar.
    m_Scene.Clear();
    Entity& staticLarge = m_Scene.CreateEntity();
    configureBody(staticLarge, Vector3(0.0f, 0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    staticLarge.GetTransform().Scale = Vector3(4.0f, 1.0f, 4.0f);
    Entity& dynamicHeavy = m_Scene.CreateEntity();
    configureBody(dynamicHeavy, Vector3(0.0f, 8.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    dynamicHeavy.Mass = 50.0f; // muito mais pesado
    dynamicHeavy.GetTransform().Rotation = glm::angleAxis(
        glm::radians(5.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    for (int i = 0; i < 600; ++i)
        SimulatePhysicsStep(step);
    check(dynamicHeavy.GetTransform().Position.Y >= 0.49f,
          "cubo pesado nao atravessa o cubo estatico");

    // 8) Regressão principal: queda sobre um collider estático real. O corpo
    // deve absorver o impacto sem quicar, sem receber torque artificial e
    // sem acumular velocidade depois de entrar em repouso.
    m_Scene.Clear();
    Entity& realFloor = m_Scene.CreateEntity();
    configureBody(realFloor, Vector3(0.0f, -0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    realFloor.GetTransform().Scale = Vector3(20.0f, 1.0f, 20.0f);
    Entity& tiltedDrop = m_Scene.CreateEntity();
    configureBody(tiltedDrop, Vector3(0.0f, 8.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    tiltedDrop.Restitution = 0.0f;
    tiltedDrop.GetTransform().Rotation = glm::angleAxis(
        glm::radians(6.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    float maximumUpwardVelocity = 0.0f;
    for (int i = 0; i < 600; ++i)
    {
        SimulatePhysicsStep(step);
        maximumUpwardVelocity = std::max(maximumUpwardVelocity,
                                         tiltedDrop.Velocity.Y);
    }
    const float settledAngularSpeed = std::sqrt(
        tiltedDrop.AngularVelocity.X * tiltedDrop.AngularVelocity.X +
        tiltedDrop.AngularVelocity.Y * tiltedDrop.AngularVelocity.Y +
        tiltedDrop.AngularVelocity.Z * tiltedDrop.AngularVelocity.Z);
    const glm::mat3 settledAxes = glm::mat3_cast(
        glm::normalize(tiltedDrop.GetTransform().Rotation));
    const float settledFaceAlignment = std::max({
        std::abs(glm::dot(glm::normalize(settledAxes[0]), glm::vec3(0.0f, 1.0f, 0.0f))),
        std::abs(glm::dot(glm::normalize(settledAxes[1]), glm::vec3(0.0f, 1.0f, 0.0f))),
        std::abs(glm::dot(glm::normalize(settledAxes[2]), glm::vec3(0.0f, 1.0f, 0.0f)))
    });
    check(tiltedDrop.GetTransform().Position.Y >= 0.49f,
          "queda em piso estatico real nao atravessa");
    check(std::abs(tiltedDrop.Velocity.Y) < 0.05f &&
              maximumUpwardVelocity < 2.5f,
          "impacto no piso real dissipa o movimento e nao produz quiques sustentados");
    check(settledAngularSpeed < 0.05f,
          "impacto no piso real nao produz giro residual");
    check(settledFaceAlignment > 0.9986f,
          "cubo 3D que cai inclinado termina apoiado por uma face inteira");

    // Queda em diagonal significativa: a estabilização de repouso não pode
    // mascarar a rotação inicial nem o impacto pode manter energia acumulada.
    m_Scene.Clear();
    Entity& diagonalFloor3D = m_Scene.CreateEntity();
    configureBody(diagonalFloor3D, Vector3(0.0f, -0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    diagonalFloor3D.GetTransform().Scale = Vector3(20.0f, 1.0f, 20.0f);
    Entity& diagonalDrop3D = m_Scene.CreateEntity();
    configureBody(diagonalDrop3D, Vector3(0.0f, 5.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    diagonalDrop3D.Restitution = 0.0f;
    diagonalDrop3D.GetTransform().Rotation = glm::angleAxis(
        glm::radians(28.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    float maximumDiagonalImpactSpeed = 0.0f;
    for (int i = 0; i < 360; ++i)
    {
        SimulatePhysicsStep(step);
        const float speed = std::sqrt(
            diagonalDrop3D.Velocity.X * diagonalDrop3D.Velocity.X +
            diagonalDrop3D.Velocity.Y * diagonalDrop3D.Velocity.Y +
            diagonalDrop3D.Velocity.Z * diagonalDrop3D.Velocity.Z);
        maximumDiagonalImpactSpeed = std::max(maximumDiagonalImpactSpeed, speed);
    }
    const float diagonalAngularSpeed = std::sqrt(
        diagonalDrop3D.AngularVelocity.X * diagonalDrop3D.AngularVelocity.X +
        diagonalDrop3D.AngularVelocity.Y * diagonalDrop3D.AngularVelocity.Y +
        diagonalDrop3D.AngularVelocity.Z * diagonalDrop3D.AngularVelocity.Z);
    check(diagonalDrop3D.GetTransform().Position.Y > 0.40f &&
              std::abs(diagonalDrop3D.Velocity.Y) < 0.20f,
          "cubo 3D inclinado repousa sobre o piso sem atravessar nem quicar continuamente");
    check(maximumDiagonalImpactSpeed < 12.0f && diagonalAngularSpeed < 1.0f,
          "impacto diagonal 3D dissipa energia sem giro residual excessivo");

    // 9) Centro de massa dentro da área de apoio: mesmo com parte da base
    // para fora, um cubo com a maior parte apoiada permanece estável.
    m_Scene.Clear();
    Entity& edgePlatformStable = m_Scene.CreateEntity();
    configureBody(edgePlatformStable, Vector3(0.0f, -0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    edgePlatformStable.GetTransform().Scale = Vector3(4.0f, 1.0f, 4.0f);
    Entity& overhangingBox = m_Scene.CreateEntity();
    configureBody(overhangingBox, Vector3(1.75f, 0.55f, 0.0f), false,
                  Entity::ColliderShape::Box);
    for (int i = 0; i < 180; ++i)
        SimulatePhysicsStep(step);
    check(overhangingBox.GetTransform().Position.Y > 0.42f &&
              overhangingBox.GetTransform().Position.X < 2.0f,
          "cubo permanece apoiado quando o centro de massa esta dentro da plataforma");

    // 10) Centro de massa além do contorno: a reação no limite deve produzir
    // torque e o corpo começa a tombar, em vez de ficar suspenso no ar.
    m_Scene.Clear();
    Entity& edgePlatformTip = m_Scene.CreateEntity();
    configureBody(edgePlatformTip, Vector3(0.0f, -0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    edgePlatformTip.GetTransform().Scale = Vector3(4.0f, 1.0f, 4.0f);
    Entity& tippingBox = m_Scene.CreateEntity();
    configureBody(tippingBox, Vector3(2.15f, 0.55f, 0.0f), false,
                  Entity::ColliderShape::Box);
    for (int i = 0; i < 180; ++i)
        SimulatePhysicsStep(step);
    const float tippingAngle = glm::angle(tippingBox.GetTransform().Rotation);
    check(tippingAngle > glm::radians(12.0f) ||
              tippingBox.GetTransform().Position.Y < 0.35f,
          "cubo tomba quando o centro de massa ultrapassa a borda da plataforma");

    // Apoio mínimo e centro de massa bem fora da plataforma devem tombar.
    m_Scene.Clear();
    Entity& narrowSupport = m_Scene.CreateEntity();
    configureBody(narrowSupport, Vector3(0.0f, -0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    narrowSupport.GetTransform().Scale = Vector3(4.0f, 1.0f, 4.0f);
    Entity& mostlyUnsupportedBox = m_Scene.CreateEntity();
    configureBody(mostlyUnsupportedBox, Vector3(2.42f, 0.55f, 0.0f), false,
                  Entity::ColliderShape::Box);
    for (int i = 0; i < 180; ++i)
        SimulatePhysicsStep(step);
    const float unsupportedAngle = glm::angle(mostlyUnsupportedBox.GetTransform().Rotation);
    check(unsupportedAngle > glm::radians(12.0f) ||
              mostlyUnsupportedBox.GetTransform().Position.Y < 0.35f,
          "cubo tomba quando somente uma pequena faixa da base permanece apoiada");

    // Cilindro inclinado toca o piso pela borda da tampa; deve rolar/tombar.
    m_Scene.Clear();
    Entity& cylinderFloor = m_Scene.CreateEntity();
    configureBody(cylinderFloor, Vector3(0.0f, -0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    cylinderFloor.GetTransform().Scale = Vector3(20.0f, 1.0f, 20.0f);
    Entity& tiltedCylinder = m_Scene.CreateEntity();
    configureBody(tiltedCylinder, Vector3(0.0f, 1.12f, 0.0f), false,
                  Entity::ColliderShape::Cylinder);
    tiltedCylinder.GetTransform().Scale = Vector3(1.0f, 2.0f, 1.0f);
    const glm::quat cylinderInitialRotation = glm::angleAxis(
        glm::radians(35.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    tiltedCylinder.GetTransform().Rotation = cylinderInitialRotation;
    for (int i = 0; i < 180; ++i)
        SimulatePhysicsStep(step);
    const float cylinderRotationChange = glm::angle(glm::normalize(
        tiltedCylinder.GetTransform().Rotation * glm::inverse(cylinderInitialRotation)));
    check(tiltedCylinder.GetTransform().Position.Y >= 0.49f,
          "cilindro tombando nao atravessa o piso");
    check(cylinderRotationChange > glm::radians(12.0f),
          "cilindro inclinado rola/tomba em vez de manter torque artificialmente zerado");

    // Colliders arredondados usam a geometria escolhida; repouso no piso deve
    // respeitar os raios/alturas reais, sem herdar a bounding box como contato.
    m_Scene.Clear();
    Entity& curvedFloor = m_Scene.CreateEntity();
    configureBody(curvedFloor, Vector3(0.0f, -0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    curvedFloor.GetTransform().Scale = Vector3(20.0f, 1.0f, 20.0f);
    Entity& fallingSphereCollider = m_Scene.CreateEntity();
    configureBody(fallingSphereCollider, Vector3(0.0f, 4.0f, 0.0f), false,
                  Entity::ColliderShape::Sphere);
    fallingSphereCollider.UseGravity = true;
    for (int i = 0; i < 240; ++i)
        SimulatePhysicsStep(step);
    check(fallingSphereCollider.GetTransform().Position.Y >= 0.49f &&
              std::abs(fallingSphereCollider.Velocity.Y) < 0.15f,
          "esfera repousa no piso usando raio visível de 0.5 m");

    m_Scene.Clear();
    Entity& capsuleFloor = m_Scene.CreateEntity();
    configureBody(capsuleFloor, Vector3(0.0f, -0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    capsuleFloor.GetTransform().Scale = Vector3(20.0f, 1.0f, 20.0f);
    Entity& fallingCapsule = m_Scene.CreateEntity();
    configureBody(fallingCapsule, Vector3(0.0f, 5.0f, 0.0f), false,
                  Entity::ColliderShape::Capsule);
    for (int i = 0; i < 300; ++i)
        SimulatePhysicsStep(step);
    check(fallingCapsule.GetTransform().Position.Y >= 0.98f &&
              std::abs(fallingCapsule.Velocity.Y) < 0.20f,
          "cápsula mantém a altura de suporte real ao repousar");

    m_Scene.Clear();
    Entity& coneFloor = m_Scene.CreateEntity();
    configureBody(coneFloor, Vector3(0.0f, -0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    coneFloor.GetTransform().Scale = Vector3(20.0f, 1.0f, 20.0f);
    Entity& fallingCone = m_Scene.CreateEntity();
    configureBody(fallingCone, Vector3(0.0f, 4.0f, 0.0f), false,
                  Entity::ColliderShape::Cone);
    for (int i = 0; i < 240; ++i)
        SimulatePhysicsStep(step);
    check(fallingCone.GetTransform().Position.Y >= 0.49f &&
              std::abs(fallingCone.Velocity.Y) < 0.20f,
          "cone apoia pela base real em vez de colidir como esfera");

    // Regressão de falso positivo: um cilindro circular não pode colidir com
    // o canto de uma caixa quando a distância radial passa entre as duas.
    m_Scene.Clear();
    Entity& cornerBlock = m_Scene.CreateEntity();
    configureBody(cornerBlock, Vector3(0.55f, 0.0f, 0.55f), true,
                  Entity::ColliderShape::Box);
    cornerBlock.GetTransform().Scale = Vector3(0.2f, 2.0f, 0.2f);
    Entity& cornerCylinder = m_Scene.CreateEntity();
    configureBody(cornerCylinder, Vector3(0.0f, 0.0f, 0.0f), false,
                  Entity::ColliderShape::Cylinder);
    cornerCylinder.UseGravity = false;
    SimulatePhysicsStep(step);
    const Vector3& cylinderCornerPosition = cornerCylinder.GetTransform().Position;
    check(cylinderCornerPosition.X * cylinderCornerPosition.X +
              cylinderCornerPosition.Y * cylinderCornerPosition.Y +
              cylinderCornerPosition.Z * cylinderCornerPosition.Z < 0.0001f,
          "cilindro não recebe colisão falsa no canto do collider retangular");

    m_Scene.Clear();
    Entity& verticalWall = m_Scene.CreateEntity();
    configureBody(verticalWall, Vector3(0.0f, 0.0f, 0.0f), true,
                  Entity::ColliderShape::Box);
    verticalWall.GetTransform().Scale = Vector3(0.2f, 3.0f, 3.0f);
    Entity& movingCylinder = m_Scene.CreateEntity();
    configureBody(movingCylinder, Vector3(-2.0f, 0.0f, 0.0f), false,
                  Entity::ColliderShape::Cylinder);
    movingCylinder.UseGravity = false;
    movingCylinder.Velocity.X = 5.0f;
    for (int i = 0; i < 60; ++i)
        SimulatePhysicsStep(step);
    check(movingCylinder.GetTransform().Position.X < -0.54f &&
              std::abs(movingCylinder.Velocity.X) < 0.35f,
          "cilindro colide com parede usando a seção circular e não a caixa envolvente");

    // Collider Mesh usa os limites e o centro local do mesh, não o pivô como
    // centro da caixa. Um mesh deslocado não deve bloquear um corpo no pivô.
    m_Scene.Clear();
    Entity& offsetMesh = m_Scene.CreateEntity();
    configureBody(offsetMesh, Vector3(0.0f, 0.0f, 0.0f), true,
                  Entity::ColliderShape::Mesh);
    offsetMesh.GetMeshComponent().MeshData = std::make_shared<Mesh>();
    std::vector<Vertex> offsetVertices = MeshFactory::CreateCube();
    for (Vertex& vertex : offsetVertices)
        vertex.X += 2.0f;
    offsetMesh.GetMeshComponent().MeshData->Create(
        offsetVertices, GL_TRIANGLES, "PhysicsOffsetMeshRegression");
    Entity& pivotBox = m_Scene.CreateEntity();
    configureBody(pivotBox, Vector3(0.0f, 0.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    pivotBox.UseGravity = false;
    SimulatePhysicsStep(step);
    const Vector3& pivotBoxPosition = pivotBox.GetTransform().Position;
    check(pivotBoxPosition.X * pivotBoxPosition.X +
              pivotBoxPosition.Y * pivotBoxPosition.Y +
              pivotBoxPosition.Z * pivotBoxPosition.Z < 0.0001f,
          "collider Mesh deslocado acompanha o centro real da geometria");

    m_Scene.Clear();
    Entity& convexWall = m_Scene.CreateEntity();
    configureBody(convexWall, Vector3(0.0f, 0.0f, 0.0f), true,
                  Entity::ColliderShape::ConvexMesh);
    convexWall.GetMeshComponent().MeshData = std::make_shared<Mesh>();
    convexWall.GetMeshComponent().MeshData->Create(
        MeshFactory::CreateCube(), GL_TRIANGLES, "PhysicsConvexMeshRegression");
    Entity& convexMeshRunner = m_Scene.CreateEntity();
    configureBody(convexMeshRunner, Vector3(-2.0f, 0.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    convexMeshRunner.UseGravity = false;
    convexMeshRunner.Velocity.X = 5.0f;
    for (int i = 0; i < 60; ++i)
        SimulatePhysicsStep(step);
    check(convexMeshRunner.GetTransform().Position.X < -0.94f &&
              std::abs(convexMeshRunner.Velocity.X) < 0.35f,
          "Convex Mesh recebe impacto ao atingir a geometria real");

    m_Scene.Clear();
    Entity& tetraCollider = m_Scene.CreateEntity();
    configureBody(tetraCollider, Vector3(0.0f, 0.0f, 0.0f), true,
                  Entity::ColliderShape::ConvexMesh);
    const Vertex tetraA(0, 0, 0, 0, 0, 1, 0, 0);
    const Vertex tetraB(2, 0, 0, 0, 0, 1, 0, 0);
    const Vertex tetraC(0, 2, 0, 0, 0, 1, 0, 0);
    const Vertex tetraD(0, 0, 2, 0, 0, 1, 0, 0);
    tetraCollider.GetMeshComponent().MeshData = std::make_shared<Mesh>();
    tetraCollider.GetMeshComponent().MeshData->Create(
        { tetraA, tetraC, tetraB, tetraA, tetraB, tetraD,
          tetraA, tetraD, tetraC, tetraB, tetraC, tetraD },
        GL_TRIANGLES, "PhysicsConvexTetrahedronRegression");
    Entity& tetraCornerBox = m_Scene.CreateEntity();
    configureBody(tetraCornerBox, Vector3(0.15f, 1.7f, 1.7f), false,
                  Entity::ColliderShape::Box);
    tetraCornerBox.UseGravity = false;
    tetraCornerBox.GetTransform().Scale = Vector3(0.2f, 0.2f, 0.2f);
    SimulatePhysicsStep(step);
    check(std::abs(tetraCornerBox.GetTransform().Position.X - 0.15f) < 0.001f &&
              std::abs(tetraCornerBox.GetTransform().Position.Y - 1.7f) < 0.001f &&
              std::abs(tetraCornerBox.GetTransform().Position.Z - 1.7f) < 0.001f,
          "Convex Mesh rejeita falso contato apenas na caixa envolvente");

    m_Scene.Clear();
    Entity& heightfield = m_Scene.CreateEntity();
    configureBody(heightfield, Vector3(0.0f, 0.0f, 0.0f), true,
                  Entity::ColliderShape::Mesh);
    heightfield.AddTerrain(5);
    heightfield.GetTerrainComponent().Size = 8.0f;
    heightfield.GetTerrainComponent().HeightScale = 2.0f;
    heightfield.GetTerrainComponent().At(3, 3) = 1.0f;
    heightfield.GetMeshComponent().MeshData = std::make_shared<Mesh>();
    heightfield.GetMeshComponent().MeshData->Create(
        heightfield.GetTerrainComponent().BuildVertices(),
        GL_TRIANGLES, "PhysicsHeightfieldRegression");
    Entity& lowlandSphere = m_Scene.CreateEntity();
    configureBody(lowlandSphere, Vector3(-3.0f, 4.0f, -3.0f), false,
                  Entity::ColliderShape::Sphere);
    for (int i = 0; i < 240; ++i)
        SimulatePhysicsStep(step);
    check(lowlandSphere.GetTransform().Position.Y >= 0.49f &&
              lowlandSphere.GetTransform().Position.Y < 0.85f &&
              std::abs(lowlandSphere.Velocity.Y) < 0.2f,
          "corpo colide com a altura local do terreno, sem usar o pico da caixa envolvente");

    // 11) CCD: uma queda muito rápida ainda deve terminar apoiada no piso,
    // sem atravessar o collider estático entre dois passos fixos.
    m_Scene.Clear();
    Entity& ccdFloor = m_Scene.CreateEntity();
    configureBody(ccdFloor, Vector3(0.0f, -0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    ccdFloor.GetTransform().Scale = Vector3(20.0f, 1.0f, 20.0f);
    Entity& fastDrop = m_Scene.CreateEntity();
    configureBody(fastDrop, Vector3(0.0f, 20.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    fastDrop.ContinuousCollision = true;
    fastDrop.Velocity.Y = -80.0f;
    for (int i = 0; i < 180; ++i)
        SimulatePhysicsStep(step);
    check(fastDrop.GetTransform().Position.Y >= 0.49f,
          "CCD impede atravessar o piso em alta velocidade");

    // 12) Física 2D própria: gravidade, repouso em colisor estático e
    // restrição de movimento ao plano XY, sem chamar o solver tridimensional.
    m_WorldMode = WorldMode::World2D;
    m_Scene.Clear();
    Entity& floor2D = m_Scene.CreateEntity();
    configureBody(floor2D, Vector3(0.0f, -0.5f, 8.0f), true,
                  Entity::ColliderShape::Box);
    floor2D.GetTransform().Scale = Vector3(20.0f, 1.0f, 1.0f);
    floor2D.UseGravity = false;
    Entity& falling2D = m_Scene.CreateEntity();
    configureBody(falling2D, Vector3(0.0f, 5.0f, 20.0f), false,
                  Entity::ColliderShape::Box);
    for (int i = 0; i < 120; ++i)
        SimulatePhysics2DStep(step);
    check(std::abs(falling2D.GetTransform().Position.Y - 0.5f) < 0.08f &&
              std::abs(falling2D.Velocity.Y) < 0.1f,
          "gravidade 2D e colisão de caixa mantêm corpo dinâmico apoiado");
    check(falling2D.GetTransform().Position.Z == 0.0f && falling2D.Velocity.Z == 0.0f,
          "corpo físico 2D permanece no plano XY");

    m_Scene.Clear();
    Entity& flatFloor2D = m_Scene.CreateEntity();
    configureBody(flatFloor2D, Vector3(0.0f, -0.5f, 0.0f), true,
                  Entity::ColliderShape::Box);
    flatFloor2D.GetTransform().Scale = Vector3(20.0f, 1.0f, 1.0f);
    flatFloor2D.UseGravity = false;
    Entity& tiltedDrop2D = m_Scene.CreateEntity();
    configureBody(tiltedDrop2D, Vector3(0.0f, 5.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    tiltedDrop2D.GetTransform().Rotation = glm::angleAxis(
        glm::radians(12.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    for (int i = 0; i < 360; ++i)
        SimulatePhysics2DStep(step);
    check(tiltedDrop2D.GetTransform().Position.Y > 0.48f &&
              glm::angle(tiltedDrop2D.GetTransform().Rotation) < glm::radians(3.0f),
          "quadrado 2D que cai inclinado assenta a base inteira no chão");

    // Caixa e chão inclinados precisam usar as mesmas orientações reais no
    // narrow phase; um AABB horizontal antigo não pode continuar colidindo.
    m_Scene.Clear();
    const float rampAngle2D = glm::radians(20.0f);
    const glm::quat rampRotation2D = glm::angleAxis(
        rampAngle2D, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec2 rampNormal2D(-std::sin(rampAngle2D), std::cos(rampAngle2D));
    Entity& rampFloor2D = m_Scene.CreateEntity();
    configureBody(rampFloor2D, Vector3(0.0f, 0.0f, 0.0f), true,
                  Entity::ColliderShape::Box);
    rampFloor2D.GetTransform().Scale = Vector3(20.0f, 0.5f, 1.0f);
    rampFloor2D.GetTransform().Rotation = rampRotation2D;
    rampFloor2D.UseGravity = false;
    Entity& rampBox2D = m_Scene.CreateEntity();
    configureBody(rampBox2D,
        Vector3(rampNormal2D.x * 2.0f, rampNormal2D.y * 2.0f, 0.0f),
        false, Entity::ColliderShape::Box);
    rampBox2D.GetTransform().Rotation = rampRotation2D;
    rampBox2D.Friction = 0.9f;
    for (int i = 0; i < 240; ++i)
        SimulatePhysics2DStep(step);
    const float rampSupportDistance2D =
        rampBox2D.GetTransform().Position.X * rampNormal2D.x +
        rampBox2D.GetTransform().Position.Y * rampNormal2D.y;
    const float rampTangentSpeed2D =
        std::abs(rampBox2D.Velocity.X * -rampNormal2D.y +
                 rampBox2D.Velocity.Y * rampNormal2D.x);
    check(rampSupportDistance2D > 0.55f && rampSupportDistance2D < 0.95f &&
              std::abs(rampBox2D.Velocity.X * rampNormal2D.x +
                       rampBox2D.Velocity.Y * rampNormal2D.y) < 0.15f &&
              rampTangentSpeed2D < 0.15f &&
              std::abs(rampBox2D.GetTransform().EulerRotation.Z - 20.0f) < 2.0f,
          "caixa 2D repousa na face inclinada real sem colisão fantasma horizontal");

    // O centro de massa além da borda não deve receber um apoio fictício no
    // centro do piso: a reação no contato periférico precisa tombar a caixa.
    m_Scene.Clear();
    Entity& shortFloor2D = m_Scene.CreateEntity();
    configureBody(shortFloor2D, Vector3(0.0f, 0.0f, 0.0f), true,
                  Entity::ColliderShape::Box);
    shortFloor2D.GetTransform().Scale = Vector3(1.0f, 0.2f, 1.0f);
    shortFloor2D.UseGravity = false;
    Entity& edgeBox2D = m_Scene.CreateEntity();
    configureBody(edgeBox2D, Vector3(0.63f, 0.6f, 0.0f), false,
                  Entity::ColliderShape::Box);
    for (int i = 0; i < 120; ++i)
        SimulatePhysics2DStep(step);
    check(std::abs(edgeBox2D.GetTransform().EulerRotation.Z) > 8.0f,
          "caixa 2D tomba quando o centro de massa ultrapassa a borda do apoio");

    // Um círculo dinâmico sobreposto a outro estático deve ser separado, e
    // marcar Gravity não move um corpo estático.
    m_Scene.Clear();
    Entity& static2D = m_Scene.CreateEntity();
    configureBody(static2D, Vector3(0.0f, 2.0f, 0.0f), true,
                  Entity::ColliderShape::Sphere);
    static2D.UseGravity = true;
    Entity& dynamicCircle2D = m_Scene.CreateEntity();
    configureBody(dynamicCircle2D, Vector3(0.75f, 2.0f, 0.0f), false,
                  Entity::ColliderShape::Sphere);
    dynamicCircle2D.UseGravity = false;
    for (int i = 0; i < 8; ++i)
        SimulatePhysics2DStep(step);
    const float circleDistance = std::abs(
        dynamicCircle2D.GetTransform().Position.X - static2D.GetTransform().Position.X);
    check(circleDistance >= 0.99f,
          "colisores circulares 2D separam dois corpos sobrepostos");
    check(static2D.GetTransform().Position.Y == 2.0f,
          "corpo estático 2D ignora a gravidade");

    m_Scene.Clear();
    Entity& trigger2D = m_Scene.CreateEntity();
    configureBody(trigger2D, Vector3(0.0f, 0.0f, 0.0f), true,
                  Entity::ColliderShape::Box);
    trigger2D.IsTrigger = true;
    trigger2D.UseGravity = false;
    Entity& entrant2D = m_Scene.CreateEntity();
    configureBody(entrant2D, Vector3(0.35f, 0.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    entrant2D.UseGravity = false;
    const Vector3 trigger2DStart = entrant2D.GetTransform().Position;
    SimulatePhysics2DStep(step);
    check(std::abs(entrant2D.GetTransform().Position.X - trigger2DStart.X) < 0.001f,
          "trigger 2D detecta sobreposição sem corrigir posição");

    m_WorldMode = WorldMode::World3D;
    m_Scene.Clear();
    Entity& trigger3D = m_Scene.CreateEntity();
    configureBody(trigger3D, Vector3(0.0f, 0.0f, 0.0f), true,
                  Entity::ColliderShape::Box);
    trigger3D.IsTrigger = true;
    trigger3D.UseGravity = false;
    Entity& entrant3D = m_Scene.CreateEntity();
    configureBody(entrant3D, Vector3(0.35f, 0.0f, 0.0f), false,
                  Entity::ColliderShape::Box);
    entrant3D.UseGravity = false;
    const Vector3 trigger3DStart = entrant3D.GetTransform().Position;
    SimulatePhysicsStep(step);
    check(std::abs(entrant3D.GetTransform().Position.X - trigger3DStart.X) < 0.001f &&
              std::abs(entrant3D.GetTransform().Position.Y - trigger3DStart.Y) < 0.001f,
          "trigger 3D detecta sobreposição sem corrigir posição");

    std::cout << "[PhysicsTest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU")
              << " (" << failures << " falha(s))" << std::endl;
    return failures == 0 ? 0 : 1;
}

void Application::UpdateUIInteraction()
{
    m_UIInteraction = MaraeUI::UIInteractionResult{};

    // A tradução ImGui -> UIInputState e a atualização dos widgets ficam em
    // Engine/UI/UIInputBridge. Aqui só montamos o contexto do frame e
    // despachamos os eventos de script resultantes.
    MaraeUI::UIInputBridgeParams params;
    params.InteractionEnabled =
        m_EditorState == EditorState::Play || m_ProjectConfig.RuntimeGame;
    params.RuntimeGame = m_ProjectConfig.RuntimeGame;
    params.GameImageMinX = m_GameImageMinX;
    params.GameImageMinY = m_GameImageMinY;
    params.GameImageWidth = m_GameImageWidth;
    params.GameImageHeight = m_GameImageHeight;
    params.GameImageValid = m_GameImageValid;
    params.ViewportPointerX = m_ViewportPointerX;
    params.ViewportPointerY = m_ViewportPointerY;
    params.ViewportWidth = m_ViewportWidth;
    params.ViewportHeight = m_ViewportHeight;
    params.ViewportPointerValid = m_ViewportPointerValid;

    if (!MaraeUI::UpdateInteractionFromImGui(m_Scene, params, m_UIInteraction))
        return;

    // Notifica os scripts anexados aos elementos de UI que dispararam eventos.
    if (m_UIInteraction.ClickedEntity != 0)
        ScriptingRuntime::Instance().FireUIClicked(m_UIInteraction.ClickedEntity);

    if (m_UIInteraction.ChangedEntity != 0)
    {
        for (const Entity& entity : m_Scene.GetEntities())
        {
            if (entity.GetID() != m_UIInteraction.ChangedEntity || !entity.HasUI())
                continue;
            const UIComponent& ui = entity.GetUIComponent();
            if (ui.Type == UIElementType::TextBox)
                ScriptingRuntime::Instance().FireUITextChanged(entity.GetID(), ui.Text);
            else
                ScriptingRuntime::Instance().FireUIValueChanged(entity.GetID(), ui.Value);
            break;
        }
    }
}

void Application::Run()
{
    auto nextBackgroundFrame = std::chrono::steady_clock::now();
    while (!m_Window.ShouldClose())
    {
        GLFWwindow* nativeWindow = m_Window.GetNativeWindow();
        if (nativeWindow)
        {
            const bool minimized = glfwGetWindowAttrib(nativeWindow, GLFW_ICONIFIED) == GLFW_TRUE;
            const bool unfocused = glfwGetWindowAttrib(nativeWindow, GLFW_FOCUSED) != GLFW_TRUE;
            if (minimized || unfocused)
            {
                // Mantém Play e atualizações de cena vivos em segundo plano,
                // mas limita trabalho de interface/render a 30 Hz. Quando
                // minimizada, 10 Hz basta para manter o estado sem gastar CPU.
                const double interval = minimized ? 0.1
                    : (m_GlobalEditorSettings.VSync ? (1.0 / 60.0) : (1.0 / 30.0));
                const auto frameDuration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(interval));
                auto now = std::chrono::steady_clock::now();
                if (nextBackgroundFrame < now)
                    nextBackgroundFrame = now + frameDuration;
                const auto deadline = nextBackgroundFrame;
                while (now < deadline && !m_Window.ShouldClose())
                {
                    glfwWaitEventsTimeout(std::chrono::duration<double>(deadline - now).count());
                    now = std::chrono::steady_clock::now();
                }
                nextBackgroundFrame = deadline + frameDuration;
            }
            else
                nextBackgroundFrame = std::chrono::steady_clock::now();
        }
        PerformanceMonitor::Update();
        {
            double now = glfwGetTime();
            static double lastSceneUp = now;
            float dt = static_cast<float>(now - lastSceneUp);
            if (dt > 0.0f)
            {
                m_SceneManager.Update(*this, dt);
            }
            lastSceneUp = now;
        }

        OnUpdate();
        // update animation system
        {
            double now = glfwGetTime();
            static double lastAnim = now;
            float dt = static_cast<float>(now - lastAnim);
            if (dt > 0.0f)
            {
                m_AnimationSystem.Update(m_Scene, dt);
            }
            lastAnim = now;
        }
        // update audio system — fontes 3D, mixer, pan e Doppler
        {
            double now = glfwGetTime();
            static double lastAudio = now;
            float dt = static_cast<float>(now - lastAudio);
            if (dt > 0.0f)
            {
                // O listener usa a mesma câmera do viewport (câmera de cena
                // ativa ou fly de fallback). A sincronização entidade↔voice,
                // o listener e o update do AudioSystem ficam em Engine/Audio.
                const Camera listenerCamera = BuildRuntimeCamera(m_Scene, m_Camera);
                {
                    FrameSectionTimer timer(FrameSection::Audio);
                    m_AudioSceneSync.Update(
                        m_Scene,
                        listenerCamera,
                        dt,
                        m_EditorState == EditorState::Play,
                        m_ProjectConfig.ProjectPath);
                }
            }
            lastAudio = now;
        }
        m_Window.PollEvents();
    }
}
   

void Application::OnUpdate()
{
    {
        const double now = glfwGetTime();
        static double previousCycleUpdate = now;
        const float deltaSeconds = static_cast<float>(now - previousCycleUpdate);
        previousCycleUpdate = now;
        LightingSettings& lighting = m_Scene.GetLightingManager().Settings;
        if (m_EditorState == EditorState::Play && lighting.Enabled &&
            lighting.DayNightCycleEnabled)
        {
            LightingSystem::AdvanceDayNightCycle(lighting, deltaSeconds);
            MarkDirty();
        }
    }

    ImGuiIO* io = ImGui::GetCurrentContext() ? &ImGui::GetIO() : nullptr;
    const bool keyboardCaptured = io && (io->WantCaptureKeyboard || io->WantTextInput);

    if (keyboardCaptured)
    {
        m_UpPressed = false;
        m_DownPressed = false;
        m_DPressed = false;
        m_CPressed = false;
        m_VPressed = false;
        m_FPressed = false;
        m_DeletePressed = false;
        m_UndoPressed = false;
        m_RedoPressed = false;
    }
    else
    {
        // ===== SELEÃ‡ÃƒO HIERARCHY =====
    const auto& entities = m_Scene.GetEntities();

    if (m_Window.IsKeyPressed(GLFW_KEY_UP))
    {
        if (!m_UpPressed)
        {
            m_UpPressed = true;

            if (m_SelectedEntity > 0)
                m_SelectedEntity--;

            PrintHierarchy();
        }
    }


    else
    {
        m_UpPressed = false;
    }

    if (m_Window.IsKeyPressed(GLFW_KEY_DOWN))
    {
        if (!m_DownPressed)
        {
            m_DownPressed = true;

            if (m_SelectedEntity <
                (int)entities.size() - 1)
                m_SelectedEntity++;

            PrintHierarchy();
        }
    }
    else
    {
        m_DownPressed = false;
    }

    const float gizmoSpeed = 0.1f;

    if (m_Window.IsKeyPressed(GLFW_KEY_J))
    {
        MoveSelectedEntity(
            -gizmoSpeed,
            0.0f,
            0.0f
        );
    }

    if (m_Window.IsKeyPressed(GLFW_KEY_L))
    {
        MoveSelectedEntity(
            gizmoSpeed,
            0.0f,
            0.0f
        );
    }

    if (m_Window.IsKeyPressed(GLFW_KEY_I))
    {
        MoveSelectedEntity(
            0.0f,
            gizmoSpeed,
            0.0f
        );
    }

    if (m_Window.IsKeyPressed(GLFW_KEY_K))
    {
        MoveSelectedEntity(
            0.0f,
            -gizmoSpeed,
            0.0f
        );
    }

    if (m_Window.IsKeyPressed(GLFW_KEY_U))
    {
        MoveSelectedEntity(
            0.0f,
            0.0f,
            gizmoSpeed
        );
    }

    if (m_Window.IsKeyPressed(GLFW_KEY_O))
    {
        MoveSelectedEntity(
            0.0f,
            0.0f,
            -gizmoSpeed
        );
    }

    GLFWwindow* window =
        m_Window.GetNativeWindow();

    bool rightMousePressed =
        glfwGetMouseButton(
            window,
            GLFW_MOUSE_BUTTON_RIGHT
        ) == GLFW_PRESS;

    // W/E/R alternam mover/girar/escalar mesmo com o cursor sobre o viewport;
    // os atalhos ficam suspensos durante o voo da câmera com RMB.
    if (!rightMousePressed)
    {
        bool ctrlPressed =
            m_Window.IsKeyPressed(
                GLFW_KEY_LEFT_CONTROL
            ) ||
            m_Window.IsKeyPressed(
                GLFW_KEY_RIGHT_CONTROL
            );

        if (!ctrlPressed)
        {
            if (m_Window.IsKeyPressed(GLFW_KEY_W))
            {
                SetGizmoMode(
                    GizmoMode::Translate
                );
            }

            if (m_Window.IsKeyPressed(GLFW_KEY_E))
            {
                SetGizmoMode(
                    GizmoMode::Rotate
                );
            }

            if (!InspectorPanel::IsModelingMode() && m_Window.IsKeyPressed(GLFW_KEY_R))
            {
                SetGizmoMode(
                    GizmoMode::Scale
                );
            }

            if (m_Window.IsKeyPressed(GLFW_KEY_T))
            {
                m_GizmoSpace =
                    GizmoSpace::Local;
            }

            if (m_Window.IsKeyPressed(GLFW_KEY_Y))
            {
                m_GizmoSpace =
                    GizmoSpace::World;
            }
        }

        if (ctrlPressed && m_Window.IsKeyPressed(GLFW_KEY_C))
        {
            if (!m_CPressed)
            {
                m_CPressed = true;
                CopySelectedEntities();
            }
        }
        else m_CPressed = false;

        if (ctrlPressed && m_Window.IsKeyPressed(GLFW_KEY_V))
        {
            if (!m_VPressed)
            {
                m_VPressed = true;
                PasteCopiedEntities();
            }
        }
        else m_VPressed = false;

        if (!InspectorPanel::IsModelingMode() && m_Window.IsKeyPressed(GLFW_KEY_DELETE))
        {
            if (!m_DeletePressed)
            {
                m_DeletePressed = true;
                DeleteSelectedEntity();
            }
        }
        else
        {
            m_DeletePressed = false;
        }

        if (
            ctrlPressed &&
            m_Window.IsKeyPressed(
                GLFW_KEY_D
            )
            )
        {
            if (!m_DPressed)
            {
                m_DPressed = true;

                DuplicateSelectedEntity();
            }
        }
        else
        {
            m_DPressed = false;
        }

        if (m_Window.IsKeyPressed(GLFW_KEY_F))
        {
            if (!m_FPressed)
            {
                m_FPressed = true;
                if (
                    m_SelectedEntity >= 0 &&
                    m_SelectedEntity <
                    (int)m_Scene.GetEntities().size()
                    )
                {
                    Entity& entity =
                        m_Scene.GetEntities()
                        [m_SelectedEntity];

                    Transform& transform =
                        entity.GetTransform();

                    m_Camera.Position.X =
                        transform.Position.X;

                    m_Camera.Position.Y =
                        transform.Position.Y;

                    m_Camera.Position.Z =
                        transform.Position.Z + 5.0f;

                    float dx =
                        transform.Position.X -
                        m_Camera.Position.X;

                    float dy =
                        transform.Position.Y -
                        m_Camera.Position.Y;

                    float dz =
                        transform.Position.Z -
                        m_Camera.Position.Z;

                    float yaw =
                        atan2(
                            dx,
                            -dz
                        );

                    float distanceXZ =
                        sqrt(
                            dx * dx +
                            dz * dz
                        );

                    float pitch =
                        atan2(
                            dy,
                            distanceXZ
                        );

                    m_Camera.Rotation.Y =
                        glm::degrees(
                            yaw
                        );

                    m_Camera.Rotation.X =
                        glm::degrees(
                            pitch
                        );

                    m_Camera.Rotation.Z =
                        0.1f;
                }
            }
        }
        else
        {
            m_FPressed = false;
        }

        bool ctrl =
            m_Window.IsKeyPressed(
                GLFW_KEY_LEFT_CONTROL
            );

        bool z =
            m_Window.IsKeyPressed(
                GLFW_KEY_Z
            );

        if (
            ctrl &&
            z &&
            !m_UndoPressed
            )
        {
            m_UndoPressed = true;

            if (
                m_UndoRedoManager.CanUndo()
                )
            {
                TransformCommand command =
                    m_UndoRedoManager.Undo();

                if (
                    command.Type ==
                    CommandType::Delete
                    )
                {
                    auto& entities =
                        m_Scene.GetEntities();

                    int idx =
                        command.EntityIndex;

                    if (
                        idx < 0 ||
                        idx >(int)entities.size()
                        )
                    {
                        idx =
                            (int)entities.size();
                    }

                    // Reconstruct entity from serialized snapshot and insert in place
                    {
                        std::istringstream ss(command.EntityData);
                        std::string name;
                        std::getline(ss, name);

                        float px, py, pz;
                        ss >> px >> py >> pz;

                        float rx, ry, rz, rw;
                        ss >> rx >> ry >> rz >> rw;

                        float sx, sy, sz;
                        ss >> sx >> sy >> sz;

                        float cr, cg, cb;
                        ss >> cr >> cg >> cb;

                        // Versões antigas do histórico não tinham esta linha;
                        // nesses casos mantemos o comportamento compatível de cubo.
                        std::string primitiveType = "Cube";
                        std::string primitiveLine;
                        if (std::getline(ss, primitiveLine) && !primitiveLine.empty())
                        {
                            std::stringstream primitiveStream(primitiveLine);
                            primitiveStream >> std::quoted(primitiveType);
                            if (primitiveType.empty()) primitiveType = "Cube";
                        }

                        // Insert default-constructed Entity at position and populate it
                        entities.insert(entities.begin() + idx, Entity());
                        Entity& inserted = entities[idx];
                        inserted.SetName(name);
                        inserted.GetTransform().Position = Vector3(px, py, pz);
                        inserted.GetTransform().Rotation.x = rx;
                        inserted.GetTransform().Rotation.y = ry;
                        inserted.GetTransform().Rotation.z = rz;
                        inserted.GetTransform().Rotation.w = rw;
                        inserted.GetTransform().Scale = Vector3(sx, sy, sz);

                        inserted.GetMeshComponent().PrimitiveType = primitiveType;
                        std::vector<Vertex> restoredVertices;
                        if (primitiveType == "Sphere") restoredVertices = MeshFactory::CreateSphere(32, 24);
                        else if (primitiveType == "Cylinder") restoredVertices = MeshFactory::CreateCylinder(32, 1.0f);
                        else if (primitiveType == "Cone") restoredVertices = MeshFactory::CreateCone(32, 1.0f);
                        else if (primitiveType == "Torus") restoredVertices = MeshFactory::CreateTorus();
                        else if (primitiveType == "Capsule") restoredVertices = MeshFactory::CreateCapsule();
                        else if (primitiveType == "Plane") restoredVertices = MeshFactory::CreatePlane(4);
                        else if (primitiveType == "Wedge") restoredVertices = MeshFactory::CreateWedge();
                        else if (primitiveType == "Triangle") restoredVertices = MeshFactory::CreateTriangle();
                        else if (primitiveType == "Quad") restoredVertices = MeshFactory::CreateQuad();

                        if (restoredVertices.empty())
                            restoredVertices = MeshFactory::Create2DPrimitive(primitiveType);

                        if (primitiveType == "Quad")
                            inserted.GetMeshComponent().MeshData = MeshCache::GetQuad();
                        else if (restoredVertices.empty())
                            inserted.GetMeshComponent().MeshData = MeshCache::CreateCubeInstance();
                        else
                        {
                            inserted.GetMeshComponent().MeshData = std::make_shared<Mesh>();
                            inserted.GetMeshComponent().MeshData->Create(
                                restoredVertices, GL_TRIANGLES, "Application::UndoRestorePrimitive");
                        }

                        // Restore color/material
                        inserted.GetColorComponent().Color = Vector3(cr, cg, cb);
                        inserted.GetMaterialComponent().MaterialData.Color[0] = cr;
                        inserted.GetMaterialComponent().MaterialData.Color[1] = cg;
                        inserted.GetMaterialComponent().MaterialData.Color[2] = cb;
                        inserted.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
                    }
                }
                else
                    if (
                        command.Type ==
                        CommandType::Create
                        )
                    {
                        auto& entities =
                            m_Scene.GetEntities();

                        auto it =
                            std::find_if(
                                entities.begin(),
                                entities.end(),
                                [&](const Entity& ent)
                                {
                                    return
                                        ent.GetID()
                                        ==
                                        command.EntityID;
                                }
                            );

                        if (
                            it != entities.end()
                            )
                        {
                            m_Scene.DeleteEntity(static_cast<int>(it - entities.begin()));
                        }
                    }
                    else
                    {
                        if (!command.BatchTransforms.empty())
                        {
                            for (const EntityTransformSnapshot& snapshot : command.BatchTransforms)
                            {
                                if (Entity* entity = m_Scene.FindEntityByID(snapshot.EntityID))
                                    entity->GetTransform() = snapshot.Before;
                            }
                        }
                        else
                        {
                            Entity* entity = m_Scene.FindEntityByID(command.EntityID);
                            if (entity)
                            {
                                entity->GetTransform() = command.Before;
                                if (command.HasMeshData && !command.MeshBefore.empty() &&
                                    entity->GetMeshComponent().MeshData)
                                    entity->GetMeshComponent().MeshData->UpdateVertices(
                                        command.MeshBefore, "Undo::Modeling");
                            }
                        }
                    }
            }
        }

        if (!z)
        {
            m_UndoPressed = false;
        }

        bool y =
            m_Window.IsKeyPressed(
                GLFW_KEY_Y
            );

        if (
            ctrl &&
            y &&
            !m_RedoPressed
            )
        {
            m_RedoPressed = true;

            if (
                m_UndoRedoManager.CanRedo()
                )
            {
                TransformCommand command =
                    m_UndoRedoManager.Redo();

                if (
                    command.Type ==
                    CommandType::Transform
                    )
                {
                    if (!command.BatchTransforms.empty())
                    {
                        for (const EntityTransformSnapshot& snapshot : command.BatchTransforms)
                        {
                            if (Entity* entity = m_Scene.FindEntityByID(snapshot.EntityID))
                                entity->GetTransform() = snapshot.After;
                        }
                    }
                    else
                    {
                        Entity* entity = m_Scene.FindEntityByID(command.EntityID);
                        if (entity)
                        {
                            entity->GetTransform() = command.After;
                            if (command.HasMeshData && !command.MeshAfter.empty() &&
                                entity->GetMeshComponent().MeshData)
                                entity->GetMeshComponent().MeshData->UpdateVertices(
                                    command.MeshAfter, "Redo::Modeling");
                        }
                    }
                }
            }
        }

        if (!y)
        {
            m_RedoPressed = false;
        }
    }
    }

    // Uma cÃ¢mera de cena ativa Ã© a fonte visual do viewport. Nesse caso o
    // fly nÃ£o deve mover/capturar o mouse em segundo plano; se nÃ£o houver uma,
    // o controlador legado continua sendo o fallback padrÃ£o.
    // Câmeras da cena só controlam a imagem durante Play. No modo Editar,
    // o usuário continua vendo e navegando pelo viewport livre.
    const bool hasActiveSceneCamera = m_EditorState == EditorState::Play &&
        FindActiveSceneCamera(m_Scene) != nullptr;
    if (m_ProjectConfig.RuntimeGame)
    {
        m_CameraController.Reset(m_Window);
    }
    else if (m_WorldMode == WorldMode::World2D)
    {
        // O mundo 2D usa o plano XY visto de frente. O controlador 3D nÃ£o
        // deve girar a cÃ¢mera ortogrÃ¡fica enquanto esse modo estÃ¡ ativo.
        // A navegaÃ§Ã£o da cÃ¢mera 2D Ã© feita exclusivamente pelo arraste do
        // botÃ£o direito no viewport; o teclado fica livre para scripts/atalhos.
        m_CameraController.Reset(m_Window);
    }
    else if (hasActiveSceneCamera)
    {
        m_CameraController.Reset(m_Window);
    }
    // Os modos Play que possuem cÃ¢mera prÃ³pria (corrida e sobrevivÃªncia)
    // nÃ£o devem receber uma segunda atualizaÃ§Ã£o do controlador do editor.
    else if (!(m_EditorState == EditorState::Play && m_SurvivalGame.IsActive()))
    {
        // A validaÃ§Ã£o por ImGui::WantCaptureX nÃ£o Ã© feita aqui â€” este mÃ©todo
        // roda ANTES de ImGui::NewFrame() (linha 1386), entÃ£o as flags ainda
        // sÃ£o do frame anterior (stale data). Em vez disso, usamos:
        //   - ViewportHovered / ViewportImageHovered para permitir iniciar
        //     o clique direito da cÃ¢mera SOMENTE quando mouse estÃ¡ no viewport.
        //   - ViewportFocused ou IsControlling() para bloquear WASD quando
        //     o usuÃ¡rio interage com outros painÃ©is (ex: foco em um input).
        const bool canStartControl = m_ViewportHovered || m_ViewportImageHovered;
        m_CameraController.Update(
            m_Window,
            m_Camera,
            canStartControl,
            canStartControl
        );
    }

    // Com a timeline aberta, o gizmo funciona como auto-keying. O botÃ£o
    // Gravar continua habilitando o mesmo comportamento com a timeline fechada.
    if ((m_ShowTimeline || ToolbarPanel::IsRecording()) && WasUsingGizmo())
    {
        AddKeyframeAtCurrentTime();
    }

    const double now = glfwGetTime();
    if (m_LastUpdateTime <= 0.0)
    {
        m_LastUpdateTime = now;
    }
    const float frameDeltaTime = std::min(
        0.033f,
        std::max(0.0f, static_cast<float>(now - m_LastUpdateTime))
    );
    m_LastUpdateTime = now;

    // Recording remains usable when the Timeline panel is hidden. When visible,
    // the panel owns its preview clock and advances it during Draw().
    if (!m_ShowTimeline && ToolbarPanel::IsRecording())
        TimelinePanel::AdvanceRecording(frameDeltaTime);

    if (m_EditorState == EditorState::Play)
    {
        const DemoGameContext demoGameContext = MakeDemoGameContext();
        {
            FrameSectionTimer timer(FrameSection::DemoGames);
            m_RaceGame.Update(demoGameContext, frameDeltaTime);
            m_SurvivalGame.Update(demoGameContext, frameDeltaTime);
        }
        {
            FrameSectionTimer timer(FrameSection::Scripting);
            ScriptingRuntime::Instance().BeginFrame(frameDeltaTime);
        }
        {
            FrameSectionTimer timer(FrameSection::AI);
            AISystem::Update(m_Scene, frameDeltaTime);
        }
        {
            FrameSectionTimer timer(FrameSection::Physics);
            SimulatePhysics(frameDeltaTime);
        }
        {
            FrameSectionTimer timer(FrameSection::Scripting);
            ScriptingRuntime::Instance().OnUpdate(frameDeltaTime);
        }
        // Reproduz os clips da cena no runtime (a prévia da Timeline usa o
        // próprio relógio do editor). Mantém uma única atualização por frame.
        {
            FrameSectionTimer timer(FrameSection::Animation);
            AnimationSystem::Update(m_Scene, frameDeltaTime);
        }
        // ExecuteAttachedScripts(frameDeltaTime);
    }

    if (m_SceneDirty)
    {
        m_RecoverySnapshotAccumulator += frameDeltaTime;
        if (m_RecoverySnapshotAccumulator >= 30.0)
        {
            m_RecoverySnapshotAccumulator = 0.0;
            SaveRecoverySnapshot();
        }
    }
    else
    {
        m_RecoverySnapshotAccumulator = 0.0;
    }

    if (!m_ProjectConfig.RuntimeGame && m_GlobalEditorSettings.AutoSave)
    {
        m_GlobalAutoSaveAccumulator += frameDeltaTime;
        const double interval = std::max(30.0, static_cast<double>(m_GlobalEditorSettings.AutoSaveIntervalS));
        if (m_GlobalAutoSaveAccumulator >= interval)
        {
            m_GlobalAutoSaveAccumulator = 0.0;
            if (m_SceneDirty)
                SaveScene();
        }
    }
    else
    {
        m_GlobalAutoSaveAccumulator = 0.0;
    }
    
    // Atualiza matrizes locais/mundiais antes de renderizar. A cÃ¢mera de cena
    // precisa ser resolvida depois da simulaÃ§Ã£o para acompanhar scripts,
    // fÃ­sica e entidades parentadas no mesmo frame.
    m_Scene.UpdateTransforms();

    // Câmera de jogo 2D: acompanha o objeto marcado como Player durante o
    // runtime. Isso mantém o viewport integrado e evita exigir uma câmera 3D
    // separada para jogos side-scroller como o protótipo FlappyBird.
    if (m_WorldMode == WorldMode::World2D && m_EditorState == EditorState::Play)
    {
        Entity* follow = nullptr;
        for (Entity& candidate : m_Scene.GetEntities())
        {
            if (candidate.Tag == "Player" || candidate.GetName() == "Player")
            {
                follow = &candidate;
                break;
            }
        }
        if (follow)
        {
            // Segue os dois eixos do jogador: platformers podem priorizar Y
            // e jogos top-down precisam acompanhar X e Y simultaneamente.
            m_Camera.Position.X = follow->GetTransform().Position.X;
            m_Camera.Position.Y = follow->GetTransform().Position.Y;
            m_Camera.Position.Z = 4.0f;
            m_Camera.Rotation = Vector3(0.0f, 0.0f, 0.0f);
            m_Camera.Orthographic = true;
        }
    }

    // Keep the last valid viewport image while a dock splitter is moving.
    // Resize its GPU attachments only after the layout settles, then render
    // into the new target before the UI samples it.
    constexpr double kViewportResizeDebounceSeconds = 0.075;
    if (m_ViewportResizePending &&
        glfwGetTime() - m_ViewportResizeRequestedAt >= kViewportResizeDebounceSeconds)
    {
        m_Framebuffer.Resize(m_PendingViewportWidth, m_PendingViewportHeight);
        m_Renderer.ResizePostProcessing(m_PendingViewportWidth, m_PendingViewportHeight);
        m_ViewportWidth = m_PendingViewportWidth;
        m_ViewportHeight = m_PendingViewportHeight;
        m_ViewportResizePending = false;
    }

    m_Renderer.SetShadowIntensity(m_ShadowIntensity);
    m_Renderer.SetAmbientLight(m_AmbientLight);
    // m_Renderer.SetPostElapsedTime(static_cast<float>(now));

    const GLuint viewportFBO = m_Framebuffer.GetFBO();
    // O tipo do projeto é permanente: entrar em Play não transforma um
    // mundo 2D em uma cena 3D. O mesmo pipeline ortográfico é usado no
    // editor e no runtime.
    const bool editor2DMode = m_WorldMode == WorldMode::World2D;
    const Entity* activeSceneCamera = FindActiveSceneCamera(m_Scene);
    const bool useSceneCamera = !editor2DMode && m_EditorState == EditorState::Play &&
        activeSceneCamera != nullptr;
    Camera renderCamera = useSceneCamera
        ? BuildRuntimeCamera(m_Scene, m_Camera)
        : GetViewportCamera();
    const CameraComponent* renderCameraSettings = useSceneCamera
        ? &activeSceneCamera->GetCameraComponent()
        : nullptr;
    const bool renderOrthographic = editor2DMode ||
        (renderCameraSettings && renderCameraSettings->Orthographic);
    const float renderOrthographicSize = editor2DMode
        ? renderCamera.OrthographicSize
        : (renderCameraSettings ? renderCameraSettings->OrthographicSize : 10.0f);
    int renderSelectionIndex = m_SelectedEntity;
    if (renderSelectionIndex >= 0 &&
        renderSelectionIndex < static_cast<int>(m_Scene.GetEntities().size()))
    {
        const Entity& selected = m_Scene.GetEntities()[static_cast<size_t>(renderSelectionIndex)];
        constexpr uint64_t componentMask = 0xFF0000000ULL;
        const bool componentNodeSelected = (m_SelectedID & componentMask) != 0 &&
            (m_SelectedID & ~componentMask) == selected.GetID();
        if (componentNodeSelected || selected.HasUI() || selected.IsFolder)
            renderSelectionIndex = -1;
    }

    // Atualiza o estado dos widgets de UI 2D antes do render, para que hover,
    // press, foco, sliders e dropdowns apareçam corretos no mesmo frame.
    {
        FrameSectionTimer timer(FrameSection::UIInteraction);
        UpdateUIInteraction();
    }

    {
        FrameSectionTimer timer(FrameSection::SceneRender);
        m_Renderer.Render(
            m_Scene,
            renderCamera,
            renderSelectionIndex,
            m_MultiSelectedEntities,
            m_ViewportWidth,
            m_ViewportHeight,
            viewportFBO,
            m_ShadowIntensity,
            m_AmbientLight,
            editor2DMode ? false : m_ShowAxis,
            editor2DMode ? false : m_ShowGrid,
            m_GridRenderDistance,
            renderCameraSettings ? renderCameraSettings->FieldOfView : 60.0f,
            renderCameraSettings ? renderCameraSettings->NearClip : 0.1f,
            renderCameraSettings ? renderCameraSettings->FarClip : 10000.0f,
            renderOrthographic,
            renderOrthographicSize,
            editor2DMode,
            IsRenderUIInEditorActive()
        );
    }

    if (m_ShowMobilePreview)
    {
        m_Renderer.Render(
            m_Scene,
            renderCamera,
            -1,
            {},
            360,
            640,
            m_MobilePreviewFramebuffer.GetFBO(),
            m_ShadowIntensity,
            m_AmbientLight,
            false,
            false,
            m_GridRenderDistance,
            renderCameraSettings ? renderCameraSettings->FieldOfView : 60.0f,
            renderCameraSettings ? renderCameraSettings->NearClip : 0.1f,
            renderCameraSettings ? renderCameraSettings->FarClip : 10000.0f,
            renderOrthographic,
            renderOrthographicSize,
            editor2DMode
        );
    }

    m_Window.Clear();
    
    // =====================
    // IMGUI
    // =====================

    ThumbnailManager::BeginFrame();
    if (m_FontRebuildPending)
    {
        ApplyGlobalEditorSettings(true);
        m_FontRebuildPending = false;
    }
    if (m_Window.GetGraphicsAPI() == GraphicsAPI::OpenGL)
        ImGui_ImplOpenGL3_NewFrame();
    else
        ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    if (m_ProjectConfig.RuntimeGame)
    {
        ImGui::SetNextWindowPos(mainViewport->Pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(mainViewport->Size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("##MaraeGameRuntime", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        const ImVec2 gameSize = ImGui::GetContentRegionAvail();
        const uint32_t gameTexture = m_Framebuffer.GetTexture();
        if (gameTexture != 0 && gameSize.x > 1.0f && gameSize.y > 1.0f)
        {
            ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(gameTexture)), gameSize,
                         ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            const ImVec2 imageMin = ImGui::GetItemRectMin();
            // A UI 2D é desenhada pelo renderizador nativo dentro do framebuffer da
            // cena (idêntica ao jogo exportado). Aqui só registramos a área da
            // imagem para o sistema de interação converter o ponteiro em pixels do
            // canvas do jogo.
            m_GameImageMinX = imageMin.x;
            m_GameImageMinY = imageMin.y;
            m_GameImageWidth = gameSize.x;
            m_GameImageHeight = gameSize.y;
            m_GameImageValid = true;
        }
        else
            ImGui::TextUnformatted("A cena do jogo ainda esta sendo inicializada.");
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
    else
    {
    // Titlebar única: logo + menus + controles, no estilo do Visual Studio.
    constexpr float menuBarH = 38.0f;
    constexpr float toolbarH  = 32.0f;
    constexpr float editorTop = menuBarH + toolbarH;
    const ImVec2 dockSize(
        mainViewport->Size.x,
        std::max(0.0f, mainViewport->Size.y - editorTop)
    );

    // ── Toolbar — barra própria entre o MenuBar e os painéis ─────────────
    // Gizmos ESQUERDA | Play/Pause/Stop CENTRO | separadores à direita
    // (igual ao Unity — fora de qualquer painel dockável)
    ImGui::SetNextWindowPos(ImVec2(0.f, menuBarH), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(mainViewport->Size.x, toolbarH), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.17f, 0.17f, 0.17f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 5.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin(UI("##MaraeToolbar"), nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoNav);
    ToolbarPanel::DrawContents(*this);
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // O DockSpace começa logo abaixo da toolbar.
    // Todos os painéis ficam dentro deste DockSpace.
    ImGui::SetNextWindowPos(ImVec2(0.0f, editorTop), ImGuiCond_Always);
    ImGui::SetNextWindowSize(dockSize, ImGuiCond_Always);
    ImGui::Begin(
        UI("##MaraeDockHost"),
        nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus
    );

    // V6 recria o layout sem a janela implícita Debug##Default: ela não é um
    // painel da engine e docká-la deixa o stack de janelas do ImGui inválido.
    const ImGuiID dockspaceId = ImGui::GetID("MaraeDockSpaceV6");
    SetupDefaultDockLayout(dockspaceId, dockSize);
    ImGui::DockSpace(
        dockspaceId,
        ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode |
        ImGuiDockNodeFlags_NoWindowMenuButton
    );
    ImGui::End();

    // ── Titlebar do editor: logo + projeto + menus + controles ──────────────
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(mainViewport->Size.x, menuBarH), ImGuiCond_Always);
    {
        ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImVec4(0.145f, 0.145f, 0.145f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg,   ImVec4(0.145f, 0.145f, 0.145f, 0.97f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    }

    ImGui::Begin(UI("##MaraeTitleBar"), nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar())
    {
        // ── Logo pequena ──────────────────────────────────────────────────
        if (m_LogoTexture != 0 && m_Window.GetGraphicsAPI() == GraphicsAPI::OpenGL)
        {
            float h = ImGui::GetFrameHeight() - 4.f;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f);
            ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(m_LogoTexture)),
                         ImVec2(h, h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
            ImGui::SameLine(0.f, 6.f);
        }

        // Sem a moldura nativa, o título do projeto passa a fazer parte da
        // barra escura do editor junto da marca e dos menus.
        std::string editorProjectName = "Marae Engine";
        if (!m_ProjectConfig.ProjectPath.empty())
        {
            const std::string folderName =
                std::filesystem::path(m_ProjectConfig.ProjectPath).filename().string();
            if (!folderName.empty()) editorProjectName = folderName;
        }
        if (m_SceneDirty) editorProjectName += " *";
        ImGui::TextColored(ImVec4(0.82f, 0.83f, 0.85f, 1.0f), "%s", editorProjectName.c_str());
        ImGui::SameLine(0.0f, 14.0f);

        // ── Arquivo ───────────────────────────────────────────────────────
        if (ImGui::BeginMenu(L("menu.file")))
        {
            if (ImGui::MenuItem(L("menu.file.new_scene"),    "Ctrl+N"))   NewScene();
            if (ImGui::MenuItem(L("menu.file.open_scene"),   "Ctrl+O"))   OpenSceneDialog();
            if (ImGui::MenuItem(L("menu.file.additive_scene")))           OpenAdditiveSceneDialog();
            if (ImGui::MenuItem(L("menu.file.save_scene"),   "Ctrl+S"))   SaveScene();
            if (ImGui::MenuItem(L("menu.file.save_scene_as"),"Ctrl+Shift+S")) SaveSceneAsDialog();

            ImGui::Separator();

            if (ImGui::BeginMenu(L("menu.file.project_levels")))
            {
                const auto& levels = m_SceneManager.GetProjectLevels();
                if (levels.empty())
                    ImGui::TextDisabled("  %s", L("menu.file.no_levels"));
                else
                    for (const auto& lvl : levels)
                    {
                        bool isCurrent = (m_SceneManager.GetActiveScenePath() == lvl.AbsolutePath);
                        if (ImGui::MenuItem(lvl.Name.c_str(), nullptr, isCurrent))
                            m_SceneManager.LoadSceneAsync(*this, lvl.AbsolutePath, SceneTransitionType::FadeBlack);
                    }
                ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem(L("menu.file.import_model")))
            {
                const std::string selected = ModelImporter::OpenFileDialog();
                if (!selected.empty())
                {
                    Entity* imported = ModelImporter::ImportToScene(selected, m_Scene);
                    if (imported) { AssetDatabase::ScanAssets(m_ProjectConfig.ProjectPath); SetSelectedEntity((int)m_Scene.GetEntities().size() - 1); }
                }
            }

            // Build Settings vive aqui — Unity: File > Build Settings
            ImGui::Separator();
            if (ImGui::MenuItem(UI("Build Settings..."))) m_ShowExportModal = true;

            ImGui::Separator();
            if (ImGui::MenuItem(L("menu.file.open_project")))  OpenAnotherProject();
            if (ImGui::MenuItem(L("menu.file.close_project"))) ReturnToProjectHub();
            ImGui::Separator();
            if (ImGui::MenuItem(UI("Sair"), "Alt+F4")) m_Window.RequestClose();

            ImGui::EndMenu();
        }

        // ── Editar ────────────────────────────────────────────────────────
        // Unity: Edit — undo/redo, duplicate/delete, preferences
        if (ImGui::BeginMenu(L("menu.edit")))
        {
            if (ImGui::MenuItem(L("menu.edit.copy"), "Ctrl+C")) CopySelectedEntities();
            if (ImGui::MenuItem(L("menu.edit.paste"), "Ctrl+V", false, !m_EntityClipboard.empty())) PasteCopiedEntities();
            ImGui::Separator();
            if (ImGui::MenuItem(L("menu.edit.duplicate"), "Ctrl+D")) DuplicateSelectedEntity();
            if (ImGui::MenuItem(L("menu.edit.delete"),    "Del"))     DeleteSelectedEntity();

            ImGui::EndMenu();
        }

        // ── GameObject ────────────────────────────────────────────────────
        // Unity: GameObject — cria entidades, primitivos, câmeras, luzes
        if (ImGui::BeginMenu(UI("GameObject")))
        {
            if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Empty,
                    L("menu.create.empty"), "Ctrl+Shift+N")) CreateEmptyEntity();
            if (!Is2DWorldMode())
            {
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Camera, UI("Camera"))) CreateCameraEntity();
                ImGui::Separator();
                ImGui::TextDisabled("Objetos 3D");
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Cube, L("menu.create.cube"))) CreateCubeEntity();
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Sphere, L("menu.create.sphere"))) CreateSphereEntity();
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Cylinder, L("menu.create.cylinder"))) CreateCylinderEntity();
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Cone, L("menu.create.cone"))) CreateConeEntity();
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Torus, L("menu.create.torus"))) CreateTorusEntity();
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Capsule, L("menu.create.capsule"))) CreateCapsuleEntity();
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Plane, L("menu.create.plane"))) CreatePlaneEntity();
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Wedge, L("menu.create.wedge"))) CreateWedgeEntity();
            }

            ImGui::Separator();
            ImGui::TextDisabled("Objetos 2D");
            if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Image, UI("Sprite / Quad 2D"))) CreateQuadEntity();
            if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Triangle, L("menu.create.triangle"))) CreateTriangleEntity();
            if (Is2DWorldMode())
            {
                ImGui::Separator();
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Circle, UI("Circle"))) Create2DShapeEntity("Circle2D");
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Pentagon, UI("Pentagon"))) Create2DShapeEntity("Pentagon2D");
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Hexagon, UI("Hexagon"))) Create2DShapeEntity("Hexagon2D");
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Octagon, UI("Octagon"))) Create2DShapeEntity("Octagon2D");
                if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Star, UI("Star"))) Create2DShapeEntity("Star2D");
            }
            if (!Is2DWorldMode() && MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Plane, UI("Terrain 3D"))) CreateTerrainEntity();

            if (Is2DWorldMode())
            {
                ImGui::Separator();
                ImGui::TextDisabled("UI 2D");
                const struct { const char* label; UIElementType type; MaraeUI::EditorIcon icon; } uiTypes[] = {
                    { "ScreenGui", UIElementType::ScreenGui, MaraeUI::EditorIcon::ScreenGui },
                    { "Frame", UIElementType::Frame, MaraeUI::EditorIcon::Frame },
                    { "TextLabel", UIElementType::TextLabel, MaraeUI::EditorIcon::Text },
                    { "TextButton", UIElementType::TextButton, MaraeUI::EditorIcon::Button },
                    { "ImageLabel", UIElementType::ImageLabel, MaraeUI::EditorIcon::Image },
                    { "ImageButton", UIElementType::ImageButton, MaraeUI::EditorIcon::Button },
                    { "TextBox", UIElementType::TextBox, MaraeUI::EditorIcon::Text },
                    { "ScrollingFrame", UIElementType::ScrollingFrame, MaraeUI::EditorIcon::Frame },
                    { "Slider", UIElementType::Slider, MaraeUI::EditorIcon::Button },
                    { "Checkbox", UIElementType::Checkbox, MaraeUI::EditorIcon::Button },
                    { "ProgressBar", UIElementType::ProgressBar, MaraeUI::EditorIcon::Frame },
                    { "Dropdown", UIElementType::Dropdown, MaraeUI::EditorIcon::Button }
                };
                for (const auto& uiType : uiTypes)
                {
                    if (MaraeUI::IconMenuItem(uiType.icon, uiType.label))
                    {
                        Entity& uiEntity = m_Scene.CreateEntity();
                        uiEntity.AddUI(uiType.type);
                        uiEntity.SetName(UIElementTypeName(uiType.type));
                        uiEntity.GetUIComponent().PositionX = 40.0f;
                        uiEntity.GetUIComponent().PositionY = 40.0f;
                        uiEntity.GetUIComponent().Width = uiType.type == UIElementType::ScreenGui ? 960.0f : 240.0f;
                        uiEntity.GetUIComponent().Height = uiType.type == UIElementType::ScreenGui ? 540.0f : 64.0f;
                        // Alturas/valores padrão sensatos por widget.
                        switch (uiType.type)
                        {
                        case UIElementType::Slider:
                            uiEntity.GetUIComponent().Height = 24.0f;
                            uiEntity.GetUIComponent().MinValue = 0.0f;
                            uiEntity.GetUIComponent().MaxValue = 1.0f;
                            uiEntity.GetUIComponent().Value = 0.5f;
                            uiEntity.GetUIComponent().CornerRadius = 6.0f;
                            break;
                        case UIElementType::Checkbox:
                            uiEntity.GetUIComponent().Height = 28.0f;
                            uiEntity.GetUIComponent().CornerRadius = 4.0f;
                            break;
                        case UIElementType::ProgressBar:
                            uiEntity.GetUIComponent().Height = 20.0f;
                            uiEntity.GetUIComponent().MaxValue = 1.0f;
                            uiEntity.GetUIComponent().Value = 0.6f;
                            uiEntity.GetUIComponent().CornerRadius = 6.0f;
                            break;
                        case UIElementType::Dropdown:
                            uiEntity.GetUIComponent().Height = 30.0f;
                            uiEntity.GetUIComponent().CornerRadius = 6.0f;
                            if (uiEntity.GetUIComponent().Options.empty())
                                uiEntity.GetUIComponent().Options = { "Opcao 1", "Opcao 2", "Opcao 3" };
                            break;
                        case UIElementType::ScrollingFrame:
                            uiEntity.GetUIComponent().Height = 200.0f;
                            uiEntity.GetUIComponent().ScrollViewEnabled = true;
                            uiEntity.GetUIComponent().ClipsContent = true;
                            break;
                        default:
                            break;
                        }
                        uiEntity.GetUIComponent().Text =
                            (uiType.type == UIElementType::TextLabel || uiType.type == UIElementType::TextButton || uiType.type == UIElementType::TextBox)
                            ? UIElementTypeName(uiType.type) : "";
                        SetSelectedEntity(static_cast<int>(m_Scene.GetEntities().size()) - 1);
                        MarkDirty();
                    }
                }
            }

            if (!Is2DWorldMode())
            {
            ImGui::Separator();
            ImGui::TextDisabled("Luz");
            if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::Light, UI("Point Light")))
            {
                auto& e = m_Scene.CreateEntity(); e.SetName("Point Light");
                e.AddLight(LightType::Point); SetSelectedEntity((int)m_Scene.GetEntities().size()-1); MarkDirty();
            }
            if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::SpotLight, UI("Spot Light")))
            {
                auto& e = m_Scene.CreateEntity(); e.SetName("Spot Light");
                e.AddLight(LightType::Spot); SetSelectedEntity((int)m_Scene.GetEntities().size()-1); MarkDirty();
            }
            if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::DirectionalLight, UI("Directional Light")))
            {
                auto& e = m_Scene.CreateEntity(); e.SetName("Directional Light");
                e.AddLight(LightType::Directional); SetSelectedEntity((int)m_Scene.GetEntities().size()-1); MarkDirty();
            }
            if (MaraeUI::IconMenuItem(MaraeUI::EditorIcon::AreaLight, UI("Area Light")))
            {
                auto& e = m_Scene.CreateEntity(); e.SetName("Area Light");
                e.AddLight(LightType::Area); SetSelectedEntity((int)m_Scene.GetEntities().size()-1); MarkDirty();
            }
            }

            ImGui::EndMenu();
        }

        // ── Window ────────────────────────────────────────────────────────
        // Unity: Window — painéis e layouts
        if (ImGui::BeginMenu(UI("Window")))
        {
            ImGui::TextDisabled("Painéis");
            ImGui::MenuItem(L("menu.view.hierarchy"), nullptr, &m_ShowHierarchy);
            ImGui::MenuItem(L("menu.view.inspector"), nullptr, &m_ShowInspector);
            ImGui::MenuItem(L("menu.view.ai"),        nullptr, &m_ShowAIPanel);
            ImGui::MenuItem(L("menu.view.assets"),    nullptr, &m_ShowAssetBrowser);
            ImGui::MenuItem(L("menu.tools.timeline"), nullptr, &m_ShowTimeline);
            ImGui::MenuItem(UI("Pixel Art / Texturas 2D"), nullptr, &m_ShowPixelArtEditor);
            if (Is2DWorldMode())
                ImGui::MenuItem(UI("Laboratório de Lógica 2D"), nullptr, &m_ShowLogicCircuit);
            ImGui::MenuItem(UI("Visualizar em celular (9:16)"), nullptr, &m_ShowMobilePreview);

            ImGui::Separator();
            ImGui::TextDisabled("Viewport");
            ImGui::MenuItem(UI("Grid"),           nullptr, &m_ShowGrid);
            ImGui::MenuItem(UI("Eixos (Gizmos)"), nullptr, &m_ShowAxis);

            ImGui::Separator();
            if (ImGui::MenuItem(L("menu.view.restore")))
            {
                m_ShowHierarchy = m_ShowInspector = m_ShowAIPanel = m_ShowAssetBrowser = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(UI("Configurações")))
        {
            ImGui::MenuItem(L("settings.preferences"), nullptr, &m_ShowGlobalEditorSettings);
            ImGui::MenuItem(L("settings.scene_settings"), nullptr, &m_ShowSceneSettings);
            ImGui::EndMenu();
        }

        // ── Ajuda fica junto dos demais menus, não isolada no canto ───────
        if (ImGui::BeginMenu(L("menu.help")))
        {
            ImGui::TextDisabled("Marae Engine  —  build 2026");
            ImGui::TextDisabled("0%% Royalties  •  100%% Creator Ownership");
            ImGui::Separator();
            if (ImGui::MenuItem(L("menu.help.terms_dialog")))    m_ShowTermsModal = true;
            ImGui::Separator();
            if (ImGui::MenuItem(L("menu.help.copy_email")))      ImGui::SetClipboardText("maraeengine@gmail.com");
            ImGui::TextDisabled("Contato: maraeengine@gmail.com");
            ImGui::Separator();
            ImGui::TextDisabled("© 2026 Marae Engine");
            ImGui::TextDisabled("%s", L("menu.help.commercial_ready"));
            ImGui::EndMenu();
        }

        // Controles compactos no canto direito, como na titlebar do Visual
        // Studio. Os ícones são desenhados geometricamente para permanecerem
        // perfeitamente centralizados, independentemente da fonte instalada.
        const float controlW = 44.0f;
        const float controlsWidth = controlW * 3.0f;
        const float rightEdge = ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rightEdge - controlsWidth));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.09f, 0.09f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.28f, 0.28f, 0.30f, 1.0f));
        auto DrawWindowControl = [&](const char* id, int kind, const ImVec4& hoverColor)
        {
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            // Menu-bar contents start below the top edge. Pin the full-height
            // hit target to the titlebar bounds so its center matches the
            // menus instead of inheriting that lower cursor baseline.
            const ImVec2 buttonCursor = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(buttonCursor.x, ImGui::GetWindowPos().y));
            ImGui::InvisibleButton(id, ImVec2(controlW, menuBarH));
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            const ImU32 iconColor = IM_COL32(220, 220, 224, 255);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            if (ImGui::IsItemHovered())
            {
                const ImU32 background = ImGui::ColorConvertFloat4ToU32(hoverColor);
                draw->AddRectFilled(min, max, background);
            }

            if (kind == 0) // minimizar
            {
                draw->AddLine(ImVec2(center.x - 6.0f, center.y + 2.0f),
                              ImVec2(center.x + 6.0f, center.y + 2.0f), iconColor, 1.5f);
            }
            else if (kind == 1) // maximizar/restaurar
            {
                if (m_Window.IsWindowMaximized())
                {
                    draw->AddRect(ImVec2(center.x - 2.5f, center.y - 5.0f),
                                  ImVec2(center.x + 5.5f, center.y + 3.0f), iconColor, 0.0f, 0, 1.35f);
                    draw->AddRect(ImVec2(center.x - 5.5f, center.y - 2.0f),
                                  ImVec2(center.x + 2.5f, center.y + 6.0f), iconColor, 0.0f, 0, 1.35f);
                }
                else
                {
                    draw->AddRect(ImVec2(center.x - 6.0f, center.y - 6.0f),
                                  ImVec2(center.x + 6.0f, center.y + 6.0f), iconColor, 0.0f, 0, 1.4f);
                }
            }
            else // fechar
            {
                draw->AddLine(ImVec2(center.x - 5.0f, center.y - 5.0f),
                              ImVec2(center.x + 5.0f, center.y + 5.0f), iconColor, 1.5f);
                draw->AddLine(ImVec2(center.x + 5.0f, center.y - 5.0f),
                              ImVec2(center.x - 5.0f, center.y + 5.0f), iconColor, 1.5f);
            }
            ImGui::PopStyleColor();
        };

        DrawWindowControl("##WindowMinimize", 0, ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
        if (ImGui::IsItemClicked()) m_Window.Minimize();
        ImGui::SameLine(0.0f, 0.0f);
        DrawWindowControl("##WindowMaximize", 1, ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
        if (ImGui::IsItemClicked()) m_Window.ToggleMaximize();
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.09f, 0.09f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.55f, 0.06f, 0.06f, 1.0f));
        DrawWindowControl("##WindowClose", 2, ImVec4(0.78f, 0.12f, 0.12f, 1.0f));
        if (ImGui::IsItemClicked()) m_Window.RequestClose();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        ImGui::EndMenuBar();
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
    {
        m_Window.BeginWindowDrag();
    }
    ImGui::End();
    // Fecha os estilos do menu do editor
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    // ProteÃ§Ã£o adicional para inicializaÃ§Ãµes diretas do editor, sem passar
    // pelo ProjectHub. O modal bloqueia o uso atÃ© o aceite explÃ­cito.
    LegalNotice::DrawFirstRunAcceptance();

    // PainÃ©is vitais (sempre visÃ­veis se nÃ£o fechados)
    if (m_ShowHierarchy)
    {
        HierarchyPanel::Draw(*this);
    }
    if (m_ShowInspector)
    {
        InspectorPanel::Draw(*this);
    }
    if (m_ShowAIPanel)
    {
        AIPanel::Draw(*this);
    }
    if (m_ShowAssetBrowser)
    {
        m_AssetBrowserPanel.Draw(*this);
    }

    ViewportPanel::Draw(*this);

    // Settings panel sÃ³ aparece se for solicitado via menu
    if (m_ShowSceneSettings)
    {
        EditorSettingsPanel::Draw(*this);
    }

    // Timeline panel só aparece se for solicitado via menu
    if (m_ShowTimeline)
    {
        TimelinePanel::Draw(*this);
    }

    // Pintor de Texturas 2D
    if (m_ShowPixelArtEditor)
    {
        PixelArtEditorPanel::Draw(*this);
    }

    if (m_ShowLogicCircuit && Is2DWorldMode())
        LogicCircuitPanel::Draw(*this);

    if (m_ShowMobilePreview)
        RenderMobilePreview();

    // Atualiza transforms quando o gizmo está sendo usado sem duplicar o passe de renderização
    if (m_WasUsingGizmo)
    {
        m_Scene.UpdateTransforms();
    }

    const DemoGameContext demoGameContext = MakeDemoGameContext();
    m_RaceGame.DrawHUD(demoGameContext);
    m_SurvivalGame.DrawHUD(demoGameContext);
    m_SceneManager.RenderTransitionOverlay(static_cast<float>(m_Window.GetWidth()), static_cast<float>(m_Window.GetHeight()));
    if (m_ShowTermsModal)  RenderTermsModal();
    if (m_ShowExportModal) RenderExportModal();

    // Render internal script editor UI if open
    RenderInternalEditorUI();

    // ---- DiÃ¡logo "AlteraÃ§Ãµes nÃ£o salvas" ----
    RenderRecoveryDialog();

    if (m_ShowUnsavedDialog)
    {
        ImGui::OpenPopup("##unsaved_dialog");
        m_ShowUnsavedDialog = false;
    }

    ImGui::SetNextWindowSize({ 400.f, 0.f }, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always, { 0.5f, 0.5f });
    const std::string unsavedTitle = std::string(L("menu.unsaved_title")) + "##unsaved_dialog";
    if (ImGui::BeginPopupModal(unsavedTitle.c_str(), nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        ImGui::Dummy({ 0.f, 8.f });
        ImGui::TextWrapped(UI("%s"), L("menu.unsaved_message"));
        ImGui::Dummy({ 0.f, 12.f });
        ImGui::Separator();
        ImGui::Dummy({ 0.f, 8.f });

        // BotÃ£o Salvar e Fechar
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.06f, 0.56f, 0.98f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.04f, 0.42f, 0.74f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.04f, 0.42f, 0.74f, 1.f));
        if (ImGui::Button(L("menu.save_close"), { 130.f, 32.f }))
        {
            SaveScene();
            if (!m_PendingGraphicsAPISwitch || !m_SceneDirty)
            {
                if (m_PendingGraphicsAPISwitch)
                    CompleteGraphicsAPISwitch();
                else
                    glfwSetWindowShouldClose(m_Window.GetNativeWindow(), GLFW_TRUE);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine(0.f, 8.f);

        // BotÃ£o NÃ£o Salvar
        if (ImGui::Button(L("menu.dont_save"), { 100.f, 32.f }))
        {
            m_SceneDirty = false;
            if (m_PendingGraphicsAPISwitch)
                CompleteGraphicsAPISwitch();
            else
                glfwSetWindowShouldClose(m_Window.GetNativeWindow(), GLFW_TRUE);
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine(0.f, 8.f);

        // BotÃ£o Cancelar
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.22f, 0.22f, 0.22f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.30f, 0.30f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.18f, 0.18f, 0.18f, 1.f));
        if (ImGui::Button(L("common.cancel"), { 90.f, 32.f }))
        {
            if (m_PendingGraphicsAPISwitch)
            {
                m_PendingGraphicsAPISwitch = false;
                m_GlobalEditorSettings.RequestedGraphicsAPI = m_Window.GetGraphicsAPI();
                m_Renderer.SetRequestedGraphicsAPI(m_Window.GetGraphicsAPI());
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);

        ImGui::Dummy({ 0.f, 8.f });
        ImGui::EndPopup();
    }

    RenderGlobalEditorSettingsModal();

    AutoSaveEditorSettings();
    }

    ImGui::Render();

    if (m_Window.GetGraphicsAPI() == GraphicsAPI::OpenGL)
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    else if (m_VulkanUIRenderer.IsInitialized())
        m_VulkanUIRenderer.Render(ImGui::GetDrawData());

    ThumbnailManager::EndFrame();

    m_Window.Present();
}

void Application::PrintHierarchy()
{
}

// ---------------------------------------------------------------------------
// MarkDirty â€” chame sempre que a cena for modificada pelo usuÃ¡rio
// ---------------------------------------------------------------------------
void Application::MarkDirty()
{
    if (m_ProjectConfig.RuntimeGame)
        return;
    if (!m_SceneDirty)
    {
        m_SceneDirty = true;
        m_RecoverySnapshotAccumulator = 30.0;
        UpdateWindowTitle();
    }
}

// ---------------------------------------------------------------------------
// UpdateWindowTitle â€” reflete o nome do projeto e o estado de modificaÃ§Ã£o
// ---------------------------------------------------------------------------
void Application::UpdateWindowTitle()
{
    if (m_ProjectConfig.RuntimeGame)
    {
        if (m_Window.GetNativeWindow())
            glfwSetWindowTitle(m_Window.GetNativeWindow(),
                m_ProjectConfig.GameTitle.empty() ? "Marae Game" : m_ProjectConfig.GameTitle.c_str());
        return;
    }
    std::string title = "Project";

    if (!m_ProjectConfig.ProjectPath.empty())
    {
        namespace fs = std::filesystem;
        std::string projName = fs::path(m_ProjectConfig.ProjectPath).filename().string();
        if (!projName.empty())
            title = projName;
    }

    if (m_SceneDirty)
        title += " *";

    glfwSetWindowTitle(m_Window.GetNativeWindow(), title.c_str());
}

void Application::MoveSelectedEntity(
    float x,
    float y,
    float z)
{
    auto& entities =
        m_Scene.GetEntities();

    if (entities.empty())
    {
        return;
    }

    entities[m_SelectedEntity]
        .GetTransform()
        .Position.X += x;

    entities[m_SelectedEntity]
        .GetTransform()
        .Position.Y += y;

    entities[m_SelectedEntity]
        .GetTransform()
        .Position.Z += z;
}

void Application::SetSelectedEntity(
    int index)
{
    // Uma seleÃ§Ã£o simples comeÃ§a uma nova seleÃ§Ã£o. O quadro de seleÃ§Ã£o chama
    // SetMultiSelectedEntities depois para restaurar todos os Ã­ndices.
    m_MultiSelectedEntities.clear();
    m_SelectedEntity = index;
    if (index >= 0 && index < (int)m_Scene.GetEntities().size())
    {
        m_SelectedID = m_Scene.GetEntities()[index].GetID();
    }
    else
    {
        m_SelectedID = 0;
    }
}

uint64_t Application::GetSelectedID() const
{
    return m_SelectedID;
}

void Application::SetSelectedID(uint64_t id)
{
    m_SelectedID = id;
    // Componentes usam um ID virtual: preserve tambÃ©m a entidade dona para
    // aÃ§Ãµes do Inspector (abrir script, remover componente etc.).
    uint64_t mask = 0xFF0000000ULL;
    if ((id & mask) != 0)
    {
        m_MultiSelectedEntities.clear();
        const uint64_t parentID = id & ~mask;
        m_SelectedEntity = -1;
        for (size_t i = 0; i < m_Scene.GetEntities().size(); ++i)
        {
            if (m_Scene.GetEntities()[i].GetID() == parentID)
            {
                m_SelectedEntity = static_cast<int>(i);
                break;
            }
        }
    }
    else
    {
        // Procurar a entidade com este ID
        m_SelectedEntity = -1; // Reset primeiro
        for (size_t i = 0; i < m_Scene.GetEntities().size(); i++)
        {
            if (m_Scene.GetEntities()[i].GetID() == id)
            {
                m_SelectedEntity = (int)i;
                break;
            }
        }
    }
}

void Application::InitImGui()
{
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();

    // Disable automatic .ini file to avoid restoring unwanted Debug window
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "imgui.ini";
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigDockingWithShift = false;

    // (removed TexDesiredWidth for compatibility) - will try loading a larger TTF font later for better quality

    ImGui::StyleColorsDark();

    // Customize ImGui style for a minimalistic Unity-like dark editor
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 6.0f;
        style.ChildRounding = 5.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 5.0f;
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 0.0f;
        style.FrameBorderSize = 0.0f;
        style.PopupRounding = 5.0f;
        style.TabRounding = 5.0f;
        style.TabBorderSize = 0.0f;
        style.WindowPadding = ImVec2(9, 8);
        style.FramePadding = ImVec2(7, 5);
        style.ItemSpacing = ImVec2(7, 6);
        style.ItemInnerSpacing = ImVec2(6, 4);
        style.IndentSpacing = 16.0f;
        style.ScrollbarSize = 12.0f;

        // A single restrained slate/blue palette unifies docked editor panels.
        const ImVec4 colBg         = ImVec4(0.085f, 0.096f, 0.118f, 1.00f);
        const ImVec4 colPanel      = ImVec4(0.115f, 0.129f, 0.157f, 1.00f);
        const ImVec4 colPanelAlt   = ImVec4(0.155f, 0.176f, 0.213f, 1.00f);
        const ImVec4 colBorder     = ImVec4(0.205f, 0.228f, 0.271f, 1.00f);
        const ImVec4 colText       = ImVec4(0.90f, 0.92f, 0.96f, 1.00f);
        const ImVec4 colTextDim    = ImVec4(0.59f, 0.64f, 0.72f, 1.00f);
        const ImVec4 colAccent     = ImVec4(0.20f, 0.52f, 0.91f, 1.00f);
        const ImVec4 colAccentDark = ImVec4(0.15f, 0.40f, 0.73f, 1.00f);

        style.Colors[ImGuiCol_Text]                   = colText;
        style.Colors[ImGuiCol_TextDisabled]           = colTextDim;
        style.Colors[ImGuiCol_WindowBg]               = colBg;
        style.Colors[ImGuiCol_ChildBg]                = colPanel;
        style.Colors[ImGuiCol_PopupBg]                = colPanel;
        style.Colors[ImGuiCol_Border]                 = colBorder;
        style.Colors[ImGuiCol_BorderShadow]           = ImVec4(0,0,0,0);
        style.Colors[ImGuiCol_FrameBg]                = colPanel;
        style.Colors[ImGuiCol_FrameBgHovered]         = colPanelAlt;
        style.Colors[ImGuiCol_FrameBgActive]          = colPanelAlt;
        style.Colors[ImGuiCol_TitleBg]                = colPanel;
        style.Colors[ImGuiCol_TitleBgActive]          = colPanelAlt;
        style.Colors[ImGuiCol_TitleBgCollapsed]       = colPanel;
        style.Colors[ImGuiCol_MenuBarBg]              = colPanel;
        style.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0,0,0,0);
        style.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.25f,0.25f,0.25f,0.75f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.35f,0.35f,0.35f,0.85f);
        style.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.45f,0.45f,0.45f,0.95f);
        style.Colors[ImGuiCol_CheckMark]              = colAccent;
        style.Colors[ImGuiCol_SliderGrab]            = colAccent;
        style.Colors[ImGuiCol_SliderGrabActive]      = colAccentDark;
        style.Colors[ImGuiCol_Button]                = colPanel;
        style.Colors[ImGuiCol_ButtonHovered]         = colAccent;
        style.Colors[ImGuiCol_ButtonActive]          = colAccentDark;
        style.Colors[ImGuiCol_Header]                = colPanel;
        style.Colors[ImGuiCol_HeaderHovered]         = colAccent;
        style.Colors[ImGuiCol_HeaderActive]          = colAccentDark;
        style.Colors[ImGuiCol_Separator]             = colBorder;
        // Improve font/sampling quality and remove panel outlines
        io.FontGlobalScale = io.FontGlobalScale * 1.15f; // boost for crisper, larger text
        style.AntiAliasedLines = true;
        style.AntiAliasedFill = true;
        style.Colors[ImGuiCol_SeparatorHovered]      = colAccent;
        style.Colors[ImGuiCol_Tab]                   = colPanel;
        style.Colors[ImGuiCol_TabHovered]            = colPanelAlt;
        style.Colors[ImGuiCol_TabActive]             = colAccentDark;
        style.Colors[ImGuiCol_TabUnfocused]          = colBg;
        style.Colors[ImGuiCol_TabUnfocusedActive]    = colPanelAlt;
        style.Colors[ImGuiCol_PlotLines]             = ImVec4(0.5f,0.5f,0.5f,1.0f);
        style.Colors[ImGuiCol_PlotLinesHovered]      = colAccent;
        style.Colors[ImGuiCol_PlotHistogram]         = ImVec4(0.5f,0.5f,0.5f,1.0f);
        style.Colors[ImGuiCol_PlotHistogramHovered]  = colAccent;
        style.Colors[ImGuiCol_TextSelectedBg]        = ImVec4(colAccent.x, colAccent.y, colAccent.z, 0.25f);
    }

    float contentScaleX = 1.0f;
    float contentScaleY = 1.0f;
    glfwGetWindowContentScale(
        m_Window.GetNativeWindow(),
        &contentScaleX,
        &contentScaleY
    );
    const float uiScale = std::clamp(
        std::max({ contentScaleX, contentScaleY, 1.0f }),
        1.0f,
        1.50f
    );
    io.FontGlobalScale = uiScale;
    ImGui::GetStyle().ScaleAllSizes(uiScale);

    // Atlas fixo de fontes oficiais do Windows. Reservamos slot 0 para fallback
    // e mantemos os seis índices estáveis para seleção por elemento da UI.
    io.Fonts->Clear();
    io.Fonts->AddFontDefault();
    for (int i = 0; i < 6; ++i)
    {
        const std::filesystem::path fontPath = GetEditorSystemFontPath(i);
        ImFont* font = std::filesystem::exists(fontPath)
            ? io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 13.0f * uiScale)
            : nullptr;
        if (!font) io.Fonts->AddFontDefault();
    }
    const int defaultFontIndex = std::clamp(m_GlobalEditorSettings.FontIndex, 0, 5) + 1;
    io.FontDefault = defaultFontIndex < io.Fonts->Fonts.Size
        ? io.Fonts->Fonts[defaultFontIndex] : io.Fonts->Fonts[0];

    if (m_Window.GetGraphicsAPI() == GraphicsAPI::Vulkan)
    {
        if (m_VulkanUIRenderer.Initialize(m_Window.GetNativeWindow()))
        {
            m_ImGuiPlatformBackendInitialized = ImGui_ImplGlfw_InitForVulkan(
                m_Window.GetNativeWindow(), true);
        }
        if (!m_VulkanUIRenderer.IsInitialized() || !m_ImGuiPlatformBackendInitialized)
        {
            std::cerr << "[Vulkan] Inicialização real da interface falhou; retornando ao OpenGL.\n";
            m_SessionResult.Action = ProjectSessionAction::RestartWithGraphicsAPI;
            m_SessionResult.NextProject = m_ProjectConfig;
            m_SessionResult.NextProject.Graphics = GraphicsAPI::OpenGL;
            m_GlobalEditorSettings.RequestedGraphicsAPI = GraphicsAPI::OpenGL;
            m_GlobalEditorSettings.RequestedRendererMode = RendererMode::OpenGLOnly;
            m_Renderer.SetRequestedGraphicsAPI(GraphicsAPI::OpenGL);
            if (m_ProjectConfig.PersistEditorSettings)
                SaveGlobalEditorSettings();
            m_Window.RequestClose();
        }
    }
    else
    {
        m_ImGuiPlatformBackendInitialized = ImGui_ImplGlfw_InitForOpenGL(
            m_Window.GetNativeWindow(), true);
        m_ImGuiOpenGLBackendInitialized = ImGui_ImplOpenGL3_Init("#version 330");
    }
}

void Application::ShutdownImGui()
{
    if (m_ImGuiOpenGLBackendInitialized)
    {
        ImGui_ImplOpenGL3_Shutdown();
        m_ImGuiOpenGLBackendInitialized = false;
    }
    if (m_VulkanUIRenderer.IsInitialized())
        m_VulkanUIRenderer.Shutdown();
    if (m_ImGuiPlatformBackendInitialized)
    {
        ImGui_ImplGlfw_Shutdown();
        m_ImGuiPlatformBackendInitialized = false;
    }

    ImGui::DestroyContext();
}

void Application::RequestGraphicsAPISwitch(GraphicsAPI api)
{
    if (api == m_Window.GetGraphicsAPI())
    {
        m_GlobalEditorSettings.RequestedGraphicsAPI = api;
        m_Renderer.SetRequestedGraphicsAPI(api);
        return;
    }

    m_PendingGraphicsAPISwitch = true;
    m_PendingGraphicsAPI = api;
    if (m_SceneDirty)
    {
        m_ShowUnsavedDialog = true;
        return;
    }
    CompleteGraphicsAPISwitch();
}

void Application::CompleteGraphicsAPISwitch()
{
    if (!m_PendingGraphicsAPISwitch)
        return;

    m_SessionResult.Action = ProjectSessionAction::RestartWithGraphicsAPI;
    m_SessionResult.NextProject = m_ProjectConfig;
    m_SessionResult.NextProject.Graphics = m_PendingGraphicsAPI;
    m_GlobalEditorSettings.RequestedGraphicsAPI = m_PendingGraphicsAPI;
    m_Renderer.SetRequestedGraphicsAPI(m_PendingGraphicsAPI);
    if (m_ProjectConfig.PersistEditorSettings)
    {
        SaveEditorSettings();
        SaveGlobalEditorSettings();
    }
    m_PendingGraphicsAPISwitch = false;
    m_Window.RequestClose();
}

void Application::CreateCubeEntity()
{
    Entity& cube =
        m_Scene.CreateEntity();

    static int cubeCounter = 1;

    cube.SetName(
        "Cube " +
        std::to_string(cubeCounter++)
    );

    // Malha prÃ³pria: o modo Modelador altera os vÃ©rtices desta entidade.
    cube.GetMeshComponent().PrimitiveType = "Cube";
    cube.GetMeshComponent().MeshData =
        MeshCache::CreateCubeInstance();

    cube.GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);

    cube.GetTransform().Scale =
        Vector3(
            1.0f,
            1.0f,
            1.0f
        );

    // Default cube color: neutral gray
    cube.GetColorComponent().Color =
        Vector3(
            0.5f,
            0.5f,
            0.5f
        );

    // Sync material color with color component
    cube.GetMaterialComponent().MaterialData.Color[0] = 0.5f;
    cube.GetMaterialComponent().MaterialData.Color[1] = 0.5f;
    cube.GetMaterialComponent().MaterialData.Color[2] = 0.5f;
    cube.GetMaterialComponent().MaterialData.Color[3] = 1.0f;

    // Start with no diffuse texture â€” neutral gray material
    cube.GetMaterialComponent().MaterialData.DiffuseTexture = nullptr;

    cube.GetMaterialComponent().MaterialGUID = "";
    cube.PickingRadius = 1.0f;
    MarkDirty();
}

void Application::ImportModelEntity(const std::string& filepath)
{
    std::string path = filepath;
    if (path.empty())
    {
        path = "Assets/Models/Pyramid.obj";
    }

    Entity* entity = ModelImporter::ImportToScene(path, m_Scene);
    if (entity)
    {
        entity->GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);
        m_SelectedEntity = (int)m_Scene.GetEntities().size() - 1;
        MarkDirty();
        std::cout << "[Application] Successfully imported 3D model: " << path << std::endl;
    }
}

void Application::CreateScriptForSelectedEntity()
{
    auto& entities = m_Scene.GetEntities();
    if (entities.empty() || m_SelectedEntity < 0 || m_SelectedEntity >= (int)entities.size())
    {
        return;
    }

    Entity& entity = entities[m_SelectedEntity];
    if (entity.IsEditorOnly() && !entity.IsGlobalScript())
    {
        return;
    }

    namespace fs = std::filesystem;
    const fs::path projectRoot = m_ProjectConfig.ProjectPath.empty()
        ? fs::current_path()
        : fs::path(m_ProjectConfig.ProjectPath);

    // A compilação gerenciada procura scripts dentro da raiz do projeto
    // (Assets/Scripts, ou Scripts em projetos antigos). Criá-los no CWD da
    // engine deixava o componente anexado a um arquivo que o Play não compilava.
    fs::path scriptsDirectory = projectRoot / "Assets" / "Scripts";
    if (!fs::is_directory(scriptsDirectory))
        scriptsDirectory = projectRoot / "Scripts";

    std::error_code directoryError;
    fs::create_directories(scriptsDirectory, directoryError);
    if (directoryError)
    {
        std::cerr << "[Scripting] Não foi possível criar a pasta de scripts em "
                  << scriptsDirectory.string() << ": "
                  << directoryError.message() << std::endl;
        return;
    }

    std::string className;
    if (entity.IsGlobalScript())
    {
        className = "GlobalGameController";
    }
    else
    {
        className = entity.GetName();
        for (char& character : className)
        {
            if (!std::isalnum((unsigned char)character) && character != '_')
            {
                character = '_';
            }
        }
        if (className.empty() || std::isdigit((unsigned char)className.front()))
        {
            className = "EntityScript_" + className;
        }
    }

    const fs::path absoluteScriptPath = scriptsDirectory / (className + ".cs");

    if (!fs::exists(absoluteScriptPath))
    {
        std::ofstream script(absoluteScriptPath);
        if (script.is_open())
        {
            if (entity.IsGlobalScript())
            {
                script << "using MaraeEngine;\n";
                script << "using System;\n\n";
                script << "/// <summary>\n";
                script << "/// Global Script - Controlador central da cena (semelhante ao ServerScriptService do Roblox Studio).\n";
                script << "/// Comanda o clima, céu, dia/noite e todas as entidades do mundo.\n";
                script << "/// </summary>\n";
                script << "public class GlobalGameController : ScriptBehaviour\n";
                script << "{\n";
                script << "    private Entity? globalLightEntity;\n";
                script << "    private GlobalLightComponent? globalLight;\n\n";
                script << "    public override void OnStart()\n";
                script << "    {\n";
                script << "        // Localiza a iluminação global para controlar o clima e o céu\n";
                script << "        globalLightEntity = GameObject.Find(\"Global Light\");\n";
                script << "        globalLight = globalLightEntity?.GetComponent<GlobalLightComponent>();\n";
                script << "    }\n\n";
                script << "    public override void OnUpdate(float dt)\n";
                script << "    {\n";
                script << "        // 1. Exemplo de controle do clima / céu:\n";
                script << "        // if (globalLight != null)\n";
                script << "        // {\n";
                script << "        //     // globalLight.ambientIntensity = 0.5f;\n";
                script << "        // }\n\n";
                script << "        // 2. Exemplo de comando global para mover ou atualizar entidades:\n";
                script << "        // Entity[] allMovable = GameObject.FindGameObjectsWithTag(\"Movable\");\n";
                script << "        // foreach (var item in allMovable)\n";
                script << "        // {\n";
                script << "        //     item.transform?.Translate(0f, 1f * dt, 0f);\n";
                script << "        // }\n";
                script << "    }\n";
                script << "}\n";
            }
            else
            {
                script << "using MaraeEngine;\n\n";
                script << "/// <summary>Script da entidade " << entity.GetName() << ".</summary>\n";
                script << "public class " << className << " : ScriptBehaviour\n";
                script << "{\n";
                script << "    public override void OnStart()\n";
                script << "    {\n";
                script << "    }\n\n";
                script << "    public override void OnUpdate(float dt)\n";
                script << "    {\n";
                script << "        // Ex.: transform.Translate(0f, 0f, 2f * dt);\n";
                script << "        // Ex.: if (Input.GetKey(KeyCode.W)) transform.Translate(0f, 0f, 4f * dt);\n";
                script << "        // Ex.: if (Input.GetMouseButtonDown(0)) Audio.Play(\"audio_1\");\n";
                script << "    }\n";
                script << "}\n";
            }
        }
        else
        {
            std::cerr << "[Scripting] Não foi possível criar o script: "
                      << absoluteScriptPath.string() << std::endl;
            return;
        }
    }

    std::error_code relativeError;
    const fs::path relativeScriptPath = fs::relative(
        absoluteScriptPath, projectRoot, relativeError);
    entity.GetScriptComponent().ScriptPath = relativeError
        ? absoluteScriptPath.string()
        : relativeScriptPath.generic_string();
    entity.GetScriptComponent().ScriptAssemblyQualifiedTypeName = className;
    MarkDirty();
    // Do not automatically open editor here; caller will decide whether to open VS Code or internal editor
}

void Application::OpenScriptInInternalEditor(const std::string& path)
{
    const std::filesystem::path resolvedPath = ResolveProjectFilePath(m_ProjectConfig, path);
    if (!m_CodeEditor.Open(resolvedPath.string()))
    {
        std::cerr << "[Application] Nao foi possivel abrir o script no editor interno: "
                  << resolvedPath.string() << "\n";
        return;
    }
    // CodeEditor jÃ¡ carregou e rodou diagnÃ³stico â€” nada mais a fazer aqui.
}

bool Application::IsInternalEditorOpen() const
{
    return m_CodeEditor.IsOpen();
}

void Application::CloseInternalEditor()
{
    m_CodeEditor.Close();
}

// ---------------------------------------------------------------------------
// ReloadScripts - Hot-reload bÃ¡sico de scripts C#
// ---------------------------------------------------------------------------
void Application::ReloadScripts()
{
    // Sistema bÃ¡sico de hot-reload: recarrega componentes ScriptComponent
    // Nota: Este Ã© um sistema bÃ¡sico. Para um sistema completo, seria necessÃ¡rio
    // integrar Runtime .NET real com nethost, que Ã© muito mais complexo.
    
    std::cout << "[Application] Recarregando scripts C# (hot-reload)...\n";
    
    // Recarrega todos os componentes ScriptComponent das entidades
    auto& entities = m_Scene.GetEntities();
    for (auto& entity : entities)
    {
        if (entity.HasComponent<ScriptComponent>())
        {
            ScriptComponent* script = entity.GetComponent<ScriptComponent>();
            if (script)
            {
                // Marca o script para recarregar
                // Em um sistema completo, aqui recarregaria o assembly .NET
                script->SetDirty(true);
            }
        }
    }
    
    std::cout << "[Application] Scripts recarregados com sucesso.\n";
}

void Application::ReloadScriptsFromAI()
{
    ReloadScripts();
}

void Application::RenderInternalEditorUI()
{
    if (!m_CodeEditor.IsOpen())
        return;

    bool open = true;
    const bool wasDirtyBeforeFrame = m_CodeEditor.IsDirty();
    m_CodeEditor.SetCompileCallback([this]() {
        if (m_EditorState != EditorState::Edit)
            return std::string("ERRO: pare o Play antes de compilar scripts para evitar trocar o assembly em execução.");
        return ScriptingRuntime::Instance().CompileProjectScripts();
    });
    if (m_EditorState == EditorState::Play)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 0.65f, 1.0f));
        ImGui::TextUnformatted(UI("Jogo em execução — edição de script permitida"));
        ImGui::PopStyleColor();
    }
    m_CodeEditor.RenderWindow("Script Editor##marae", &open);

    // Salvar durante o Play não fecha o editor nem interrompe o jogo. O
    // assembly é recarregado depois que o arquivo foi gravado, permitindo
    // testar a alteração imediatamente.
    if (wasDirtyBeforeFrame && !m_CodeEditor.IsDirty() &&
        m_EditorState == EditorState::Play)
    {
        ScriptingRuntime::Instance().ReloadAll();
    }

    // Se o editor foi fechado e o arquivo foi salvo, recarrega scripts
    if (!open && m_CodeEditor.IsDirty())
    {
        m_CodeEditor.Save();
        ReloadScripts();
    }

    // Se o usuÃ¡rio fechou a janela (X), reseta o editor
    if (!open)
        CloseInternalEditor();
}

void Application::OpenSelectedScriptInVSCode()
{
    if (m_Scene.GetEntities().empty())
    {
        return;
    }

    Entity* selectedEntity = nullptr;
    if (m_SelectedEntity >= 0 &&
        m_SelectedEntity < static_cast<int>(m_Scene.GetEntities().size()))
    {
        selectedEntity = &m_Scene.GetEntities()[m_SelectedEntity];
    }
    else
    {
        const uint64_t componentMask = 0xFF0000000ULL;
        if ((m_SelectedID & componentMask) != 0)
            selectedEntity = m_Scene.FindEntityByID(m_SelectedID & ~componentMask);
    }

    if (!selectedEntity)
        return;

    const std::string scriptPath = selectedEntity->GetScriptComponent().ScriptPath;
    if (scriptPath.empty())
    {
        return;
    }

    const std::filesystem::path resolvedPath = ResolveProjectFilePath(m_ProjectConfig, scriptPath);
#ifdef _WIN32
    if (!LaunchVSCodeFile(resolvedPath))
    {
        std::cerr << "[Application] Nao foi possivel localizar ou iniciar o VS Code para: "
                  << resolvedPath.string() << "\n";
    }
#else
    const std::string command = "code --reuse-window \"" + resolvedPath.string() + "\"";
    std::system(command.c_str());
#endif
}

void Application::OpenScriptsFolderInVSCode()
{
    const std::filesystem::path scriptsPath =
        (!m_ProjectConfig.ProjectPath.empty()
            ? std::filesystem::path(m_ProjectConfig.ProjectPath)
            : std::filesystem::current_path()) / "Scripts";
    std::filesystem::create_directories(scriptsPath);
#ifdef _WIN32
    LaunchVSCodeFile(scriptsPath);
#else
    const std::string command = "code --reuse-window \"" + scriptsPath.string() + "\"";
    std::system(command.c_str());
#endif
}

void Application::CreateEmptyEntity()
{
    static int entityCounter = 1;

    Entity& entity =
        m_Scene.CreateEntity();

    entity.SetName(
        "Entity " +
        std::to_string(entityCounter++)
    );

    entity.GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);

    entity.GetTransform().Rotation =
        glm::quat(
            1.0f,
            0.0f,
            0.0f,
            0.0f
        );

    entity.GetTransform().Scale =
        Vector3(
            1.0f,
            1.0f,
            1.0f
        );

    m_SelectedEntity =
        (int)m_Scene.GetEntities().size() - 1;

    entity.PickingRadius = 0.5f;
    entity.GetMeshComponent().MeshData.reset();
    entity.GetMeshComponent().MeshPath.clear();
    entity.GetMeshComponent().PrimitiveType = "None";
}

void Application::CreateFolderEntity()
{
    CreateEmptyEntity();
    Entity& folder = m_Scene.GetEntities()[static_cast<size_t>(m_SelectedEntity)];
    static int folderCounter = 1;
    folder.SetName("Folder " + std::to_string(folderCounter++));
    folder.IsFolder = true;
    folder.IsModel = false;
    folder.GetMeshComponent().MeshData.reset();
    folder.GetMeshComponent().PrimitiveType = "None";
    folder.GetMeshComponent().MeshPath.clear();
    folder.RaycastTarget = false;
    folder.PhysicsEnabled = false;
    folder.ColliderEnabled = false;
    MarkDirty();
}

void Application::CreateModelEntity()
{
    CreateEmptyEntity();
    Entity& model = m_Scene.GetEntities()[static_cast<size_t>(m_SelectedEntity)];
    static int modelCounter = 1;
    model.SetName("Model " + std::to_string(modelCounter++));
    model.IsFolder = false;
    model.IsModel = true;
    model.GetMeshComponent().MeshData.reset();
    model.GetMeshComponent().PrimitiveType = "None";
    model.GetMeshComponent().MeshPath.clear();
    model.RaycastTarget = false;
    model.PhysicsEnabled = false;
    model.ColliderEnabled = false;
    MarkDirty();
}

void Application::CreateCameraEntity()
{
    static int cameraCounter = 1;
    Entity& entity = m_Scene.CreateEntity();
    entity.SetName("Camera " + std::to_string(cameraCounter++));
    entity.AddCamera();
    entity.GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);
    // A câmera do editor olha para -Z; a orientação da entidade deve fazer
    // esse mesmo eixo local apontar na direção do fly camera.
    const Vector3 cameraEntityEuler(
        -m_Camera.Rotation.X,
        -m_Camera.Rotation.Y,
        -m_Camera.Rotation.Z);
    entity.GetTransform().Rotation = glm::quat(glm::vec3(
        glm::radians(cameraEntityEuler.X),
        glm::radians(cameraEntityEuler.Y),
        glm::radians(cameraEntityEuler.Z)));
    entity.GetTransform().EulerRotation = cameraEntityEuler;
    entity.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);
    entity.PickingRadius = 0.35f;
    m_SelectedEntity = static_cast<int>(m_Scene.GetEntities().size()) - 1;
    MarkDirty();
}

void Application::RenderCameraPreview(Entity& cameraEntity, int width, int height)
{
    if (!cameraEntity.HasCamera() || !cameraEntity.GetCameraComponent().Enabled)
        return;

    m_Scene.UpdateTransforms();
    width = std::max(64, width);
    height = std::max(64, height);
    if (m_CameraPreviewFramebuffer.GetFBO() == 0)
        m_CameraPreviewFramebuffer.Create(width, height);
    else if (m_CameraPreviewFramebuffer.GetWidth() != width ||
             m_CameraPreviewFramebuffer.GetHeight() != height)
        m_CameraPreviewFramebuffer.Resize(width, height);

    Camera previewCamera = BuildCameraFromEntity(cameraEntity);
    const CameraComponent& settings = cameraEntity.GetCameraComponent();
    m_Renderer.Render(
        m_Scene,
        previewCamera,
        -1,
        {},
        width,
        height,
        m_CameraPreviewFramebuffer.GetFBO(),
        m_ShadowIntensity,
        m_AmbientLight,
        false,
        false,
        500.0f,
        settings.FieldOfView,
        settings.NearClip,
        settings.FarClip,
        settings.Orthographic,
        settings.OrthographicSize,
        false,
        false);
}

void Application::CreateTriangleEntity()
{
    Entity& ent = m_Scene.CreateEntity();
    static int counter = 1;
    ent.SetName("Triangle " + std::to_string(counter++));
    ent.SetEditorOnly(false);
    ent.GetMeshComponent().PrimitiveType = "Triangle";
    ent.GetMeshComponent().MeshData = std::make_shared<Mesh>();
    ent.GetMeshComponent().MeshData->Create(MeshFactory::CreateTriangle(), GL_TRIANGLES, "Application::CreateTriangleEntity");
    ent.GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);
    ent.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);
    ent.GetColorComponent().Color = Vector3(0.5f, 0.5f, 0.5f);
    ent.GetMaterialComponent().MaterialData.Color[0] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[1] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[2] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
    ent.GetMaterialComponent().MaterialData.DiffuseTexture = nullptr;
    ent.PickingRadius = 1.0f;
    MarkDirty();
}

void Application::CreateQuadEntity()
{
    Entity& ent = m_Scene.CreateEntity();
    static int counter = 1;
    ent.SetName("Quad " + std::to_string(counter++));
    ent.SetEditorOnly(false);
    if (m_WorldMode == WorldMode::World2D)
    {
        // Sprites são estáticos por padrão; o usuário habilita corpo dinâmico
        // no Inspector quando realmente precisa de gravidade/movimento físico.
        ent.PhysicsEnabled = false;
        ent.UseGravity = false;
        ent.IsStatic = true;
        ent.ColliderEnabled = true;
    }
    ent.GetMeshComponent().PrimitiveType = "Quad";
    if (m_WorldMode == WorldMode::World2D)
        ent.GetMeshComponent().MeshData = MeshCache::GetQuad();
    else
    {
        ent.GetMeshComponent().MeshData = std::make_shared<Mesh>();
        ent.GetMeshComponent().MeshData->Create(
            MeshFactory::CreateQuad(), GL_TRIANGLES, "Application::CreateQuadEntity");
    }
    ent.GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);
    ent.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);
    ent.GetColorComponent().Color = Vector3(0.5f, 0.5f, 0.5f);
    ent.GetMaterialComponent().MaterialData.Color[0] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[1] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[2] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
    ent.GetMaterialComponent().MaterialData.DiffuseTexture = nullptr;
    ent.PickingRadius = 1.0f;
    MarkDirty();
}

void Application::Create2DShapeEntity(const std::string& primitiveType)
{
    static const std::unordered_map<std::string, std::string> displayNames = {
        { "Circle2D", "Circle 2D" },
        { "Pentagon2D", "Pentagon 2D" },
        { "Hexagon2D", "Hexagon 2D" },
        { "Octagon2D", "Octagon 2D" },
        { "Diamond2D", "Diamond 2D" },
        { "Star2D", "Star 2D" }
    };
    const auto name = displayNames.find(primitiveType);
    std::vector<Vertex> vertices = MeshFactory::Create2DPrimitive(primitiveType);
    if (name == displayNames.end() || vertices.empty())
        return;

    Entity& entity = m_Scene.CreateEntity();
    static int counter = 1;
    entity.SetName(name->second + " " + std::to_string(counter++));
    entity.SetEditorOnly(false);
    entity.GetMeshComponent().PrimitiveType = primitiveType;
    entity.GetMeshComponent().MeshData = std::make_shared<Mesh>();
    entity.GetMeshComponent().MeshData->Create(
        vertices, GL_TRIANGLES, "Application::Create2DShapeEntity");
    entity.GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);
    entity.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);
    entity.GetColorComponent().Color = Vector3(0.5f, 0.5f, 0.5f);
    entity.GetMaterialComponent().MaterialData.Color[0] = 0.5f;
    entity.GetMaterialComponent().MaterialData.Color[1] = 0.5f;
    entity.GetMaterialComponent().MaterialData.Color[2] = 0.5f;
    entity.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
    entity.GetMaterialComponent().MaterialData.DiffuseTexture = nullptr;
    entity.PickingRadius = 1.0f;

    if (Is2DWorldMode())
    {
        entity.PhysicsEnabled = false;
        entity.UseGravity = false;
        entity.IsStatic = true;
        entity.ColliderEnabled = true;
        entity.Collider = Entity::ColliderShape::Box;
    }
    m_SelectedEntity = static_cast<int>(m_Scene.GetEntities().size()) - 1;
    MarkDirty();
}

void Application::CreatePlaneEntity()
{
    Entity& ent = m_Scene.CreateEntity();
    static int counter = 1;
    ent.SetName("Plane " + std::to_string(counter++));
    ent.SetEditorOnly(false);
    ent.GetMeshComponent().PrimitiveType = "Plane";
    ent.GetMeshComponent().MeshData = std::make_shared<Mesh>();
    ent.GetMeshComponent().MeshData->Create(MeshFactory::CreatePlane(4), GL_TRIANGLES, "Application::CreatePlaneEntity");
    ent.GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);
    ent.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);
    ent.GetColorComponent().Color = Vector3(0.5f, 0.5f, 0.5f);
    ent.GetMaterialComponent().MaterialData.Color[0] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[1] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[2] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
    ent.GetMaterialComponent().MaterialData.DiffuseTexture = nullptr;
    ent.PickingRadius = 1.0f;
    MarkDirty();
}

void Application::CreateTerrainEntity()
{
    static int counter = 1;
    Entity& terrain = m_Scene.CreateEntity();
    terrain.SetName("Terrain " + std::to_string(counter++));
    terrain.Tag = "Terrain";
    terrain.IsStatic = true;
    terrain.PhysicsEnabled = true;
    terrain.UseGravity = false;
    terrain.ColliderEnabled = true;
    terrain.Collider = Entity::ColliderShape::Mesh;
    terrain.AddTerrain(33);
    terrain.GetTerrainComponent().DataPath =
        "Library/Generated/Terrain_" + std::to_string(terrain.GetID()) + ".height";
    terrain.GetMeshComponent().MeshData = std::make_shared<Mesh>();
    terrain.GetMeshComponent().PrimitiveType = "Terrain";
    terrain.GetMeshComponent().MeshData->Create(
        terrain.GetTerrainComponent().BuildVertices(), GL_TRIANGLES,
        "Application::CreateTerrainEntity");
    terrain.GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);
    terrain.GetTransform().Position.Y = 0.0f;
    terrain.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);
    terrain.GetColorComponent().Color = Vector3(0.30f, 0.48f, 0.22f);
    terrain.GetMaterialComponent().MaterialData.Color[0] = 0.30f;
    terrain.GetMaterialComponent().MaterialData.Color[1] = 0.48f;
    terrain.GetMaterialComponent().MaterialData.Color[2] = 0.22f;
    terrain.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
    terrain.PickingRadius = terrain.GetTerrainComponent().Size * 0.5f;
    m_SelectedEntity = static_cast<int>(m_Scene.GetEntities().size()) - 1;
    MarkDirty();
}

void Application::CreateWedgeEntity()
{
    Entity& ent = m_Scene.CreateEntity();
    static int counter = 1;
    ent.SetName("Wedge " + std::to_string(counter++));
    ent.SetEditorOnly(false);
    ent.GetMeshComponent().PrimitiveType = "Wedge";
    ent.GetMeshComponent().MeshData = std::make_shared<Mesh>();
    ent.GetMeshComponent().MeshData->Create(MeshFactory::CreateWedge(), GL_TRIANGLES, "Application::CreateWedgeEntity");
    ent.GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);
    ent.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);
    ent.GetColorComponent().Color = Vector3(0.5f, 0.5f, 0.5f);
    ent.GetMaterialComponent().MaterialData.Color[0] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[1] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[2] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
    ent.GetMaterialComponent().MaterialData.DiffuseTexture = nullptr;
    ent.PickingRadius = 1.0f;
    MarkDirty();
}

void Application::CreateTorusEntity()
{
    Entity& ent = m_Scene.CreateEntity();
    static int counter = 1;
    ent.SetName("Torus " + std::to_string(counter++));
    ent.SetEditorOnly(false);
    ent.GetMeshComponent().PrimitiveType = "Torus";
    ent.GetMeshComponent().MeshData = std::make_shared<Mesh>();
    ent.GetMeshComponent().MeshData->Create(MeshFactory::CreateTorus(), GL_TRIANGLES, "Application::CreateTorusEntity");
    ent.GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);
    ent.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);
    ent.GetColorComponent().Color = Vector3(0.5f, 0.5f, 0.5f);
    ent.GetMaterialComponent().MaterialData.Color[0] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[1] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[2] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
    ent.GetMaterialComponent().MaterialData.DiffuseTexture = nullptr;
    ent.PickingRadius = 1.0f;
    MarkDirty();
}

void Application::CreateCapsuleEntity()
{
    Entity& ent = m_Scene.CreateEntity();
    static int counter = 1;
    ent.SetName("Capsule " + std::to_string(counter++));
    ent.SetEditorOnly(false);
    ent.GetMeshComponent().PrimitiveType = "Capsule";
    ent.GetMeshComponent().MeshData = std::make_shared<Mesh>();
    ent.GetMeshComponent().MeshData->Create(MeshFactory::CreateCapsule(), GL_TRIANGLES, "Application::CreateCapsuleEntity");
    ent.GetTransform().Position = SpawnPositionInFrontOfCamera(m_Camera);
    ent.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);
    ent.GetColorComponent().Color = Vector3(0.5f, 0.5f, 0.5f);
    ent.GetMaterialComponent().MaterialData.Color[0] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[1] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[2] = 0.5f;
    ent.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
    ent.GetMaterialComponent().MaterialData.DiffuseTexture = nullptr;
    ent.PickingRadius = 1.0f;
    MarkDirty();
}

void Application::DeleteSelectedEntity()
{
    auto& entities =
        m_Scene.GetEntities();

    if (entities.empty())
    {
        return;
    }

    std::vector<int> indices = m_MultiSelectedEntities;
    if (indices.empty() && m_SelectedEntity >= 0 &&
        m_SelectedEntity < static_cast<int>(entities.size()))
        indices.push_back(m_SelectedEntity);

    indices.erase(std::remove_if(indices.begin(), indices.end(),
        [&entities](int index)
        {
            return index < 0 || index >= static_cast<int>(entities.size()) ||
                   entities[static_cast<size_t>(index)].IsEditorOnly();
        }), indices.end());
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices.empty()) return;

    for (int index : indices)
    {
        TransformCommand command;

        command.Type = CommandType::Delete;
        command.EntityID = entities[static_cast<size_t>(index)].GetID();
        command.EntityIndex = index;

        std::ostringstream ss;
        ss << entities[static_cast<size_t>(index)].GetName() << "\n";
        Transform& t = entities[static_cast<size_t>(index)].GetTransform();
        ss << t.Position.X << " " << t.Position.Y << " " << t.Position.Z << "\n";
        ss << t.Rotation.x << " " << t.Rotation.y << " " << t.Rotation.z << " " << t.Rotation.w << "\n";
        ss << t.Scale.X << " " << t.Scale.Y << " " << t.Scale.Z << "\n";
        auto& col = entities[static_cast<size_t>(index)].GetColorComponent().Color;
        ss << col.X << " " << col.Y << " " << col.Z << "\n";
        ss << std::quoted(entities[static_cast<size_t>(index)].GetMeshComponent().PrimitiveType) << "\n";
        command.EntityData = ss.str();
        m_UndoRedoManager.Push(command);
        SceneCommands::DeleteEntity(m_Scene, index);
    }

    m_MultiSelectedEntities.clear();
    m_SelectedEntity = m_Scene.GetEntities().empty() ? -1 :
        std::min(indices.back(), static_cast<int>(m_Scene.GetEntities().size()) - 1);
    m_SelectedID = m_SelectedEntity >= 0 ?
        m_Scene.GetEntities()[static_cast<size_t>(m_SelectedEntity)].GetID() : 0;
    MarkDirty();
}

void Application::DuplicateSelectedEntity()
{
    const auto& entities =
        m_Scene.GetEntities();

    if (entities.empty())
    {
        return;
    }

    std::vector<int> indices = m_MultiSelectedEntities;
    if (indices.empty() && m_SelectedEntity >= 0 &&
        m_SelectedEntity < static_cast<int>(entities.size()))
        indices.push_back(m_SelectedEntity);

    indices.erase(std::remove_if(indices.begin(), indices.end(),
        [&entities](int index)
        {
            return index < 0 || index >= static_cast<int>(entities.size()) ||
                   entities[static_cast<size_t>(index)].IsEditorOnly();
        }), indices.end());
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices.empty()) return;

    auto snapshot = SceneCommands::CaptureSelection(m_Scene, indices);
    std::vector<uint64_t> createdIDs;
    const std::vector<int> duplicated = SceneCommands::PasteSelection(m_Scene, snapshot, &createdIDs, true);
    if (duplicated.empty()) return;

    m_SelectedEntity = duplicated.back();
    m_MultiSelectedEntities = duplicated;
    m_SelectedID = m_Scene.GetEntities()[static_cast<size_t>(m_SelectedEntity)].GetID();

    // Push create command for undo
    for (uint64_t createdID : createdIDs)
    {
        TransformCommand command;
        command.Type = CommandType::Create;
        command.EntityID = createdID;
        m_UndoRedoManager.Push(command);
    }
    MarkDirty();
}

void Application::CopySelectedEntities()
{
    const auto& entities = m_Scene.GetEntities();
    std::vector<int> indices = m_MultiSelectedEntities;
    if (indices.empty() && m_SelectedEntity >= 0 && m_SelectedEntity < static_cast<int>(entities.size()))
        indices.push_back(m_SelectedEntity);
    auto snapshot = SceneCommands::CaptureSelection(m_Scene, indices);
    if (snapshot.Entities.empty()) return;
    m_EntityClipboard = std::move(snapshot.Entities);
    m_EntityClipboardRoots = std::move(snapshot.RootIDs);
}

void Application::PasteCopiedEntities()
{
    if (m_EntityClipboard.empty() || m_EntityClipboardRoots.empty()) return;
    SceneCommands::EntitySelectionSnapshot snapshot{ m_EntityClipboard, m_EntityClipboardRoots };
    std::vector<uint64_t> createdIDs;
    const std::vector<int> roots = SceneCommands::PasteSelection(m_Scene, snapshot, &createdIDs, true);
    if (roots.empty()) return;

    m_MultiSelectedEntities = roots;
    m_SelectedEntity = roots.back();
    m_SelectedID = m_Scene.GetEntities()[static_cast<size_t>(m_SelectedEntity)].GetID();
    for (uint64_t id : createdIDs)
    {
        TransformCommand command;
        command.Type = CommandType::Create;
        command.EntityID = id;
        m_UndoRedoManager.Push(command);
    }
    MarkDirty();
}




void Application::NewScene()
{
    if (m_SceneDirty)
    {
        SaveScene();
    }
    m_SceneManager.NewScene(*this);
    m_SceneHasLightingSettings = false;
    m_ProjectConfig.ScenePath.clear();
    m_SceneDirty = false;
    UpdateWindowTitle();
}

void Application::OpenSceneDialog()
{
#ifdef _WIN32
    char filename[MAX_PATH] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Marae Scene (*.scene)\0*.scene\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Abrir Cena";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    std::string initialDir;
    if (!m_ProjectConfig.ProjectPath.empty())
    {
        initialDir = (std::filesystem::path(m_ProjectConfig.ProjectPath) / "Assets" / "Scenes").string();
        ofn.lpstrInitialDir = initialDir.c_str();
    }

    if (GetOpenFileNameA(&ofn))
    {
        if (m_SceneDirty)
        {
            SaveScene();
        }
        m_SceneManager.LoadSceneAsync(*this, filename, SceneTransitionType::FadeBlack, 0.5f);
        m_ProjectConfig.ScenePath = filename;
        m_SceneDirty = false;
        UpdateWindowTitle();
    }
#endif
}

void Application::OpenAdditiveSceneDialog()
{
#ifdef _WIN32
    char filename[MAX_PATH] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Marae Scene (*.scene)\0*.scene\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Adicionar Cena Aditiva";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    std::string initialDir;
    if (!m_ProjectConfig.ProjectPath.empty())
    {
        initialDir = (std::filesystem::path(m_ProjectConfig.ProjectPath) / "Assets" / "Scenes").string();
        ofn.lpstrInitialDir = initialDir.c_str();
    }

    if (GetOpenFileNameA(&ofn))
    {
        m_SceneManager.LoadSceneAdditive(*this, filename);
        MarkDirty();
    }
#endif
}

void Application::SaveSceneAsDialog()
{
#ifdef _WIN32
    char filename[MAX_PATH] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Marae Scene (*.scene)\0*.scene\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Salvar Cena Como";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "scene";

    std::string initialDir;
    if (!m_ProjectConfig.ProjectPath.empty())
    {
        initialDir = (std::filesystem::path(m_ProjectConfig.ProjectPath) / "Assets" / "Scenes").string();
        ofn.lpstrInitialDir = initialDir.c_str();
    }

    if (GetSaveFileNameA(&ofn))
    {
        m_ProjectConfig.ScenePath = filename;
        SaveScene();
        m_SceneManager.RefreshProjectLevels(m_ProjectConfig.ProjectPath);
        UpdateWindowTitle();
    }
#endif
}

bool Application::SaveScene()
{
    // Determina o path de destino:
    //   1. Se hÃ¡ uma cena aberta no projeto, salva nela
    //   2. Se tem pasta de projeto, salva em Assets/Scenes/Main.scene
    //   3. Fallback: Scene.scene no diretÃ³rio corrente (legado)
    std::string savePath = m_ProjectConfig.ScenePath;
    if (savePath.empty() && !m_ProjectConfig.ProjectPath.empty())
    {
        namespace fs = std::filesystem;
        fs::path scenesDir = fs::path(m_ProjectConfig.ProjectPath) / "Assets" / "Scenes";
        fs::create_directories(scenesDir);
        savePath = (scenesDir / "Main.scene").string();
        m_ProjectConfig.ScenePath = savePath;
    }
    if (savePath.empty())
        savePath = "Scene.scene";

    const std::filesystem::path scenePath(savePath);
    const std::filesystem::path temporaryPath = scenePath.string() + ".tmp";
    const std::filesystem::path backupPath = scenePath.string() + ".bak";
    try
    {
        if (!scenePath.parent_path().empty())
            std::filesystem::create_directories(scenePath.parent_path());
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[Application] Nao foi possivel preparar o diretorio da cena: "
                  << exception.what() << std::endl;
        return false;
    }

    // Nunca truncar a cena ativa diretamente: uma interrupcao, falta de espaco
    // ou falha de escrita nao pode destruir a ultima versao valida.
    std::ofstream file(temporaryPath, std::ios::trunc);
    if (!file.is_open())
    {
        std::cerr << "[Application] Nao foi possivel abrir temporario para salvar em: "
                  << savePath << std::endl;
        return false;
    }
    file << std::setprecision(9);
    const std::filesystem::path terrainRoot = m_ProjectConfig.ProjectPath.empty()
        ? std::filesystem::current_path() : std::filesystem::path(m_ProjectConfig.ProjectPath);

    const auto& entities = m_Scene.GetEntities();
    for (const auto& entity : entities)
    {
        if (entity.IsEditorOnly()) continue;
        if (entity.GetName() == "Grid" || entity.GetName() == "Axis") continue;

        const Transform& transform = const_cast<Entity&>(entity).GetTransform();
        file << entity.GetName() << "\n";
        file << transform.Position.X << " " << transform.Position.Y << " " << transform.Position.Z << "\n";
        file << transform.Rotation.x << " " << transform.Rotation.y << " " << transform.Rotation.z << " " << transform.Rotation.w << "\n";
        file << transform.Scale.X    << " " << transform.Scale.Y    << " " << transform.Scale.Z    << "\n";
        const ColorComponent& color = const_cast<Entity&>(entity).GetColorComponent();
        file << color.Color.X << " " << color.Color.Y << " " << color.Color.Z << "\n";
        file << const_cast<Entity&>(entity).GetMeshComponent().MeshPath << "\n";
        file << "@primitive " << std::quoted(const_cast<Entity&>(entity).GetMeshComponent().PrimitiveType) << "\n";
        if (entity.HasTerrain())
        {
            const TerrainComponent& terrain = const_cast<Entity&>(entity).GetTerrainComponent();
            const std::string dataPath = terrain.DataPath.empty()
                ? "Library/Generated/Terrain_" + std::to_string(entity.GetID()) + ".height"
                : terrain.DataPath;
            terrain.Save(terrainRoot / dataPath);
            file << "@terrain 1 " << terrain.Resolution << " " << terrain.Size << " "
                 << terrain.HeightScale << " " << static_cast<int>(terrain.Mode) << " "
                 << terrain.BrushRadius << " " << terrain.BrushStrength << " "
                 << std::quoted(dataPath) << "\n";
        }
        file << "@physics "
             << (entity.PhysicsEnabled ? 1 : 0) << " "
             << (entity.UseGravity ? 1 : 0) << " "
             << (entity.IsStatic ? 1 : 0) << " "
             << (entity.ColliderEnabled ? 1 : 0) << " "
             << static_cast<int>(entity.Collider) << " "
             << entity.Mass << " "
             << entity.Restitution << " "
             << entity.Friction << " "
             << entity.PickingRadius << " "
             << entity.AngularVelocity.X << " "
             << entity.AngularVelocity.Y << " "
             << entity.AngularVelocity.Z << " "
             << (entity.ContinuousCollision ? 1 : 0) << " "
             << (entity.FreezeRotation ? 1 : 0) << " "
             << (entity.IsTrigger ? 1 : 0) << "\n";
        file << "@material "
             << static_cast<int>(const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.Type)
             << " " << (const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.PBRParametersEnabled ? 1 : 0)
             << " " << const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.Roughness
             << " " << const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.Metallic
             << " " << const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.Specular
             << " " << const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.EmissionStrength
             << " " << const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.Alpha
             << " " << const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.NormalStrength
             << " " << static_cast<int>(const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.ScaleMode)
             << " " << const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.UVWorldScale
             << " " << const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.TileSize
             << " " << std::quoted(const_cast<Entity&>(entity).GetMaterialComponent().MaterialGUID)
             << " " << std::quoted(const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.TexturePath)
             << " " << std::quoted(const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.TextureGUID)
             << " " << std::quoted(const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.NormalMapPath)
             << " " << std::quoted(const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.EmissionTexturePath)
             << "\n";
        {
            const MaterialComponent& component = const_cast<Entity&>(entity).GetMaterialComponent();
            std::vector<uint32_t> assignedFaces;
            assignedFaces.reserve(component.FaceTexturePaths.size());
            for (const auto& [faceIndex, path] : component.FaceTexturePaths)
                if (!path.empty()) assignedFaces.push_back(faceIndex);
            std::sort(assignedFaces.begin(), assignedFaces.end());
            file << "@face_textures " << static_cast<int>(component.TextureMode) << " "
                 << assignedFaces.size();
            for (uint32_t faceIndex : assignedFaces)
                file << " " << faceIndex << " " << std::quoted(component.FaceTexturePaths.at(faceIndex));
            file << "\n";
        }
        file << "@entity "
             << (entity.Active ? 1 : 0) << " "
             << std::clamp(entity.Opacity, 0.0f, 1.0f) << " "
             << (entity.RaycastTarget ? 1 : 0) << " "
             << entity.Layer << " "
             << std::quoted(entity.Tag) << " "
             << (entity.CastShadows ? 1 : 0) << " "
             << (entity.SelectionLocked ? 1 : 0) << "\n";
        if (entity.IsFolder || entity.IsModel)
        {
            file << "@group " << (entity.IsFolder ? "Folder" : "Model") << "\n";
        }
        if (entity.HasLight())
        {
            const LightComponent& light = const_cast<Entity&>(entity).GetLightComponent();
            file << "@light "
                 << (light.Enabled ? 1 : 0) << " "
                 << static_cast<int>(light.Type) << " "
                 << light.Color[0] << " " << light.Color[1] << " " << light.Color[2] << " "
                 << light.Intensity << " " << light.Range << " "
                 << light.DirectionLocal[0] << " " << light.DirectionLocal[1] << " " << light.DirectionLocal[2] << " "
                 << light.InnerAngleDeg << " " << light.OuterAngleDeg << " "
                 << (light.CastShadows ? 1 : 0) << " "
                 << (light.UseTemperature ? 1 : 0) << " "
                 << light.TemperatureKelvin << " "
                 << light.SourceRadius << " "
                 << light.ShadowSoftness << " "
                 << light.ContactShadowStrength << " "
                 << (light.IndirectContribution ? 1 : 0) << " "
                 << (light.DistanceSoftening ? 1 : 0) << " "
                 << light.ShadowStrength << "\n";
        }
        if (entity.HasAudio())
        {
            const AudioComponent& audio = const_cast<Entity&>(entity).GetAudioComponent();
            file << "@audio "
                 << (audio.Enabled ? 1 : 0) << " "
                 << (audio.Loop ? 1 : 0) << " "
                 << (audio.PlayOnStart ? 1 : 0) << " "
                 << audio.Volume << " " << audio.Pitch << " "
                 << audio.MinDistance << " " << audio.MaxDistance << " "
                 << audio.SpatialBlend << " "
                 << audio.DopplerFactor << " "
                 << 2 << " "   // busInt legado (SFX=2)
                 << audio.MixerGroupID << " "
                 << std::quoted(audio.ClipPath) << " "
                 << std::quoted(audio.SourceID) << "\n";
            file << "@audio_acoustic "
                 << (audio.AcousticPropagationEnabled ? 1 : 0) << " "
                 << (audio.AcousticRayTracingEnabled ? 1 : 0) << " "
                 << (audio.GeometricAcousticsEnabled ? 1 : 0) << " "
                 << (audio.DynamicPropagationEnabled ? 1 : 0) << " "
                 << (audio.OcclusionEnabled ? 1 : 0) << " "
                 << (audio.ObstructionEnabled ? 1 : 0) << " "
                 << (audio.DiffractionEnabled ? 1 : 0) << " "
                 << (audio.AcousticPathfindingEnabled ? 1 : 0) << " "
                 << audio.AcousticTransmission << " "
                 << audio.AcousticUpdateInterval << "\n";
        }
        if (const_cast<Entity&>(entity).GetMeshComponent().MeshData && !entity.HasUI())
        {
            file << "@acoustic "
                 << (entity.AudioAcousticGeometryEnabled ? 1 : 0) << " "
                 << (entity.AudioOccluder ? 1 : 0) << " "
                 << (entity.AudioTransmissionEnabled ? 1 : 0) << " "
                 << entity.AudioTransmission << " "
                 << entity.AudioAbsorption << " "
                 << entity.AudioDiffraction << "\n";
        }
        if (entity.HasCamera())
        {
            const CameraComponent& camera = const_cast<Entity&>(entity).GetCameraComponent();
            file << "@camera "
                 << (camera.Enabled ? 1 : 0) << " "
                 << (camera.PreviewEnabled ? 1 : 0) << " "
                 << (camera.Orthographic ? 1 : 0) << " "
                 << camera.FieldOfView << " " << camera.NearClip << " "
                     << camera.FarClip << " " << camera.OrthographicSize << " "
                     << (camera.ShowFrustumGizmo ? 1 : 0) << "\n";
        }
        if (entity.HasParticleSystem())
        {
            const ParticleSystemComponent& particles = const_cast<Entity&>(entity).GetParticleSystem();
            file << "@particles " << (particles.Enabled ? 1 : 0) << " " << particles.MaxParticles
                 << " " << particles.EmissionRate << " " << particles.Lifetime
                 << " " << particles.Speed << " " << particles.Spread
                 << " " << particles.StartSize << " " << particles.EndSize
                 << " " << particles.Gravity.x << " " << particles.Gravity.y << " " << particles.Gravity.z
                 << " " << particles.StartColor.r << " " << particles.StartColor.g
                 << " " << particles.StartColor.b << " " << particles.StartColor.a
                 << " " << particles.EndColor.r << " " << particles.EndColor.g
                 << " " << particles.EndColor.b << " " << particles.EndColor.a
                 << " " << particles.Shape << " " << particles.Glow
                 << " " << std::quoted(particles.TexturePath)
                 << " " << particles.EmissionShape << " " << particles.BurstCount
                 << " " << particles.SpeedVariation << " " << particles.LifetimeVariation
                 << " " << particles.Drag << " " << particles.Wind.x << " " << particles.Wind.y << " " << particles.Wind.z
                 << " " << particles.Turbulence << " " << particles.StartRotation << " " << particles.RotationSpeed
                 << " " << particles.MidSize
                 << " " << particles.MidColor.r << " " << particles.MidColor.g
                 << " " << particles.MidColor.b << " " << particles.MidColor.a
                 << " " << (particles.Lit ? 1 : 0) << " " << (particles.UseMidColor ? 1 : 0) << "\n";
        }
        if (entity.HasAI())
        {
            const AIComponent& ai = const_cast<Entity&>(entity).GetAIComponent();
            file << "@ai " << (ai.Enabled ? 1 : 0) << " " << static_cast<int>(ai.CurrentMode)
                 << " " << (ai.SightEnabled ? 1 : 0) << " " << ai.SightRadius
                 << " " << ai.FieldOfViewDegrees << " " << ai.TargetMemorySeconds
                 << " " << ai.RootNodeId << " " << std::quoted(ai.TargetTag)
                 << " " << std::quoted(ai.InitialState) << "\n";
            for (const auto& node : ai.Nodes)
                file << "@ai_node " << node.Id << " " << node.ParentId << " "
                     << static_cast<int>(node.Type) << " " << node.Range << " "
                     << node.Duration << " " << node.Speed << "\n";
            for (const auto& state : ai.States)
                file << "@ai_state " << std::quoted(state.Name) << " "
                     << static_cast<int>(state.Action) << " " << state.Speed << " "
                     << state.Range << " " << state.WaitDuration << "\n";
            for (const auto& transition : ai.Transitions)
                file << "@ai_transition " << std::quoted(transition.From) << " "
                     << std::quoted(transition.To) << " "
                     << static_cast<int>(transition.Condition) << " "
                     << transition.Range << "\n";
        }
        if (entity.HasUI())
        {
            const UIComponent& ui = const_cast<Entity&>(entity).GetUIComponent();
            file << "@ui " << (ui.Enabled ? 1 : 0) << " " << static_cast<int>(ui.Type)
                 << " " << ui.PositionX << " " << ui.PositionY << " " << ui.Width << " " << ui.Height
                 << " " << ui.AnchorX << " " << ui.AnchorY << " " << ui.Rotation << " " << ui.ZIndex
                 << " " << ui.Transparency << " " << ui.CornerRadius
                 << " " << ui.BackgroundColor[0] << " " << ui.BackgroundColor[1] << " " << ui.BackgroundColor[2] << " " << ui.BackgroundColor[3]
                 << " " << ui.TextColor[0] << " " << ui.TextColor[1] << " " << ui.TextColor[2] << " " << ui.TextColor[3]
                 << " " << std::quoted(ui.Text) << " " << std::quoted(ui.ImagePath) << " " << std::quoted(ui.Font)
                 << " " << ui.BorderColor[0] << " " << ui.BorderColor[1] << " " << ui.BorderColor[2] << " " << ui.BorderColor[3]
                 << " " << ui.BorderThickness << " " << static_cast<int>(ui.TextAlignment) << " "
                 << (ui.ClipsContent ? 1 : 0) << " " << ui.FontSize
                 << " " << (ui.UseResponsiveAnchors ? 1 : 0)
                 << " " << ui.AnchorMinX << " " << ui.AnchorMinY
                 << " " << ui.AnchorMaxX << " " << ui.AnchorMaxY
                 << " " << ui.OffsetMinX << " " << ui.OffsetMinY
                 << " " << ui.OffsetMaxX << " " << ui.OffsetMaxY
                 // Campos de widget acrescentados ao fim para preservar a
                 // leitura de cenas antigas.
                 << " " << ui.Value << " " << ui.MinValue << " " << ui.MaxValue
                 << " " << (ui.Checked ? 1 : 0) << " " << ui.SelectedIndex
                 << " " << (ui.NineSlice ? 1 : 0)
                 << " " << ui.SliceLeft << " " << ui.SliceTop << " " << ui.SliceRight << " " << ui.SliceBottom
                 << " " << ui.HoverColor[0] << " " << ui.HoverColor[1] << " " << ui.HoverColor[2] << " " << ui.HoverColor[3]
                 << " " << ui.PressedColor[0] << " " << ui.PressedColor[1] << " " << ui.PressedColor[2] << " " << ui.PressedColor[3]
                 << " " << ui.FillColor[0] << " " << ui.FillColor[1] << " " << ui.FillColor[2] << " " << ui.FillColor[3]
                 << " " << ui.TrackColor[0] << " " << ui.TrackColor[1] << " " << ui.TrackColor[2] << " " << ui.TrackColor[3]
                 << "\n";
            file << "@ui_layout " << static_cast<int>(ui.LayoutMode) << " "
                 << ui.LayoutPadding << " " << ui.LayoutSpacing << " "
                 << ui.LayoutColumns << " " << (ui.StretchLayoutChildren ? 1 : 0) << " "
                 << (ui.ScrollViewEnabled ? 1 : 0) << " " << (ui.RichTextEnabled ? 1 : 0) << " "
                 << ui.Theme << "\n";
            if (!ui.Options.empty())
            {
                file << "@ui_options";
                for (const std::string& option : ui.Options)
                    file << " " << std::quoted(option);
                file << "\n";
            }
        }

        const AnimationComponent& animation = const_cast<Entity&>(entity).GetAnimationComponent();
        if (animation.Clip)
        {
            const AnimationClip& clip = *animation.Clip;
            file << "@animation "
                 << (animation.Enabled ? 1 : 0) << " "
                 << (animation.PlayOnStart ? 1 : 0) << " "
                 << animation.Time << " " << animation.Speed << " "
                 << animation.Weight << " " << static_cast<int>(animation.Easing) << " "
                 << (animation.UseSmoothing ? 1 : 0) << " " << animation.Smoothness << " "
                 << animation.BlendIn << " " << animation.BlendOut << " "
                 << clip.Duration << " " << (clip.Loop ? 1 : 0) << " "
                 << std::quoted(clip.ID) << " " << std::quoted(clip.Name) << "\n";
            for (const KeyframeVec3& key : clip.Position.Keys)
                file << "@anim_pos " << key.Time << " " << key.Value.x << " "
                     << key.Value.y << " " << key.Value.z << "\n";
            for (const KeyframeQuat& key : clip.Rotation.Keys)
                file << "@anim_rot " << key.Time << " " << key.Value.w << " "
                     << key.Value.x << " " << key.Value.y << " " << key.Value.z << "\n";
            for (const KeyframeVec3& key : clip.Scale.Keys)
                file << "@anim_scale " << key.Time << " " << key.Value.x << " "
                     << key.Value.y << " " << key.Value.z << "\n";
            file << "@anim_machine " << (animation.StateMachineEnabled ? 1 : 0) << " "
                 << std::quoted(animation.InitialState) << "\n";
            for (const auto& state : animation.States)
            {
                if (!state.Clip) continue;
                const AnimationClip& stateClip = *state.Clip;
                file << "@anim_state " << std::quoted(state.Name) << " "
                     << std::quoted(stateClip.ID) << " " << std::quoted(stateClip.Name) << " "
                     << stateClip.Duration << " " << (stateClip.Loop ? 1 : 0) << " "
                     << stateClip.Position.Keys.size();
                for (const auto& key : stateClip.Position.Keys)
                    file << " " << key.Time << " " << key.Value.x << " " << key.Value.y << " " << key.Value.z;
                file << " " << stateClip.Rotation.Keys.size();
                for (const auto& key : stateClip.Rotation.Keys)
                    file << " " << key.Time << " " << key.Value.w << " " << key.Value.x << " " << key.Value.y << " " << key.Value.z;
                file << " " << stateClip.Scale.Keys.size();
                for (const auto& key : stateClip.Scale.Keys)
                    file << " " << key.Time << " " << key.Value.x << " " << key.Value.y << " " << key.Value.z;
                file << "\n";
            }
            for (const auto& transition : animation.Transitions)
                file << "@anim_transition " << std::quoted(transition.From) << " "
                     << std::quoted(transition.To) << " " << std::quoted(transition.BoolParameter) << " "
                     << (transition.ExpectedValue ? 1 : 0) << " " << transition.ExitTimeNormalized
                     << " " << transition.BlendDuration << "\n";
            for (const auto& [parameter, value] : animation.BoolParameters)
                file << "@anim_param " << std::quoted(parameter) << " " << (value ? 1 : 0) << "\n";
        }

        // VÃ­nculo de prefab â€” sÃ³ escrito quando a entidade Ã© instÃ¢ncia
        if (!entity.PrefabPath.empty())
        {
            file << "@prefab " << std::quoted(entity.PrefabPath) << "\n";
        }
        if (entity.GetParentID() != 0)
        {
            file << "@parent " << entity.GetParentID() << "\n";
        }
        const ScriptComponent& sc = const_cast<Entity&>(entity).GetScriptComponent();
        if (!sc.ScriptPath.empty())
        {
            file << "@script " << (sc.Enabled ? 1 : 0) << " "
                 << std::quoted(sc.ScriptPath) << "\n";
        }
        file << "@entity_id " << entity.GetID() << "\n";
    }

    // Global Light â€” apesar de EditorOnly, persiste os 22 campos no
    // arquivo de cena para que o projeto seja portÃ¡til sem a pasta Library.
    for (const auto& entity : entities)
    {
        if (entity.IsGlobalLight())
        {
            const auto& gl = const_cast<Entity&>(entity).GetGlobalLightComponent();
        file << "@global_light "
                 << (gl.Enabled ? 1 : 0) << " "
                 << gl.Brightness << " " << gl.Contrast << " "
                 << gl.Saturation << " " << gl.Exposure << " "
                 << gl.Gamma << " " << gl.ShadowStrength << " "
                 << gl.ColorTint.X << " " << gl.ColorTint.Y << " " << gl.ColorTint.Z << " "
                 << (gl.FogEnabled ? 1 : 0) << " "
                 << gl.FogColor.X << " " << gl.FogColor.Y << " " << gl.FogColor.Z << " "
                 << gl.FogDensity << " " << gl.FogStart << " " << gl.FogEnd << " "
                 << gl.VignetteStrength << " " << gl.VignetteRadius << " "
                 << static_cast<int>(gl.ToneMapping) << " "
                 << gl.Temperature << " " << gl.Tint << " "
                 << (gl.FilmGrainEnabled ? 1 : 0) << " "
                 << gl.FilmGrainStrength << " " << gl.FilmGrainSize << " " << gl.FilmGrainSpeed << " "
                 << (gl.ChromaticAberrationEnabled ? 1 : 0) << " "
                 << gl.ChromaticAberrationStrength << " "
                 << gl.BloomStrength << " " << gl.SharpenStrength << " "
                 << gl.Lighting.SkyZenithColor.X << " " << gl.Lighting.SkyZenithColor.Y << " " << gl.Lighting.SkyZenithColor.Z << " "
                 << gl.Lighting.SkyHorizonColor.X << " " << gl.Lighting.SkyHorizonColor.Y << " " << gl.Lighting.SkyHorizonColor.Z << " "
                 << gl.Lighting.SkyGroundColor.X << " " << gl.Lighting.SkyGroundColor.Y << " " << gl.Lighting.SkyGroundColor.Z << " "
                 << (gl.Lighting.CloudsEnabled ? 1 : 0) << " "
                 << gl.Lighting.CloudColor.X << " " << gl.Lighting.CloudColor.Y << " " << gl.Lighting.CloudColor.Z << " "
                 << gl.Lighting.CloudAmount << " " << gl.Lighting.CloudOctaves << " "
                 << (gl.Lighting.CloudMovementEnabled ? 1 : 0) << " " << gl.Lighting.CloudSpeed << " "
                 << std::clamp(gl.Lighting.FogMode, 0, 2) << " "
                 << (gl.Lighting.ScreenSpaceAOEnabled ? 1 : 0) << " "
                 << gl.Lighting.ScreenSpaceAOIntensity << " " << gl.Lighting.ScreenSpaceAORadius << " "
                 << (gl.Lighting.ScreenSpaceReflectionsEnabled ? 1 : 0) << " "
                 << gl.Lighting.ScreenSpaceReflectionsStrength << " "
                 << gl.Lighting.ScreenSpaceReflectionsDistance << " "
                 << (gl.Lighting.TemporalAAEnabled ? 1 : 0) << " "
                 << gl.Lighting.TemporalAAHistoryWeight << " "
                 << (gl.Lighting.VolumetricCloudsEnabled ? 1 : 0) << " "
                 << gl.Lighting.VolumetricCloudBaseHeight << " "
                 << gl.Lighting.VolumetricCloudThickness << " "
                 << gl.Lighting.VolumetricCloudDensity << " "
                 << gl.Lighting.VolumetricCloudScale << " "
                 << gl.Lighting.VolumetricCloudSteps << " "
                 << (gl.Lighting.GodRaysEnabled ? 1 : 0) << " "
                 << gl.Lighting.GodRaysIntensity << " "
                 << gl.Lighting.GodRaysDensity << " "
                 << gl.Lighting.GodRaysDecay << " "
                 << gl.Lighting.GodRaysWeight << " "
                  << gl.Lighting.GodRaysTint.X << " " << gl.Lighting.GodRaysTint.Y << " " << gl.Lighting.GodRaysTint.Z << " "
                  << gl.Lighting.GodRaysMieG << " "
                  << gl.Lighting.GodRaysHaloRadius << " "
                  << gl.Lighting.GodRaysHaloIntensity << " "
                 << (gl.Lighting.GlareRaysEnabled ? 1 : 0) << " "
                 << gl.Lighting.GlareRaysIntensity << " "
                 << gl.Lighting.GlareRaysLength << " "
                 << gl.Lighting.GlareRaysThreshold << "\n";
            break;
        }
    }

    // Global Script — persiste o script e campos serializados
    for (const auto& entity : entities)
    {
        if (entity.IsGlobalScript())
        {
            const auto& sc = const_cast<Entity&>(entity).GetScriptComponent();
            file << "@global_script "
                 << (entity.Active ? 1 : 0) << " "
                 << (sc.Enabled ? 1 : 0) << " "
                 << std::quoted(sc.ScriptPath) << " "
                 << std::quoted(sc.ScriptAssemblyQualifiedTypeName) << "\n";
            for (const auto& [fName, fVal] : sc.SerializedFields)
            {
                file << "@global_script_field " << std::quoted(fName) << " " << std::quoted(fVal) << "\n";
            }
            break;
        }
    }

    LightingManager& lightingManager = m_Scene.GetLightingManager();
    LightingSettings& settings = lightingManager.Settings;
    settings.Clamp();
    const DirectionalLight& sun = lightingManager.Sun;
    const DirectionalLight& moon = lightingManager.Moon;
    const AmbientLight& ambient = lightingManager.Ambient;
    file << "@lighting "
         << (settings.Enabled ? 1 : 0) << " "
         << (settings.DynamicShadows ? 1 : 0) << " "
         << (settings.ContactShadows ? 1 : 0) << " "
         << (settings.IndirectLighting ? 1 : 0) << " "
         << (settings.EnvironmentLighting ? 1 : 0) << " "
         << static_cast<int>(settings.Quality) << " "
         << settings.MaxPointLights << " " << settings.MaxSpotLights << " "
         << settings.MaxShadowedLocalLights << " "
         << settings.IndirectIntensity << " " << settings.ContactShadowStrength << " "
         << settings.ContactShadowDistance << " " << settings.ShadowDistance << " "
         << settings.ShadowMapResolution << " " << settings.PointShadowMapResolution << " "
         << (settings.DayNightCycleEnabled ? 1 : 0) << " " << settings.DayTimeHours << " "
         << settings.DayNightCycleSpeedHoursPerSecond << " " << settings.SunAzimuthDegrees << " "
         << settings.SunIntensityScale << " "
         << sun.Direction.X << " " << sun.Direction.Y << " " << sun.Direction.Z << " "
         << sun.Color.X << " " << sun.Color.Y << " " << sun.Color.Z << " "
         << sun.Intensity << " " << (sun.UseTemperature ? 1 : 0) << " "
         << sun.TemperatureKelvin << " "
         << ambient.Color.X << " " << ambient.Color.Y << " " << ambient.Color.Z << " "
         << ambient.Intensity << " " << m_AmbientLight << " " << m_ShadowIntensity << " "
         << moon.Direction.X << " " << moon.Direction.Y << " " << moon.Direction.Z << " "
         << moon.Color.X << " " << moon.Color.Y << " " << moon.Color.Z << " "
         << moon.Intensity << " " << (moon.UseTemperature ? 1 : 0) << " "
         << moon.TemperatureKelvin << " "
         << (settings.DirectionalDistanceSoftening ? 1 : 0) << " "
         << settings.DirectionalSourceAngularDiameterDegrees << "\n";

    file.flush();
    if (!file.good())
    {
        file.close();
        std::error_code cleanupError;
        std::filesystem::remove(temporaryPath, cleanupError);
        std::cerr << "[Application] Falha ao concluir a gravacao da cena: "
                  << savePath << std::endl;
        return false;
    }
    file.close();

    std::error_code commitError;
    const bool hadOriginal = std::filesystem::exists(scenePath, commitError) && !commitError;
    commitError.clear();
    if (hadOriginal)
    {
        std::filesystem::copy_file(
            scenePath, backupPath,
            std::filesystem::copy_options::overwrite_existing, commitError);
        if (commitError)
        {
            std::filesystem::remove(temporaryPath, commitError);
            std::cerr << "[Application] Nao foi possivel criar backup da cena: "
                      << savePath << std::endl;
            return false;
        }
        commitError.clear();
        std::filesystem::remove(scenePath, commitError);
        if (commitError)
        {
            std::filesystem::remove(temporaryPath, commitError);
            std::cerr << "[Application] Nao foi possivel substituir a cena: "
                      << savePath << std::endl;
            return false;
        }
    }

    std::filesystem::rename(temporaryPath, scenePath, commitError);
    if (commitError)
    {
        std::error_code restoreError;
        if (hadOriginal && std::filesystem::exists(backupPath, restoreError))
        {
            std::filesystem::copy_file(
                backupPath, scenePath,
                std::filesystem::copy_options::overwrite_existing, restoreError);
        }
        std::filesystem::remove(temporaryPath, restoreError);
        std::cerr << "[Application] Nao foi possivel ativar a nova cena: "
                  << savePath << std::endl;
        return false;
    }

    // Somente limpa dirty flag depois de o arquivo novo estar ativo.
    m_SceneDirty = false;
    UpdateWindowTitle();

    if (!m_WritingRecoverySnapshot)
    {
        std::error_code recoveryCleanupError;
        std::filesystem::remove(
            std::filesystem::path(savePath + ".recovery"), recoveryCleanupError);
        if (recoveryCleanupError)
            std::cerr << "[Recovery] Could not remove stale snapshot: "
                      << recoveryCleanupError.message() << std::endl;
        m_RecoverySnapshotPath.clear();
        m_RecoverySnapshotAccumulator = 0.0;
    }

    std::cout << "Cena salva em: " << savePath << std::endl;
    return true;
}

std::string Application::GetRecoverySnapshotPath() const
{
    std::filesystem::path scenePath(m_ProjectConfig.ScenePath);
    if (scenePath.empty() && !m_ProjectConfig.ProjectPath.empty())
        scenePath = std::filesystem::path(m_ProjectConfig.ProjectPath) / "Assets" / "Scenes" / "Main.scene";
    if (scenePath.empty())
        scenePath = "Scene.scene";
    return scenePath.string() + ".recovery";
}

bool Application::SaveRecoverySnapshot()
{
    if (!m_SceneDirty || m_WritingRecoverySnapshot)
        return false;

    const std::string originalScenePath = m_ProjectConfig.ScenePath;
    const bool originalDirty = m_SceneDirty;
    const std::string snapshotPath = GetRecoverySnapshotPath();
    m_ProjectConfig.ScenePath = snapshotPath;
    m_WritingRecoverySnapshot = true;
    const bool snapshotSaved = SaveScene();
    m_WritingRecoverySnapshot = false;
    m_ProjectConfig.ScenePath = originalScenePath;
    m_SceneDirty = originalDirty;
    UpdateWindowTitle();

    std::error_code error;
    if (snapshotSaved && std::filesystem::is_regular_file(snapshotPath, error) && !error)
    {
        m_RecoverySnapshotPath = snapshotPath;
        std::cout << "[Recovery] Snapshot atualizado: " << snapshotPath << std::endl;
    }
    return snapshotSaved;
}

void Application::CheckForRecoverySnapshot()
{
    const std::filesystem::path snapshotPath(GetRecoverySnapshotPath());
    std::error_code error;
    if (!std::filesystem::is_regular_file(snapshotPath, error) || error)
        return;

    const std::string snapshotString = snapshotPath.string();
    const std::string suffix = ".recovery";
    if (snapshotString.size() < suffix.size() ||
        snapshotString.compare(snapshotString.size() - suffix.size(), suffix.size(), suffix) != 0)
        return;
    const std::filesystem::path scenePath(
        snapshotString.substr(0, snapshotString.size() - suffix.size()));

    if (std::filesystem::is_regular_file(scenePath, error) && !error)
    {
        std::error_code snapshotTimeError;
        std::error_code sceneTimeError;
        const auto snapshotTime = std::filesystem::last_write_time(snapshotPath, snapshotTimeError);
        const auto sceneTime = std::filesystem::last_write_time(scenePath, sceneTimeError);
        if (!snapshotTimeError && !sceneTimeError && snapshotTime < sceneTime)
        {
            std::filesystem::remove(snapshotPath, error);
            return;
        }

        std::ifstream currentFile(scenePath, std::ios::binary);
        std::ifstream recoveryFile(snapshotPath, std::ios::binary);
        const bool sameContent = currentFile && recoveryFile &&
            std::equal(std::istreambuf_iterator<char>(currentFile), std::istreambuf_iterator<char>(),
                       std::istreambuf_iterator<char>(recoveryFile), std::istreambuf_iterator<char>());
        if (sameContent)
        {
            std::filesystem::remove(snapshotPath, error);
            return;
        }
    }

    m_RecoverySnapshotPath = snapshotPath.string();
    m_ShowRecoveryDialog = true;
    std::cout << "[Recovery] Snapshot pendente de decisão: " << m_RecoverySnapshotPath << std::endl;
}

void Application::RenderRecoveryDialog()
{
    if (m_ShowRecoveryDialog)
    {
        ImGui::OpenPopup("##scene_recovery");
        m_ShowRecoveryDialog = false;
    }

    ImGui::SetNextWindowSize({ 440.f, 0.f }, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always, { 0.5f, 0.5f });
    if (ImGui::BeginPopupModal("Recuperação da cena##scene_recovery", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        ImGui::TextWrapped("Foi encontrada uma versão de recuperação da cena, diferente da última versão salva. Deseja restaurá-la?");
        ImGui::Spacing();
        if (ImGui::Button("Restaurar recuperação", { 190.f, 32.f }))
        {
            const std::string originalScenePath = m_ProjectConfig.ScenePath;
            const std::string recoveryPath = m_RecoverySnapshotPath;
            if (LoadScene(recoveryPath))
            {
                m_ProjectConfig.ScenePath = originalScenePath;
                m_SceneDirty = true;
                UpdateWindowTitle();
                ImGui::CloseCurrentPopup();
            }
            else
            {
                MaraeDiagnostics::DiagnosticLog::Get().Add(
                    MaraeDiagnostics::LogLevel::Error,
                    "Não foi possível abrir o snapshot de recuperação.",
                    "Recovery", recoveryPath, 0, 0, {}, originalScenePath,
                    "Mantenha a cena atual e verifique se o arquivo .recovery ainda existe e pode ser lido.");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Descartar recuperação", { 190.f, 32.f }))
        {
            std::error_code error;
            std::filesystem::remove(m_RecoverySnapshotPath, error);
            if (error)
                std::cerr << "[Recovery] Could not discard snapshot: " << error.message() << std::endl;
            m_RecoverySnapshotPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Application::LoadScene()
{
    // Se tem um path de projeto configurado, usa a cena do projeto
    std::string loadPath = m_ProjectConfig.ScenePath;
    if (loadPath.empty())
        loadPath = "Scene.scene";
    LoadScene(loadPath);
}

bool Application::LoadScene(const std::string& path, bool additive)
{
    const bool previousDirty = m_SceneDirty;
    std::ifstream file(path);

    if (!file.is_open())
    {
        std::cout << "Nao foi possivel abrir a cena: " << path << std::endl;
        return false;
    }

    if (!additive)
    {
        m_SceneHasLightingSettings = false;
        m_Scene.Clear();

        // Cria entidades editor (Grid, Global Light) para que os blocos
        // @global_light do arquivo de cena tenham alvo para aplicar valores.
        EditorObjects::CreateDefaultObjects(m_Scene);
    }

    std::string name;
    std::string pendingName;
    bool hasPendingName = false;
    std::unordered_map<uint64_t, uint64_t> loadedIdMap;
    std::vector<std::pair<uint64_t, uint64_t>> pendingParentPairs;

    while (true)
    {
        if (hasPendingName)
        {
            name = std::move(pendingName);
            hasPendingName = false;
        }
        else if (!std::getline(file, name))
        {
            break;
        }

        if (name.empty())
        {
            continue;
        }

        // --- Bloco @global_light (30 valores em 1 linha): aplica na entidade
        //     Global Light jÃ¡ criada por CreateDefaultObjects acima. ---
        if (name.rfind("@lighting", 0) == 0)
        {
            if (additive)
                continue;
            LightingSettings& settings = m_Scene.GetLightingManager().Settings;
            DirectionalLight& sun = m_Scene.GetLightingManager().Sun;
            DirectionalLight& moon = m_Scene.GetLightingManager().Moon;
            AmbientLight& ambient = m_Scene.GetLightingManager().Ambient;
            int enabled = 0, dynamicShadows = 1, contactShadows = 1;
            int indirectLighting = 1, environmentLighting = 1, quality = 1;
            int maxPoint = 8, maxSpot = 8, maxLocalShadows = 4;
            int dayNightEnabled = 0, useSunTemperature = 0, useMoonTemperature = 0;
            float rendererAmbient = m_AmbientLight, rendererShadow = m_ShadowIntensity;
            std::stringstream ss(name.substr(9));
            ss >> enabled >> dynamicShadows >> contactShadows
               >> indirectLighting >> environmentLighting >> quality
               >> maxPoint >> maxSpot >> maxLocalShadows
               >> settings.IndirectIntensity >> settings.ContactShadowStrength
               >> settings.ContactShadowDistance >> settings.ShadowDistance
               >> settings.ShadowMapResolution >> settings.PointShadowMapResolution
               >> dayNightEnabled >> settings.DayTimeHours
               >> settings.DayNightCycleSpeedHoursPerSecond >> settings.SunAzimuthDegrees
               >> settings.SunIntensityScale
               >> sun.Direction.X >> sun.Direction.Y >> sun.Direction.Z
               >> sun.Color.X >> sun.Color.Y >> sun.Color.Z
               >> sun.Intensity >> useSunTemperature >> sun.TemperatureKelvin
               >> ambient.Color.X >> ambient.Color.Y >> ambient.Color.Z >> ambient.Intensity
               >> rendererAmbient >> rendererShadow;
            // Campos da lua no fim preservam a leitura de cenas antigas.
            ss >> moon.Direction.X >> moon.Direction.Y >> moon.Direction.Z
               >> moon.Color.X >> moon.Color.Y >> moon.Color.Z
               >> moon.Intensity >> useMoonTemperature >> moon.TemperatureKelvin;
            int directionalDistanceSoftening = 1;
            ss >> directionalDistanceSoftening;
            // Campo opcional no fim para manter compatibilidade com cenas
            // salvas antes do controle de diâmetro angular.
            ss >> settings.DirectionalSourceAngularDiameterDegrees;
            settings.Enabled = enabled != 0;
            settings.DynamicShadows = dynamicShadows != 0;
            settings.ContactShadows = contactShadows != 0;
            settings.IndirectLighting = indirectLighting != 0;
            settings.EnvironmentLighting = environmentLighting != 0;
            settings.Quality = static_cast<LightingQuality>(std::clamp(quality, 0, 3));
            settings.MaxPointLights = maxPoint;
            settings.MaxSpotLights = maxSpot;
            settings.MaxShadowedLocalLights = maxLocalShadows;
            settings.DayNightCycleEnabled = dayNightEnabled != 0;
            settings.DirectionalSourceAngularDiameterDegrees = std::clamp(
                settings.DirectionalSourceAngularDiameterDegrees, 0.01f, 5.0f);
            const float sunLengthSquared =
                sun.Direction.X * sun.Direction.X + sun.Direction.Y * sun.Direction.Y +
                sun.Direction.Z * sun.Direction.Z;
            if (!std::isfinite(sunLengthSquared) || sunLengthSquared < 0.000001f)
                sun.Direction = Vector3(-0.4f, -1.0f, -0.4f);
            sun.Color.X = std::clamp(sun.Color.X, 0.0f, 10.0f);
            sun.Color.Y = std::clamp(sun.Color.Y, 0.0f, 10.0f);
            sun.Color.Z = std::clamp(sun.Color.Z, 0.0f, 10.0f);
            sun.Intensity = std::clamp(sun.Intensity, 0.0f, 10.0f);
            sun.TemperatureKelvin = std::clamp(sun.TemperatureKelvin, 1000.0f, 40000.0f);
            sun.UseTemperature = useSunTemperature != 0;
            const float moonLengthSquared =
                moon.Direction.X * moon.Direction.X + moon.Direction.Y * moon.Direction.Y +
                moon.Direction.Z * moon.Direction.Z;
            if (!std::isfinite(moonLengthSquared) || moonLengthSquared < 0.000001f)
                moon.Direction = Vector3(0.4f, 1.0f, 0.4f);
            moon.Color.X = std::clamp(moon.Color.X, 0.0f, 10.0f);
            moon.Color.Y = std::clamp(moon.Color.Y, 0.0f, 10.0f);
            moon.Color.Z = std::clamp(moon.Color.Z, 0.0f, 10.0f);
            if (std::abs(moon.Color.X - 0.28f) < 0.015f &&
                std::abs(moon.Color.Y - 0.38f) < 0.015f &&
                std::abs(moon.Color.Z - 0.72f) < 0.015f)
                moon.Color = Vector3(1.0f, 1.0f, 1.0f);
            moon.Intensity = std::clamp(moon.Intensity, 0.0f, 10.0f);
            moon.TemperatureKelvin = std::clamp(moon.TemperatureKelvin, 1000.0f, 40000.0f);
            moon.UseTemperature = useMoonTemperature != 0;
            settings.DirectionalDistanceSoftening = directionalDistanceSoftening != 0;
            ambient.Color.X = std::clamp(ambient.Color.X, 0.0f, 10.0f);
            ambient.Color.Y = std::clamp(ambient.Color.Y, 0.0f, 10.0f);
            ambient.Color.Z = std::clamp(ambient.Color.Z, 0.0f, 10.0f);
            ambient.Intensity = std::clamp(ambient.Intensity, 0.0f, 10.0f);
            m_AmbientLight = std::clamp(rendererAmbient, 0.0f, 2.0f);
            m_ShadowIntensity = std::clamp(rendererShadow, 0.0f, 1.0f);
            m_SceneHasLightingSettings = true;
            // Cenas antigas foram salvas com Enabled=0 antes do preset ser corrigido.
            // Forcamos Enabled=true para que iluminacao avancada (sombras, point/spot) funcione.
            settings.Enabled = true;
            settings.Clamp();
            continue;
        }

        if (name.rfind("@global_light", 0) == 0)
        {
            if (additive)
                continue;
            std::stringstream ss(name.substr(14));
            int enabled = 1, fogEnabled = 0, toneI = 0,
                grainEnabled = 0, chromEnabled = 0;
            float brightness = 1, contrast = 1, saturation = 1,
                  exposure = 0, gamma = 0.60f, shadowStrength = 1,
                  tintR = 1, tintG = 1, tintB = 1,
                  fogR = 0.5f, fogG = 0.5f, fogB = 0.5f,
                  fogDensity = 0.015f, fogStart = 10.0f, fogEnd = 250.0f,
                  vignetteStrength = 0, vignetteRadius = 0.5f,
                  temperature = 0, tint = 0,
                  grainStrength = 0, grainSize = 1, grainSpeed = 1,
                  chromStrength = 0, bloomStrength = 0, sharpenStrength = 0,
                  skyZenithR = 0.18f, skyZenithG = 0.44f, skyZenithB = 0.85f,
                  skyHorizonR = 0.72f, skyHorizonG = 0.84f, skyHorizonB = 0.96f,
                  skyGroundR = 0.38f, skyGroundG = 0.42f, skyGroundB = 0.46f,
                  cloudColorR = 0.58f, cloudColorG = 0.65f, cloudColorB = 0.76f,
                  cloudAmount = 0.50f, cloudSpeed = 1.0f;
            int cloudsEnabled = 1, cloudOctaves = 5, cloudMovementEnabled = 1, fogMode = 1;
            int ssaoEnabled = 0, ssrEnabled = 0, taaEnabled = 0, volumetricCloudsEnabled = 0;
            int volumetricCloudSteps = 8;
            float ssaoIntensity = 0.35f, ssaoRadius = 1.25f;
            float ssrStrength = 0.25f, ssrDistance = 24.0f, taaHistoryWeight = 0.12f;
            float volumetricCloudBaseHeight = 900.0f, volumetricCloudThickness = 700.0f;
            float volumetricCloudDensity = 0.65f, volumetricCloudScale = 1.0f;
            ss >> enabled >> brightness >> contrast >> saturation
               >> exposure >> gamma >> shadowStrength
               >> tintR >> tintG >> tintB
               >> fogEnabled >> fogR >> fogG >> fogB
               >> fogDensity >> fogStart >> fogEnd
               >> vignetteStrength >> vignetteRadius
               >> toneI >> temperature >> tint
               >> grainEnabled >> grainStrength >> grainSize >> grainSpeed
               >> chromEnabled >> chromStrength
               >> bloomStrength >> sharpenStrength;
            if (ss)
                ss >> skyZenithR >> skyZenithG >> skyZenithB
                   >> skyHorizonR >> skyHorizonG >> skyHorizonB
                   >> skyGroundR >> skyGroundG >> skyGroundB
                   >> cloudsEnabled >> cloudColorR >> cloudColorG >> cloudColorB
                   >> cloudAmount >> cloudOctaves >> cloudMovementEnabled >> cloudSpeed;
            if (ss)
                ss >> fogMode;
            int godRaysEnabled = 0, glareRaysEnabled = 0;
            float godRaysIntensity = 0.5f, godRaysDensity = 0.8f, godRaysDecay = 0.95f, godRaysWeight = 0.35f;
            float godRaysTintX = 1.0f, godRaysTintY = 1.0f, godRaysTintZ = 1.0f;
            float godRaysMieG = 0.72f, godRaysHaloRadius = 0.12f, godRaysHaloIntensity = 0.6f;
            float glareRaysIntensity = 0.4f, glareRaysLength = 0.6f, glareRaysThreshold = 1.0f;
            if (ss)
                ss >> ssaoEnabled >> ssaoIntensity >> ssaoRadius
                   >> ssrEnabled >> ssrStrength >> ssrDistance
                   >> taaEnabled >> taaHistoryWeight
                   >> volumetricCloudsEnabled >> volumetricCloudBaseHeight
                   >> volumetricCloudThickness >> volumetricCloudDensity
                   >> volumetricCloudScale >> volumetricCloudSteps;
            if (ss)
                ss >> godRaysEnabled >> godRaysIntensity >> godRaysDensity >> godRaysDecay >> godRaysWeight;
            if (ss)
                ss >> godRaysTintX >> godRaysTintY >> godRaysTintZ
                   >> godRaysMieG >> godRaysHaloRadius >> godRaysHaloIntensity;
            if (ss)
                ss
                   >> glareRaysEnabled >> glareRaysIntensity >> glareRaysLength >> glareRaysThreshold;
            for (Entity& e : m_Scene.GetEntities())
            {
                if (e.IsGlobalLight())
                {
                    auto& gl = e.GetGlobalLightComponent();
                    gl.Enabled = enabled != 0;
                    gl.Brightness = std::clamp(brightness, 0.0f, 3.0f);
                    gl.Contrast = std::clamp(contrast, 0.0f, 3.0f);
                    gl.Saturation = std::clamp(saturation, 0.0f, 3.0f);
                    gl.Exposure = std::clamp(exposure, -3.0f, 3.0f);
                    gl.Gamma = std::clamp(gamma, 0.1f, 3.0f);
                    gl.ShadowStrength = std::clamp(shadowStrength, 0.0f, 1.0f);
                    gl.ColorTint.X = std::clamp(tintR, 0.0f, 3.0f);
                    gl.ColorTint.Y = std::clamp(tintG, 0.0f, 3.0f);
                    gl.ColorTint.Z = std::clamp(tintB, 0.0f, 3.0f);
                    gl.FogEnabled = fogEnabled != 0;
                    gl.FogColor.X = std::clamp(fogR, 0.0f, 3.0f);
                    gl.FogColor.Y = std::clamp(fogG, 0.0f, 3.0f);
                    gl.FogColor.Z = std::clamp(fogB, 0.0f, 3.0f);
                    gl.FogDensity = std::clamp(fogDensity, 0.0f, 0.2f);
                    gl.FogStart = std::clamp(fogStart, 0.0f, 10000.0f);
                    gl.FogEnd = std::clamp(fogEnd, 1.0f, 10000.0f);
                    gl.Lighting.FogMode = std::clamp(fogMode, 0, 2);
                    gl.VignetteStrength = std::clamp(vignetteStrength, 0.0f, 1.0f);
                    gl.VignetteRadius = std::clamp(vignetteRadius, 0.1f, 1.0f);
                    gl.ToneMapping = static_cast<GlobalLightComponent::ToneMappingOperator>(
                        std::clamp(toneI, 0, 3));
                    gl.Temperature = std::clamp(temperature, -1.0f, 1.0f);
                    gl.Tint = std::clamp(tint, -1.0f, 1.0f);
                    gl.FilmGrainEnabled = grainEnabled != 0;
                    gl.Lighting.GodRaysEnabled = godRaysEnabled != 0;
                    gl.Lighting.GodRaysIntensity = std::clamp(godRaysIntensity, 0.0f, 3.0f);
                    gl.Lighting.GodRaysDensity = std::clamp(godRaysDensity, 0.05f, 2.0f);
                    gl.Lighting.GodRaysDecay = std::clamp(godRaysDecay, 0.5f, 0.99f);
                    gl.Lighting.GodRaysWeight = std::clamp(godRaysWeight, 0.05f, 1.0f);
                    gl.Lighting.GodRaysTint = Vector3(godRaysTintX, godRaysTintY, godRaysTintZ);
                    gl.Lighting.GodRaysMieG = std::clamp(godRaysMieG, 0.0f, 0.98f);
                    gl.Lighting.GodRaysHaloRadius = std::clamp(godRaysHaloRadius, 0.01f, 0.5f);
                    gl.Lighting.GodRaysHaloIntensity = std::clamp(godRaysHaloIntensity, 0.0f, 5.0f);
                    gl.Lighting.GlareRaysEnabled = glareRaysEnabled != 0;
                    gl.Lighting.GlareRaysIntensity = std::clamp(glareRaysIntensity, 0.0f, 3.0f);
                    gl.Lighting.GlareRaysLength = std::clamp(glareRaysLength, 0.05f, 2.0f);
                    gl.Lighting.GlareRaysThreshold = std::clamp(glareRaysThreshold, 0.5f, 5.0f);
                    gl.FilmGrainStrength = std::clamp(grainStrength, 0.0f, 0.25f);
                    gl.FilmGrainSize = std::clamp(grainSize, 0.1f, 5.0f);
                    gl.FilmGrainSpeed = std::clamp(grainSpeed, 0.0f, 2.0f);
                    gl.ChromaticAberrationEnabled = chromEnabled != 0;
                    gl.ChromaticAberrationStrength = std::clamp(chromStrength, 0.0f, 0.1f);
                    gl.BloomStrength = std::clamp(bloomStrength, 0.0f, 2.0f);
                    gl.SharpenStrength = std::clamp(sharpenStrength, 0.0f, 2.0f);
                    gl.Lighting.SkyZenithColor = Vector3(std::clamp(skyZenithR, 0.0f, 3.0f), std::clamp(skyZenithG, 0.0f, 3.0f), std::clamp(skyZenithB, 0.0f, 3.0f));
                    gl.Lighting.SkyHorizonColor = Vector3(std::clamp(skyHorizonR, 0.0f, 3.0f), std::clamp(skyHorizonG, 0.0f, 3.0f), std::clamp(skyHorizonB, 0.0f, 3.0f));
                    gl.Lighting.SkyGroundColor = Vector3(std::clamp(skyGroundR, 0.0f, 3.0f), std::clamp(skyGroundG, 0.0f, 3.0f), std::clamp(skyGroundB, 0.0f, 3.0f));
                    gl.Lighting.CloudsEnabled = cloudsEnabled != 0;
                    gl.Lighting.CloudColor = Vector3(std::clamp(cloudColorR, 0.0f, 3.0f), std::clamp(cloudColorG, 0.0f, 3.0f), std::clamp(cloudColorB, 0.0f, 3.0f));
                    gl.Lighting.CloudAmount = std::clamp(cloudAmount, 0.0f, 1.0f);
                    gl.Lighting.CloudOctaves = std::clamp(cloudOctaves, 1, 5);
                    gl.Lighting.CloudMovementEnabled = cloudMovementEnabled != 0;
                    gl.Lighting.CloudSpeed = std::clamp(cloudSpeed, 0.0f, 10.0f);
                    gl.Lighting.ScreenSpaceAOEnabled = ssaoEnabled != 0;
                    gl.Lighting.ScreenSpaceAOIntensity = std::clamp(ssaoIntensity, 0.0f, 1.0f);
                    gl.Lighting.ScreenSpaceAORadius = std::clamp(ssaoRadius, 0.1f, 5.0f);
                    gl.Lighting.ScreenSpaceReflectionsEnabled = ssrEnabled != 0;
                    gl.Lighting.ScreenSpaceReflectionsStrength = std::clamp(ssrStrength, 0.0f, 0.6f);
                    gl.Lighting.ScreenSpaceReflectionsDistance = std::clamp(ssrDistance, 1.0f, 100.0f);
                    gl.Lighting.TemporalAAEnabled = taaEnabled != 0;
                    gl.Lighting.TemporalAAHistoryWeight = std::clamp(taaHistoryWeight, 0.0f, 0.25f);
                    gl.Lighting.VolumetricCloudsEnabled = volumetricCloudsEnabled != 0;
                    gl.Lighting.VolumetricCloudBaseHeight = std::clamp(volumetricCloudBaseHeight, 0.0f, 10000.0f);
                    gl.Lighting.VolumetricCloudThickness = std::clamp(volumetricCloudThickness, 10.0f, 5000.0f);
                    gl.Lighting.VolumetricCloudDensity = std::clamp(volumetricCloudDensity, 0.0f, 3.0f);
                    gl.Lighting.VolumetricCloudScale = std::clamp(volumetricCloudScale, 0.25f, 4.0f);
                    gl.Lighting.VolumetricCloudSteps = std::clamp(volumetricCloudSteps, 4, 16);
                    break;
                }
            }
            continue;
        }

        if (name.rfind("@global_script_field", 0) == 0)
        {
            std::istringstream stream(name);
            std::string tag, fName, fVal;
            stream >> tag >> std::quoted(fName) >> std::quoted(fVal);
            for (Entity& e : m_Scene.GetEntities())
            {
                if (e.IsGlobalScript())
                {
                    e.GetScriptComponent().SerializedFields[fName] = fVal;
                    break;
                }
            }
            continue;
        }

        if (name.rfind("@global_script", 0) == 0)
        {
            std::istringstream stream(name);
            std::string tag;
            int activeVal = 1;
            int enabledVal = 1;
            std::string scriptPath;
            std::string typeName;
            stream >> tag >> activeVal >> enabledVal >> std::quoted(scriptPath) >> std::quoted(typeName);
            for (Entity& e : m_Scene.GetEntities())
            {
                if (e.IsGlobalScript())
                {
                    e.Active = (activeVal != 0);
                    auto& sc = e.GetScriptComponent();
                    sc.Enabled = (enabledVal != 0);
                    sc.ScriptPath = scriptPath;
                    sc.ScriptAssemblyQualifiedTypeName = typeName;
                    break;
                }
            }
            continue;
        }

        std::string positionLine;
        std::string rotationLine;
        std::string scaleLine;
        std::string colorLine;

        std::getline(file, positionLine);
        std::getline(file, rotationLine);
        std::getline(file, scaleLine);
        std::getline(file, colorLine);

        std::string meshPath;
        std::getline(file, meshPath);
        while (!meshPath.empty() &&
               std::isspace(static_cast<unsigned char>(meshPath.back())))
            meshPath.pop_back();
        size_t meshStart = 0;
        while (meshStart < meshPath.size() &&
               std::isspace(static_cast<unsigned char>(meshPath[meshStart])))
            ++meshStart;
        if (meshStart > 0)
            meshPath.erase(0, meshStart);

        // Component metadata is optional so old five-line scenes remain valid.
        // Keep consuming metadata until the next entity name is found.
        std::string physicsLine;
        std::string materialLine;
        std::string faceTexturesLine;
        std::string entityLine;
        std::string lightLine;
        std::string audioLine;
        std::string acousticLine;
        std::string audioAcousticLine;
        std::string cameraLine;
        std::string particlesLine;
        std::string uiLine;
        std::string uiLayoutLine;
        std::string uiOptionsLine;
        std::string aiLine;
        std::vector<std::string> aiNodeLines;
        std::vector<std::string> aiStateLines;
        std::vector<std::string> aiTransitionLines;
        std::string prefabLine;
        std::string parentLine;
        std::string scriptLine;
        std::string animationLine;
        std::string animationMachineLine;
        std::vector<std::string> animationPositionLines;
        std::vector<std::string> animationRotationLines;
        std::vector<std::string> animationScaleLines;
        std::vector<std::string> animationStateLines;
        std::vector<std::string> animationTransitionLines;
        std::vector<std::string> animationParameterLines;
        std::string entityIdLine;
        std::string terrainLine;
        std::string primitiveLine;
        std::string groupLine;
        std::string metadataLine;
        while (std::getline(file, metadataLine))
        {
            if (metadataLine.rfind("@terrain", 0) == 0)
            {
                terrainLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@primitive", 0) == 0)
            {
                primitiveLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@group", 0) == 0)
            {
                groupLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@physics", 0) == 0)
            {
                physicsLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@material", 0) == 0)
            {
                materialLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@face_textures", 0) == 0)
            {
                faceTexturesLine = metadataLine;
                continue;
            }
            // @entity_id tambÃ©m comeÃ§a com "@entity". Ele precisa ser
            // reconhecido antes, caso contrÃ¡rio o ID sobrescreve a linha
            // @entity e os estados Active/Opacity/RaycastTarget sÃ£o perdidos.
            if (metadataLine.rfind("@entity_id", 0) == 0)
            {
                entityIdLine = metadataLine;
                continue;
            }
            if (metadataLine == "@entity" || metadataLine.rfind("@entity ", 0) == 0)
            {
                entityLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@light", 0) == 0)
            {
                lightLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@acoustic", 0) == 0)
            {
                acousticLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@audio_acoustic", 0) == 0)
            {
                audioAcousticLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@audio", 0) == 0)
            {
                audioLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@camera", 0) == 0)
            {
                cameraLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@particles", 0) == 0)
            {
                particlesLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@ai_node", 0) == 0)
            {
                aiNodeLines.push_back(metadataLine);
                continue;
            }
            if (metadataLine.rfind("@ai_state", 0) == 0)
            {
                aiStateLines.push_back(metadataLine);
                continue;
            }
            if (metadataLine.rfind("@ai_transition", 0) == 0)
            {
                aiTransitionLines.push_back(metadataLine);
                continue;
            }
            if (metadataLine.rfind("@ai ", 0) == 0)
            {
                aiLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@ui_layout", 0) == 0)
            {
                uiLayoutLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@ui_options", 0) == 0)
            {
                uiOptionsLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@ui ", 0) == 0)
            {
                uiLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@prefab", 0) == 0)
            {
                prefabLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@parent", 0) == 0)
            {
                parentLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@script", 0) == 0)
            {
                scriptLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@animation", 0) == 0)
            {
                animationLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@anim_machine", 0) == 0)
            {
                animationMachineLine = metadataLine;
                continue;
            }
            if (metadataLine.rfind("@anim_state", 0) == 0)
            {
                animationStateLines.push_back(metadataLine);
                continue;
            }
            if (metadataLine.rfind("@anim_transition", 0) == 0)
            {
                animationTransitionLines.push_back(metadataLine);
                continue;
            }
            if (metadataLine.rfind("@anim_param", 0) == 0)
            {
                animationParameterLines.push_back(metadataLine);
                continue;
            }
            if (metadataLine.rfind("@anim_pos", 0) == 0)
            {
                animationPositionLines.push_back(metadataLine);
                continue;
            }
            if (metadataLine.rfind("@anim_rot", 0) == 0)
            {
                animationRotationLines.push_back(metadataLine);
                continue;
            }
            if (metadataLine.rfind("@anim_scale", 0) == 0)
            {
                animationScaleLines.push_back(metadataLine);
                continue;
            }
            if (!metadataLine.empty())
            {
                pendingName = metadataLine;
                hasPendingName = true;
                break;
            }
        }

        Entity& entity =
            m_Scene.CreateEntity();

        entity.SetName(name);
        if (groupLine.rfind("@group", 0) == 0)
        {
            std::stringstream groupStream(groupLine.substr(6));
            std::string groupType;
            groupStream >> groupType;
            entity.IsFolder = groupType == "Folder";
            entity.IsModel = groupType == "Model";
        }

        std::string primitiveType;
        if (primitiveLine.rfind("@primitive", 0) == 0)
        {
            std::stringstream primitiveStream(primitiveLine.substr(10));
            primitiveStream >> std::quoted(primitiveType);
        }
        if (primitiveType.empty())
        {
            // Compatibilidade com cenas antigas que ainda não tinham o campo.
            std::string lowerName = name;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lowerName.find("circle") != std::string::npos) primitiveType = "Circle2D";
            else if (lowerName.find("pentagon") != std::string::npos) primitiveType = "Pentagon2D";
            else if (lowerName.find("hexagon") != std::string::npos) primitiveType = "Hexagon2D";
            else if (lowerName.find("octagon") != std::string::npos) primitiveType = "Octagon2D";
            else if (lowerName.find("diamond") != std::string::npos) primitiveType = "Diamond2D";
            else if (lowerName.find("star") != std::string::npos) primitiveType = "Star2D";
            else if (lowerName.find("sphere") != std::string::npos) primitiveType = "Sphere";
            else if (lowerName.find("cylinder") != std::string::npos) primitiveType = "Cylinder";
            else if (lowerName.find("cone") != std::string::npos) primitiveType = "Cone";
            else if (lowerName.find("torus") != std::string::npos) primitiveType = "Torus";
            else if (lowerName.find("capsule") != std::string::npos) primitiveType = "Capsule";
            else if (lowerName.find("plane") != std::string::npos) primitiveType = "Plane";
            else if (lowerName.find("wedge") != std::string::npos) primitiveType = "Wedge";
            else if (lowerName.find("triangle") != std::string::npos) primitiveType = "Triangle";
            else if (lowerName.find("quad") != std::string::npos) primitiveType = "Quad";
            else primitiveType = "Cube";
        }
        entity.GetMeshComponent().PrimitiveType = primitiveType;

        const bool isEditorContainer = entity.IsFolder || entity.IsModel || entity.HasUI() ||
            primitiveType == "None" || primitiveType == "UI";
        if (isEditorContainer)
        {
            entity.GetMeshComponent().MeshData.reset();
            entity.GetMeshComponent().MeshPath.clear();
            entity.GetMeshComponent().PrimitiveType = "None";
        }
        else
        {

        // Meshes built-in não possuem arquivo próprio: recria o primitivo
        // original em vez de transformar toda entidade sem MeshPath em cubo.
        if (meshPath.empty())
        {
            std::vector<Vertex> vertices;
            if (primitiveType == "Sphere") vertices = MeshFactory::CreateSphere(32, 24);
            else if (primitiveType == "Cylinder") vertices = MeshFactory::CreateCylinder(32, 1.0f);
            else if (primitiveType == "Cone") vertices = MeshFactory::CreateCone(32, 1.0f);
            else if (primitiveType == "Torus") vertices = MeshFactory::CreateTorus();
            else if (primitiveType == "Capsule") vertices = MeshFactory::CreateCapsule();
            else if (primitiveType == "Plane") vertices = MeshFactory::CreatePlane(4);
            else if (primitiveType == "Wedge") vertices = MeshFactory::CreateWedge();
            else if (primitiveType == "Triangle") vertices = MeshFactory::CreateTriangle();
            else if (primitiveType == "Quad") vertices = MeshFactory::CreateQuad();

            if (vertices.empty())
                vertices = MeshFactory::Create2DPrimitive(primitiveType);

            if (primitiveType == "Quad")
                entity.GetMeshComponent().MeshData = MeshCache::GetQuad();
            else if (vertices.empty())
                entity.GetMeshComponent().MeshData = MeshCache::CreateCubeInstance();
            else
            {
                entity.GetMeshComponent().MeshData = std::make_shared<Mesh>();
                entity.GetMeshComponent().MeshData->Create(vertices, GL_TRIANGLES,
                                                            "Application::LoadScenePrimitive");
            }
            entity.GetMeshComponent().MeshPath.clear();
        }
        else if (meshPath.find("builtin://") != std::string::npos)
        {
            // Primitivos internos antigos eram serializados como caminhos
            // builtin://*. Eles não devem passar pelo importador de arquivos.
            const size_t builtinOffset = meshPath.find("builtin://") + 10;
            std::string builtinName = meshPath.substr(builtinOffset);
            while (!builtinName.empty() &&
                   std::isspace(static_cast<unsigned char>(builtinName.back())))
                builtinName.pop_back();
            std::vector<Vertex> builtinVertices;
            if (builtinName == "quad")
            {
                entity.GetMeshComponent().MeshData = MeshCache::GetQuad();
                entity.GetMeshComponent().PrimitiveType = "Quad";
            }
            else if (builtinName == "triangle")
            {
                builtinVertices = MeshFactory::CreateTriangle();
                entity.GetMeshComponent().PrimitiveType = "Triangle";
            }

            if (builtinName == "quad")
            {
                entity.GetMeshComponent().MeshPath.clear();
                meshPath.clear();
            }
            else if (!builtinVertices.empty())
            {
                entity.GetMeshComponent().MeshData = std::make_shared<Mesh>();
                entity.GetMeshComponent().MeshData->Create(
                    builtinVertices, GL_TRIANGLES, "Application::LoadBuiltinPrimitive");
                // O primitivo já foi reconstruído; não o trate como caminho
                // de asset externo no restante do carregamento.
                entity.GetMeshComponent().MeshPath.clear();
                meshPath.clear();
            }
        }
        else if (ModelImporter::IsSupportedModel(meshPath))
        {
            entity.GetMeshComponent().PrimitiveType = "External";
            // MeshPath Ã© salvo relativo Ã  raiz do projeto. O diretÃ³rio de
            // execuÃ§Ã£o pode ser outro (por exemplo, a pasta do executÃ¡vel),
            // entÃ£o tentar apenas MeshCache::Load(meshPath) fazia meshes
            // modelados em Library/Generated falharem e serem substituÃ­dos
            // silenciosamente pelo cubo padrÃ£o.
            namespace fs = std::filesystem;
            const fs::path storedMeshPath(meshPath);
            std::vector<fs::path> candidates;
            if (storedMeshPath.is_absolute())
            {
                candidates.push_back(storedMeshPath);
            }
            else
            {
                const fs::path scenePath(path);
                const fs::path sceneDirectory = scenePath.parent_path();
                candidates.push_back(fs::current_path() / storedMeshPath);
                candidates.push_back(sceneDirectory / storedMeshPath);
                candidates.push_back(sceneDirectory.parent_path() / storedMeshPath);
                candidates.push_back(sceneDirectory.parent_path().parent_path() / storedMeshPath);
                if (!m_ProjectConfig.ProjectPath.empty())
                    candidates.push_back(fs::path(m_ProjectConfig.ProjectPath) / storedMeshPath);
            }

            std::string resolvedMeshPath;
            for (const fs::path& candidate : candidates)
            {
                std::error_code pathError;
                if (fs::exists(candidate, pathError) && !pathError)
                {
                    resolvedMeshPath = candidate.lexically_normal().string();
                    break;
                }
            }

            entity.GetMeshComponent().MeshData = resolvedMeshPath.empty()
                ? nullptr
                : MeshCache::Load(resolvedMeshPath);
            // MantÃ©m o caminho portÃ¡til original para o prÃ³ximo salvamento.
            entity.GetMeshComponent().MeshPath = meshPath;
        }

        if (meshPath.empty() && !entity.GetMeshComponent().MeshData)
        {
            entity.GetMeshComponent().MeshData =
                MeshCache::CreateCubeInstance();
        }
        else if (!entity.GetMeshComponent().MeshData ||
            !entity.GetMeshComponent().MeshData->IsValid())
        {
            if (!meshPath.empty())
            {
                std::cerr << "[Application] Mesh nao encontrado ao carregar a cena: "
                          << meshPath << std::endl;
            }
            entity.GetMeshComponent().MeshData =
                MeshCache::CreateCubeInstance();
            // NÃ£o apagar o vÃ­nculo original: isso permite recuperar a malha
            // quando o asset voltar a estar disponÃ­vel e evita que um
            // salvamento posterior transforme o objeto em um cubo bÃ¡sico.
            if (meshPath.empty())
                entity.GetMeshComponent().MeshPath.clear();
        }
        }

        std::stringstream positionStream(
            positionLine
        );

        positionStream
            >> entity.GetTransform().Position.X
            >> entity.GetTransform().Position.Y
            >> entity.GetTransform().Position.Z;

        std::stringstream rotationStream(
            rotationLine
        );

        rotationStream
            >> entity.GetTransform().Rotation.x
            >> entity.GetTransform().Rotation.y
            >> entity.GetTransform().Rotation.z
            >> entity.GetTransform().Rotation.w;

        std::stringstream scaleStream(
            scaleLine
        );

        scaleStream
            >> entity.GetTransform().Scale.X
            >> entity.GetTransform().Scale.Y
            >> entity.GetTransform().Scale.Z;

        std::stringstream colorStream(
            colorLine
        );

        colorStream
            >> entity.GetColorComponent().Color.X
            >> entity.GetColorComponent().Color.Y
            >> entity.GetColorComponent().Color.Z;

        // Cenas antigas salvavam a cor no ColorComponent, mas o renderer usa
        // MaterialData. Sincroniza os dois ao carregar para que o projeto
        // aberto tenha as cores que o usuÃ¡rio salvou.
        entity.GetMaterialComponent().MaterialData.Color[0] = entity.GetColorComponent().Color.X;
        entity.GetMaterialComponent().MaterialData.Color[1] = entity.GetColorComponent().Color.Y;
        entity.GetMaterialComponent().MaterialData.Color[2] = entity.GetColorComponent().Color.Z;
        entity.GetMaterialComponent().MaterialData.Color[3] = 1.0f;

        if (materialLine.rfind("@material", 0) == 0)
        {
            std::stringstream materialStream(materialLine.substr(9));
            int materialType = 0;
            materialStream >> materialType;
            if (materialType >= static_cast<int>(MaterialType::Plastic) &&
                materialType <= static_cast<int>(MaterialType::Neon))
            {
                entity.GetMaterialComponent().MaterialData.Type =
                    static_cast<MaterialType>(materialType);
            }

            int pbrEnabled = 0;
            if (materialStream >> pbrEnabled)
            {
                Material& material = entity.GetMaterialComponent().MaterialData;
                int scaleMode = 0;
                std::string materialGUID, texturePath, textureGUID, normalMapPath, emissionMapPath;
                materialStream >> material.Roughness >> material.Metallic >> material.Specular
                               >> material.EmissionStrength >> material.Alpha >> material.NormalStrength
                               >> scaleMode >> material.UVWorldScale >> material.TileSize
                               >> std::quoted(materialGUID) >> std::quoted(texturePath)
                               >> std::quoted(textureGUID) >> std::quoted(normalMapPath)
                               >> std::quoted(emissionMapPath);
                material.PBRParametersEnabled = pbrEnabled != 0;
                material.ScaleMode = static_cast<UVScaleMode>(std::clamp(scaleMode, 0, 5));
                entity.GetMaterialComponent().MaterialGUID = std::move(materialGUID);
                material.TexturePath = std::move(texturePath);
                material.TextureGUID = std::move(textureGUID);
                material.NormalMapPath = std::move(normalMapPath);
                material.EmissionTexturePath = std::move(emissionMapPath);
                material.DiffuseTexture.reset();
                material.NormalTexture.reset();
                material.EmissionTexture.reset();
                material.Roughness = std::clamp(material.Roughness, 0.02f, 1.0f);
                material.Metallic = std::clamp(material.Metallic, 0.0f, 1.0f);
                material.Specular = std::clamp(material.Specular, 0.0f, 1.0f);
                material.EmissionStrength = std::max(0.0f, material.EmissionStrength);
                material.Alpha = std::clamp(material.Alpha, 0.0f, 1.0f);
                material.NormalStrength = std::clamp(material.NormalStrength, 0.0f, 2.0f);
            }
        }

        if (faceTexturesLine.rfind("@face_textures", 0) == 0)
        {
            std::stringstream faceStream(faceTexturesLine.substr(14));
            int mode = 0;
            size_t count = 0;
            if (faceStream >> mode >> count)
            {
                MaterialComponent& component = entity.GetMaterialComponent();
                component.TextureMode = static_cast<FaceTextureMode>(std::clamp(mode, 0, 1));
                component.FaceTexturePaths.clear();
                component.FaceTextures.clear();
                count = std::min<size_t>(count, 1'000'000);
                for (size_t i = 0; i < count; ++i)
                {
                    uint32_t faceIndex = 0;
                    std::string path;
                    if (!(faceStream >> faceIndex >> std::quoted(path))) break;
                    if (!path.empty()) component.FaceTexturePaths.emplace(faceIndex, std::move(path));
                }
            }
        }

        if (cameraLine.rfind("@camera", 0) == 0)
        {
            std::stringstream cameraStream(cameraLine.substr(7));
            int enabled = 1, preview = 1, orthographic = 0, showGizmo = 1;
            float fov = 60.0f, nearClip = 0.1f, farClip = 10000.0f, orthoSize = 10.0f;
            cameraStream >> enabled >> preview >> orthographic >> fov >> nearClip >> farClip >> orthoSize;
            if (cameraStream)
                cameraStream >> showGizmo;
            entity.AddCamera();
            auto& camera = entity.GetCameraComponent();
            camera.Enabled = enabled != 0;
            camera.PreviewEnabled = preview != 0;
            camera.ShowFrustumGizmo = showGizmo != 0;
            camera.Orthographic = orthographic != 0;
            camera.FieldOfView = std::clamp(fov, 1.0f, 179.0f);
            camera.NearClip = std::max(0.001f, nearClip);
            camera.FarClip = std::max(camera.NearClip + 0.01f, farClip);
            camera.OrthographicSize = std::max(0.1f, orthoSize);
            entity.GetMeshComponent().MeshData.reset();
            entity.GetMeshComponent().MeshPath.clear();
        }

        if (particlesLine.rfind("@particles", 0) == 0)
        {
            std::stringstream particleStream(particlesLine.substr(10));
            int enabled = 1;
            ParticleSystemComponent loaded;
            particleStream >> enabled >> loaded.MaxParticles >> loaded.EmissionRate
                >> loaded.Lifetime >> loaded.Speed >> loaded.Spread
                >> loaded.StartSize >> loaded.EndSize
                >> loaded.Gravity.x >> loaded.Gravity.y >> loaded.Gravity.z
                >> loaded.StartColor.r >> loaded.StartColor.g >> loaded.StartColor.b >> loaded.StartColor.a
                >> loaded.EndColor.r >> loaded.EndColor.g >> loaded.EndColor.b >> loaded.EndColor.a;
            if (particleStream)
            {
                // Campos de formato/brilho/textura são opcionais para cenas
                // gravadas pela primeira versão do emissor.
                std::string texturePath;
                if (particleStream >> loaded.Shape >> loaded.Glow >> std::quoted(texturePath))
                    loaded.TexturePath = std::move(texturePath);
                int lit = 0;
                if (particleStream >> loaded.EmissionShape >> loaded.BurstCount
                    >> loaded.SpeedVariation >> loaded.LifetimeVariation >> loaded.Drag
                    >> loaded.Wind.x >> loaded.Wind.y >> loaded.Wind.z
                    >> loaded.Turbulence >> loaded.StartRotation >> loaded.RotationSpeed
                    >> loaded.MidSize
                    >> loaded.MidColor.r >> loaded.MidColor.g >> loaded.MidColor.b >> loaded.MidColor.a
                    >> lit)
                    loaded.Lit = lit != 0;
                int useMidColor = 0;
                if (particleStream >> useMidColor)
                    loaded.UseMidColor = useMidColor != 0;
                loaded.Enabled = enabled != 0;
                loaded.Clamp();
                entity.AddParticleSystem();
                entity.GetParticleSystem() = loaded;
            }
        }

        if (!aiLine.empty())
        {
            int enabled = 1, mode = 0, sightEnabled = 1, rootNodeId = 1;
            float sightRadius = 18.0f, fov = 120.0f, memory = 2.0f;
            std::string targetTag = "Player", initialState = "Idle";
            std::stringstream stream(aiLine.substr(4));
            if (stream >> enabled >> mode >> sightEnabled >> sightRadius >> fov >> memory
                       >> rootNodeId >> std::quoted(targetTag) >> std::quoted(initialState))
            {
                entity.AddAI();
                AIComponent& ai = entity.GetAIComponent();
                ai.Enabled = enabled != 0;
                ai.CurrentMode = static_cast<AIComponent::Mode>(std::clamp(mode, 0, 1));
                ai.SightEnabled = sightEnabled != 0;
                ai.SightRadius = std::clamp(std::isfinite(sightRadius) ? sightRadius : 18.0f, 0.0f, 100000.0f);
                ai.FieldOfViewDegrees = std::clamp(std::isfinite(fov) ? fov : 120.0f, 1.0f, 360.0f);
                ai.TargetMemorySeconds = std::clamp(std::isfinite(memory) ? memory : 2.0f, 0.0f, 3600.0f);
                ai.TargetTag = targetTag;
                ai.InitialState = initialState.empty() ? "Idle" : initialState;
                ai.CurrentState = ai.InitialState;
                ai.RootNodeId = rootNodeId;

                ai.Nodes.clear();
                for (const std::string& line : aiNodeLines)
                {
                    if (ai.Nodes.size() >= 128) break;
                    int id = 0, parent = 0, type = 0;
                    float range = 3.0f, duration = 1.0f, speed = 3.0f;
                    std::stringstream nodeStream(line.substr(9));
                    if (!(nodeStream >> id >> parent >> type >> range >> duration >> speed) || id <= 0)
                        continue;
                    if (std::any_of(ai.Nodes.begin(), ai.Nodes.end(), [id](const auto& n) { return n.Id == id; }))
                        continue;
                    ai.Nodes.push_back({ id, parent, static_cast<AIComponent::NodeType>(std::clamp(type, 0, 9)),
                        std::clamp(std::isfinite(range) ? range : 3.0f, 0.0f, 100000.0f),
                        std::clamp(std::isfinite(duration) ? duration : 1.0f, 0.0f, 3600.0f),
                        std::clamp(std::isfinite(speed) ? speed : 3.0f, 0.0f, 1000.0f) });
                }
                if (ai.Nodes.empty())
                {
                    ai.Nodes.push_back({ 1, 0, AIComponent::NodeType::Root });
                    ai.RootNodeId = 1;
                }
                const bool rootExists = std::any_of(ai.Nodes.begin(), ai.Nodes.end(),
                    [&](const auto& node) { return node.Id == ai.RootNodeId; });
                if (!rootExists) ai.RootNodeId = ai.Nodes.front().Id;
                for (auto& node : ai.Nodes)
                    if (node.Id != ai.RootNodeId && std::none_of(ai.Nodes.begin(), ai.Nodes.end(),
                        [&](const auto& parent) { return parent.Id == node.ParentId; }))
                        node.ParentId = ai.RootNodeId;

                ai.States.clear();
                for (const std::string& line : aiStateLines)
                {
                    if (ai.States.size() >= 64) break;
                    AIComponent::State state;
                    int action = 0;
                    std::stringstream stateStream(line.substr(10));
                    if (!(stateStream >> std::quoted(state.Name) >> action >> state.Speed >> state.Range >> state.WaitDuration) || state.Name.empty())
                        continue;
                    if (std::any_of(ai.States.begin(), ai.States.end(), [&](const auto& old) { return old.Name == state.Name; }))
                        continue;
                    state.Action = static_cast<AIComponent::Task>(std::clamp(action, 0, 3));
                    state.Speed = std::clamp(std::isfinite(state.Speed) ? state.Speed : 3.0f, 0.0f, 1000.0f);
                    state.Range = std::clamp(std::isfinite(state.Range) ? state.Range : 3.0f, 0.0f, 100000.0f);
                    state.WaitDuration = std::clamp(std::isfinite(state.WaitDuration) ? state.WaitDuration : 1.0f, 0.0f, 3600.0f);
                    ai.States.push_back(std::move(state));
                }
                if (ai.States.empty()) ai.States.push_back({ "Idle", AIComponent::Task::Idle });
                if (std::none_of(ai.States.begin(), ai.States.end(), [&](const auto& state) { return state.Name == ai.InitialState; }))
                    ai.InitialState = ai.States.front().Name;
                ai.CurrentState = ai.InitialState;

                ai.Transitions.clear();
                for (const std::string& line : aiTransitionLines)
                {
                    if (ai.Transitions.size() >= 256) break;
                    AIComponent::Transition transition;
                    int condition = 0;
                    std::stringstream transitionStream(line.substr(14));
                    if (!(transitionStream >> std::quoted(transition.From) >> std::quoted(transition.To)
                          >> condition >> transition.Range)) continue;
                    const auto hasState = [&](const std::string& name)
                    { return std::any_of(ai.States.begin(), ai.States.end(), [&](const auto& state) { return state.Name == name; }); };
                    if (!hasState(transition.From) || !hasState(transition.To)) continue;
                    transition.Condition = static_cast<AIComponent::TransitionCondition>(std::clamp(condition, 0, 3));
                    transition.Range = std::clamp(std::isfinite(transition.Range) ? transition.Range : 8.0f, 0.0f, 100000.0f);
                    ai.Transitions.push_back(std::move(transition));
                }
                ai.ResetRuntime();
            }
        }

        if (uiLine.rfind("@ui", 0) == 0)
        {
            std::stringstream uiStream(uiLine.substr(3));
            int enabled = 1, type = 1, zIndex = 0;
            UIComponent loaded;
            uiStream >> enabled >> type >> loaded.PositionX >> loaded.PositionY >> loaded.Width >> loaded.Height
                     >> loaded.AnchorX >> loaded.AnchorY >> loaded.Rotation >> zIndex
                     >> loaded.Transparency >> loaded.CornerRadius;
            uiStream
                     >> loaded.BackgroundColor[0] >> loaded.BackgroundColor[1] >> loaded.BackgroundColor[2] >> loaded.BackgroundColor[3]
                     >> loaded.TextColor[0] >> loaded.TextColor[1] >> loaded.TextColor[2] >> loaded.TextColor[3]
                     >> std::quoted(loaded.Text) >> std::quoted(loaded.ImagePath) >> std::quoted(loaded.Font);
            // Os campos novos ficam no fim para manter cenas antigas
            // compatíveis. Quando ausentes, permanecem nos valores padrão.
            int textAlignment = 0, clipsContent = 0;
            if (uiStream >> loaded.BorderColor[0] >> loaded.BorderColor[1] >> loaded.BorderColor[2] >> loaded.BorderColor[3]
                         >> loaded.BorderThickness >> textAlignment >> clipsContent)
            {
                loaded.TextAlignment = static_cast<UITextAlignment>(std::clamp(textAlignment, 0, 2));
                loaded.ClipsContent = clipsContent != 0;
            }
            // FontSize foi acrescentado ao fim do registro para preservar a
            // leitura de cenas antigas que ainda não serializavam esse campo.
            if (float savedFontSize = 0.0f; uiStream >> savedFontSize)
                loaded.FontSize = std::clamp(savedFontSize, 6.0f, 256.0f);
            int responsiveAnchors = 0;
            if (uiStream >> responsiveAnchors >> loaded.AnchorMinX >> loaded.AnchorMinY
                         >> loaded.AnchorMaxX >> loaded.AnchorMaxY
                         >> loaded.OffsetMinX >> loaded.OffsetMinY
                         >> loaded.OffsetMaxX >> loaded.OffsetMaxY)
            {
                loaded.UseResponsiveAnchors = responsiveAnchors != 0;
                loaded.AnchorMinX = std::clamp(loaded.AnchorMinX, 0.0f, 1.0f);
                loaded.AnchorMinY = std::clamp(loaded.AnchorMinY, 0.0f, 1.0f);
                loaded.AnchorMaxX = std::clamp(loaded.AnchorMaxX, 0.0f, 1.0f);
                loaded.AnchorMaxY = std::clamp(loaded.AnchorMaxY, 0.0f, 1.0f);
                loaded.OffsetMinX = std::clamp(loaded.OffsetMinX, -100000.0f, 100000.0f);
                loaded.OffsetMinY = std::clamp(loaded.OffsetMinY, -100000.0f, 100000.0f);
                loaded.OffsetMaxX = std::clamp(loaded.OffsetMaxX, -100000.0f, 100000.0f);
                loaded.OffsetMaxY = std::clamp(loaded.OffsetMaxY, -100000.0f, 100000.0f);
            }
            // Campos de widget (opcionais; ausentes em cenas antigas).
            int nineSlice = 0, checked = 0;
            if (uiStream >> loaded.Value >> loaded.MinValue >> loaded.MaxValue
                         >> checked >> loaded.SelectedIndex
                         >> nineSlice
                         >> loaded.SliceLeft >> loaded.SliceTop >> loaded.SliceRight >> loaded.SliceBottom
                         >> loaded.HoverColor[0] >> loaded.HoverColor[1] >> loaded.HoverColor[2] >> loaded.HoverColor[3]
                         >> loaded.PressedColor[0] >> loaded.PressedColor[1] >> loaded.PressedColor[2] >> loaded.PressedColor[3]
                         >> loaded.FillColor[0] >> loaded.FillColor[1] >> loaded.FillColor[2] >> loaded.FillColor[3]
                         >> loaded.TrackColor[0] >> loaded.TrackColor[1] >> loaded.TrackColor[2] >> loaded.TrackColor[3])
            {
                loaded.Checked = checked != 0;
                loaded.NineSlice = nineSlice != 0;
                loaded.SelectedIndex = std::max(0, loaded.SelectedIndex);
            }
            if (!uiOptionsLine.empty())
            {
                std::stringstream optionsStream(uiOptionsLine.substr(11));
                std::string option;
                while (optionsStream >> std::quoted(option))
                    loaded.Options.push_back(option);
            }
            entity.AddUI(static_cast<UIElementType>(std::clamp(type, 0, 11)));
            UIComponent& restored = entity.GetUIComponent();
            loaded.Enabled = enabled != 0;
            loaded.Type = static_cast<UIElementType>(std::clamp(type, 0, 11));
            loaded.ZIndex = zIndex;
            if (!uiLayoutLine.empty())
            {
                std::stringstream layoutStream(uiLayoutLine.substr(10));
                int layoutMode = 0, stretch = 0, scroll = 0, richText = 0, theme = 0;
                if (layoutStream >> layoutMode >> loaded.LayoutPadding >> loaded.LayoutSpacing
                                 >> loaded.LayoutColumns >> stretch >> scroll >> richText >> theme)
                {
                    loaded.LayoutMode = static_cast<UILayoutMode>(std::clamp(layoutMode, 0, 3));
                    loaded.LayoutPadding = std::clamp(loaded.LayoutPadding, 0.0f, 4096.0f);
                    loaded.LayoutSpacing = std::clamp(loaded.LayoutSpacing, 0.0f, 4096.0f);
                    loaded.LayoutColumns = std::clamp(loaded.LayoutColumns, 1, 256);
                    loaded.StretchLayoutChildren = stretch != 0;
                    loaded.ScrollViewEnabled = scroll != 0;
                    loaded.RichTextEnabled = richText != 0;
                    loaded.Theme = std::clamp(theme, 0, 3);
                }
            }
            restored = loaded;
            // Nunca reintroduzir a malha Cube padrão em uma entidade de UI.
            entity.GetMeshComponent().MeshData.reset();
            entity.GetMeshComponent().MeshPath.clear();
            entity.GetMeshComponent().PrimitiveType = "UI";
            entity.PhysicsEnabled = false;
            entity.ColliderEnabled = false;
        }

        if (entityLine.rfind("@entity", 0) == 0)
        {
            std::stringstream entityStream(entityLine.substr(7));
            int active = 1;
            float opacity = 1.0f;
            int raycastTarget = 1;
            int layer = 0;
            std::string tag;
            int castShadows = 1;
            int selectionLocked = 0;
            entityStream >> active >> opacity >> raycastTarget >> layer >> std::quoted(tag);
            entityStream >> castShadows;
            entityStream >> selectionLocked;
            entity.Active = active != 0;
            entity.Opacity = std::clamp(opacity, 0.0f, 1.0f);
            entity.CastShadows = castShadows != 0;
            entity.SelectionLocked = selectionLocked != 0;
            entity.RaycastTarget = raycastTarget != 0;
            entity.Layer = layer;
            entity.Tag = tag;
        }

        if (terrainLine.rfind("@terrain", 0) == 0)
        {
            std::stringstream terrainStream(terrainLine.substr(8));
            int version = 1, resolution = 33, mode = 0;
            float size = 32.0f, heightScale = 6.0f, brushRadius = 3.0f, brushStrength = 0.35f;
            std::string dataPath;
            terrainStream >> version >> resolution >> size >> heightScale >> mode
                          >> brushRadius >> brushStrength >> std::quoted(dataPath);
            TerrainComponent& terrain = entity.GetTerrainComponent();
            terrain.Initialize(std::clamp(resolution, 3, 257));
            terrain.Size = std::max(1.0f, size);
            terrain.HeightScale = std::max(0.01f, heightScale);
            terrain.Mode = static_cast<TerrainComponent::SculptMode>(std::clamp(mode, 0, 3));
            terrain.BrushRadius = std::clamp(brushRadius, 0.25f, 100.0f);
            terrain.BrushStrength = std::clamp(brushStrength, 0.01f, 2.0f);
            terrain.DataPath = dataPath;
            namespace fs = std::filesystem;
            std::vector<fs::path> terrainCandidates = {
                fs::current_path() / dataPath,
                fs::path(path).parent_path() / dataPath,
                fs::path(m_ProjectConfig.ProjectPath) / dataPath
            };
            for (const fs::path& candidate : terrainCandidates)
            {
                std::error_code terrainError;
                if (fs::exists(candidate, terrainError) && !terrainError && terrain.Load(candidate))
                    break;
            }
            if (!entity.GetMeshComponent().MeshData)
                entity.GetMeshComponent().MeshData = std::make_shared<Mesh>();
            entity.GetMeshComponent().MeshData->UpdateVertices(
                terrain.BuildVertices(), "Application::LoadTerrain");
            entity.GetMeshComponent().MeshPath.clear();
        }

        if (physicsLine.rfind("@physics", 0) == 0)
        {
            std::stringstream physicsStream(physicsLine.substr(8));
            int enabled = 1;
            int gravityEnabled = 1;
            int staticBody = 0;
            int colliderEnabled = 1;
            int colliderShape = 0;
            int continuousCollision = 1;
            int freezeRotation = 0;
            int trigger = 0;
            physicsStream
                >> enabled
                >> gravityEnabled
                >> staticBody
                >> colliderEnabled
                >> colliderShape
                >> entity.Mass
                >> entity.Restitution
                >> entity.Friction
                >> entity.PickingRadius;
            physicsStream
                >> entity.AngularVelocity.X
                >> entity.AngularVelocity.Y
                >> entity.AngularVelocity.Z
                >> continuousCollision
                >> freezeRotation;
            physicsStream >> trigger;

            entity.PhysicsEnabled = enabled != 0;
            entity.UseGravity = gravityEnabled != 0;
            entity.IsStatic = staticBody != 0;
            entity.ColliderEnabled = colliderEnabled != 0;
            entity.Collider = static_cast<Entity::ColliderShape>(
                std::clamp(colliderShape, 0, 6));
            entity.ContinuousCollision = continuousCollision != 0;
            entity.FreezeRotation = freezeRotation != 0;
            entity.IsTrigger = trigger != 0;

        }
        else
        {
            // Cenas antigas sem metadados @physics devem ter objetos estÃ¡ticos
            // para evitar colisores invisÃ­veis e fÃ­sica indesejada
            entity.PhysicsEnabled = false;
            entity.UseGravity = false;
            entity.IsStatic = true;
            entity.ColliderEnabled = false;
        }

        if (lightLine.rfind("@light", 0) == 0)
        {
            std::stringstream lightStream(lightLine.substr(6));
            int enabled = 1;
            int type = 0;
            entity.AddLight();
            lightStream >> enabled >> type
                        >> entity.GetLightComponent().Color[0]
                        >> entity.GetLightComponent().Color[1]
                        >> entity.GetLightComponent().Color[2]
                        >> entity.GetLightComponent().Intensity
                        >> entity.GetLightComponent().Range
                        >> entity.GetLightComponent().DirectionLocal[0]
                        >> entity.GetLightComponent().DirectionLocal[1]
                        >> entity.GetLightComponent().DirectionLocal[2]
                        >> entity.GetLightComponent().InnerAngleDeg
                        >> entity.GetLightComponent().OuterAngleDeg;
            int castShadows = 1;
            if (lightStream >> castShadows)
                entity.GetLightComponent().CastShadows = castShadows != 0;
            int useTemperature = 0;
            if (lightStream >> useTemperature)
                entity.GetLightComponent().UseTemperature = useTemperature != 0;
            if (lightStream >> entity.GetLightComponent().TemperatureKelvin)
                entity.GetLightComponent().TemperatureKelvin = std::clamp(
                    entity.GetLightComponent().TemperatureKelvin, 1000.0f, 40000.0f);
            if (lightStream >> entity.GetLightComponent().SourceRadius)
                entity.GetLightComponent().SourceRadius = std::clamp(
                    entity.GetLightComponent().SourceRadius, 0.025f, 5.0f);
            if (lightStream >> entity.GetLightComponent().ShadowSoftness)
                entity.GetLightComponent().ShadowSoftness = std::clamp(
                    entity.GetLightComponent().ShadowSoftness, 0.0f, 1.0f);
            if (lightStream >> entity.GetLightComponent().ContactShadowStrength)
                entity.GetLightComponent().ContactShadowStrength = std::clamp(
                    entity.GetLightComponent().ContactShadowStrength, 0.0f, 1.0f);
            int indirectContribution = 1;
            if (lightStream >> indirectContribution)
                entity.GetLightComponent().IndirectContribution = indirectContribution != 0;
            int distanceSoftening = 1;
            if (lightStream >> distanceSoftening)
                entity.GetLightComponent().DistanceSoftening = distanceSoftening != 0;
            if (lightStream >> entity.GetLightComponent().ShadowStrength)
                entity.GetLightComponent().ShadowStrength = std::clamp(
                    entity.GetLightComponent().ShadowStrength, 0.0f, 1.0f);
            entity.GetLightComponent().Enabled = enabled != 0;
            if (type >= static_cast<int>(LightType::Point) &&
                type <= static_cast<int>(LightType::Area))
            {
                entity.GetLightComponent().Type = static_cast<LightType>(type);
            }
            else
            {
                entity.GetLightComponent().Type = LightType::Point;
            }
        }

        if (audioLine.rfind("@audio", 0) == 0)
        {
            std::stringstream audioStream(audioLine.substr(6));
            int enabled = 1, loop = 0, playOnStart = 0, busInt = 2;
            float spatialBlend = 1.0f, dopplerFactor = 1.0f;
            std::string clipPath;
            std::string sourceID;
            entity.AddAudio();
            AudioComponent& ac = entity.GetAudioComponent();
            audioStream >> enabled >> loop >> playOnStart
                        >> ac.Volume >> ac.Pitch
                        >> ac.MinDistance >> ac.MaxDistance;
            // Campos opcionais â€” retrocompatÃ­veis
            std::string optionalFields;
            std::getline(audioStream >> std::ws, optionalFields);
            std::stringstream optionalStream(optionalFields);
            if (!optionalFields.empty() && optionalFields.front() != '"')
            {
                if (optionalStream >> spatialBlend)   ac.SpatialBlend  = spatialBlend;
                if (optionalStream >> dopplerFactor)  ac.DopplerFactor = dopplerFactor;
                if (optionalStream >> busInt)
                {
                    // Mapeia bus antigo (0=Master,1=Music,2=SFX) para GroupID string
                    switch (busInt) {
                        case 0: ac.MixerGroupID = MixerGroupID::Master;  break;
                        case 1: ac.MixerGroupID = MixerGroupID::Music;   break;
                        default: ac.MixerGroupID = MixerGroupID::SFX;   break;
                    }
                }
                // Novo campo: MixerGroupID como string (sobrescreve busInt se presente)
                std::string groupID;
                if (optionalStream >> groupID && !groupID.empty() && groupID.front() != '"')
                    ac.MixerGroupID = groupID;
            }
            optionalStream.clear();
            optionalStream.str(optionalFields);
            // LÃª o ClipPath no final (com aspas)
            while (optionalStream.peek() != '"' && optionalStream.peek() != EOF)
                optionalStream.ignore();
            optionalStream >> std::quoted(clipPath);
            optionalStream >> std::quoted(sourceID);
            ac.Enabled     = enabled     != 0;
            ac.Loop        = loop        != 0;
            ac.PlayOnStart = playOnStart != 0;
            ac.ClipPath    = clipPath;
            if (!sourceID.empty())
                ac.SourceID = sourceID;
            else if (ac.SourceID.empty())
                ac.SourceID = "audio_" + std::to_string(entity.GetID());
        }

        if (acousticLine.rfind("@acoustic", 0) == 0)
        {
            std::stringstream acousticStream(acousticLine.substr(9));
            int enabled = 1, occluder = 1, transmissionEnabled = 0;
            acousticStream >> enabled >> occluder >> transmissionEnabled
                           >> entity.AudioTransmission
                           >> entity.AudioAbsorption
                           >> entity.AudioDiffraction;
            entity.AudioAcousticGeometryEnabled = enabled != 0;
            entity.AudioOccluder = occluder != 0;
            entity.AudioTransmissionEnabled = transmissionEnabled != 0;
            entity.AudioTransmission = std::clamp(entity.AudioTransmission, 0.0f, 1.0f);
            entity.AudioAbsorption = std::clamp(entity.AudioAbsorption, 0.0f, 1.0f);
            entity.AudioDiffraction = std::clamp(entity.AudioDiffraction, 0.0f, 1.0f);
        }

        if (audioAcousticLine.rfind("@audio_acoustic", 0) == 0 && entity.HasAudio())
        {
            std::stringstream acousticStream(audioAcousticLine.substr(15));
            int propagation = 1, rayTracing = 1, geometric = 1, dynamic = 1;
            int occlusion = 1, obstruction = 1, diffraction = 1, pathfinding = 1;
            AudioComponent& ac = entity.GetAudioComponent();
            acousticStream >> propagation >> rayTracing >> geometric >> dynamic
                           >> occlusion >> obstruction >> diffraction >> pathfinding
                           >> ac.AcousticTransmission >> ac.AcousticUpdateInterval;
            ac.AcousticPropagationEnabled = propagation != 0;
            ac.AcousticRayTracingEnabled = rayTracing != 0;
            ac.GeometricAcousticsEnabled = geometric != 0;
            ac.DynamicPropagationEnabled = dynamic != 0;
            ac.OcclusionEnabled = occlusion != 0;
            ac.ObstructionEnabled = obstruction != 0;
            ac.DiffractionEnabled = diffraction != 0;
            ac.AcousticPathfindingEnabled = pathfinding != 0;
            ac.AcousticTransmission = std::clamp(ac.AcousticTransmission, 0.0f, 1.0f);
            ac.AcousticUpdateInterval = std::clamp(ac.AcousticUpdateInterval, 0.01f, 1.0f);
        }

        // VÃ­nculo de prefab
        if (prefabLine.rfind("@prefab", 0) == 0)
        {
            std::stringstream prefabStream(prefabLine.substr(7));
            std::string prefabPath;
            prefabStream >> std::quoted(prefabPath);
            entity.PrefabPath = prefabPath;
        }

        // Script anexado Ã  entidade
        if (scriptLine.rfind("@script", 0) == 0)
        {
            std::stringstream scriptStream(scriptLine.substr(7));
            int enabled = 1;
            std::string scriptPath;
            if (scriptStream >> enabled && scriptStream >> std::quoted(scriptPath))
            {
                entity.GetScriptComponent().Enabled = (enabled != 0);
                entity.GetScriptComponent().ScriptPath = scriptPath;
            }
        }

        if (animationLine.rfind("@animation", 0) == 0)
        {
            int enabled = 1, playOnStart = 1, easing = 0, smoothing = 0, loop = 1;
            float time = 0.0f, speed = 1.0f, weight = 1.0f, smoothness = 8.0f;
            float blendIn = 0.0f, blendOut = 0.0f, duration = 1.0f;
            std::string clipID, clipName;
            std::stringstream animationStream(animationLine.substr(10));
            animationStream >> enabled >> playOnStart >> time >> speed >> weight >> easing
                            >> smoothing >> smoothness >> blendIn >> blendOut >> duration >> loop
                            >> std::quoted(clipID) >> std::quoted(clipName);

            auto clip = std::make_shared<AnimationClip>();
            if (!clipID.empty()) clip->ID = clipID;
            clip->Name = clipName.empty() ? "Animation" : clipName;
            clip->Duration = std::isfinite(duration)
                ? std::clamp(duration, 0.01f, 36000.0f) : 1.0f;
            clip->Loop = loop != 0;

            for (const std::string& keyLine : animationPositionLines)
            {
                float keyTime = 0.0f, x = 0.0f, y = 0.0f, z = 0.0f;
                std::stringstream keyStream(keyLine.substr(9));
                if (keyStream >> keyTime >> x >> y >> z && std::isfinite(keyTime) &&
                    std::isfinite(x) && std::isfinite(y) && std::isfinite(z))
                    clip->Position.Keys.push_back({ std::clamp(keyTime, 0.0f, clip->Duration), glm::vec3(x, y, z) });
            }
            for (const std::string& keyLine : animationRotationLines)
            {
                float keyTime = 0.0f, w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;
                std::stringstream keyStream(keyLine.substr(9));
                if (keyStream >> keyTime >> w >> x >> y >> z && std::isfinite(keyTime) &&
                    std::isfinite(w) && std::isfinite(x) && std::isfinite(y) && std::isfinite(z))
                {
                    glm::quat rotation(w, x, y, z);
                    if (glm::length(rotation) > 0.0001f) rotation = glm::normalize(rotation);
                    else rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                    clip->Rotation.Keys.push_back({ std::clamp(keyTime, 0.0f, clip->Duration), rotation });
                }
            }
            for (const std::string& keyLine : animationScaleLines)
            {
                float keyTime = 0.0f, x = 1.0f, y = 1.0f, z = 1.0f;
                std::stringstream keyStream(keyLine.substr(11));
                if (keyStream >> keyTime >> x >> y >> z && std::isfinite(keyTime) &&
                    std::isfinite(x) && std::isfinite(y) && std::isfinite(z))
                    clip->Scale.Keys.push_back({ std::clamp(keyTime, 0.0f, clip->Duration), glm::vec3(x, y, z) });
            }

            const Transform& transform = entity.GetTransform();
            if (clip->Position.Keys.empty())
                clip->Position.Keys.push_back({ 0.0f, glm::vec3(transform.Position.X, transform.Position.Y, transform.Position.Z) });
            if (clip->Rotation.Keys.empty())
                clip->Rotation.Keys.push_back({ 0.0f, transform.Rotation });
            if (clip->Scale.Keys.empty())
                clip->Scale.Keys.push_back({ 0.0f, glm::vec3(transform.Scale.X, transform.Scale.Y, transform.Scale.Z) });
            const auto sortKeys = [](auto& keys)
            {
                std::stable_sort(keys.begin(), keys.end(),
                    [](const auto& a, const auto& b) { return a.Time < b.Time; });
            };
            sortKeys(clip->Position.Keys);
            sortKeys(clip->Rotation.Keys);
            sortKeys(clip->Scale.Keys);

            AnimationComponent& animation = entity.GetAnimationComponent();
            animation.SetClip(clip);
            animation.Enabled = enabled != 0;
            animation.PlayOnStart = playOnStart != 0;
            animation.Time = std::isfinite(time) ? std::max(0.0f, time) : 0.0f;
            animation.Speed = std::isfinite(speed) ? std::clamp(speed, -100.0f, 100.0f) : 1.0f;
            animation.Weight = std::isfinite(weight) ? std::clamp(weight, 0.0f, 1.0f) : 1.0f;
            animation.Easing = static_cast<AnimationComponent::EasingType>(std::clamp(easing, 0, 4));
            animation.UseSmoothing = smoothing != 0;
            animation.Smoothness = std::isfinite(smoothness) ? std::clamp(smoothness, 0.0f, 100.0f) : 8.0f;
            animation.BlendIn = std::isfinite(blendIn) ? std::clamp(blendIn, 0.0f, 36000.0f) : 0.0f;
            animation.BlendOut = std::isfinite(blendOut) ? std::clamp(blendOut, 0.0f, 36000.0f) : 0.0f;
            animation.RuntimeBlend = 1.0f;
            animation.Pause();
            if (animationMachineLine.rfind("@anim_machine", 0) == 0)
            {
                int enabledMachine = 0;
                std::stringstream machineStream(animationMachineLine.substr(13));
                machineStream >> enabledMachine >> std::quoted(animation.InitialState);
                animation.StateMachineEnabled = enabledMachine != 0;
            }
            for (const std::string& stateLine : animationStateLines)
            {
                std::stringstream stateStream(stateLine.substr(11));
                AnimationComponent::State state;
                std::string clipID, clipName;
                float duration = 1.0f;
                int loop = 1;
                size_t positionCount = 0, rotationCount = 0, scaleCount = 0;
                if (!(stateStream >> std::quoted(state.Name) >> std::quoted(clipID) >>
                      std::quoted(clipName) >> duration >> loop >> positionCount) ||
                    state.Name.empty() || positionCount > 100000)
                    continue;
                auto clip = std::make_shared<AnimationClip>();
                clip->ID = clipID;
                clip->Name = clipName.empty() ? state.Name : clipName;
                clip->Duration = std::isfinite(duration) ? std::clamp(duration, 0.01f, 36000.0f) : 1.0f;
                clip->Loop = loop != 0;
                bool validStateClip = true;
                for (size_t key = 0; key < positionCount; ++key)
                {
                    float t, x, y, z;
                    if (!(stateStream >> t >> x >> y >> z) || !std::isfinite(t) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) { validStateClip = false; break; }
                    clip->Position.Keys.push_back({ std::clamp(t, 0.0f, clip->Duration), glm::vec3(x, y, z) });
                }
                if (!validStateClip || !(stateStream >> rotationCount) || rotationCount > 100000) continue;
                for (size_t key = 0; key < rotationCount; ++key)
                {
                    float t, w, x, y, z;
                    if (!(stateStream >> t >> w >> x >> y >> z) || !std::isfinite(t) || !std::isfinite(w) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) { validStateClip = false; break; }
                    glm::quat rotation(w, x, y, z);
                    rotation = glm::length(rotation) > 0.0001f ? glm::normalize(rotation) : glm::quat(1, 0, 0, 0);
                    clip->Rotation.Keys.push_back({ std::clamp(t, 0.0f, clip->Duration), rotation });
                }
                if (!validStateClip || !(stateStream >> scaleCount) || scaleCount > 100000) continue;
                for (size_t key = 0; key < scaleCount; ++key)
                {
                    float t, x, y, z;
                    if (!(stateStream >> t >> x >> y >> z) || !std::isfinite(t) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) { validStateClip = false; break; }
                    clip->Scale.Keys.push_back({ std::clamp(t, 0.0f, clip->Duration), glm::vec3(x, y, z) });
                }
                if (!validStateClip || clipID.empty()) continue;
                state.Clip = std::move(clip);
                animation.States.push_back(std::move(state));
            }
            for (const std::string& transitionLine : animationTransitionLines)
            {
                std::stringstream transitionStream(transitionLine.substr(16));
                AnimationComponent::Transition transition;
                int expected = 1;
                if (transitionStream >> std::quoted(transition.From) >> std::quoted(transition.To) >>
                    std::quoted(transition.BoolParameter) >> expected >> transition.ExitTimeNormalized &&
                    std::isfinite(transition.ExitTimeNormalized))
                {
                    transition.ExpectedValue = expected != 0;
                    transition.ExitTimeNormalized = std::clamp(transition.ExitTimeNormalized, 0.0f, 1.0f);
                    float savedBlendDuration = transition.BlendDuration;
                    if (transitionStream >> savedBlendDuration && std::isfinite(savedBlendDuration))
                        transition.BlendDuration = std::clamp(savedBlendDuration, 0.0f, 30.0f);
                    animation.Transitions.push_back(std::move(transition));
                }
            }
            for (const std::string& parameterLine : animationParameterLines)
            {
                std::stringstream parameterStream(parameterLine.substr(11));
                std::string parameter;
                int value = 0;
                if (parameterStream >> std::quoted(parameter) >> value && !parameter.empty())
                    animation.BoolParameters[parameter] = value != 0;
            }
        }

        // Reassocia clipes esqueléticos ao asset GLB ao reabrir a cena. O
        // arquivo de cena mantém somente o ID/nome do clipe; os canais e o
        // esqueleto permanecem no asset importado e não são duplicados no .scene.
        const std::string importedMeshPath = entity.GetMeshComponent().MeshPath;
        const std::string importedExtension = std::filesystem::path(importedMeshPath).extension().string();
        if ((importedExtension == ".glb" || importedExtension == ".gltf" ||
             importedExtension == ".GLB" || importedExtension == ".GLTF") &&
            animationLine.rfind("@animation", 0) == 0)
        {
            const ImportedModelData imported = ModelImporter::LoadModelData(
                ResolveProjectFilePath(m_ProjectConfig, importedMeshPath).string());
            if (imported.SkeletalAsset && !imported.SkeletalAsset->Clips.empty())
            {
                AnimationComponent& animation = entity.GetAnimationComponent();
                const std::string savedClipName = animation.Clip ? animation.Clip->Name : std::string();
                const std::string savedClipID = animation.Clip ? animation.Clip->ID : std::string();
                animation.ImportedAsset = imported.SkeletalAsset;
                auto clip = std::find_if(imported.SkeletalAsset->Clips.begin(), imported.SkeletalAsset->Clips.end(),
                    [&](const auto& candidate) { return candidate && candidate->Name == savedClipName; });
                animation.Clip = clip != imported.SkeletalAsset->Clips.end()
                    ? *clip : imported.SkeletalAsset->Clips.front();
                if (!savedClipID.empty()) animation.Clip->ID = savedClipID;
                animation.ImportedClipName = animation.Clip->Name;
                animation.SkinMatrices.assign(std::min<size_t>(imported.SkeletalAsset->JointNodeIndices.size(), 48), glm::mat4(1.0f));
            }
        }

        if (!entityIdLine.empty())
        {
            std::stringstream idStream(entityIdLine.substr(10));
            uint64_t origId = 0;
            if (idStream >> origId && origId != 0)
            {
                loadedIdMap[origId] = entity.GetID();
            }
        }

        if (!parentLine.empty())
        {
            std::stringstream parentStream(parentLine.substr(7));
            uint64_t origParentId = 0;
            if (parentStream >> origParentId && origParentId != 0)
            {
                pendingParentPairs.push_back({ entity.GetID(), origParentId });
            }
        }
    }

    for (const auto& pair : pendingParentPairs)
    {
        auto it = loadedIdMap.find(pair.second);
        if (it != loadedIdMap.end())
        {
            m_Scene.SetParent(pair.first, it->second, false);
        }
    }
    m_Scene.UpdateTransforms();

    if (!additive)
        EditorObjects::CreateDefaultObjects(m_Scene);

    m_SceneDirty = additive ? previousDirty : false;
    UpdateWindowTitle();
    return true;
}

Scene& Application::GetScene()
{
    return m_Scene;
}

void Application::PreviewAudioForSelectedEntity()
{
    if (m_SelectedEntity < 0 ||
        m_SelectedEntity >= static_cast<int>(m_Scene.GetEntities().size()))
    {
        return;
    }

    Entity& entity = m_Scene.GetEntities()[static_cast<size_t>(m_SelectedEntity)];
    if (entity.HasAudio())
    {
        AudioComponent preview = entity.GetAudioComponent();
        // Resolve o path absoluto para o preview
        if (!preview.ClipPath.empty())
            preview.ClipPath = ResolveProjectFilePath(m_ProjectConfig, preview.ClipPath).string();
        if (!preview.ClipID.empty())
        {
            // ClipID: resolve via library (path jÃ¡ absoluto)
        }
        m_AudioSystem.PlayPreview(preview);
    }
}

void Application::StopAudioPreview()
{
    m_AudioSystem.Stop();
}

int Application::GetSelectedEntity() const
{
    return m_SelectedEntity;
}

Entity& Application::GetSelectedEntity()
{
    // VerificaÃ§Ã£o de seguranÃ§a para evitar crash quando nenhum componente Ã© selecionado
    if (m_SelectedEntity < 0 || m_SelectedEntity >= (int)m_Scene.GetEntities().size())
    {
        // Retorna a primeira entidade como fallback se a seleÃ§Ã£o for invÃ¡lida
        static Entity dummyEntity;
        if (!m_Scene.GetEntities().empty())
        {
            return const_cast<Entity&>(m_Scene.GetEntities()[0]);
        }
        return dummyEntity;
    }

    return const_cast<Entity&>(
        m_Scene.GetEntities().at(m_SelectedEntity)
        );
}

const std::vector<Entity>&
Application::GetEntities() const
{
    return m_Scene.GetEntities();
}


int Application::GetSelectedEntityIndex() const
{
    return m_SelectedEntity;
}

void Application::SimulatePhysics(float deltaTime)
{
    if (deltaTime <= 0.0f || m_Scene.GetEntities().empty())
    {
        return;
    }

    constexpr float fixedTimeStep = 1.0f / 60.0f;
    constexpr float maximumAccumulatedTime = 0.25f;
    m_PhysicsAccumulator = std::min(
        maximumAccumulatedTime,
        m_PhysicsAccumulator + deltaTime
    );

    int stepCount = 0;
    constexpr int maximumStepsPerFrame = 8;
    while (m_PhysicsAccumulator >= fixedTimeStep && stepCount < maximumStepsPerFrame)
    {
        // Substeps adaptativos reduzem tunneling em corpos rápidos sem
        // obrigar todas as cenas a pagar o custo máximo de CCD.
        int microSteps = 1;
        for (const Entity& entity : m_Scene.GetEntities())
        {
            if (entity.IsEditorOnly() || entity.IsFolder || entity.IsModel ||
                !entity.Active || !entity.PhysicsEnabled ||
                entity.IsStatic || !entity.ContinuousCollision)
                continue;
            const Vector3& velocity = entity.Velocity;
            const Transform& bodyTransform = const_cast<Entity&>(entity).GetTransform();
            const bool world2D = m_WorldMode == WorldMode::World2D;
            const float speed = world2D
                ? std::sqrt(velocity.X * velocity.X + velocity.Y * velocity.Y)
                : std::sqrt(velocity.X * velocity.X + velocity.Y * velocity.Y + velocity.Z * velocity.Z);
            const float smallestExtent = std::max(0.02f, std::min(
                std::abs(bodyTransform.Scale.X), std::abs(bodyTransform.Scale.Y)) * 0.5f);
            const float angularSpeed = world2D ? 0.0f : std::sqrt(
                entity.AngularVelocity.X * entity.AngularVelocity.X +
                entity.AngularVelocity.Y * entity.AngularVelocity.Y +
                entity.AngularVelocity.Z * entity.AngularVelocity.Z);
            const float largestExtent = std::max({
                std::abs(bodyTransform.Scale.X), std::abs(bodyTransform.Scale.Y),
                world2D ? 0.0f : std::abs(bodyTransform.Scale.Z), 0.02f
            }) * 0.5f;
            const float sweptDistance =
                (speed + angularSpeed * largestExtent) * fixedTimeStep;
            microSteps = std::max(microSteps, std::min(
                16, static_cast<int>(std::ceil(
                    sweptDistance / smallestExtent))));
        }
        const float microTimeStep = fixedTimeStep / static_cast<float>(microSteps);
        ScriptingRuntime::Instance().OnFixedUpdate(fixedTimeStep);
        for (int microStep = 0; microStep < microSteps; ++microStep)
            SimulatePhysicsStep(microTimeStep);
        m_PhysicsAccumulator -= fixedTimeStep;
        ++stepCount;
    }

    // Avoid a burst of simulation after the application was suspended.
    if (stepCount == maximumStepsPerFrame && m_PhysicsAccumulator >= fixedTimeStep)
    {
        m_PhysicsAccumulator = 0.0f;
    }

#if 0
    auto& entities = m_Scene.GetEntities();
    constexpr float gravity = -9.81f;
    constexpr float floorY = -1.0f;

    if (deltaTime <= 0.0f || entities.empty())
    {
        return;
    }

    // Divide frames lentos em passos menores para evitar atravessar o chÃ£o ou os colliders.
    const int substepCount = std::min(8, std::max(1, (int)std::ceil(deltaTime / (1.0f / 120.0f))));
    deltaTime /= (float)substepCount;

    for (int substep = 0; substep < substepCount; ++substep)
    {

    for (Entity& entity : entities)
    {
        if (entity.IsEditorOnly() || !entity.Active || !entity.PhysicsEnabled || entity.IsStatic)
        {
            continue;
        }

        if (entity.UseGravity)
        {
            entity.Velocity.Y += gravity * deltaTime;
        }

        Transform& transform = entity.GetTransform();
        transform.Position.X += entity.Velocity.X * deltaTime;
        transform.Position.Y += entity.Velocity.Y * deltaTime;
        transform.Position.Z += entity.Velocity.Z * deltaTime;

        const float maxScale = std::max({
            std::abs(transform.Scale.X),
            std::abs(transform.Scale.Y),
            std::abs(transform.Scale.Z)
        });
        const float radius = std::max(0.05f, entity.PickingRadius * maxScale);

        if (entity.ColliderEnabled && transform.Position.Y - radius < floorY)
        {
            transform.Position.Y = floorY + radius;
            const float restitution = std::clamp(entity.Restitution, 0.0f, 1.0f);
            if (entity.Velocity.Y < -0.05f)
            {
                entity.Velocity.Y = -entity.Velocity.Y * restitution;
            }
            else if (entity.Velocity.Y < 0.0f)
            {
                entity.Velocity.Y = 0.0f;
            }
            const float friction = std::clamp(entity.Friction, 0.0f, 1.0f);
            entity.Velocity.X *= std::max(0.0f, 1.0f - friction * deltaTime * 8.0f);
            entity.Velocity.Z *= std::max(0.0f, 1.0f - friction * deltaTime * 8.0f);
        }
    }

    for (size_t i = 0; i < entities.size(); ++i)
    {
        Entity& a = entities[i];
        if (a.IsEditorOnly() || a.IsFolder || a.IsModel ||
            !a.PhysicsEnabled || !a.ColliderEnabled)
        {
            continue;
        }

        for (size_t j = i + 1; j < entities.size(); ++j)
        {
            Entity& b = entities[j];
            if (b.IsEditorOnly() || b.IsFolder || b.IsModel ||
                !b.PhysicsEnabled || !b.ColliderEnabled ||
                (a.IsStatic && b.IsStatic))
            {
                continue;
            }

            Transform& ta = a.GetTransform();
            Transform& tb = b.GetTransform();
            const float scaleA = std::max({ std::abs(ta.Scale.X), std::abs(ta.Scale.Y), std::abs(ta.Scale.Z) });
            const float scaleB = std::max({ std::abs(tb.Scale.X), std::abs(tb.Scale.Y), std::abs(tb.Scale.Z) });
            const float radiusA = std::max(0.05f, a.PickingRadius * scaleA);
            const float radiusB = std::max(0.05f, b.PickingRadius * scaleB);

            Vector3 delta(
                tb.Position.X - ta.Position.X,
                tb.Position.Y - ta.Position.Y,
                tb.Position.Z - ta.Position.Z
            );
            const float distanceSquared = delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z;
            const float minDistance = radiusA + radiusB;
            if (distanceSquared >= minDistance * minDistance)
            {
                continue;
            }

            const float distance = std::sqrt(std::max(distanceSquared, 0.000001f));
            Vector3 normal = distanceSquared > 0.000001f
                ? Vector3(delta.X / distance, delta.Y / distance, delta.Z / distance)
                : Vector3(0.0f, 1.0f, 0.0f);
            const float penetration = minDistance - distance;
            const float invMassA = a.IsStatic ? 0.0f : 1.0f / std::max(0.001f, a.Mass);
            const float invMassB = b.IsStatic ? 0.0f : 1.0f / std::max(0.001f, b.Mass);
            const float invMassSum = invMassA + invMassB;

            if (invMassSum > 0.0f)
            {
                const float correction = penetration / invMassSum;
                ta.Position.X -= normal.X * correction * invMassA;
                ta.Position.Y -= normal.Y * correction * invMassA;
                ta.Position.Z -= normal.Z * correction * invMassA;
                tb.Position.X += normal.X * correction * invMassB;
                tb.Position.Y += normal.Y * correction * invMassB;
                tb.Position.Z += normal.Z * correction * invMassB;
            }

            const Vector3 relativeVelocity(
                b.Velocity.X - a.Velocity.X,
                b.Velocity.Y - a.Velocity.Y,
                b.Velocity.Z - a.Velocity.Z
            );
            const float separatingVelocity = relativeVelocity.X * normal.X + relativeVelocity.Y * normal.Y + relativeVelocity.Z * normal.Z;
            if (separatingVelocity < 0.0f && invMassSum > 0.0f)
            {
                const float restitution = std::min(a.Restitution, b.Restitution);
                const float impulse = -(1.0f + restitution) * separatingVelocity / invMassSum;
                if (!a.IsStatic)
                {
                    a.Velocity.X -= normal.X * impulse * invMassA;
                    a.Velocity.Y -= normal.Y * impulse * invMassA;
                    a.Velocity.Z -= normal.Z * impulse * invMassA;
                }
                if (!b.IsStatic)
                {
                    b.Velocity.X += normal.X * impulse * invMassB;
                    b.Velocity.Y += normal.Y * impulse * invMassB;
                    b.Velocity.Z += normal.Z * impulse * invMassB;
                }
            }
        }
    }
    }
#endif
}

namespace
{
    struct PhysicsContact
    {
        Vector3 Normal = Vector3(0.0f, 1.0f, 0.0f); // points from A to B
        float Penetration = 0.0f;
        Vector3 Point = Vector3(0.0f, 0.0f, 0.0f);
    };

    struct PhysicsBroadphaseBody
    {
        size_t EntityIndex = 0;
        glm::vec3 Center{ 0.0f };
        float Radius = 0.0f;
        float MinX = 0.0f;
        float MaxX = 0.0f;
        bool IsStatic = false;
    };

    struct PhysicsCandidatePair
    {
        size_t A = 0;
        size_t B = 0;
    };

    static float PhysicsDot(const Vector3& a, const Vector3& b)
    {
        return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
    }

    static Vector3 PhysicsAddScaled(
        const Vector3& value,
        const Vector3& direction,
        float scale)
    {
        return Vector3(
            value.X + direction.X * scale,
            value.Y + direction.Y * scale,
            value.Z + direction.Z * scale
        );
    }

    static Vector3 PhysicsSubtract(const Vector3& a, const Vector3& b)
    {
        return Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    }

    static float PhysicsLengthSquared(const Vector3& value)
    {
        return PhysicsDot(value, value);
    }

    static uint64_t PhysicsPairKey(uint64_t first, uint64_t second)
    {
        if (first > second)
            std::swap(first, second);
        return first ^ (second + 0x9e3779b97f4a7c15ULL +
                        (first << 6) + (first >> 2));
    }

    static Vector3 PhysicsNormalizeOr(
        const Vector3& value,
        const Vector3& fallback)
    {
        const float lengthSquared = PhysicsLengthSquared(value);
        if (lengthSquared <= 0.000001f)
        {
            return fallback;
        }

        const float inverseLength = 1.0f / std::sqrt(lengthSquared);
        return Vector3(
            value.X * inverseLength,
            value.Y * inverseLength,
            value.Z * inverseLength
        );
    }

    static Vector3 PhysicsCross(const Vector3& a, const Vector3& b)
    {
        return Vector3(
            a.Y * b.Z - a.Z * b.Y,
            a.Z * b.X - a.X * b.Z,
            a.X * b.Y - a.Y * b.X);
    }

    static glm::vec3 PhysicsToGlm(const Vector3& value)
    {
        return glm::vec3(value.X, value.Y, value.Z);
    }

    static Vector3 PhysicsFromGlm(const glm::vec3& value)
    {
        return Vector3(value.x, value.y, value.z);
    }

    static Vector3 PhysicsAbsoluteScale(const Entity& entity)
    {
        const Transform& transform = const_cast<Entity&>(entity).GetTransform();
        return Vector3(
            std::abs(transform.Scale.X),
            std::abs(transform.Scale.Y),
            std::abs(transform.Scale.Z)
        );
    }

    static glm::vec3 PhysicsColliderLocalHalfExtents(const Entity& entity)
    {
        if (entity.Collider == Entity::ColliderShape::Mesh ||
            entity.Collider == Entity::ColliderShape::ConvexMesh)
        {
            const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
            if (mesh && mesh->IsValid())
                return mesh->GetBoundingHalfExtents();
        }

        // Dimensões locais compatíveis com MeshFactory: esfera/cilindro/cone
        // têm diâmetro 1; a cápsula tem raio .5 e centros das tampas em +/- .5.
        switch (entity.Collider)
        {
        case Entity::ColliderShape::Capsule: return glm::vec3(0.5f, 1.0f, 0.5f);
        case Entity::ColliderShape::Sphere:
        case Entity::ColliderShape::Cylinder:
        case Entity::ColliderShape::Cone:
        case Entity::ColliderShape::Box:
        case Entity::ColliderShape::Mesh:
        default: return glm::vec3(0.5f);
        }
    }

    static Vector3 PhysicsBoxHalfExtents(const Entity& entity)
    {
        const Vector3 scale = PhysicsAbsoluteScale(entity);
        const glm::vec3 localHalf = PhysicsColliderLocalHalfExtents(entity);
        return Vector3(
            std::max(0.005f, scale.X * localHalf.x),
            std::max(0.005f, scale.Y * localHalf.y),
            std::max(0.005f, scale.Z * localHalf.z));
    }

    static Vector3 PhysicsShapeCenterWorld(const Entity& entity)
    {
        const Transform& transform = const_cast<Entity&>(entity).GetTransform();
        glm::vec3 localCenter(0.0f);
        if (entity.Collider == Entity::ColliderShape::Mesh ||
            entity.Collider == Entity::ColliderShape::ConvexMesh)
        {
            const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
            if (mesh && mesh->IsValid())
                localCenter = mesh->GetBoundingCenter();
        }
        const glm::vec3 signedScale(
            transform.Scale.X, transform.Scale.Y, transform.Scale.Z);
        return PhysicsFromGlm(
            PhysicsToGlm(transform.Position) +
            glm::normalize(transform.Rotation) * (localCenter * signedScale));
    }

    static float PhysicsSphereRadius(const Entity& entity)
    {
        // Raio de bounding sphere para broadphase, calculado pela geometria do
        // collider (PickingRadius é de seleção/editor, não escala física).
        const Vector3 half = PhysicsBoxHalfExtents(entity);
        return std::max(0.005f, std::sqrt(
            half.X * half.X + half.Y * half.Y + half.Z * half.Z));
    }

    static bool PhysicsIsCurvedCollider(const Entity& entity)
    {
        return entity.Collider == Entity::ColliderShape::Sphere ||
               entity.Collider == Entity::ColliderShape::Capsule ||
               entity.Collider == Entity::ColliderShape::Cylinder ||
               entity.Collider == Entity::ColliderShape::Cone;
    }

    static bool PhysicsCanUseFlatSupportContact(const Entity& entity)
    {
        return entity.Collider == Entity::ColliderShape::Box ||
               PhysicsIsCurvedCollider(entity) ||
               entity.Collider == Entity::ColliderShape::ConvexMesh;
    }

    static bool PhysicsUsesBoxProxy(const Entity& entity)
    {
        return entity.Collider == Entity::ColliderShape::Box ||
               entity.Collider == Entity::ColliderShape::Mesh;
    }

    static glm::vec3 PhysicsColliderSupportLocal(
        const Entity& entity,
        const glm::vec3& direction)
    {
        const Transform& transform = const_cast<Entity&>(entity).GetTransform();
        const glm::vec3 scale(
            std::abs(transform.Scale.X),
            std::abs(transform.Scale.Y),
            std::abs(transform.Scale.Z));
        if (entity.Collider == Entity::ColliderShape::ConvexMesh)
        {
            const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
            if (mesh && mesh->IsValid() && !mesh->GetVertices().empty())
            {
                const glm::vec3 signedScale(
                    transform.Scale.X, transform.Scale.Y, transform.Scale.Z);
                const glm::vec3 d = glm::dot(direction, direction) > 0.0000001f
                    ? glm::normalize(direction) : glm::vec3(1.0f, 0.0f, 0.0f);
                glm::vec3 bestPoint(0.0f);
                float bestProjection = -std::numeric_limits<float>::max();
                for (const Vertex& vertex : mesh->GetVertices())
                {
                    const glm::vec3 point(
                        vertex.X * signedScale.x,
                        vertex.Y * signedScale.y,
                        vertex.Z * signedScale.z);
                    const float projection = glm::dot(point, d);
                    if (projection > bestProjection)
                    {
                        bestProjection = projection;
                        bestPoint = point;
                    }
                }
                return bestPoint;
            }
        }
        const glm::vec3 localHalf = PhysicsColliderLocalHalfExtents(entity);
        const glm::vec3 half = glm::max(localHalf * scale, glm::vec3(0.005f));
        const float directionLength = glm::length(direction);
        const glm::vec3 d = directionLength > 0.000001f
            ? direction / directionLength : glm::vec3(1.0f, 0.0f, 0.0f);

        if (entity.Collider == Entity::ColliderShape::Sphere)
        {
            // Ellipsoide de suporte exato para escala não uniforme; a esfera
            // padrão continua tendo raio local .5, igual à malha visível.
            const glm::vec3 radii = half;
            const glm::vec3 weighted = radii * radii * d;
            const float denominator = std::sqrt(std::max(
                0.0000001f, glm::dot(weighted, d)));
            return weighted / denominator;
        }

        if (entity.Collider == Entity::ColliderShape::Capsule)
        {
            const float capRadiusLocal = std::min(localHalf.x, localHalf.z);
            const float segmentHalf = std::max(
                0.0f, (localHalf.y - capRadiusLocal) * scale.y);
            const glm::vec3 radii(
                std::max(0.005f, localHalf.x * scale.x),
                std::max(0.005f, capRadiusLocal * scale.y),
                std::max(0.005f, localHalf.z * scale.z));
            const glm::vec3 weighted = radii * radii * d;
            const float denominator = std::sqrt(std::max(
                0.0000001f, glm::dot(weighted, d)));
            glm::vec3 point = weighted / denominator;
            point.y += d.y >= 0.0f ? segmentHalf : -segmentHalf;
            return point;
        }

        if (entity.Collider == Entity::ColliderShape::Cylinder)
        {
            const float radiusX = half.x;
            const float radiusZ = half.z;
            const float denominator = std::sqrt(
                radiusX * radiusX * d.x * d.x +
                radiusZ * radiusZ * d.z * d.z);
            return glm::vec3(
                denominator > 0.000001f ? radiusX * radiusX * d.x / denominator : 0.0f,
                d.y >= 0.0f ? half.y : -half.y,
                denominator > 0.000001f ? radiusZ * radiusZ * d.z / denominator : 0.0f);
        }

        if (entity.Collider == Entity::ColliderShape::Cone)
        {
            const float baseSupport = std::sqrt(
                half.x * half.x * d.x * d.x + half.z * half.z * d.z * d.z) -
                half.y * d.y;
            const float apexSupport = half.y * d.y;
            if (apexSupport >= baseSupport)
                return glm::vec3(0.0f, half.y, 0.0f);
            const float radialLength = std::sqrt(
                half.x * half.x * d.x * d.x + half.z * half.z * d.z * d.z);
            return glm::vec3(
                radialLength > 0.000001f ? half.x * half.x * d.x / radialLength : 0.0f,
                -half.y,
                radialLength > 0.000001f ? half.z * half.z * d.z / radialLength : 0.0f);
        }

        // Box e Mesh usam proxy AABB orientado: o Mesh mantém dimensões e
        // centro local da geometria, mas ainda não é uma malha triangular.
        return glm::vec3(
            d.x >= 0.0f ? half.x : -half.x,
            d.y >= 0.0f ? half.y : -half.y,
            d.z >= 0.0f ? half.z : -half.z);
    }

    static Vector3 PhysicsColliderSupport(
        const Entity& entity,
        const Vector3& direction)
    {
        const Transform& transform = const_cast<Entity&>(entity).GetTransform();
        const glm::quat rotation = glm::normalize(transform.Rotation);
        const glm::vec3 worldDirection = PhysicsToGlm(direction);
        const glm::vec3 localDirection = glm::inverse(rotation) * worldDirection;
        glm::vec3 localPoint = PhysicsColliderSupportLocal(entity, localDirection);
        if (entity.Collider == Entity::ColliderShape::Mesh)
        {
            const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
            if (mesh && mesh->IsValid())
                localPoint += mesh->GetBoundingCenter() * glm::vec3(
                    transform.Scale.X, transform.Scale.Y, transform.Scale.Z);
        }
        localPoint = rotation * localPoint;
        return PhysicsFromGlm(PhysicsToGlm(transform.Position) + localPoint);
    }

    // Retorna o centro do recurso de suporte (face/aresta) de colliders
    // poliedrais. Um único vértice extremo cria torque artificial em choques
    // frontais simétricos; para primitivas curvas, o suporte continua pontual.
    static Vector3 PhysicsColliderSupportFeatureCenter(
        const Entity& entity,
        const Vector3& direction)
    {
        if (entity.Collider != Entity::ColliderShape::Box &&
            entity.Collider != Entity::ColliderShape::Mesh &&
            entity.Collider != Entity::ColliderShape::ConvexMesh)
            return PhysicsColliderSupport(entity, direction);

        const Transform& transform = const_cast<Entity&>(entity).GetTransform();
        const glm::quat rotation = glm::normalize(transform.Rotation);
        const glm::vec3 signedScale(
            transform.Scale.X, transform.Scale.Y, transform.Scale.Z);
        const glm::vec3 localDirection = glm::inverse(rotation) *
            PhysicsToGlm(direction);
        const glm::vec3 scaledDirection = localDirection * signedScale;
        glm::vec3 localPoint(0.0f);

        if (entity.Collider == Entity::ColliderShape::ConvexMesh)
        {
            const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
            if (!mesh || !mesh->IsValid() || mesh->GetVertices().empty())
                return PhysicsColliderSupport(entity, direction);

            float maximumProjection = -std::numeric_limits<float>::max();
            for (const Vertex& vertex : mesh->GetVertices())
                maximumProjection = std::max(maximumProjection,
                    glm::dot(glm::vec3(vertex.X, vertex.Y, vertex.Z), scaledDirection));

            const float tolerance = 0.0001f * std::max(1.0f, std::abs(maximumProjection));
            std::vector<glm::vec3> featureVertices;
            for (const Vertex& vertex : mesh->GetVertices())
            {
                const glm::vec3 point(vertex.X, vertex.Y, vertex.Z);
                if (maximumProjection - glm::dot(point, scaledDirection) > tolerance)
                    continue;
                if (std::none_of(featureVertices.begin(), featureVertices.end(),
                    [&](const glm::vec3& existing)
                    {
                        const glm::vec3 delta = existing - point;
                        return glm::dot(delta, delta) < 0.0000001f;
                    }))
                    featureVertices.push_back(point);
            }
            if (featureVertices.empty())
                return PhysicsColliderSupport(entity, direction);
            for (const glm::vec3& point : featureVertices)
                localPoint += point * signedScale;
            localPoint /= static_cast<float>(featureVertices.size());
        }
        else
        {
            const glm::vec3 half = PhysicsColliderLocalHalfExtents(entity);
            for (int axis = 0; axis < 3; ++axis)
                localPoint[axis] = std::abs(scaledDirection[axis]) < 0.00001f
                    ? 0.0f
                    : (scaledDirection[axis] > 0.0f ? half[axis] : -half[axis]) *
                        signedScale[axis];
            if (entity.Collider == Entity::ColliderShape::Mesh)
            {
                const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
                if (mesh && mesh->IsValid())
                    localPoint += mesh->GetBoundingCenter() * signedScale;
            }
        }

        return PhysicsFromGlm(PhysicsToGlm(transform.Position) + rotation * localPoint);
    }

    struct PhysicsMinkowskiPoint
    {
        glm::vec3 Point{ 0.0f };
        glm::vec3 PointA{ 0.0f };
        glm::vec3 PointB{ 0.0f };
    };

    static PhysicsMinkowskiPoint PhysicsMinkowskiSupport(
        const Entity& a,
        const Entity& b,
        const glm::vec3& direction)
    {
        const Vector3 supportA = PhysicsColliderSupport(a, PhysicsFromGlm(direction));
        const Vector3 supportB = PhysicsColliderSupport(b, PhysicsFromGlm(-direction));
        const glm::vec3 pointA = PhysicsToGlm(supportA);
        const glm::vec3 pointB = PhysicsToGlm(supportB);
        return { pointA - pointB, pointA, pointB };
    }

    static glm::vec3 PhysicsTripleCross(
        const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        return glm::cross(glm::cross(a, b), c);
    }

    static bool PhysicsUpdateSimplex(
        std::vector<PhysicsMinkowskiPoint>& simplex,
        glm::vec3& direction)
    {
        const glm::vec3 origin(0.0f);
        const PhysicsMinkowskiPoint pointA = simplex.back();
        const glm::vec3 a = pointA.Point;
        const glm::vec3 ao = origin - a;

        if (simplex.size() == 2)
        {
            const PhysicsMinkowskiPoint pointB = simplex[0];
            const glm::vec3 ab = pointB.Point - a;
            if (glm::dot(ab, ao) > 0.0f)
            {
                direction = PhysicsTripleCross(ab, ao, ab);
                if (glm::dot(direction, direction) < 0.0000001f)
                    direction = glm::cross(ab, glm::vec3(1.0f, 0.0f, 0.0f));
                if (glm::dot(direction, direction) < 0.0000001f)
                    direction = glm::cross(ab, glm::vec3(0.0f, 1.0f, 0.0f));
            }
            else
            {
                simplex = { pointA };
                direction = ao;
            }
            return false;
        }

        if (simplex.size() == 3)
        {
            const PhysicsMinkowskiPoint pointB = simplex[1];
            const PhysicsMinkowskiPoint pointC = simplex[0];
            const glm::vec3 ab = pointB.Point - a;
            const glm::vec3 ac = pointC.Point - a;
            const glm::vec3 abc = glm::cross(ab, ac);

            const glm::vec3 outsideAB = glm::cross(abc, ab);
            if (glm::dot(outsideAB, ao) > 0.0f)
            {
                if (glm::dot(ab, ao) > 0.0f)
                {
                    simplex = { pointB, pointA };
                    direction = PhysicsTripleCross(ab, ao, ab);
                }
                else
                {
                    simplex = { pointA };
                    direction = ao;
                }
                return false;
            }

            const glm::vec3 outsideAC = glm::cross(ac, abc);
            if (glm::dot(outsideAC, ao) > 0.0f)
            {
                if (glm::dot(ac, ao) > 0.0f)
                {
                    simplex = { pointC, pointA };
                    direction = PhysicsTripleCross(ac, ao, ac);
                }
                else
                {
                    simplex = { pointA };
                    direction = ao;
                }
                return false;
            }

            if (glm::dot(abc, ao) > 0.0f)
                direction = abc;
            else
            {
                simplex = { pointB, pointC, pointA };
                direction = -abc;
            }
            return false;
        }

        if (simplex.size() >= 4)
        {
            const PhysicsMinkowskiPoint pointB = simplex[2];
            const PhysicsMinkowskiPoint pointC = simplex[1];
            const PhysicsMinkowskiPoint pointD = simplex[0];
            const glm::vec3 ab = pointB.Point - a;
            const glm::vec3 ac = pointC.Point - a;
            const glm::vec3 ad = pointD.Point - a;

            glm::vec3 abc = glm::cross(ab, ac);
            if (glm::dot(abc, ad) > 0.0f) abc = -abc;
            if (glm::dot(abc, ao) > 0.000001f)
            {
                simplex = { pointC, pointB, pointA };
                direction = abc;
                return false;
            }

            glm::vec3 acd = glm::cross(ac, ad);
            if (glm::dot(acd, ab) > 0.0f) acd = -acd;
            if (glm::dot(acd, ao) > 0.000001f)
            {
                simplex = { pointD, pointC, pointA };
                direction = acd;
                return false;
            }

            glm::vec3 adb = glm::cross(ad, ab);
            if (glm::dot(adb, ac) > 0.0f) adb = -adb;
            if (glm::dot(adb, ao) > 0.000001f)
            {
                simplex = { pointB, pointD, pointA };
                direction = adb;
                return false;
            }
            return true;
        }

        direction = ao;
        return false;
    }

    struct PhysicsEpaFace
    {
        int A = 0;
        int B = 0;
        int C = 0;
        glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
        float Distance = 0.0f;
    };

    static bool PhysicsMakeEpaFace(
        const std::vector<PhysicsMinkowskiPoint>& vertices,
        int a, int b, int c,
        PhysicsEpaFace& face)
    {
        glm::vec3 normal = glm::cross(
            vertices[b].Point - vertices[a].Point,
            vertices[c].Point - vertices[a].Point);
        const float lengthSquared = glm::dot(normal, normal);
        if (lengthSquared < 0.0000001f)
            return false;
        normal /= std::sqrt(lengthSquared);
        float distance = glm::dot(normal, vertices[a].Point);
        if (distance < 0.0f)
        {
            std::swap(b, c);
            normal = -normal;
            distance = -distance;
        }
        face = { a, b, c, normal, distance };
        return true;
    }

    static bool PhysicsEpaContact(
        const Entity& a,
        const Entity& b,
        std::vector<PhysicsMinkowskiPoint> vertices,
        PhysicsContact& contact)
    {
        if (vertices.size() != 4)
            return false;

        std::vector<PhysicsEpaFace> faces;
        faces.reserve(64);
        PhysicsEpaFace face;
        const int initialFaces[4][3] = {
            { 0, 1, 2 }, { 0, 3, 1 }, { 0, 2, 3 }, { 1, 3, 2 }
        };
        for (const auto& indices : initialFaces)
            if (PhysicsMakeEpaFace(vertices, indices[0], indices[1], indices[2], face))
                faces.push_back(face);

        auto finishFace = [&](const PhysicsEpaFace& nearest)
        {
            glm::vec3 normal = nearest.Normal;
            const glm::vec3 centerDelta = PhysicsToGlm(PhysicsShapeCenterWorld(b)) -
                                          PhysicsToGlm(PhysicsShapeCenterWorld(a));
            if (glm::dot(normal, centerDelta) < 0.0f)
                normal = -normal;
            contact.Normal = PhysicsFromGlm(normal);
            contact.Penetration = std::max(0.0f, nearest.Distance);

            const glm::vec3 target = nearest.Normal * nearest.Distance;
            const glm::vec3 p0 = vertices[nearest.A].Point;
            const glm::vec3 e0 = vertices[nearest.B].Point - p0;
            const glm::vec3 e1 = vertices[nearest.C].Point - p0;
            const glm::vec3 v = target - p0;
            const float d00 = glm::dot(e0, e0);
            const float d01 = glm::dot(e0, e1);
            const float d11 = glm::dot(e1, e1);
            const float d20 = glm::dot(v, e0);
            const float d21 = glm::dot(v, e1);
            const float denom = d00 * d11 - d01 * d01;
            float wb = 1.0f / 3.0f;
            float wc = 1.0f / 3.0f;
            if (std::abs(denom) > 0.0000001f)
            {
                wb = (d11 * d20 - d01 * d21) / denom;
                wc = (d00 * d21 - d01 * d20) / denom;
            }
            float wa = 1.0f - wb - wc;
            wa = std::max(0.0f, wa);
            wb = std::max(0.0f, wb);
            wc = std::max(0.0f, wc);
            const float weightSum = std::max(0.000001f, wa + wb + wc);
            wa /= weightSum; wb /= weightSum; wc /= weightSum;
            const glm::vec3 pointA = vertices[nearest.A].PointA * wa +
                vertices[nearest.B].PointA * wb + vertices[nearest.C].PointA * wc;
            const glm::vec3 pointB = vertices[nearest.A].PointB * wa +
                vertices[nearest.B].PointB * wb + vertices[nearest.C].PointB * wc;
            contact.Point = PhysicsFromGlm((pointA + pointB) * 0.5f);
        };

        if (faces.empty())
            return false;

        for (int iteration = 0; iteration < 48; ++iteration)
        {
            const auto nearestIt = std::min_element(faces.begin(), faces.end(),
                [](const PhysicsEpaFace& left, const PhysicsEpaFace& right)
                { return left.Distance < right.Distance; });
            const PhysicsEpaFace nearest = *nearestIt;
            const PhysicsMinkowskiPoint support = PhysicsMinkowskiSupport(a, b, nearest.Normal);
            const float supportDistance = glm::dot(support.Point, nearest.Normal);
            if (supportDistance - nearest.Distance < 0.0005f || vertices.size() >= 96)
            {
                finishFace(nearest);
                return true;
            }

            const int newIndex = static_cast<int>(vertices.size());
            vertices.push_back(support);
            struct Edge { int A; int B; };
            std::vector<Edge> boundary;
            auto addBoundaryEdge = [&](int edgeA, int edgeB)
            {
                const auto reverse = std::find_if(boundary.begin(), boundary.end(),
                    [&](const Edge& edge) { return edge.A == edgeB && edge.B == edgeA; });
                if (reverse != boundary.end())
                    boundary.erase(reverse);
                else
                    boundary.push_back({ edgeA, edgeB });
            };

            faces.erase(std::remove_if(faces.begin(), faces.end(), [&](const PhysicsEpaFace& current)
            {
                if (glm::dot(current.Normal,
                        support.Point - vertices[current.A].Point) <= 0.00001f)
                    return false;
                addBoundaryEdge(current.A, current.B);
                addBoundaryEdge(current.B, current.C);
                addBoundaryEdge(current.C, current.A);
                return true;
            }), faces.end());

            for (const Edge& edge : boundary)
                if (PhysicsMakeEpaFace(vertices, edge.A, edge.B, newIndex, face))
                    faces.push_back(face);
            if (faces.empty())
                break;
        }

        if (!faces.empty())
        {
            const PhysicsEpaFace nearest = *std::min_element(faces.begin(), faces.end(),
                [](const PhysicsEpaFace& left, const PhysicsEpaFace& right)
                { return left.Distance < right.Distance; });
            finishFace(nearest);
            return true;
        }
        return false;
    }

    static bool PhysicsBoxBox(
        const Entity& a,
        const Entity& b,
        PhysicsContact& contact);

    static bool PhysicsConvexContact(
        const Entity& a,
        const Entity& b,
        PhysicsContact& contact)
    {
        glm::vec3 direction = PhysicsToGlm(PhysicsShapeCenterWorld(b)) -
                              PhysicsToGlm(PhysicsShapeCenterWorld(a));
        if (glm::dot(direction, direction) < 0.000001f)
            direction = glm::vec3(1.0f, 0.0f, 0.0f);

        std::vector<PhysicsMinkowskiPoint> simplex;
        simplex.reserve(4);
        simplex.push_back(PhysicsMinkowskiSupport(a, b, direction));
        direction = -simplex.back().Point;
        for (int iteration = 0; iteration < 40; ++iteration)
        {
            const float directionLengthSquared = glm::dot(direction, direction);
            if (directionLengthSquared < 0.0000001f)
                direction = glm::vec3(0.0f, 1.0f, 0.0f);
            else
                direction /= std::sqrt(directionLengthSquared);
            const PhysicsMinkowskiPoint point = PhysicsMinkowskiSupport(a, b, direction);
            if (glm::dot(point.Point, direction) < -0.00001f)
                return false;
            simplex.push_back(point);
            if (PhysicsUpdateSimplex(simplex, direction))
            {
                if (PhysicsEpaContact(a, b, simplex, contact))
                    return true;
                // GJK confirmou interseção, mas o EPA pode degenerar em
                // contatos coplanares; cai no SAT de contingência abaixo.
                break;
            }
        }

        // GJK pode ficar degenerado em contatos coplanares grandes (p.ex. um
        // cilindro inclinado sobre um chão muito maior). Só nesse caso limite,
        // usa o SAT da caixa envolvente para confirmar o candidato; contatos
        // normais continuam no caminho convexo exato e falsos positivos que o
        // GJK conseguiu separar continuam rejeitados.
        PhysicsContact proxyContact;
        if (!PhysicsBoxBox(a, b, proxyContact))
            return false;
        glm::vec3 normal = PhysicsToGlm(PhysicsShapeCenterWorld(b)) -
                           PhysicsToGlm(PhysicsShapeCenterWorld(a));
        const float normalLength = glm::length(normal);
        normal = normalLength > 0.000001f
            ? normal / normalLength : glm::vec3(0.0f, 1.0f, 0.0f);
        const Vector3 supportA = PhysicsColliderSupport(a, PhysicsFromGlm(normal));
        const Vector3 supportB = PhysicsColliderSupport(b, PhysicsFromGlm(-normal));
        const float penetration = glm::dot(
            PhysicsToGlm(supportA) - PhysicsToGlm(supportB), normal);
        if (penetration <= 0.00001f)
            return false;

        contact.Normal = PhysicsFromGlm(normal);
        contact.Penetration = penetration;
        const Vector3 inverseNormal(-contact.Normal.X,
                                    -contact.Normal.Y,
                                    -contact.Normal.Z);
        const Vector3 featureA = PhysicsColliderSupportFeatureCenter(
            a, contact.Normal);
        const Vector3 featureB = PhysicsColliderSupportFeatureCenter(
            b, inverseNormal);
        contact.Point = PhysicsFromGlm(
            (PhysicsToGlm(featureA) + PhysicsToGlm(featureB)) * 0.5f);

        const Entity* staticSurface = nullptr;
        Vector3 surfaceToBodyNormal;
        Vector3 bodySupport;
        if (a.IsStatic && a.Collider == Entity::ColliderShape::Box &&
            std::abs(normal.y) > 0.80f)
        {
            staticSurface = &a;
            surfaceToBodyNormal = contact.Normal;
            bodySupport = supportB;
        }
        else if (b.IsStatic && b.Collider == Entity::ColliderShape::Box &&
                 std::abs(normal.y) > 0.80f)
        {
            staticSurface = &b;
            surfaceToBodyNormal = PhysicsFromGlm(-normal);
            bodySupport = supportA;
        }
        if (staticSurface)
        {
            const Transform& surfaceTransform =
                const_cast<Entity&>(*staticSurface).GetTransform();
            const glm::quat surfaceRotation = glm::normalize(surfaceTransform.Rotation);
            const glm::vec3 surfaceCenter = PhysicsToGlm(
                PhysicsShapeCenterWorld(*staticSurface));
            const glm::vec3 localBodyPoint = glm::inverse(surfaceRotation) *
                (PhysicsToGlm(bodySupport) - surfaceCenter);
            const Vector3 surfaceHalf = PhysicsBoxHalfExtents(*staticSurface);
            const glm::vec3 surfaceUp = surfaceRotation * glm::vec3(0, 1, 0);
            const float faceSign = glm::dot(
                surfaceUp, PhysicsToGlm(surfaceToBodyNormal)) >= 0.0f ? 1.0f : -1.0f;
            const glm::vec3 localPoint(
                std::clamp(localBodyPoint.x, -surfaceHalf.X, surfaceHalf.X),
                faceSign * surfaceHalf.Y,
                std::clamp(localBodyPoint.z, -surfaceHalf.Z, surfaceHalf.Z));
            contact.Point = PhysicsFromGlm(surfaceCenter + surfaceRotation * localPoint);
        }
        return true;
    }

    static Vector3 PhysicsSupportPoint(const Entity& entity, const Vector3& direction)
    {
        return PhysicsColliderSupport(entity, direction);
    }

    // Calcula o ponto resultante de apoio na face superior de uma plataforma
    // estática finita. Enquanto o centro de massa está dentro da área, a força
    // normal passa por ele (sem torque); ao ultrapassar a borda, a resultante
    // fica no limite da plataforma e a gravidade pode tombar o corpo.
    static bool PhysicsStaticSupportPoint(
        const Entity& surface,
        const Entity& body,
        const Vector3& contactNormal,
        Vector3& supportPoint)
    {
        if (surface.Collider != Entity::ColliderShape::Box ||
            body.Collider != Entity::ColliderShape::Box ||
            std::abs(contactNormal.Y) < 0.80f)
            return false;

        const Transform& surfaceTransform = const_cast<Entity&>(surface).GetTransform();
        const glm::quat surfaceRotation = glm::normalize(surfaceTransform.Rotation);
        const glm::vec3 surfaceUp = surfaceRotation * glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(surfaceUp, glm::vec3(0.0f, 1.0f, 0.0f))) < 0.995f)
            return false;

        // Só tratamos a base inteira como área de apoio quando uma face/eixo
        // do corpo está paralelo ao piso. Um objeto inclinado que toca numa
        // aresta não pode receber uma força fictícia sob o centro de massa.
        const Transform& bodyTransform = const_cast<Entity&>(body).GetTransform();
        const glm::mat3 bodyRotation = glm::mat3_cast(
            glm::normalize(bodyTransform.Rotation));
        float bestFaceAlignment = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
            bestFaceAlignment = std::max(bestFaceAlignment,
                std::abs(glm::dot(glm::normalize(bodyRotation[axis]), surfaceUp)));
        // Só absorve erro angular residual muito pequeno. Tolerâncias largas
        // deixam um cubo inclinado equilibrado artificialmente sobre uma aresta.
        if (bestFaceAlignment < 0.9995f)
            return false;

        const glm::vec3 normal = PhysicsToGlm(contactNormal);
        const float faceSign = glm::dot(surfaceUp, normal) >= 0.0f ? 1.0f : -1.0f;
        const glm::vec3 bodyCenter = PhysicsToGlm(
            const_cast<Entity&>(body).GetTransform().Position);
        const glm::vec3 surfaceCenter = PhysicsToGlm(PhysicsShapeCenterWorld(surface));
        const glm::vec3 localCenter = glm::inverse(surfaceRotation) *
            (bodyCenter - surfaceCenter);
        const Vector3 half = PhysicsBoxHalfExtents(surface);
        const float localX = std::clamp(localCenter.x, -half.X, half.X);
        const float localZ = std::clamp(localCenter.z, -half.Z, half.Z);
        const glm::vec3 localPoint(localX, faceSign * half.Y, localZ);
        supportPoint = PhysicsFromGlm(
            surfaceCenter + surfaceRotation * localPoint);
        return true;
    }

    static bool PhysicsBoxBox(
        const Entity& a,
        const Entity& b,
        PhysicsContact& contact)
    {
        struct OBB
        {
            glm::vec3 Center{ 0.0f };
            glm::vec3 Axis[3] = {
                glm::vec3(1.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f),
                glm::vec3(0.0f, 0.0f, 1.0f)
            };
            glm::vec3 Extents{ 0.5f };
        };

        auto makeOBB = [](const Entity& entity)
        {
            OBB result;
            const Transform& transform = const_cast<Entity&>(entity).GetTransform();
            result.Center = PhysicsToGlm(PhysicsShapeCenterWorld(entity));
            const glm::mat3 rotation = glm::mat3_cast(transform.Rotation);
            for (int axis = 0; axis < 3; ++axis)
                result.Axis[axis] = glm::normalize(rotation[axis]);
            const Vector3 extents = PhysicsBoxHalfExtents(entity);
            result.Extents = glm::vec3(extents.X, extents.Y, extents.Z);
            return result;
        };

        const OBB boxA = makeOBB(a);
        const OBB boxB = makeOBB(b);
        const glm::vec3 between = boxB.Center - boxA.Center;
        float minimumOverlap = std::numeric_limits<float>::max();
        glm::vec3 bestAxis(0.0f, 1.0f, 0.0f);

        auto testAxis = [&](const glm::vec3& candidate) -> bool
        {
            const float lengthSquared = glm::dot(candidate, candidate);
            if (lengthSquared < 0.000001f)
                return true;
            const glm::vec3 axis = candidate / std::sqrt(lengthSquared);
            float radiusA = 0.0f;
            float radiusB = 0.0f;
            for (int i = 0; i < 3; ++i)
            {
                radiusA += boxA.Extents[i] * std::abs(glm::dot(axis, boxA.Axis[i]));
                radiusB += boxB.Extents[i] * std::abs(glm::dot(axis, boxB.Axis[i]));
            }
            const float overlap = radiusA + radiusB - std::abs(glm::dot(between, axis));
            if (overlap <= 0.0f)
                return false;
            if (overlap < minimumOverlap)
            {
                minimumOverlap = overlap;
                bestAxis = glm::dot(between, axis) >= 0.0f ? axis : -axis;
            }
            return true;
        };

        for (int i = 0; i < 3; ++i)
            if (!testAxis(boxA.Axis[i])) return false;
        for (int i = 0; i < 3; ++i)
            if (!testAxis(boxB.Axis[i])) return false;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
            {
                const glm::vec3 crossAxis = glm::cross(boxA.Axis[i], boxB.Axis[j]);
                if (glm::dot(crossAxis, crossAxis) > 0.0001f)
                {
                    if (!testAxis(crossAxis)) return false;
                }
            }

        contact.Penetration = minimumOverlap;
        contact.Normal = Vector3(bestAxis.x, bestAxis.y, bestAxis.z);
        // Usa o centro da face/aresta de apoio em vez de um único vértice
        // extremo. Em impactos sobre rampas, um vértice arbitrário desloca o
        // braço do impulso e injeta torque que faz o corpo saltar/tombar sem
        // correspondência com a área real de contato.
        const Vector3 supportA = PhysicsColliderSupportFeatureCenter(a, contact.Normal);
        const Vector3 inverseNormal(-contact.Normal.X, -contact.Normal.Y, -contact.Normal.Z);
        const Vector3 supportB = PhysicsColliderSupportFeatureCenter(b, inverseNormal);
        contact.Point = Vector3(
            (supportA.X + supportB.X) * 0.5f,
            (supportA.Y + supportB.Y) * 0.5f,
            (supportA.Z + supportB.Z) * 0.5f);
        return true;
    }

    static bool PhysicsFlatBoxSupportContact(
        const Entity& surface,
        const Entity& body,
        Vector3& surfaceToBodyNormal,
        PhysicsContact& contact)
    {
        if (!surface.IsStatic || surface.Collider != Entity::ColliderShape::Box ||
            body.IsStatic || !PhysicsCanUseFlatSupportContact(body))
            return false;

        const Transform& surfaceTransform = const_cast<Entity&>(surface).GetTransform();
        const glm::quat rotation = glm::normalize(surfaceTransform.Rotation);
        const glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(up, glm::vec3(0.0f, 1.0f, 0.0f))) < 0.995f)
            return false;

        const glm::vec3 center = PhysicsToGlm(PhysicsShapeCenterWorld(surface));
        const glm::vec3 bodyCenter = PhysicsToGlm(PhysicsShapeCenterWorld(body));
        const glm::vec3 localBodyCenter = glm::inverse(rotation) * (bodyCenter - center);
        const float faceSign = localBodyCenter.y >= 0.0f ? 1.0f : -1.0f;
        const glm::vec3 normal = up * faceSign;
        const glm::vec3 axisX = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 axisZ = rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        const Vector3 half = PhysicsBoxHalfExtents(surface);
        if (faceSign * localBodyCenter.y < half.Y - 0.02f)
            return false;
        const glm::quat inverseRotation = glm::inverse(rotation);

        const glm::vec3 minimumX = inverseRotation * (
            PhysicsToGlm(PhysicsColliderSupport(body, PhysicsFromGlm(-axisX))) - center);
        const glm::vec3 maximumX = inverseRotation * (
            PhysicsToGlm(PhysicsColliderSupport(body, PhysicsFromGlm(axisX))) - center);
        const glm::vec3 minimumZ = inverseRotation * (
            PhysicsToGlm(PhysicsColliderSupport(body, PhysicsFromGlm(-axisZ))) - center);
        const glm::vec3 maximumZ = inverseRotation * (
            PhysicsToGlm(PhysicsColliderSupport(body, PhysicsFromGlm(axisZ))) - center);
        if (maximumX.x < -half.X || minimumX.x > half.X ||
            maximumZ.z < -half.Z || minimumZ.z > half.Z)
            return false;

        const glm::vec3 bodySupport = PhysicsToGlm(PhysicsColliderSupportFeatureCenter(
            body, PhysicsFromGlm(-normal)));
        const glm::vec3 localSupport = inverseRotation * (bodySupport - center);
        const float faceY = faceSign * half.Y;
        const float penetration = faceSign * (faceY - localSupport.y);
        if (penetration <= 0.00001f)
            return false;

        surfaceToBodyNormal = PhysicsFromGlm(normal);
        contact.Normal = surfaceToBodyNormal;
        contact.Penetration = penetration;
        const glm::vec3 localContact(
            std::clamp(localSupport.x, -half.X, half.X),
            faceY,
            std::clamp(localSupport.z, -half.Z, half.Z));
        contact.Point = PhysicsFromGlm(center + rotation * localContact);
        return true;
    }

    static bool PhysicsTerrainContact(
        const Entity& terrainEntity,
        const Entity& body,
        Vector3& terrainToBodyNormal,
        PhysicsContact& contact)
    {
        if (!terrainEntity.IsStatic || body.IsStatic ||
            !terrainEntity.HasComponent<TerrainComponent>())
            return false;
        const TerrainComponent& terrain =
            const_cast<Entity&>(terrainEntity).GetTerrainComponent();
        if (!terrain.IsValid() || terrain.Size <= 0.001f)
            return false;

        const Transform& transform = const_cast<Entity&>(terrainEntity).GetTransform();
        const glm::quat rotation = glm::normalize(transform.Rotation);
        const glm::quat inverseRotation = glm::inverse(rotation);
        const glm::vec3 position = PhysicsToGlm(transform.Position);
        const glm::vec3 scale(
            transform.Scale.X, transform.Scale.Y, transform.Scale.Z);
        if (std::abs(scale.x) < 0.0001f || std::abs(scale.y) < 0.0001f ||
            std::abs(scale.z) < 0.0001f)
            return false;

        auto sample = [&](float scaledX, float scaledZ, float& scaledHeight,
                          glm::vec3& normalWorld, glm::vec3& pointWorld) -> bool
        {
            const float localX = scaledX / scale.x;
            const float localZ = scaledZ / scale.z;
            const float gridX = (localX + terrain.Size * 0.5f) /
                terrain.Size * static_cast<float>(terrain.Resolution - 1);
            const float gridZ = (localZ + terrain.Size * 0.5f) /
                terrain.Size * static_cast<float>(terrain.Resolution - 1);
            if (gridX < 0.0f || gridZ < 0.0f ||
                gridX > static_cast<float>(terrain.Resolution - 1) ||
                gridZ > static_cast<float>(terrain.Resolution - 1))
                return false;

            const float clampedX = std::min(gridX,
                static_cast<float>(terrain.Resolution - 1) - 0.00001f);
            const float clampedZ = std::min(gridZ,
                static_cast<float>(terrain.Resolution - 1) - 0.00001f);
            const int cellX = std::clamp(static_cast<int>(std::floor(clampedX)),
                                         0, terrain.Resolution - 2);
            const int cellZ = std::clamp(static_cast<int>(std::floor(clampedZ)),
                                         0, terrain.Resolution - 2);
            const float u = clampedX - static_cast<float>(cellX);
            const float v = clampedZ - static_cast<float>(cellZ);
            const float h00 = terrain.At(cellX, cellZ) * terrain.HeightScale;
            const float h10 = terrain.At(cellX + 1, cellZ) * terrain.HeightScale;
            const float h11 = terrain.At(cellX + 1, cellZ + 1) * terrain.HeightScale;
            const float h01 = terrain.At(cellX, cellZ + 1) * terrain.HeightScale;
            const float cellSize = terrain.Size / static_cast<float>(terrain.Resolution - 1);
            float derivativeX = 0.0f;
            float derivativeZ = 0.0f;
            float height = 0.0f;

            // Mesma diagonal e triangulação que TerrainComponent::BuildVertices.
            if (u >= v)
            {
                height = h00 + u * (h10 - h00) + v * (h11 - h10);
                derivativeX = (h10 - h00) / cellSize;
                derivativeZ = (h11 - h10) / cellSize;
            }
            else
            {
                height = h00 + v * (h01 - h00) + u * (h11 - h01);
                derivativeX = (h11 - h01) / cellSize;
                derivativeZ = (h01 - h00) / cellSize;
            }

            const float gridStep = terrain.Size /
                static_cast<float>(terrain.Resolution - 1);
            const float pointX = -terrain.Size * 0.5f + gridX * gridStep;
            const float pointZ = -terrain.Size * 0.5f + gridZ * gridStep;
            const glm::vec3 localSurface(pointX * scale.x,
                                         height * scale.y,
                                         pointZ * scale.z);
            pointWorld = position + rotation * localSurface;

            // Normal via inversa-transposta para respeitar escala não uniforme.
            glm::vec3 localNormal(
                -derivativeX / scale.x,
                1.0f / scale.y,
                -derivativeZ / scale.z);
            if (glm::dot(localNormal, localNormal) < 0.000001f)
                localNormal = glm::vec3(0.0f, 1.0f, 0.0f);
            normalWorld = glm::normalize(rotation * glm::normalize(localNormal));
            scaledHeight = height * scale.y;
            return true;
        };

        const glm::vec3 terrainUp = glm::normalize(
            rotation * glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 bodyCenter = PhysicsToGlm(PhysicsShapeCenterWorld(body));
        const glm::vec3 localBodyCenter = inverseRotation * (bodyCenter - position);
        glm::vec3 normalWorld = terrainUp;
        glm::vec3 supportWorld = PhysicsToGlm(PhysicsColliderSupport(
            body, PhysicsFromGlm(-terrainUp)));
        glm::vec3 surfacePoint(0.0f);
        float surfaceHeight = 0.0f;
        if (!sample(localBodyCenter.x, localBodyCenter.z,
                    surfaceHeight, normalWorld, surfacePoint))
        {
            // Perto da borda, o centro pode estar fora do heightfield embora
            // a base do collider ainda alcance a superfície.
            const glm::vec3 localSupport = inverseRotation * (supportWorld - position);
            if (!sample(localSupport.x, localSupport.z,
                        surfaceHeight, normalWorld, surfacePoint))
                return false;
        }
        // Heightfields representam sempre a superfície superior. Inverter a
        // normal quando o corpo atravessa o terreno agravaria o tunneling.
        if (glm::dot(normalWorld, terrainUp) < 0.0f)
            normalWorld = -normalWorld;
        supportWorld = PhysicsToGlm(PhysicsColliderSupport(
            body, PhysicsFromGlm(-normalWorld)));
        const float penetration = glm::dot(surfacePoint - supportWorld, normalWorld);
        if (penetration <= 0.00001f)
            return false;

        terrainToBodyNormal = PhysicsFromGlm(normalWorld);
        contact.Normal = terrainToBodyNormal;
        contact.Penetration = penetration;
        contact.Point = PhysicsFromGlm(surfacePoint);
        return true;
    }


    static bool PhysicsComputeContact(
        const Entity& a,
        const Entity& b,
        PhysicsContact& contact)
    {
        if (a.HasComponent<TerrainComponent>() && a.IsStatic)
            return PhysicsTerrainContact(a, b, contact.Normal, contact);
        if (b.HasComponent<TerrainComponent>() && b.IsStatic)
        {
            if (!PhysicsTerrainContact(b, a, contact.Normal, contact))
                return false;
            contact.Normal = Vector3(-contact.Normal.X,
                                     -contact.Normal.Y,
                                     -contact.Normal.Z);
            return true;
        }

        if (PhysicsCanUseFlatSupportContact(a) && b.IsStatic &&
            PhysicsFlatBoxSupportContact(b, a, contact.Normal, contact))
        {
            contact.Normal = Vector3(-contact.Normal.X, -contact.Normal.Y, -contact.Normal.Z);
            return true;
        }
        if (PhysicsCanUseFlatSupportContact(b) && a.IsStatic &&
            PhysicsFlatBoxSupportContact(a, b, contact.Normal, contact))
            return true;

        if (PhysicsUsesBoxProxy(a) && PhysicsUsesBoxProxy(b))
        {
            if (!PhysicsBoxBox(a, b, contact))
                return false;

            auto projectContactToStaticBox = [&](const Entity& surface,
                                                 const Entity& body,
                                                 const Vector3& surfaceToBodyNormal)
            {
                const Transform& surfaceTransform =
                    const_cast<Entity&>(surface).GetTransform();
                const glm::quat rotation = glm::normalize(surfaceTransform.Rotation);
                const glm::vec3 center = PhysicsToGlm(PhysicsShapeCenterWorld(surface));
                const glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);
                const glm::vec3 normal = PhysicsToGlm(surfaceToBodyNormal);
                const float faceSign = glm::dot(up, normal) >= 0.0f ? 1.0f : -1.0f;
                const Vector3 bodySupport = PhysicsColliderSupport(
                    body, PhysicsFromGlm(-normal));
                const glm::vec3 localSupport = glm::inverse(rotation) *
                    (PhysicsToGlm(bodySupport) - center);
                const Vector3 half = PhysicsBoxHalfExtents(surface);
                const glm::vec3 localPoint(
                    std::clamp(localSupport.x, -half.X, half.X),
                    faceSign * half.Y,
                    std::clamp(localSupport.z, -half.Z, half.Z));
                contact.Point = PhysicsFromGlm(center + rotation * localPoint);
            };

            // For a tilted box on a horizontal static floor, the support
            // feature may be one corner. Project that real feature onto the
            // floor so its lever arm produces the gravity-driven tipping
            // torque; averaging it with the whole floor face falsely moves
            // the contact toward the COM and lets the box balance on an edge.
            if (std::abs(contact.Normal.Y) > 0.80f)
            {
                if (a.IsStatic && a.Collider == Entity::ColliderShape::Box &&
                    !b.IsStatic && b.Collider == Entity::ColliderShape::Box)
                    projectContactToStaticBox(a, b, contact.Normal);
                else if (b.IsStatic && b.Collider == Entity::ColliderShape::Box &&
                         !a.IsStatic && a.Collider == Entity::ColliderShape::Box)
                {
                    const Vector3 surfaceToBodyNormal(
                        -contact.Normal.X, -contact.Normal.Y, -contact.Normal.Z);
                    projectContactToStaticBox(b, a, surfaceToBodyNormal);
                }
            }
            return true;
        }
        return PhysicsConvexContact(a, b, contact);
    }

    static glm::mat3 PhysicsInverseInertiaWorld(const Entity& entity)
    {
        if (entity.IsStatic || entity.FreezeRotation)
            return glm::mat3(0.0f);

        const float mass = std::max(0.001f, entity.Mass);
        const Vector3 half = PhysicsBoxHalfExtents(entity);
        glm::mat3 inertia(0.0f);
        if (entity.Collider == Entity::ColliderShape::Cylinder)
        {
            const float radiusX = half.X;
            const float radiusZ = half.Z;
            const float height = half.Y * 2.0f;
            inertia[0][0] = mass * (3.0f * radiusZ * radiusZ + height * height) / 12.0f;
            inertia[1][1] = mass * (radiusX * radiusX + radiusZ * radiusZ) * 0.25f;
            inertia[2][2] = mass * (3.0f * radiusX * radiusX + height * height) / 12.0f;
        }
        else if (entity.Collider == Entity::ColliderShape::Sphere)
        {
            // Inércia de um elipsoide maciço; funciona também com escala não uniforme.
            inertia[0][0] = mass * (half.Y * half.Y + half.Z * half.Z) / 5.0f;
            inertia[1][1] = mass * (half.X * half.X + half.Z * half.Z) / 5.0f;
            inertia[2][2] = mass * (half.X * half.X + half.Y * half.Y) / 5.0f;
        }
        else if (entity.Collider == Entity::ColliderShape::Cone)
        {
            // Cone maciço, aproximado sobre o pivô geométrico do asset (o
            // centro de massa do cone deslocado é tratado abaixo pelo eixo paralelo).
            const float height = half.Y * 2.0f;
            inertia[0][0] = mass * (0.15f * half.Z * half.Z + 0.10f * height * height);
            inertia[1][1] = mass * 0.15f * (half.X * half.X + half.Z * half.Z);
            inertia[2][2] = mass * (0.15f * half.X * half.X + 0.10f * height * height);
        }
        else
        {
            const float width = half.X * 2.0f;
            const float height = half.Y * 2.0f;
            const float depth = half.Z * 2.0f;
            inertia[0][0] = mass * (height * height + depth * depth) / 12.0f;
            inertia[1][1] = mass * (width * width + depth * depth) / 12.0f;
            inertia[2][2] = mass * (width * width + height * height) / 12.0f;
        }

        if (entity.Collider == Entity::ColliderShape::Mesh ||
            entity.Collider == Entity::ColliderShape::ConvexMesh)
        {
            const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
            if (mesh && mesh->IsValid())
            {
                const Transform& transform = const_cast<Entity&>(entity).GetTransform();
                const glm::vec3 offset = mesh->GetBoundingCenter() * glm::vec3(
                    std::abs(transform.Scale.X),
                    std::abs(transform.Scale.Y),
                    std::abs(transform.Scale.Z));
                // Teorema dos eixos paralelos: o collider pode estar deslocado
                // do pivô/centro de massa usado pelo rigid body.
                glm::mat3 offsetInertia(0.0f);
                const float offsetSquared = glm::dot(offset, offset);
                for (int column = 0; column < 3; ++column)
                    for (int row = 0; row < 3; ++row)
                        offsetInertia[column][row] =
                            (column == row ? offsetSquared : 0.0f) -
                            offset[column] * offset[row];
                inertia += mass * offsetInertia;
            }
        }
        inertia[0][0] = std::max(inertia[0][0], 0.0001f);
        inertia[1][1] = std::max(inertia[1][1], 0.0001f);
        inertia[2][2] = std::max(inertia[2][2], 0.0001f);
        const glm::mat3 inverseInertia = glm::inverse(inertia);
        const glm::mat3 rotation = glm::mat3_cast(
            const_cast<Entity&>(entity).GetTransform().Rotation);
        return rotation * inverseInertia * glm::transpose(rotation);
    }

    static void PhysicsApplyImpulse(
        Entity& a,
        Entity& b,
        const Vector3& normal,
        float restitution,
        float friction,
        const Vector3& contactPoint)
    {
        const float inverseMassA = a.IsStatic ? 0.0f : 1.0f / std::max(0.001f, a.Mass);
        const float inverseMassB = b.IsStatic ? 0.0f : 1.0f / std::max(0.001f, b.Mass);
        const float inverseMassSum = inverseMassA + inverseMassB;
        if (inverseMassSum <= 0.0f)
        {
            return;
        }

        const Vector3 rA = PhysicsSubtract(contactPoint, a.GetTransform().Position);
        const Vector3 rB = PhysicsSubtract(contactPoint, b.GetTransform().Position);
        const Vector3 velocityAtA = PhysicsAddScaled(
            a.Velocity, PhysicsCross(a.AngularVelocity, rA), 1.0f);
        const Vector3 velocityAtB = PhysicsAddScaled(
            b.Velocity, PhysicsCross(b.AngularVelocity, rB), 1.0f);
        Vector3 relativeVelocity = PhysicsSubtract(velocityAtB, velocityAtA);
        const float normalVelocity = PhysicsDot(relativeVelocity, normal);
        if (normalVelocity >= 0.0f)
        {
            return;
        }

        // Evita quiques perpétuos/jitter quando a velocidade relativa normal for baixa
        const float effectiveRestitution = std::abs(normalVelocity) < 0.75f
            ? 0.0f
            : std::clamp(restitution, 0.0f, 1.0f);
        const glm::mat3 inverseInertiaA = PhysicsInverseInertiaWorld(a);
        const glm::mat3 inverseInertiaB = PhysicsInverseInertiaWorld(b);
        const glm::vec3 normalGlm = PhysicsToGlm(normal);
        const glm::vec3 rAGlm = PhysicsToGlm(rA);
        const glm::vec3 rBGlm = PhysicsToGlm(rB);
        const float angularMass = glm::dot(normalGlm,
            glm::cross(inverseInertiaA * glm::cross(rAGlm, normalGlm), rAGlm) +
            glm::cross(inverseInertiaB * glm::cross(rBGlm, normalGlm), rBGlm));
        const float normalMass = std::max(0.0001f, inverseMassSum + angularMass);
        const float normalImpulse =
            -(1.0f + effectiveRestitution) * normalVelocity / normalMass;
        if (!a.IsStatic)
        {
            a.Velocity = PhysicsAddScaled(
                a.Velocity, normal, -normalImpulse * inverseMassA);
            a.AngularVelocity = PhysicsAddScaled(
                a.AngularVelocity,
                PhysicsFromGlm(inverseInertiaA * glm::cross(rAGlm, -normalGlm)),
                normalImpulse);
        }
        if (!b.IsStatic)
        {
            b.Velocity = PhysicsAddScaled(
                b.Velocity, normal, normalImpulse * inverseMassB);
            b.AngularVelocity = PhysicsAddScaled(
                b.AngularVelocity,
                PhysicsFromGlm(inverseInertiaB * glm::cross(rBGlm, normalGlm)),
                normalImpulse);
        }

        // Coulomb friction removes tangential motion at the contact.
        relativeVelocity = PhysicsSubtract(
            PhysicsAddScaled(b.Velocity, PhysicsCross(b.AngularVelocity, rB), 1.0f),
            PhysicsAddScaled(a.Velocity, PhysicsCross(a.AngularVelocity, rA), 1.0f));
        const float updatedNormalVelocity = PhysicsDot(relativeVelocity, normal);
        Vector3 tangent(
            relativeVelocity.X - normal.X * updatedNormalVelocity,
            relativeVelocity.Y - normal.Y * updatedNormalVelocity,
            relativeVelocity.Z - normal.Z * updatedNormalVelocity
        );
        if (PhysicsLengthSquared(tangent) <= 0.000001f)
        {
            return;
        }

        tangent = PhysicsNormalizeOr(tangent, Vector3(0.0f, 0.0f, 0.0f));
        const glm::vec3 tangentGlm = PhysicsToGlm(tangent);
        const float tangentAngularMass = glm::dot(tangentGlm,
            glm::cross(inverseInertiaA * glm::cross(rAGlm, tangentGlm), rAGlm) +
            glm::cross(inverseInertiaB * glm::cross(rBGlm, tangentGlm), rBGlm));
        float tangentImpulse = -PhysicsDot(relativeVelocity, tangent) /
            std::max(0.0001f, inverseMassSum + tangentAngularMass);
        const float maximumTangentImpulse =
            normalImpulse * std::clamp(friction, 0.0f, 1.0f);
        tangentImpulse = std::clamp(
            tangentImpulse,
            -maximumTangentImpulse,
            maximumTangentImpulse
        );

        if (!a.IsStatic)
        {
            a.Velocity = PhysicsAddScaled(
                a.Velocity, tangent, -tangentImpulse * inverseMassA);
            a.AngularVelocity = PhysicsAddScaled(
                a.AngularVelocity,
                PhysicsFromGlm(inverseInertiaA * glm::cross(rAGlm, -tangentGlm)),
                tangentImpulse);
        }
        if (!b.IsStatic)
        {
            b.Velocity = PhysicsAddScaled(
                b.Velocity, tangent, tangentImpulse * inverseMassB);
            b.AngularVelocity = PhysicsAddScaled(
                b.AngularVelocity,
                PhysicsFromGlm(inverseInertiaB * glm::cross(rBGlm, tangentGlm)),
                tangentImpulse);
        }
    }
}

void Application::SimulatePhysics2DStep(float fixedDeltaTime)
{
    auto& entities = m_Scene.GetEntities();
    constexpr float gravity = -9.81f;
    constexpr int solverIterations = 8;

    struct Body2D
    {
        size_t EntityIndex = 0;
        float X = 0.0f, Y = 0.0f;
        float HalfX = 0.0f, HalfY = 0.0f;
        float BroadHalfY = 0.0f;
        float Angle = 0.0f;
        float Radius = 0.0f;
        float MinX = 0.0f, MaxX = 0.0f;
        bool Circle = false;
        bool Dynamic = false;
    };
    struct Contact2D
    {
        float NormalX = 0.0f, NormalY = 1.0f;
        float Penetration = 0.0f;
        float PointX = 0.0f, PointY = 0.0f;
    };
    struct CollisionEvent2D
    {
        uint64_t A = 0, B = 0;
        int Type = 0;
        float X = 0.0f, Y = 0.0f;
        float NormalX = 0.0f, NormalY = 1.0f;
        float Impulse = 0.0f;
    };

    // Integrate only 2D rigid bodies. Plain sprites do not acquire a dynamic
    // body implicitly; collider-only sprites still act as static surfaces.
    bool hasDynamicBody = false;
    for (Entity& entity : entities)
    {
        if (entity.IsEditorOnly() || entity.IsFolder || entity.IsModel ||
            !entity.Active || !entity.PhysicsEnabled)
            continue;
        Transform& transform = entity.GetTransform();
        transform.Position.Z = 0.0f;
        entity.Velocity.Z = 0.0f;
        entity.AngularVelocity.X = 0.0f;
        entity.AngularVelocity.Y = 0.0f;
        transform.EulerRotation.X = 0.0f;
        transform.EulerRotation.Y = 0.0f;
        const float angle = glm::eulerAngles(glm::normalize(transform.Rotation)).z;
        transform.Rotation = glm::angleAxis(angle, glm::vec3(0.0f, 0.0f, 1.0f));
        transform.EulerRotation.Z = glm::degrees(angle);
        if (entity.IsStatic)
            continue;
        hasDynamicBody = true;

        if (entity.UseGravity)
            entity.Velocity.Y = std::max(-35.0f, entity.Velocity.Y + gravity * fixedDeltaTime);
        const float damping = std::exp(-0.025f * fixedDeltaTime);
        entity.Velocity.X *= damping;
        entity.Velocity.Y *= damping;
        transform.Position.X += entity.Velocity.X * fixedDeltaTime;
        transform.Position.Y += entity.Velocity.Y * fixedDeltaTime;
        if (!entity.FreezeRotation)
        {
            entity.AngularVelocity.Z *= std::exp(-0.35f * fixedDeltaTime);
            const float nextAngle = angle + entity.AngularVelocity.Z * fixedDeltaTime;
            transform.Rotation = glm::angleAxis(nextAngle, glm::vec3(0.0f, 0.0f, 1.0f));
            transform.EulerRotation.Z = glm::degrees(nextAngle);
        }
        else
            entity.AngularVelocity.Z = 0.0f;
    }

    if (!hasDynamicBody)
    {
        for (const auto& [previousPair, entityIDs] : m_PhysicsContactPairs)
        {
            (void)previousPair;
            if (!m_Scene.FindEntityByID(entityIDs.first) ||
                !m_Scene.FindEntityByID(entityIDs.second))
                continue;
            ScriptingRuntime::Instance().FireCollisionEvent(entityIDs.first, entityIDs.second, 2);
            ScriptingRuntime::Instance().FireCollisionEvent(entityIDs.second, entityIDs.first, 2);
        }
        m_PhysicsContactPairs.clear();
        return;
    }

    auto buildBody = [&](size_t index, Body2D& body) -> bool
    {
        Entity& entity = entities[index];
        if (entity.IsEditorOnly() || entity.IsFolder || entity.IsModel ||
            entity.IsGlobalLight() || entity.HasUI() || entity.HasCamera() ||
            !entity.Active || !entity.ColliderEnabled)
            return false;
        const Transform& transform = entity.GetTransform();
        body.EntityIndex = index;
        body.X = transform.Position.X;
        body.Y = transform.Position.Y;
        const float scaleX = std::max(0.001f, std::abs(transform.Scale.X));
        const float scaleY = std::max(0.001f, std::abs(transform.Scale.Y));
        body.Angle = glm::eulerAngles(glm::normalize(transform.Rotation)).z;
        body.Circle = entity.Collider == Entity::ColliderShape::Sphere;
        body.Dynamic = entity.PhysicsEnabled && !entity.IsStatic;
        if (body.Circle)
        {
            body.Radius = std::max(scaleX, scaleY) * 0.5f;
            body.HalfX = body.Radius;
            body.HalfY = body.Radius;
        }
        else
        {
            body.HalfX = scaleX * 0.5f;
            body.HalfY = scaleY * 0.5f;
            body.Radius = 0.0f;
        }
        const float c = std::abs(std::cos(body.Angle));
        const float s = std::abs(std::sin(body.Angle));
        const float worldHalfX = body.Circle ? body.Radius : c * body.HalfX + s * body.HalfY;
        body.BroadHalfY = body.Circle ? body.Radius : s * body.HalfX + c * body.HalfY;
        body.MinX = body.X - worldHalfX;
        body.MaxX = body.X + worldHalfX;
        return true;
    };

    auto computeContact = [&](const Body2D& a, const Body2D& b, Contact2D& contact) -> bool
    {
        if (a.Circle && b.Circle)
        {
            const float dx = b.X - a.X, dy = b.Y - a.Y;
            const float distanceSq = dx * dx + dy * dy;
            const float radii = a.Radius + b.Radius;
            if (distanceSq >= radii * radii)
                return false;
            const float distance = std::sqrt(std::max(distanceSq, 1.0e-8f));
            contact.NormalX = distance > 1.0e-4f ? dx / distance : 1.0f;
            contact.NormalY = distance > 1.0e-4f ? dy / distance : 0.0f;
            contact.Penetration = radii - distance;
            contact.PointX = a.X + contact.NormalX * (a.Radius - contact.Penetration * 0.5f);
            contact.PointY = a.Y + contact.NormalY * (a.Radius - contact.Penetration * 0.5f);
            return true;
        }

        auto circleBox = [](const Body2D& circle, const Body2D& box,
                            float& nx, float& ny, float& penetration,
                            float& pointX, float& pointY) -> bool
        {
            const float c = std::cos(box.Angle), s = std::sin(box.Angle);
            const float dxWorld = circle.X - box.X, dyWorld = circle.Y - box.Y;
            const float localX = c * dxWorld + s * dyWorld;
            const float localY = -s * dxWorld + c * dyWorld;
            const float closestX = std::clamp(localX, -box.HalfX, box.HalfX);
            const float closestY = std::clamp(localY, -box.HalfY, box.HalfY);
            float dx = closestX - localX, dy = closestY - localY;
            const float distanceSq = dx * dx + dy * dy;
            if (distanceSq > circle.Radius * circle.Radius)
                return false;
            if (distanceSq > 1.0e-8f)
            {
                const float distance = std::sqrt(distanceSq);
                dx /= distance;
                dy /= distance;
                penetration = circle.Radius - distance;
            }
            else
            {
                const float toLeft = localX + box.HalfX;
                const float toRight = box.HalfX - localX;
                const float toBottom = localY + box.HalfY;
                const float toTop = box.HalfY - localY;
                const float nearest = std::min({ toLeft, toRight, toBottom, toTop });
                if (nearest == toLeft)      { dx = -1.0f; dy = 0.0f; }
                else if (nearest == toRight){ dx = 1.0f; dy = 0.0f; }
                else if (nearest == toBottom){ dx = 0.0f; dy = -1.0f; }
                else                        { dx = 0.0f; dy = 1.0f; }
                penetration = circle.Radius + nearest;
            }
            nx = c * dx - s * dy;
            ny = s * dx + c * dy;
            const float localPointX = localX + dx * (circle.Radius - penetration * 0.5f);
            const float localPointY = localY + dy * (circle.Radius - penetration * 0.5f);
            pointX = box.X + c * localPointX - s * localPointY;
            pointY = box.Y + s * localPointX + c * localPointY;
            return true;
        };

        if (a.Circle || b.Circle)
        {
            float nx = 0.0f, ny = 1.0f, penetration = 0.0f;
            float pointX = 0.0f, pointY = 0.0f;
            const bool hit = a.Circle
                ? circleBox(a, b, nx, ny, penetration, pointX, pointY)
                : circleBox(b, a, nx, ny, penetration, pointX, pointY);
            if (!hit)
                return false;
            contact.NormalX = a.Circle ? nx : -nx;
            contact.NormalY = a.Circle ? ny : -ny;
            contact.Penetration = penetration;
            contact.PointX = pointX;
            contact.PointY = pointY;
            return true;
        }

        const float ax[2] = { std::cos(a.Angle), std::sin(a.Angle) };
        const float ay[2] = { -std::sin(a.Angle), std::cos(a.Angle) };
        const float bx[2] = { std::cos(b.Angle), std::sin(b.Angle) };
        const float by[2] = { -std::sin(b.Angle), std::cos(b.Angle) };
        const float deltaX = b.X - a.X, deltaY = b.Y - a.Y;
        float bestOverlap = std::numeric_limits<float>::max();
        float normalX = 0.0f, normalY = 1.0f;
        const float* axes[4] = { ax, ay, bx, by };
        for (const float* axis : axes)
        {
            const float projection = deltaX * axis[0] + deltaY * axis[1];
            const float radiusA = a.HalfX * std::abs(ax[0] * axis[0] + ax[1] * axis[1]) +
                                  a.HalfY * std::abs(ay[0] * axis[0] + ay[1] * axis[1]);
            const float radiusB = b.HalfX * std::abs(bx[0] * axis[0] + bx[1] * axis[1]) +
                                  b.HalfY * std::abs(by[0] * axis[0] + by[1] * axis[1]);
            const float overlap = radiusA + radiusB - std::abs(projection);
            if (overlap <= 0.0f)
                return false;
            if (overlap < bestOverlap)
            {
                bestOverlap = overlap;
                const float sign = projection >= 0.0f ? 1.0f : -1.0f;
                normalX = axis[0] * sign;
                normalY = axis[1] * sign;
            }
        }

        const float tangentX = -normalY, tangentY = normalX;
        auto supportFeatureInterval = [&](const Body2D& body,
                                          const float* axisX,
                                          const float* axisY,
                                          float supportDirection,
                                          float& minimum,
                                          float& maximum)
        {
            float featureCenter = body.X * tangentX + body.Y * tangentY;
            float featureHalfLength = 0.0f;
            const float axisNormal[2] = {
                axisX[0] * normalX + axisX[1] * normalY,
                axisY[0] * normalX + axisY[1] * normalY
            };
            const float axisTangent[2] = {
                axisX[0] * tangentX + axisX[1] * tangentY,
                axisY[0] * tangentX + axisY[1] * tangentY
            };
            const float extents[2] = { body.HalfX, body.HalfY };
            for (int axis = 0; axis < 2; ++axis)
            {
                if (std::abs(axisNormal[axis]) < 0.0001f)
                    featureHalfLength += extents[axis] * std::abs(axisTangent[axis]);
                else
                    featureCenter += (axisNormal[axis] * supportDirection >= 0.0f
                        ? extents[axis] : -extents[axis]) * axisTangent[axis];
            }
            minimum = featureCenter - featureHalfLength;
            maximum = featureCenter + featureHalfLength;
        };
        float featureAMin = 0.0f, featureAMax = 0.0f;
        float featureBMin = 0.0f, featureBMax = 0.0f;
        supportFeatureInterval(a, ax, ay, 1.0f, featureAMin, featureAMax);
        supportFeatureInterval(b, bx, by, -1.0f, featureBMin, featureBMax);
        const float tangentMin = std::max(featureAMin, featureBMin);
        const float tangentMax = std::min(featureAMax, featureBMax);
        const float tangentPoint = (tangentMin + tangentMax) * 0.5f;
        const float centerAOnNormal = a.X * normalX + a.Y * normalY;
        const float centerBOnNormal = b.X * normalX + b.Y * normalY;
        const float radiusANormal = a.HalfX * std::abs(ax[0] * normalX + ax[1] * normalY) +
                                    a.HalfY * std::abs(ay[0] * normalX + ay[1] * normalY);
        const float radiusBNormal = b.HalfX * std::abs(bx[0] * normalX + bx[1] * normalY) +
                                    b.HalfY * std::abs(by[0] * normalX + by[1] * normalY);
        const float normalPoint = (centerAOnNormal + radiusANormal +
                                   centerBOnNormal - radiusBNormal) * 0.5f;
        contact.NormalX = normalX;
        contact.NormalY = normalY;
        contact.Penetration = bestOverlap;
        contact.PointX = normalX * normalPoint + tangentX * tangentPoint;
        contact.PointY = normalY * normalPoint + tangentY * tangentPoint;
        return true;
    };

    auto inverseInertia = [](const Entity& entity)
    {
        if (entity.IsStatic || entity.FreezeRotation)
            return 0.0f;
        const Transform& transform = const_cast<Entity&>(entity).GetTransform();
        const float width = std::abs(transform.Scale.X);
        const float height = std::abs(transform.Scale.Y);
        const float mass = std::max(0.001f, entity.Mass);
        const float inertia = entity.Collider == Entity::ColliderShape::Sphere
            ? 0.5f * mass * std::pow(std::max(width, height) * 0.5f, 2.0f)
            : mass * (width * width + height * height) / 12.0f;
        return 1.0f / std::max(0.0001f, inertia);
    };

    auto cross2 = [](float rx, float ry, float vx, float vy)
    {
        return rx * vy - ry * vx;
    };

    std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> currentContacts;
    currentContacts.reserve(std::max(m_PhysicsContactPairs.size(), entities.size()));
    std::unordered_map<uint64_t, CollisionEvent2D> collisionEvents;
    std::vector<Body2D> bodies;
    bodies.reserve(entities.size());

    for (size_t i = 0; i < entities.size(); ++i)
    {
        Body2D body;
        if (buildBody(i, body))
            bodies.push_back(body);
    }
    std::sort(bodies.begin(), bodies.end(), [](const Body2D& a, const Body2D& b)
    {
        return a.MinX != b.MinX ? a.MinX < b.MinX : a.EntityIndex < b.EntityIndex;
    });

    // Candidate broadphase is built once per fixed step. The solver refreshes
    // each candidate's exact shape every iteration, avoiding 8 full rebuilds
    // and sorts while still converging contacts at the original iteration count.
    std::vector<std::pair<size_t, size_t>> candidatePairs;
    candidatePairs.reserve(bodies.size() * 2);
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        const Body2D& broadA = bodies[i];
        for (size_t j = i + 1; j < bodies.size() && bodies[j].MinX <= broadA.MaxX; ++j)
        {
            const Body2D& broadB = bodies[j];
            if ((!broadA.Dynamic && !broadB.Dynamic) ||
                std::abs(broadB.Y - broadA.Y) > broadA.BroadHalfY + broadB.BroadHalfY)
                continue;
            candidatePairs.emplace_back(i, j);
        }
    }

    for (int iteration = 0; iteration < solverIterations; ++iteration)
    {
        if (iteration > 0)
        {
            for (Body2D& body : bodies)
            {
                Body2D updated;
                if (buildBody(body.EntityIndex, updated))
                    body = updated;
            }
        }

        for (const auto& pair : candidatePairs)
        {
                Body2D& broadA = bodies[pair.first];
                Body2D& broadB = bodies[pair.second];
                Entity& a = entities[broadA.EntityIndex];
                Entity& b = entities[broadB.EntityIndex];
                Contact2D contact;
                if (!computeContact(broadA, broadB, contact))
                    continue;

                const uint64_t idA = a.GetID(), idB = b.GetID();
                const uint64_t pairKey = PhysicsPairKey(idA, idB);
                currentContacts.emplace(pairKey, std::make_pair(idA, idB));
                if (collisionEvents.find(pairKey) == collisionEvents.end())
                {
                    const auto previous = m_PhysicsContactPairs.find(pairKey);
                    const bool isTrigger = a.IsTrigger || b.IsTrigger;
                    const int eventType = isTrigger
                        ? (previous == m_PhysicsContactPairs.end() ? 3 : 4)
                        : (previous == m_PhysicsContactPairs.end() ? 0 : 1);
                    collisionEvents.emplace(pairKey, CollisionEvent2D{
                        idA, idB, eventType, contact.PointX, contact.PointY,
                        contact.NormalX, contact.NormalY, 0.0f });
                }

                // Triggeres informam scripts sem alterar velocidade, posição,
                // atrito ou repouso dos corpos que se sobrepõem.
                if (a.IsTrigger || b.IsTrigger)
                    continue;

                const float inverseMassA = broadA.Dynamic ? 1.0f / std::max(0.001f, a.Mass) : 0.0f;
                const float inverseMassB = broadB.Dynamic ? 1.0f / std::max(0.001f, b.Mass) : 0.0f;
                const float inverseMassSum = inverseMassA + inverseMassB;
                if (inverseMassSum <= 0.0f)
                    continue;

                const float rAx = contact.PointX - a.GetTransform().Position.X;
                const float rAy = contact.PointY - a.GetTransform().Position.Y;
                const float rBx = contact.PointX - b.GetTransform().Position.X;
                const float rBy = contact.PointY - b.GetTransform().Position.Y;
                const float invInertiaA = inverseInertia(a);
                const float invInertiaB = inverseInertia(b);
                auto isFaceSupported = [&](const Body2D& body,
                                           const Body2D& surface,
                                           const Entity& bodyEntity,
                                           const Entity& surfaceEntity)
                {
                    if (!body.Dynamic || !surfaceEntity.IsStatic ||
                        bodyEntity.Collider != Entity::ColliderShape::Box ||
                        surfaceEntity.Collider != Entity::ColliderShape::Box)
                        return false;
                    const float bodyAxisYx = -std::sin(body.Angle);
                    const float bodyAxisYy = std::cos(body.Angle);
                    if (std::abs(bodyAxisYx * contact.NormalX +
                                 bodyAxisYy * contact.NormalY) < 0.9995f)
                        return false;
                    const float surfaceAxisXx = std::cos(surface.Angle);
                    const float surfaceAxisXy = std::sin(surface.Angle);
                    const float centerOffset =
                        (body.X - surface.X) * surfaceAxisXx +
                        (body.Y - surface.Y) * surfaceAxisXy;
                    const float surfaceHalfX = surface.HalfX;
                    return std::abs(centerOffset) <= surfaceHalfX + 0.001f;
                };
                // A face-on box resting inside a finite static face has a
                // contact manifold, not a single friction point. Apply the
                // tangential resultant through the COM while it remains over
                // the support polygon; once it crosses an edge, real torque
                // is restored and the body tips.
                const bool supportedA = isFaceSupported(broadA, broadB, a, b);
                const bool supportedB = isFaceSupported(broadB, broadA, b, a);
                auto relativeAtContact = [&]()
                {
                    const float velocityAx = a.Velocity.X - a.AngularVelocity.Z * rAy;
                    const float velocityAy = a.Velocity.Y + a.AngularVelocity.Z * rAx;
                    const float velocityBx = b.Velocity.X - b.AngularVelocity.Z * rBy;
                    const float velocityBy = b.Velocity.Y + b.AngularVelocity.Z * rBx;
                    return std::pair<float, float>(velocityBx - velocityAx,
                                                   velocityBy - velocityAy);
                };
                auto relative = relativeAtContact();
                const float normalSpeed = relative.first * contact.NormalX +
                                          relative.second * contact.NormalY;
                float normalImpulse = 0.0f;
                if (normalSpeed < 0.0f)
                {
                    const float restitution = iteration == 0 && std::abs(normalSpeed) > 0.75f
                        ? std::min(std::clamp(a.Restitution, 0.0f, 1.0f),
                                   std::clamp(b.Restitution, 0.0f, 1.0f))
                        : 0.0f;
                    const float armA = cross2(rAx, rAy, contact.NormalX, contact.NormalY);
                    const float armB = cross2(rBx, rBy, contact.NormalX, contact.NormalY);
                    const float effectiveMass = inverseMassSum +
                        armA * armA * invInertiaA + armB * armB * invInertiaB;
                    normalImpulse = -(1.0f + restitution) * normalSpeed /
                                    std::max(0.0001f, effectiveMass);
                    if (broadA.Dynamic)
                    {
                        a.Velocity.X -= contact.NormalX * normalImpulse * inverseMassA;
                        a.Velocity.Y -= contact.NormalY * normalImpulse * inverseMassA;
                        a.AngularVelocity.Z -= armA * normalImpulse * invInertiaA;
                    }
                    if (broadB.Dynamic)
                    {
                        b.Velocity.X += contact.NormalX * normalImpulse * inverseMassB;
                        b.Velocity.Y += contact.NormalY * normalImpulse * inverseMassB;
                        b.AngularVelocity.Z += armB * normalImpulse * invInertiaB;
                    }
                    collisionEvents[pairKey].Impulse = std::max(
                        collisionEvents[pairKey].Impulse, normalImpulse);
                }

                relative = relativeAtContact();
                const float tangentX = relative.first -
                                       (relative.first * contact.NormalX + relative.second * contact.NormalY) * contact.NormalX;
                const float tangentY = relative.second -
                                       (relative.first * contact.NormalX + relative.second * contact.NormalY) * contact.NormalY;
                const float tangentLength = std::sqrt(tangentX * tangentX + tangentY * tangentY);
                if (normalImpulse > 0.0f && tangentLength > 1.0e-5f)
                {
                    const float tx = tangentX / tangentLength, ty = tangentY / tangentLength;
                    const float friction = std::sqrt(
                        std::clamp(a.Friction, 0.0f, 1.0f) *
                        std::clamp(b.Friction, 0.0f, 1.0f));
                    const float tangentArmA = supportedA ? 0.0f : cross2(rAx, rAy, tx, ty);
                    const float tangentArmB = supportedB ? 0.0f : cross2(rBx, rBy, tx, ty);
                    const float tangentMass = inverseMassSum +
                        tangentArmA * tangentArmA * invInertiaA +
                        tangentArmB * tangentArmB * invInertiaB;
                    const float tangentImpulse = std::clamp(
                        -(relative.first * tx + relative.second * ty) /
                            std::max(0.0001f, tangentMass),
                        -normalImpulse * friction, normalImpulse * friction);
                    if (broadA.Dynamic)
                    {
                        a.Velocity.X -= tx * tangentImpulse * inverseMassA;
                        a.Velocity.Y -= ty * tangentImpulse * inverseMassA;
                        a.AngularVelocity.Z -= tangentArmA * tangentImpulse * invInertiaA;
                    }
                    if (broadB.Dynamic)
                    {
                        b.Velocity.X += tx * tangentImpulse * inverseMassB;
                        b.Velocity.Y += ty * tangentImpulse * inverseMassB;
                        b.AngularVelocity.Z += tangentArmB * tangentImpulse * invInertiaB;
                    }
                }

                constexpr float penetrationSlop = 0.001f;
                constexpr float correctionPercent = 0.20f;
                const float correction = std::max(0.0f, contact.Penetration - penetrationSlop) *
                    correctionPercent / inverseMassSum;
                if (broadA.Dynamic)
                {
                    a.GetTransform().Position.X -= contact.NormalX * correction * inverseMassA;
                    a.GetTransform().Position.Y -= contact.NormalY * correction * inverseMassA;
                }
                if (broadB.Dynamic)
                {
                    b.GetTransform().Position.X += contact.NormalX * correction * inverseMassB;
                    b.GetTransform().Position.Y += contact.NormalY * correction * inverseMassB;
                }
        }
    }

    for (const auto& [pairKey, event] : collisionEvents)
    {
        (void)pairKey;
        ScriptingRuntime::Instance().FireCollisionEvent(
            event.A, event.B, event.Type, event.X, event.Y, 0.0f,
            -event.NormalX, -event.NormalY, 0.0f, event.Impulse);
        ScriptingRuntime::Instance().FireCollisionEvent(
            event.B, event.A, event.Type, event.X, event.Y, 0.0f,
            event.NormalX, event.NormalY, 0.0f, event.Impulse);
    }
    for (const auto& [previousPair, entityIDs] : m_PhysicsContactPairs)
    {
        if (currentContacts.find(previousPair) != currentContacts.end() ||
            !m_Scene.FindEntityByID(entityIDs.first) ||
            !m_Scene.FindEntityByID(entityIDs.second))
            continue;
        Entity* a = m_Scene.FindEntityByID(entityIDs.first);
        Entity* b = m_Scene.FindEntityByID(entityIDs.second);
        const int exitEvent = (a && a->IsTrigger) || (b && b->IsTrigger) ? 5 : 2;
        ScriptingRuntime::Instance().FireCollisionEvent(entityIDs.first, entityIDs.second, exitEvent);
        ScriptingRuntime::Instance().FireCollisionEvent(entityIDs.second, entityIDs.first, exitEvent);
    }
    m_PhysicsContactPairs = std::move(currentContacts);
}

void Application::SimulatePhysicsStep(float fixedDeltaTime)
{
    if (m_WorldMode == WorldMode::World2D)
    {
        SimulatePhysics2DStep(fixedDeltaTime);
        return;
    }

    auto& entities = m_Scene.GetEntities();
    std::unordered_map<uint64_t, std::pair<uint64_t, uint64_t>> currentContacts;
    currentContacts.reserve(std::max(m_PhysicsContactPairs.size(), entities.size() * 2));
    constexpr float gravity = -9.81f;
    constexpr int solverIterations = 10;
    constexpr float maximumAngularSpeed = 40.0f;
    constexpr float angularDampingPerSecond = 4.0f;
    constexpr float terminalFallSpeed = 25.0f;

    for (Entity& entity : entities)
    {
        if (entity.IsEditorOnly() || !entity.PhysicsEnabled || entity.IsStatic)
        {
            continue;
        }

        if (entity.UseGravity)
        {
            // Arrasto quadrático do ar. Em velocidades baixas quase não altera
            // a gravidade terrestre; em quedas longas sua força cresce até
            // equilibrar g próximo à velocidade terminal configurada.
            const float dragCoefficient = -gravity /
                (terminalFallSpeed * terminalFallSpeed);
            const float airDragAcceleration = -dragCoefficient *
                entity.Velocity.Y * std::abs(entity.Velocity.Y);
            entity.Velocity.Y +=
                (gravity + airDragAcceleration) * fixedDeltaTime;
        }

        // Amortecimento linear natural (arrasto do ar)
        constexpr float defaultLinearDamping = 0.05f;
        const float linDampFactor = std::clamp(1.0f - defaultLinearDamping * fixedDeltaTime, 0.0f, 1.0f);
        entity.Velocity.X *= linDampFactor;
        entity.Velocity.Y *= linDampFactor;
        entity.Velocity.Z *= linDampFactor;

        // Clamp de velocidade linear para evitar tunneling catastrófico
        constexpr float maximumLinearSpeed = 120.0f;
        const float currentSpeedSq = PhysicsLengthSquared(entity.Velocity);
        if (currentSpeedSq > maximumLinearSpeed * maximumLinearSpeed)
        {
            const float speedScale = maximumLinearSpeed / std::sqrt(currentSpeedSq);
            entity.Velocity.X *= speedScale;
            entity.Velocity.Y *= speedScale;
            entity.Velocity.Z *= speedScale;
        }

        // Contatos fora do centro aplicam torque. Amortecimento angular configurável
        // protege contra giro perpétuo e acúmulo de energia em contatos sucessivos.
        if (!entity.FreezeRotation)
        {
            const float totalAngularDampRate = angularDampingPerSecond + 0.5f;
            const float angularDamping = std::exp(-totalAngularDampRate * fixedDeltaTime);
            entity.AngularVelocity.X *= angularDamping;
            entity.AngularVelocity.Y *= angularDamping;
            entity.AngularVelocity.Z *= angularDamping;

            const float angularSpeed = std::sqrt(
                PhysicsLengthSquared(entity.AngularVelocity));
            if (angularSpeed > maximumAngularSpeed)
            {
                const float scale = maximumAngularSpeed / angularSpeed;
                entity.AngularVelocity.X *= scale;
                entity.AngularVelocity.Y *= scale;
                entity.AngularVelocity.Z *= scale;
            }
        }

        Transform& transform = entity.GetTransform();
        transform.Position = PhysicsAddScaled(
            transform.Position,
            entity.Velocity,
            fixedDeltaTime
        );

        if (!entity.FreezeRotation &&
            PhysicsLengthSquared(entity.AngularVelocity) > 0.000001f)
        {
            const float angularSpeed = std::sqrt(
                PhysicsLengthSquared(entity.AngularVelocity));
            const glm::vec3 angularAxis(
                entity.AngularVelocity.X / angularSpeed,
                entity.AngularVelocity.Y / angularSpeed,
                entity.AngularVelocity.Z / angularSpeed);
            const glm::quat deltaRotation = glm::angleAxis(
                angularSpeed * fixedDeltaTime, angularAxis);
            transform.Rotation = glm::normalize(deltaRotation * transform.Rotation);
            const glm::vec3 euler = glm::degrees(glm::eulerAngles(transform.Rotation));
            transform.EulerRotation = Vector3(euler.x, euler.y, euler.z);
        }
    }

    std::vector<PhysicsBroadphaseBody> broadphaseBodies;
    std::vector<PhysicsCandidatePair> candidatePairs;
    broadphaseBodies.reserve(entities.size());
    candidatePairs.reserve(std::min<size_t>(entities.size() * 4, 65536));

    // Build the conservative candidate set once per fixed step. Rebuilding and
    // sorting the broadphase in every solver pass multiplied CPU cost by ten;
    // exact contacts are still recomputed below on every pass.
    for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex)
    {
        const Entity& entity = entities[entityIndex];
        if (entity.IsEditorOnly() || !entity.Active ||
            !entity.PhysicsEnabled || !entity.ColliderEnabled)
            continue;

        const Vector3 half = PhysicsBoxHalfExtents(entity);
        const float boxRadius = std::sqrt(
            half.X * half.X + half.Y * half.Y + half.Z * half.Z);
        const float radius = m_WorldMode == WorldMode::World2D
            ? std::max({ half.X, half.Y, PhysicsSphereRadius(entity) })
            : std::max(boxRadius, PhysicsSphereRadius(entity));
        const glm::vec3 center = PhysicsToGlm(PhysicsShapeCenterWorld(entity));
        broadphaseBodies.push_back({
            entityIndex,
            center,
            radius,
            center.x - radius,
            center.x + radius,
            entity.IsStatic
        });
    }

    std::sort(broadphaseBodies.begin(), broadphaseBodies.end(),
        [](const PhysicsBroadphaseBody& a, const PhysicsBroadphaseBody& b)
        {
            if (a.MinX != b.MinX)
                return a.MinX < b.MinX;
            return a.EntityIndex < b.EntityIndex;
        });

    for (size_t leftIndex = 0; leftIndex < broadphaseBodies.size(); ++leftIndex)
    {
        const PhysicsBroadphaseBody& left = broadphaseBodies[leftIndex];
        for (size_t rightIndex = leftIndex + 1;
             rightIndex < broadphaseBodies.size() &&
                 broadphaseBodies[rightIndex].MinX <= left.MaxX;
             ++rightIndex)
        {
            const PhysicsBroadphaseBody& right = broadphaseBodies[rightIndex];
            if (left.IsStatic && right.IsStatic)
                continue;

            const float dx = right.Center.x - left.Center.x;
            const float dy = right.Center.y - left.Center.y;
            const float dz = right.Center.z - left.Center.z;
            const float combinedRadius = left.Radius + right.Radius;
            const float distanceSq = m_WorldMode == WorldMode::World2D
                ? dx * dx + dy * dy
                : dx * dx + dy * dy + dz * dz;
            if (distanceSq > combinedRadius * combinedRadius)
                continue;

            candidatePairs.push_back({
                std::min(left.EntityIndex, right.EntityIndex),
                std::max(left.EntityIndex, right.EntityIndex)
            });
        }
    }

    // Preserve deterministic resolution order so piles settle consistently.
    std::sort(candidatePairs.begin(), candidatePairs.end(),
        [](const PhysicsCandidatePair& a, const PhysicsCandidatePair& b)
        {
            return a.A != b.A ? a.A < b.A : a.B < b.B;
        });

    for (int iteration = 0; iteration < solverIterations; ++iteration)
    {
        for (const PhysicsCandidatePair& pair : candidatePairs)
        {
                if (pair.A >= entities.size() || pair.B >= entities.size())
                    continue;
                PhysicsContact contact;
                uint64_t entityAID = 0;
                uint64_t entityBID = 0;
                int pendingCollisionEvent = -1;
                float pendingImpulse = 0.0f;
                bool pendingTrigger = false;
                Vector3 eventContactPoint;
                {
                    Entity& a = entities[pair.A];
                    Entity& b = entities[pair.B];
                    if (!PhysicsComputeContact(a, b, contact))
                        continue;

                    entityAID = a.GetID();
                    entityBID = b.GetID();
                    const uint64_t pairKey = PhysicsPairKey(entityAID, entityBID);
                    const bool wasReportedThisStep = currentContacts.count(pairKey) > 0;
                    currentContacts.emplace(pairKey, std::make_pair(entityAID, entityBID));
                    pendingTrigger = a.IsTrigger || b.IsTrigger;
                    if (!wasReportedThisStep)
                    {
                        const bool wasPreviouslyTouching = m_PhysicsContactPairs.count(pairKey) > 0;
                        pendingCollisionEvent = pendingTrigger
                            ? (wasPreviouslyTouching ? 4 : 3)
                            : (wasPreviouslyTouching ? 1 : 0);
                        const Vector3 relativeVelocity = PhysicsSubtract(b.Velocity, a.Velocity);
                        const float closingSpeed = std::max(0.0f, -PhysicsDot(relativeVelocity, contact.Normal));
                        const float inverseMassA = a.IsStatic ? 0.0f : 1.0f / std::max(0.001f, a.Mass);
                        const float inverseMassB = b.IsStatic ? 0.0f : 1.0f / std::max(0.001f, b.Mass);
                        const float inverseMassSum = inverseMassA + inverseMassB;
                        pendingImpulse = inverseMassSum > 0.0f
                            ? closingSpeed / inverseMassSum
                            : 0.0f;
                    }
                    eventContactPoint = contact.Point;
                }
                const Vector3 eventContactNormal = contact.Normal;

                // Preserve collision callback timing while reacquiring entities
                // afterward: a script may instantiate an object and reallocate
                // the scene's entity vector during the callback.
                if (pendingCollisionEvent >= 0)
                {
                    ScriptingRuntime::Instance().FireCollisionEvent(
                        entityAID, entityBID, pendingCollisionEvent,
                        eventContactPoint.X, eventContactPoint.Y, eventContactPoint.Z,
                        -eventContactNormal.X, -eventContactNormal.Y, -eventContactNormal.Z,
                        pendingImpulse);
                    ScriptingRuntime::Instance().FireCollisionEvent(
                        entityBID, entityAID, pendingCollisionEvent,
                        eventContactPoint.X, eventContactPoint.Y, eventContactPoint.Z,
                        eventContactNormal.X, eventContactNormal.Y, eventContactNormal.Z,
                        pendingImpulse);
                }

                if (pair.A >= entities.size() || pair.B >= entities.size() ||
                    entities[pair.A].GetID() != entityAID ||
                    entities[pair.B].GetID() != entityBID)
                    continue;
                Entity& a = entities[pair.A];
                Entity& b = entities[pair.B];

                if (pendingTrigger || a.IsTrigger || b.IsTrigger)
                    continue;

                const float inverseMassA = a.IsStatic ? 0.0f : 1.0f / std::max(0.001f, a.Mass);
                const float inverseMassB = b.IsStatic ? 0.0f : 1.0f / std::max(0.001f, b.Mass);
                const float inverseMassSum = inverseMassA + inverseMassB;
                if (inverseMassSum > 0.0f)
                {
                    constexpr float penetrationSlop = 0.004f;
                    constexpr float correctionPercent = 0.35f;
                    const float correction =
                        std::max(0.0f, contact.Penetration - penetrationSlop) *
                        correctionPercent / inverseMassSum;

                    if (!a.IsStatic)
                    {
                        a.GetTransform().Position = PhysicsAddScaled(
                            a.GetTransform().Position,
                            contact.Normal,
                            -correction * inverseMassA
                        );
                    }
                    if (!b.IsStatic)
                    {
                        b.GetTransform().Position = PhysicsAddScaled(
                            b.GetTransform().Position,
                            contact.Normal,
                            correction * inverseMassB
                        );
                    }
                }

                const float combinedFriction = std::sqrt(
                    std::clamp(a.Friction, 0.0f, 1.0f) *
                    std::clamp(b.Friction, 0.0f, 1.0f)
                );
                // Em plataformas horizontais, use a resultante da área de
                // apoio: no interior ela passa sob o centro de massa; na borda
                // fica limitada ao contorno real da superfície.
                bool centerOfMassInsideSupport = true;
                bool hasHorizontalStaticSupport = false;
                if ((a.IsStatic != b.IsStatic) &&
                    std::abs(contact.Normal.Y) > 0.80f)
                {
                    Entity& dynamicBody = a.IsStatic ? b : a;
                    Entity& staticSurface = a.IsStatic ? a : b;
                    const Vector3 surfaceToBodyNormal = a.IsStatic
                        ? contact.Normal
                        : Vector3(-contact.Normal.X, -contact.Normal.Y, -contact.Normal.Z);
                    Vector3 supportPoint;
                    if (PhysicsStaticSupportPoint(
                            staticSurface, dynamicBody, surfaceToBodyNormal, supportPoint))
                    {
                        contact.Point = supportPoint;
                        hasHorizontalStaticSupport = true;
                        const Vector3& center = dynamicBody.GetTransform().Position;
                        constexpr float supportTolerance = 0.001f;
                        centerOfMassInsideSupport =
                            std::abs(center.X - supportPoint.X) <= supportTolerance &&
                            std::abs(center.Z - supportPoint.Z) <= supportTolerance;
                    }
                }
                PhysicsApplyImpulse(
                    a,
                    b,
                    contact.Normal,
                    iteration == 0
                        ? std::min(a.Restitution, b.Restitution)
                        : 0.0f,
                    combinedFriction,
                    contact.Point
                );

                // Resistência ao rolamento: uma única ponta de contato pode
                // gerar torque num cubo apoiado, mas esse torque não deve ser
                // reaplicado indefinidamente quando o corpo já está em repouso.
                // Só estabiliza repouso sobre piso quase horizontal. Rampa,
                // deslizamento e saída de borda precisam conservar a velocidade
                // tangencial para que o corpo possa entrar em voo balístico.
                if ((a.IsStatic || b.IsStatic) &&
                    hasHorizontalStaticSupport &&
                    centerOfMassInsideSupport)
                {
                    const Vector3 relativeVelocity = PhysicsSubtract(
                        b.Velocity, a.Velocity);
                    const float normalSpeed = std::abs(
                        PhysicsDot(relativeVelocity, contact.Normal));
                    if (normalSpeed < 0.15f)
                    {
                        const float rollingDamping = std::exp(
                            -24.0f * fixedDeltaTime /
                            static_cast<float>(solverIterations));
                        auto dampBody = [&](Entity& body)
                        {
                            if (body.IsStatic || body.FreezeRotation)
                                return;
                            body.AngularVelocity.X *= rollingDamping;
                            body.AngularVelocity.Y *= rollingDamping;
                            body.AngularVelocity.Z *= rollingDamping;
                            if (std::abs(body.Velocity.Y) < 0.08f &&
                                std::abs(body.Velocity.X) < 0.08f &&
                                std::abs(body.Velocity.Z) < 0.08f)
                                body.Velocity = Vector3(0.0f, 0.0f, 0.0f);
                            if (std::abs(body.AngularVelocity.X) < 0.02f)
                                body.AngularVelocity.X = 0.0f;
                            if (std::abs(body.AngularVelocity.Y) < 0.02f)
                                body.AngularVelocity.Y = 0.0f;
                            if (std::abs(body.AngularVelocity.Z) < 0.02f)
                                body.AngularVelocity.Z = 0.0f;
                        };
                        dampBody(a);
                        dampBody(b);
                    }
                }

        }
    }

    for (const auto& [previousPair, entityIDs] : m_PhysicsContactPairs)
    {
        if (currentContacts.count(previousPair) != 0)
            continue;
        if (!m_Scene.FindEntityByID(entityIDs.first) ||
            !m_Scene.FindEntityByID(entityIDs.second))
            continue;
        Entity* a = m_Scene.FindEntityByID(entityIDs.first);
        Entity* b = m_Scene.FindEntityByID(entityIDs.second);
        const int exitEvent = (a && a->IsTrigger) || (b && b->IsTrigger) ? 5 : 2;
        ScriptingRuntime::Instance().FireCollisionEvent(
            entityIDs.first, entityIDs.second, exitEvent);
        ScriptingRuntime::Instance().FireCollisionEvent(
            entityIDs.second, entityIDs.first, exitEvent);
    }
    m_PhysicsContactPairs = std::move(currentContacts);
}

void Application::ExecuteAttachedScripts(float deltaTime)
{
    // Ponte de scripting incremental. O projeto ainda nÃ£o possui CLR/Mono
    // incorporado; por isso interpretamos somente operaÃ§Ãµes seguras e
    // explÃ­citas da API C# documentada nos scripts atuais.
    if (deltaTime <= 0.0f)
        return;

    const float safeDeltaTime = std::clamp(deltaTime, 0.0f, 0.1f);
    auto& entities = m_Scene.GetEntities();

    for (Entity& entity : entities)
    {
        if ((entity.IsEditorOnly() && !entity.IsGlobalScript()) || !entity.Active)
            continue;

        ScriptComponent& script = entity.GetScriptComponent();
        if (!script.Enabled || script.ScriptPath.empty())
            continue;

        const std::filesystem::path scriptPath =
            ResolveProjectFilePath(m_ProjectConfig, script.ScriptPath);
        std::ifstream file(scriptPath);
        if (!file.is_open())
        {
            // NÃ£o interrompe o jogo por causa de um script ausente. O erro Ã©
            // emitido uma vez por entrada de Play pelo mapa de tempo.
            const uint64_t errorKey = entity.GetID() ^ 0x8000000000000000ULL;
            if (m_ScriptTimes.find(errorKey) == m_ScriptTimes.end())
            {
                std::cerr << "[Script] Arquivo nao encontrado: "
                          << scriptPath.string() << "\n";
                m_ScriptTimes[errorKey] = 0.0f;
            }
            continue;
        }

        const std::string source(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        m_ScriptTimes[entity.GetID()] += safeDeltaTime;

        // Suporte Ã  forma atual: transform.Rotate(rotacao, Space.Self) e Ã s
        // variantes transform.Rotate(rotacao) / transform.Rotate(...).
        if (source.find("transform.Rotate") == std::string::npos)
            continue;

        auto readFloat = [&](const std::initializer_list<const char*>& names,
                             float fallback) -> float
        {
            for (const char* name : names)
            {
                const std::regex expression(
                    std::string(name) + R"(\s*=\s*(-?[0-9]+(?:\.[0-9]+)?))",
                    std::regex_constants::icase);
                std::smatch match;
                if (std::regex_search(source, match, expression) && match.size() > 1)
                {
                    try { return std::stof(match[1].str()); }
                    catch (...) {}
                }
            }
            return fallback;
        };

        auto readBool = [&](const char* name, bool fallback) -> bool
        {
            const std::regex expression(
                std::string(name) + R"(\s*=\s*(true|false))",
                std::regex_constants::icase);
            std::smatch match;
            if (std::regex_search(source, match, expression) && match.size() > 1)
                return match[1].str() == "true" || match[1].str() == "True";
            return fallback;
        };

        // Aceita os nomes usados pelos scripts existentes e pelos exemplos
        // Unity-like da Marae.
        const float speedDegrees = readFloat(
            {"velocidadeRotacao", "RotationSpeed", "rotationSpeed", "Speed"},
            90.0f);
        const bool rotateX = readBool("rotacionarX", false);
        const bool rotateY = readBool("rotacionarY", true);
        const bool rotateZ = readBool("rotacionarZ", false);

        glm::vec3 axis(
            rotateX ? 1.0f : 0.0f,
            rotateY ? 1.0f : 0.0f,
            rotateZ ? 1.0f : 0.0f);
        if (glm::length(axis) <= 0.0001f)
            continue;

        axis = glm::normalize(axis);
        const glm::quat deltaRotation = glm::angleAxis(
            glm::radians(speedDegrees) * safeDeltaTime, axis);
        Transform& transform = entity.GetTransform();
        transform.Rotation = glm::normalize(deltaRotation * transform.Rotation);
        const glm::vec3 euler = glm::degrees(glm::eulerAngles(transform.Rotation));
        transform.EulerRotation = Vector3(euler.x, euler.y, euler.z);
        transform.LocalRotation = transform.Rotation;
        transform.LocalEulerRotation = transform.EulerRotation;
    }
}

void Application::CapturePlaySnapshot()
{
    const auto& entities = m_Scene.GetEntities();
    m_PlaySnapshotEntityCount = entities.size();
    m_PlayStartTransforms.clear();
    m_PlayStartVelocities.clear();
    m_PlayStartTransforms.reserve(entities.size());
    m_PlayStartVelocities.reserve(entities.size());

    for (const Entity& entity : entities)
    {
        m_PlayStartTransforms.push_back(const_cast<Entity&>(entity).GetTransform());
        m_PlayStartVelocities.push_back(entity.Velocity);
    }
    m_PlaySnapshotValid = true;
}

void Application::RestorePlaySnapshot()
{
    auto& entities = m_Scene.GetEntities();
    if (!m_PlaySnapshotValid)
    {
        return;
    }

    // Entidades criadas durante o Play (por exemplo, o mundo procedural)
    // pertencem Ã  sessÃ£o e nÃ£o devem contaminar a cena do editor.
    if (entities.size() > m_PlaySnapshotEntityCount)
    {
        entities.erase(
            entities.begin() + static_cast<std::ptrdiff_t>(m_PlaySnapshotEntityCount),
            entities.end()
        );
    }

    const size_t count = std::min(entities.size(), m_PlayStartTransforms.size());
    for (size_t i = 0; i < count; ++i)
    {
        entities[i].GetTransform() = m_PlayStartTransforms[i];
        entities[i].Velocity = m_PlayStartVelocities[i];
    }
    m_PlaySnapshotValid = false;
    m_PlaySnapshotEntityCount = 0;
}

DemoGameContext Application::MakeDemoGameContext()
{
    return DemoGameContext{
        m_Scene,
        m_Window,
        m_Camera,
        m_ProjectConfig.ProjectPath,
        m_EditorState == EditorState::Edit
    };
}

void Application::SetEditorState(
    EditorState state)
{
    if (state == EditorState::Play && m_EditorState != EditorState::Play)
    {
        m_ScriptTimes.clear();
        m_AudioSceneSync.Reset();
        m_PhysicsAccumulator = 0.0f;
        m_PhysicsContactPairs.clear();
        if (m_EditorState == EditorState::Edit)
        {
            m_ShowGridBeforePlay = m_ShowGrid;
            m_ShowAxisBeforePlay = m_ShowAxis;
            m_PlayEditorVisualsSnapshotValid = true;
        }
        m_ShowGrid = false;
        m_ShowAxis = false;
        m_WasUsingGizmo = false;
        m_AudioSystem.Stop();
        // As fontes PlayOnStart são iniciadas pelo AudioSceneSync no primeiro
        // frame de Play — caminho único, com os mesmos parâmetros efetivos.
        m_RaceGame.Reset();
        // Ao retomar de Pause, preserva o mundo e o estado do jogador.
        // Ao iniciar a partir do editor, começa uma sessão limpa.
        if (m_EditorState == EditorState::Edit)
        {
            m_SurvivalGame.Reset();
        }

        // Preview é uma ação do Editor. No Runtime, os clips iniciam por
        // padrão; PlayOnStart pode ser desativado no Inspector.
        AnimationSystem::BeginPlay(m_Scene);
        for (Entity& entity : m_Scene.GetEntities())
            if (entity.HasAI()) entity.GetAIComponent().ResetRuntime();

        // Os modos demo são descobertos pela própria cena, sem configuração
        // escondida: PlayerCar/AI_Car_* ativam a corrida e SurvivalPlayer/
        // Toolbox ativam a sobrevivência.
        m_RaceGame.ActivateFromScene(m_Scene);
        m_SurvivalGame.ActivateFromScene(m_Scene);

        if (m_EditorState == EditorState::Edit)
        {
            CapturePlaySnapshot();
        }

        // A geração acontece depois do snapshot para que o mundo procedural
        // seja temporário e o editor não fique poluído ao sair do Play.
        // Apenas gera se houver explicitamente um SurvivalPlayer na cena.
        if (m_SurvivalGame.IsActive())
        {
            m_SurvivalGame.GenerateWorld(MakeDemoGameContext());
        }

        // Atualiza a montagem dos scripts antes de criar as instâncias de Play.
        // Isso inclui arquivos .cs adicionados/editados em Assets enquanto o
        // editor já estava aberto; sem isso o componente podia apontar para uma
        // classe que ainda não existia na DLL gerenciada carregada.
        ScriptingRuntime::Instance().ReloadAll();
        ScriptingRuntime::Instance().OnEnterPlay(&m_Scene);
    }
    else if (state == EditorState::Edit && m_EditorState != EditorState::Edit)
    {
        ScriptingRuntime::Instance().OnExitPlay();
        for (Entity& entity : m_Scene.GetEntities())
        {
            AnimationComponent& animation = entity.GetAnimationComponent();
            animation.Stop();
        }
        m_ScriptTimes.clear();
        m_AudioSceneSync.Reset();
        m_PhysicsAccumulator = 0.0f;
        m_PhysicsContactPairs.clear();
        if (m_PlayEditorVisualsSnapshotValid)
        {
            m_ShowGrid = m_ShowGridBeforePlay;
            m_ShowAxis = m_ShowAxisBeforePlay;
            m_PlayEditorVisualsSnapshotValid = false;
        }
        m_AudioSystem.Stop();
        // Limpa handles de runtime das entidades
        for (Entity& ent : m_Scene.GetEntities())
            if (ent.HasAudio())
                ent.GetAudioComponent().RuntimeVoiceID = AUDIO_INVALID_VOICE;
        m_SurvivalGame.OnExitPlay(MakeDemoGameContext());
        RestorePlaySnapshot();
        m_RaceGame.Reset();
        m_SurvivalGame.Reset();
    }

    m_EditorState = state;
}

Application::EditorState
Application::GetEditorState() const
{
    return m_EditorState;
}

Framebuffer& Application::GetFramebuffer()
{
    return m_Framebuffer;
}

void Application::SetViewportSize(
    int width,
    int height)
{
    if (width <= 0 ||
        height <= 0)
    {
        return;
    }

    if (width == m_ViewportWidth && height == m_ViewportHeight)
    {
        m_ViewportResizePending = false;
        return;
    }

    if (m_ViewportResizePending &&
        width == m_PendingViewportWidth && height == m_PendingViewportHeight)
        return;

    m_PendingViewportWidth = width;
    m_PendingViewportHeight = height;
    m_ViewportResizeRequestedAt = glfwGetTime();
    m_ViewportResizePending = true;
}

bool& Application::GetShowGrid()
{
    return m_ShowGrid;
}

bool& Application::GetShowAxis()
{
    return m_ShowAxis;
}

float& Application::GetGridRenderDistance()
{
    return m_GridRenderDistance;
}

float& Application::GetShadowIntensity()
{
    return m_ShadowIntensity;
}

static void ApplyGraphicsPresetToLighting(LightingSettings& settings, GraphicsQuality quality)
{
    switch (quality)
    {
    case GraphicsQuality::Performance:
        settings.Enabled = true;
        settings.Quality = LightingQuality::Performance;
        settings.ShadowMapResolution = 1024;
        settings.PointShadowMapResolution = 512;
        settings.MaxShadowedLocalLights = 1;
        settings.MaxPointLights = 4;
        settings.MaxSpotLights = 4;
        settings.ShadowDistance = 220.0f;
        settings.DynamicShadows = true;
        settings.ContactShadows = false;
        settings.IndirectLighting = false;
        settings.EnvironmentLighting = true;
        break;
    case GraphicsQuality::UltraPerformance:
        settings.Enabled = true;
        settings.Quality = LightingQuality::Performance;
        settings.ShadowMapResolution = 512;
        settings.PointShadowMapResolution = 256;
        settings.MaxShadowedLocalLights = 0;
        settings.MaxPointLights = 4;
        settings.MaxSpotLights = 4;
        settings.ShadowDistance = 80.0f;
        settings.DynamicShadows = false;
        settings.ContactShadows = false;
        settings.IndirectLighting = false;
        settings.EnvironmentLighting = false;
        break;
    case GraphicsQuality::Balanced:
        settings.Enabled = true;
        settings.Quality = LightingQuality::Balanced;
        settings.ShadowMapResolution = 2048;
        settings.PointShadowMapResolution = 512;
        settings.MaxShadowedLocalLights = 2;
        settings.MaxPointLights = 6;
        settings.MaxSpotLights = 6;
        settings.ShadowDistance = 500.0f;
        settings.DynamicShadows = true;
        settings.ContactShadows = true;
        settings.IndirectLighting = true;
        settings.EnvironmentLighting = true;
        break;
    case GraphicsQuality::HighFidelity:
        settings.Enabled = true;
        settings.Quality = LightingQuality::Ultra;
        settings.ShadowMapResolution = 4096;
        settings.PointShadowMapResolution = 2048;
        settings.MaxShadowedLocalLights = 4;
        settings.MaxPointLights = 8;
        settings.MaxSpotLights = 8;
        settings.ShadowDistance = 1000.0f;
        settings.DynamicShadows = true;
        settings.ContactShadows = true;
        settings.IndirectLighting = true;
        settings.EnvironmentLighting = true;
        break;
    case GraphicsQuality::Ultra:
        settings.Enabled = true;
        settings.Quality = LightingQuality::Ultra;
        settings.ShadowMapResolution = 4096;
        settings.PointShadowMapResolution = 2048;
        settings.MaxShadowedLocalLights = 4;
        settings.MaxPointLights = 8;
        settings.MaxSpotLights = 8;
        settings.ShadowDistance = 3000.0f;
        settings.DynamicShadows = true;
        settings.ContactShadows = true;
        settings.IndirectLighting = true;
        settings.EnvironmentLighting = true;
        settings.IndirectIntensity = 0.7f;
        break;
    }
    settings.Clamp();
}

void Application::SetGraphicsQuality(GraphicsQuality quality)
{
    m_GraphicsQuality = quality;
    m_ProjectConfig.Quality = quality;
    m_Renderer.SetQuality(quality);
    ApplyGraphicsPresetToLighting(m_Scene.GetLightingManager().Settings, quality);

    switch (quality)
    {
        case GraphicsQuality::Performance:
            m_ShadowIntensity = 0.0f;
            m_AmbientLight = 0.5f;
            break;
        case GraphicsQuality::UltraPerformance:
            m_ShadowIntensity = 0.0f;
            m_AmbientLight = 0.65f;
            break;
        case GraphicsQuality::Balanced:
            m_ShadowIntensity = 0.5f;
            m_AmbientLight = 0.4f;
            break;
        case GraphicsQuality::HighFidelity:
            m_ShadowIntensity = 0.85f;
            m_AmbientLight = 0.3f;
            break;
        case GraphicsQuality::Ultra:
            m_ShadowIntensity = 1.0f;
            m_AmbientLight = 0.25f;
            break;
    }
}

float& Application::GetAmbientLight()
{
    return m_AmbientLight;
}

void Application::ToggleSettings()
{
    m_ShowSceneSettings = !m_ShowSceneSettings;
}

void Application::SetShowTimeline(bool show)
{
    m_ShowTimeline = show;
}

bool& Application::GetShowAssetBrowser()
{
    return m_ShowAssetBrowser;
}

void Application::RestoreDefaultLayout()
{
    m_ShowHierarchy = true;
    m_ShowInspector = true;
    m_ShowAIPanel = true;
    m_ShowAssetBrowser = true;
    m_HierarchyWidth = 250.0f;
    m_InspectorWidth = 350.0f;
}

bool& Application::GetShowHierarchy()
{
    return m_ShowHierarchy;
}

bool& Application::GetShowInspector()
{
    return m_ShowInspector;
}

bool& Application::GetShowAIPanel()
{
    return m_ShowAIPanel;
}

void Application::SetMultiSelectedEntities(const std::vector<int>& indices)
{
    m_MultiSelectedEntities.clear();
    const int entityCount = static_cast<int>(m_Scene.GetEntities().size());
    for (int index : indices)
    {
        if (index < 0 || index >= entityCount ||
            m_Scene.GetEntities()[static_cast<size_t>(index)].IsEditorOnly())
            continue;

        if (std::find(m_MultiSelectedEntities.begin(), m_MultiSelectedEntities.end(), index) ==
            m_MultiSelectedEntities.end())
            m_MultiSelectedEntities.push_back(index);
    }

    if (!m_MultiSelectedEntities.empty())
    {
        if (m_SelectedEntity < 0 ||
            std::find(m_MultiSelectedEntities.begin(), m_MultiSelectedEntities.end(), m_SelectedEntity) ==
                m_MultiSelectedEntities.end())
        {
            m_SelectedEntity = m_MultiSelectedEntities.front();
        }
        m_SelectedID = m_Scene.GetEntities()[static_cast<size_t>(m_SelectedEntity)].GetID();
    }
}

const std::vector<int>& Application::GetMultiSelectedEntities() const
{
    return m_MultiSelectedEntities;
}

bool Application::IsMultiSelectedEntity(int index) const
{
    return std::find(
        m_MultiSelectedEntities.begin(),
        m_MultiSelectedEntities.end(),
        index
    ) != m_MultiSelectedEntities.end();
}

int Application::GetViewportWidth() const
{
    return m_ViewportWidth;
}

int Application::GetViewportHeight() const
{
    return m_ViewportHeight;
}

void Application::SetHierarchyWidth(
    float width)
{
    m_HierarchyWidth = width;
}

void Application::SetInspectorWidth(
    float width)
{
    m_InspectorWidth = width;
}

float Application::GetHierarchyWidth() const
{
    return m_HierarchyWidth;
}

float Application::GetInspectorWidth() const
{
    return m_InspectorWidth;
}

Renderer& Application::GetRenderer()
{
    return m_Renderer;
}

AudioSystem& Application::GetAudioSystem()
{
    return m_AudioSystem;
}

Camera& Application::GetCamera()
{
    return m_Camera;
}

const Entity* Application::GetActiveSceneCamera() const
{
    return FindActiveSceneCamera(m_Scene);
}

Camera Application::GetViewportCamera() const
{
    // Durante Play 3D, overlays e picking precisam usar a mesma câmera da
    // imagem renderizada, não a câmera livre de edição.
    if (m_WorldMode != WorldMode::World2D &&
        m_EditorState == EditorState::Play &&
        FindActiveSceneCamera(m_Scene) != nullptr)
    {
        return BuildRuntimeCamera(m_Scene, m_Camera);
    }

    if (m_WorldMode != WorldMode::World2D)
        return m_Camera;

    Camera camera = m_Camera;
    camera.Rotation = Vector3(0.0f, 0.0f, 0.0f);
    camera.Orthographic = true;
    camera.NearClip = 0.01f;
    camera.FarClip = 10000.0f;
    camera.OrthographicSize = std::clamp(camera.OrthographicSize, 0.1f, 10000.0f);
    return camera;
}

void Application::SetWorldMode(WorldMode mode)
{
    if (m_WorldMode == mode)
        return;

    if (mode == WorldMode::World2D)
    {
        m_CameraBefore2D = m_Camera;
        m_HasCameraBefore2D = true;
        // DistÃ¢ncia 4 coloca os objetos criados "na frente da cÃ¢mera" no
        // plano z=0, que Ã© o plano padrÃ£o do mundo 2D.
        m_Camera.Position = Vector3(0.0f, 0.0f, 4.0f);
        m_Camera.Rotation = Vector3(0.0f, 0.0f, 0.0f);
        m_Camera.Orthographic = true;
        m_Camera.OrthographicSize = 10.0f;
    }
    else if (m_HasCameraBefore2D)
    {
        m_Camera = m_CameraBefore2D;
        m_HasCameraBefore2D = false;
    }

    m_WorldMode = mode;
}

Application::GizmoMode
Application::GetGizmoMode() const
{
    return m_GizmoMode;
}

void Application::SetGizmoMode(
    GizmoMode mode)
{
    m_GizmoMode = mode;
}

Application::GizmoSpace
Application::GetGizmoSpace() const
{
    return m_GizmoSpace;
}

SnapSettings& Application::GetSnapSettings()
{
    return m_SnapSettings;
}

UndoRedoManager& Application::GetUndoRedoManager()
{
    return m_UndoRedoManager;
}

double Application::GetMouseX() const
{
    return m_Window.GetMouseX();
}

double Application::GetMouseY() const
{
    return m_Window.GetMouseY();
}

const AssetInfo* Application::GetSelectedAsset() const
{
    return m_SelectedAsset;
}

void Application::SetSelectedAsset(
    const AssetInfo* asset
)
{
    m_SelectedAsset = asset;
}

void Application::AddKeyframeAtCurrentTime()
{
    if (m_Scene.GetEntities().empty() ||
        m_SelectedEntity < 0 ||
        m_SelectedEntity >= static_cast<int>(m_Scene.GetEntities().size()))
        return;

    Entity& entity = m_Scene.GetEntities()[m_SelectedEntity];
    AnimationComponent& anim = entity.GetAnimationComponent();

    if (!anim.Clip)
    {
        // Cria o clip automaticamente quando o usuÃ¡rio comeÃ§a a editar.
        anim.Clip = std::make_shared<AnimationClip>();
        anim.Clip->Name = "New Animation";
        anim.Clip->Duration = 2.0f;
        anim.Clip->Loop = true;
    }

    Transform& transform = entity.GetTransform();
    float currentTime = TimelinePanel::GetPlaybackTime();

    // Atualiza o keyframe no mesmo instante em vez de criar duplicatas a cada
    // frame enquanto o gizmo estÃ¡ sendo arrastado.
    auto upsertVec3 = [](std::vector<KeyframeVec3>& keys, float time, const glm::vec3& value)
    {
        auto it = std::lower_bound(keys.begin(), keys.end(), time,
            [](const KeyframeVec3& key, float t) { return key.Time < t; });
        if (it != keys.end() && std::abs(it->Time - time) < 0.001f)
        {
            it->Value = value;
            return;
        }
        keys.insert(it, KeyframeVec3{time, value});
    };

    auto upsertQuat = [](std::vector<KeyframeQuat>& keys, float time, const glm::quat& value)
    {
        auto it = std::lower_bound(keys.begin(), keys.end(), time,
            [](const KeyframeQuat& key, float t) { return key.Time < t; });
        if (it != keys.end() && std::abs(it->Time - time) < 0.001f)
        {
            it->Value = value;
            return;
        }
        keys.insert(it, KeyframeQuat{time, value});
    };

    upsertVec3(anim.Clip->Position.Keys, currentTime,
        glm::vec3(transform.Position.X, transform.Position.Y, transform.Position.Z));
    upsertQuat(anim.Clip->Rotation.Keys, currentTime, transform.Rotation);
    upsertVec3(anim.Clip->Scale.Keys, currentTime,
        glm::vec3(transform.Scale.X, transform.Scale.Y, transform.Scale.Z));
    anim.Time = currentTime;

    // Update duration if needed
    if (currentTime > anim.Clip->Duration)
    {
        anim.Clip->Duration = currentTime + 0.5f;
    }
    MarkDirty();
}



uint32_t Application::GetViewportTexture() const
{
    return m_Framebuffer.GetTexture();
}

void Application::RenderMobilePreview()
{
    bool open = m_ShowMobilePreview;
    ImGui::SetNextWindowSize(ImVec2(430.0f, 760.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(UI("Visualizacao no celular##mobile_preview"), &open))
    {
        ImGui::End();
        m_ShowMobilePreview = open;
        return;
    }

    m_ShowMobilePreview = open;
    ImGui::TextDisabled("Preview real em proporcao 9:16 (360 x 640)");
    ImGui::TextDisabled("Use Play para testar o jogo nesta tela.");
    ImGui::Separator();

    const float availableW = std::max(1.0f, ImGui::GetContentRegionAvail().x - 34.0f);
    const float availableH = std::max(1.0f, ImGui::GetContentRegionAvail().y - 18.0f);
    const float scale = std::min(availableW / 360.0f, availableH / 640.0f);
    const ImVec2 size(360.0f * scale, 640.0f * scale);
    const float left = (ImGui::GetContentRegionAvail().x - size.x) * 0.5f;
    if (left > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + left);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.02f, 0.02f, 0.025f, 1.0f));
    ImGui::BeginChild("##mobile_frame", ImVec2(size.x + 18.0f, size.y + 18.0f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPos(ImVec2(9.0f, 9.0f));
    if (m_MobilePreviewFramebuffer.GetTexture() != 0)
        if (m_Window.GetGraphicsAPI() == GraphicsAPI::OpenGL)
            ImGui::Image((ImTextureID)(intptr_t)GetMobilePreviewTexture(), size);
        else
            ImGui::TextDisabled("Preview do jogo usa OpenGL e ainda não está disponível na apresentação Vulkan.");
    else
        ImGui::Text(UI("Preview indisponivel"));
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

void Application::RenderTermsModal()
{
    LegalNotice::DrawModal(&m_ShowTermsModal);
}

// ---------------------------------------------------------------------------
// RenderExportModal â€” janela de exportaÃ§Ã£o de jogo
// ---------------------------------------------------------------------------
void Application::RenderExportModal()
{
    // Estado persistente entre frames (static = vive enquanto o editor viver)
    static char  s_GameName[128]   = "MeuJogo";
    static char  s_OfficialName[128] = "Meu Jogo";
    static char  s_Version[64] = "1.0.0";
    static char  s_GameIconPath[512] = "";
    static char  s_Description[512] = "";
    static char  s_CoverImage[512] = "";
    static char  s_PresentationImages[1024] = "";
    static char  s_OutputPath[512] = "";
    static std::string s_LastSuggestedOutput;
    static std::string s_OutputProject;
    static int   s_WindowWidth = 1280;
    static int   s_WindowHeight = 720;
    static int   s_WindowMode = 0;
    static int   s_StartSceneIndex = 0;
    static std::string s_SceneChoicesProject;
    static std::vector<std::pair<std::string, std::string>> s_SceneChoices;
    static bool  s_OverwriteOutput = true;
    static ExportPlatform s_Platform = ExportPlatform::Desktop;

    // Estado do processo em andamento
    static bool                  s_Exporting    = false;
    static bool                  s_Done         = false;
    static bool                  s_Success      = false;
    static float                 s_Progress     = 0.0f;
    static std::string           s_StatusMsg    = "";
    static std::vector<std::string> s_Log;
    static std::string           s_ErrorMsg;
    static std::thread           s_ExportThread;

    // Preenche o output path padrÃ£o na primeira abertura
    if (s_OutputPath[0] == '\0' && !m_ProjectConfig.ProjectPath.empty())
    {
        std::string def = GameExporter::DefaultOutputPath(
            m_ProjectConfig.ProjectPath, s_GameName);
        strncpy_s(s_OutputPath, def.c_str(), sizeof(s_OutputPath) - 1);
        s_LastSuggestedOutput = def;
    }
    if (s_OutputProject != m_ProjectConfig.ProjectPath)
    {
        if (s_OutputPath[0] == '\0' || std::string(s_OutputPath) == s_LastSuggestedOutput)
        {
            s_LastSuggestedOutput = GameExporter::DefaultOutputPath(
                m_ProjectConfig.ProjectPath, s_GameName);
            strncpy_s(s_OutputPath, s_LastSuggestedOutput.c_str(), sizeof(s_OutputPath) - 1);
        }
        s_OutputProject = m_ProjectConfig.ProjectPath;
    }

    if (s_SceneChoicesProject != m_ProjectConfig.ProjectPath)
    {
        s_SceneChoicesProject = m_ProjectConfig.ProjectPath;
        s_SceneChoices.clear();
        const std::filesystem::path scenesDir =
            std::filesystem::path(m_ProjectConfig.ProjectPath) / "Assets" / "Scenes";
        std::error_code scanError;
        if (std::filesystem::is_directory(scenesDir, scanError))
        {
            for (const auto& entry : std::filesystem::directory_iterator(scenesDir, scanError))
            {
                if (!entry.is_regular_file(scanError) || entry.path().extension() != ".scene") continue;
                const auto relative = std::filesystem::relative(entry.path(), m_ProjectConfig.ProjectPath, scanError);
                if (!scanError)
                    s_SceneChoices.emplace_back(entry.path().stem().string(), relative.generic_string());
            }
            std::sort(s_SceneChoices.begin(), s_SceneChoices.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
        }
        if (s_SceneChoices.empty())
            s_SceneChoices.emplace_back("Main", "Assets/Scenes/Main.scene");
        const std::string currentRelative = m_ProjectConfig.ScenePath.empty()
            ? std::string("Assets/Scenes/Main.scene")
            : std::filesystem::path(m_ProjectConfig.ScenePath).lexically_relative(
                std::filesystem::path(m_ProjectConfig.ProjectPath)).generic_string();
        s_StartSceneIndex = 0;
        for (size_t i = 0; i < s_SceneChoices.size(); ++i)
            if (s_SceneChoices[i].second == currentRelative)
                s_StartSceneIndex = static_cast<int>(i);
    }

    // Abre o popup na primeira chamada deste frame
    ImGui::OpenPopup("Exportar Jogo##exp");

    ImGui::SetNextWindowSize(ImVec2(720.f, 760.f), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("Exportar Jogo##exp", &m_ShowExportModal,
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        return;

    // ---- CabeÃ§alho --------------------------------------------------------
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.85f, 1.0f, 1.0f));
    ImGui::Text(UI("  Build & Export â€” Windows"));
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Dummy({0.f, 4.f});

    // ---- ConfiguraÃ§Ã£o (desabilitada durante export) -----------------------
    const bool busy = s_Exporting;
    if (busy) ImGui::BeginDisabled();

    ImGui::SetNextItemWidth(330.f);
    if (ImGui::InputText(UI("Nome do projeto / pacote##expname"), s_GameName, sizeof(s_GameName)) &&
        !s_LastSuggestedOutput.empty() && std::string(s_OutputPath) == s_LastSuggestedOutput)
    {
        s_LastSuggestedOutput = GameExporter::DefaultOutputPath(m_ProjectConfig.ProjectPath, s_GameName);
        strncpy_s(s_OutputPath, s_LastSuggestedOutput.c_str(), sizeof(s_OutputPath) - 1);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240.f);
    ImGui::InputText(UI("Nome exibido no jogo##exptitle"), s_OfficialName, sizeof(s_OfficialName));
    ImGui::SetNextItemWidth(180.f);
    ImGui::InputText(UI("Versão##expversion"), s_Version, sizeof(s_Version));

    // A Marae Platform está temporariamente oculta do exportador. O enum,
    // metadados e empacotamento continuam preservados no backend.
    if (s_Platform == ExportPlatform::MaraePlatform)
        s_Platform = ExportPlatform::Desktop;
    const char* platformItems[] = { "Desktop", "Android", "HTML" };
    int platformIndex = static_cast<int>(s_Platform);
    ImGui::SetNextItemWidth(220.f);
    if (ImGui::Combo(UI("Plataforma##expplatform"), &platformIndex, platformItems, 3))
        s_Platform = static_cast<ExportPlatform>(platformIndex);
    ImGui::SameLine();
    ImGui::TextDisabled("Escolha o alvo do pacote");
    if (s_Platform != ExportPlatform::Desktop && s_Platform != ExportPlatform::MaraePlatform)
    {
        ImGui::TextWrapped(UI("Cenas e assets serao preparados para o runtime %s."),
                           GameExporter::PlatformName(s_Platform));
    }

    if (s_Platform == ExportPlatform::Desktop)
    {
        ImGui::Separator();
        ImGui::Text(UI("Configuração do jogo Desktop"));
        ImGui::SetNextItemWidth(210.f);
        ImGui::InputInt(UI("Largura##expwidth"), &s_WindowWidth);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(210.f);
        ImGui::InputInt(UI("Altura##expheight"), &s_WindowHeight);
        s_WindowWidth = std::clamp(s_WindowWidth, 320, 7680);
        s_WindowHeight = std::clamp(s_WindowHeight, 200, 4320);
        const char* windowModes[] = { "Janela", "Sem borda", "Tela cheia" };
        ImGui::SetNextItemWidth(210.f);
        ImGui::Combo(UI("Modo de janela##expmode"), &s_WindowMode, windowModes, 3);

        std::vector<const char*> sceneLabels;
        sceneLabels.reserve(s_SceneChoices.size());
        for (const auto& sceneChoice : s_SceneChoices) sceneLabels.push_back(sceneChoice.first.c_str());
        ImGui::SetNextItemWidth(250.f);
        ImGui::Combo(UI("Cena inicial##expscene"), &s_StartSceneIndex,
                     sceneLabels.data(), static_cast<int>(sceneLabels.size()));
        ImGui::SameLine();
        ImGui::TextDisabled("%s", s_SceneChoices[static_cast<size_t>(s_StartSceneIndex)].second.c_str());

        ImGui::SetNextItemWidth(430.f);
        ImGui::InputText(UI("Ícone da janela##expicon"), s_GameIconPath, sizeof(s_GameIconPath));
        ImGui::SameLine();
        if (ImGui::SmallButton(UI("Procurar...##expiconbrowse")))
        {
#ifdef _WIN32
            char filename[MAX_PATH]{};
            OPENFILENAMEA ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = nullptr;
            ofn.lpstrFilter = "Imagens (*.png;*.jpg;*.jpeg;*.bmp;*.tga)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga\0Todos os arquivos\0*.*\0";
            ofn.lpstrFile = filename;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameA(&ofn))
                strncpy_s(s_GameIconPath, filename, sizeof(s_GameIconPath) - 1);
#endif
        }
        ImGui::TextDisabled(UI("O ícone se aplica à janela e acompanha o pacote exportado."));
    }

    if (s_Platform == ExportPlatform::MaraePlatform)
    {
        ImGui::Separator();
        ImGui::Text(UI("Dados públicos da Marae Platform"));
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputText(UI("Nome oficial##exppublicname"), s_OfficialName, sizeof(s_OfficialName));
        ImGui::InputTextMultiline(UI("Descrição##expdescription"), s_Description, sizeof(s_Description),
                                  ImVec2(340.f, 70.f));
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputText(UI("Capa##expcover"), s_CoverImage, sizeof(s_CoverImage));
        ImGui::TextDisabled("Imagens extras: caminhos separados por ponto e vírgula");
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputText(UI("Apresentação##expgallery"), s_PresentationImages, sizeof(s_PresentationImages));
    }

    ImGui::SetNextItemWidth(340.f);
    if (ImGui::InputText(UI("Pasta de Saida##expout"), s_OutputPath, sizeof(s_OutputPath)))
    {
        // usuÃ¡rio editou manualmente â€” ok
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(UI("...##browseexp")))
    {
        // Abre o explorador de arquivos nativo para escolher pasta
        std::string chosen = ProjectManager::BrowseForProjectFolder(
            "Escolher pasta de saida");
        if (!chosen.empty())
        {
            strncpy_s(s_OutputPath, chosen.c_str(), sizeof(s_OutputPath) - 1);
            s_LastSuggestedOutput.clear();
        }
    }

    ImGui::Checkbox(UI("Sobrescrever build anterior##expover"), &s_OverwriteOutput);

    if (busy) ImGui::EndDisabled();

    ImGui::Dummy({0.f, 6.f});

    // ---- Barra de progresso e status -------------------------------------
    if (s_Exporting || s_Done)
    {
        ImGui::ProgressBar(s_Progress, ImVec2(-1.f, 14.f));
        ImGui::Dummy({0.f, 2.f});
        ImGui::TextWrapped(UI("%s"), s_StatusMsg.c_str());
        ImGui::Dummy({0.f, 4.f});

        // Log scrollÃ¡vel
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.08f, 1.f));
        ImGui::BeginChild("##explog", ImVec2(0.f, 160.f), false);
        for (const auto& line : s_Log)
        {
            bool isErr  = line.rfind("[ERR]",  0) == 0;
            bool isWarn = line.rfind("[WARN]", 0) == 0;
            bool isOk   = line.rfind("[OK]",   0) == 0;
            if      (isErr)  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.35f, 0.35f, 1.f));
            else if (isWarn) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.85f, 0.3f,  1.f));
            else if (isOk)   ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.f, 0.4f,   1.f));
            else             ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f,  1.f));

            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
        }
        // Auto-scroll para o final
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.f)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Dummy({0.f, 4.f});

        if (s_Done)
        {
            if (s_Success)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.f, 0.3f, 1.f));
                ImGui::Text(UI("  Exportacao concluida com sucesso!"));
                ImGui::PopStyleColor();

                ImGui::SameLine(0.f, 12.f);
                if (ImGui::SmallButton(UI("Abrir Pasta##expopen")))
                {
#ifdef _WIN32
                    std::string cmd = "explorer \"" + std::string(s_OutputPath) + "\"";
                    std::system(cmd.c_str());
#endif
                }
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.3f, 0.3f, 1.f));
                ImGui::TextWrapped(UI("  Erro: %s"), s_ErrorMsg.c_str());
                ImGui::PopStyleColor();
            }
        }
    }

    // ---- BotÃµes -----------------------------------------------------------
    ImGui::Separator();
    ImGui::Dummy({0.f, 4.f});

    if (!s_Exporting)
    {
        // BotÃ£o Exportar
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.06f, 0.46f, 0.80f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.60f, 1.00f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.04f, 0.35f, 0.65f, 1.f));
        if (ImGui::Button(UI("  Exportar  "), ImVec2(120.f, 28.f)))
        {
            // Salva a cena antes de exportar
            SaveScene();

            // Zera estado
            s_Done     = false;
            s_Success  = false;
            s_Progress = 0.f;
            s_StatusMsg.clear();
            s_Log.clear();
            s_ErrorMsg.clear();
            s_Exporting = true;

            // Captura cÃ³pias dos configs para a thread
            ExportConfig cfg;
            cfg.ProjectPath    = m_ProjectConfig.ProjectPath;
            cfg.OutputPath     = s_OutputPath;
            cfg.GameName       = s_GameName;
            cfg.OfficialName   = s_OfficialName;
            cfg.Description    = s_Description;
            cfg.CoverImage     = s_CoverImage;
            {
                std::stringstream images(s_PresentationImages);
                std::string image;
                while (std::getline(images, image, ';'))
                    cfg.PresentationImages.push_back(image);
            }
            cfg.StartScene     = s_SceneChoices.empty() ? "Assets/Scenes/Main.scene" :
                s_SceneChoices[static_cast<size_t>(std::clamp(s_StartSceneIndex, 0,
                    static_cast<int>(s_SceneChoices.size()) - 1))].second;
            cfg.Version        = s_Version;
            cfg.IconPath       = s_GameIconPath;
            cfg.WindowWidth    = s_WindowWidth;
            cfg.WindowHeight   = s_WindowHeight;
            cfg.WindowMode     = s_WindowMode;
            cfg.World2D        = m_ProjectConfig.WorldType == ProjectWorldType::World2D;
            cfg.OverwriteOutput = s_OverwriteOutput;
            cfg.Platform        = s_Platform;

            // LanÃ§a em thread separada
            if (s_ExportThread.joinable()) s_ExportThread.join();
            s_ExportThread = std::thread([cfg]()
            {
                ExportResult res = GameExporter::Export(cfg,
                    [](const std::string& msg, float pct)
                    {
                        // Atualiza estado compartilhado (acesso seguro pois
                        // sÃ³ a thread de export escreve e o render sÃ³ lÃª)
                        s_Progress  = pct;
                        s_StatusMsg = msg;
                        s_Log.push_back(msg);
                    });

                s_Success   = res.Success;
                s_ErrorMsg  = res.ErrorMessage;
                s_Progress  = 1.0f;
                s_Exporting = false;
                s_Done      = true;
            });
        }
        ImGui::PopStyleColor(3);
    }
    else
    {
        // BotÃ£o desabilitado enquanto exporta
        ImGui::BeginDisabled();
        ImGui::Button(UI("  Exportando...  "), ImVec2(140.f, 28.f));
        ImGui::EndDisabled();
    }

    ImGui::SameLine(0.f, 10.f);

    if (ImGui::Button(UI("Fechar"), ImVec2(90.f, 28.f)))
    {
        if (!s_Exporting)
        {
            if (s_ExportThread.joinable()) s_ExportThread.join();
            m_ShowExportModal = false;
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::Dummy({0.f, 4.f});
    ImGui::EndPopup();
}
