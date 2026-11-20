#include "Core/Application.h"
#include "Hub/ProjectHub.h"
#include "Platform/LocalPlatform.h"
#include "Editor/Exporter/GameExporter.h"
#include "Engine/Scripting/ScriptingRuntime.h"
#include "Engine/Scripting/ScriptDebugger.h"
#include "Engine/Input/InputMap.h"
#include "Scene/Prefab/Prefab.h"
#include "Core/Diagnostics/DiagnosticLog.h"
#include "Core/Diagnostics/PerformanceMonitor.h"
#include "Assets/AssetDatabase/AssetDatabase.h"
#include "Engine/Navigation/NavigationSystem.h"
#include "Engine/AI/AISystem.h"
#include "Renderer/VulkanRuntimeProbe.h"
#include "Renderer/VulkanSmokeWindow.h"
#include "Renderer/SceneRenderer/SceneRenderer.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#define VK_NO_PROTOTYPES
#include <backends/imgui_impl_vulkan.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cctype>
#include <iterator>
#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(
    void* module, wchar_t* filename, unsigned long size);
#endif

static std::ofstream g_PackagedRuntimeLog;

static bool ConfigurePackagedGame(ProjectConfig& config, std::string& error)
{
#ifdef _WIN32
    wchar_t modulePath[32768]{};
    const unsigned long length = GetModuleFileNameW(nullptr, modulePath,
                                             static_cast<unsigned long>(std::size(modulePath)));
    if (length == 0 || length >= std::size(modulePath)) return false;
    const std::filesystem::path executable(modulePath);
#else
    const std::filesystem::path executable = std::filesystem::absolute("MaraeGame.exe");
#endif
    std::string executableName = executable.filename().string();
    std::transform(executableName.begin(), executableName.end(), executableName.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (executableName != "maraegame.exe") return false;

    const std::filesystem::path gameRoot = executable.parent_path();
    config.ProjectPath = gameRoot.string();
    std::ifstream manifest(gameRoot / "MaraeExport.manifest", std::ios::binary);
    if (!manifest)
    {
        error = "MaraeExport.manifest não foi encontrado ao lado do executável.";
        return true;
    }

    std::string gameName = "MaraeGame";
    std::string officialName;
    std::string version = "1.0.0";
    std::string startScene;
    std::string world = "3d";
    std::string icon;
    std::string windowMode = "windowed";
    int width = 1280;
    int height = 720;
    std::string line;
    while (std::getline(manifest, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF)
            line.erase(0, 3);
        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "name") gameName = value;
        else if (key == "official_name") officialName = value;
        else if (key == "version") version = value;
        else if (key == "width") { try { width = std::stoi(value); } catch (...) {} }
        else if (key == "height") { try { height = std::stoi(value); } catch (...) {} }
        else if (key == "window_mode") windowMode = value;
        else if (key == "icon") icon = value;
        else if (key == "start_scene") startScene = value;
        else if (key == "world") world = value;
    }

    if (startScene.empty())
    {
        error = "O manifesto não define start_scene.";
        return true;
    }
    const std::filesystem::path relativeScene(startScene);
    if (relativeScene.is_absolute() ||
        std::any_of(relativeScene.begin(), relativeScene.end(),
            [](const std::filesystem::path& part) { return part == ".."; }))
    {
        error = "O caminho start_scene do manifesto é inválido.";
        return true;
    }
    const std::filesystem::path scene = gameRoot / relativeScene;
    if (!std::filesystem::is_regular_file(scene))
    {
        error = "A cena inicial exportada não foi encontrada: " + scene.string();
        return true;
    }

    const std::filesystem::path relativeIcon(icon);
    if (!icon.empty() && (relativeIcon.is_absolute() ||
        std::any_of(relativeIcon.begin(), relativeIcon.end(),
            [](const std::filesystem::path& part) { return part == ".."; })))
    {
        error = "O caminho do ícone no manifesto é inválido.";
        return true;
    }
    if (!icon.empty() && !std::filesystem::is_regular_file(gameRoot / relativeIcon))
    {
        error = "O ícone exportado não foi encontrado: " + icon;
        return true;
    }

    config.ScenePath = scene.string();
    if (officialName.empty()) officialName = gameName;
    config.GameVersion = version;
    config.GameTitle = officialName + " - v" + version;
    if (!icon.empty()) config.GameIconPath = (gameRoot / relativeIcon).string();
    config.RuntimeWidth = std::clamp(width, 320, 7680);
    config.RuntimeHeight = std::clamp(height, 200, 4320);
    config.RuntimeWindowMode = windowMode == "fullscreen" ? 2 :
        (windowMode == "borderless" ? 1 : 0);
    config.RuntimeGame = true;
    config.PersistEditorSettings = false;
    config.Graphics = GraphicsAPI::OpenGL;
    config.WorldType = world == "2d" ? ProjectWorldType::World2D : ProjectWorldType::World3D;
    return true;
}

static void StartPackagedRuntimeLog(const ProjectConfig& config)
{
    if (!config.RuntimeGame || config.ProjectPath.empty()) return;
    g_PackagedRuntimeLog.open(std::filesystem::path(config.ProjectPath) / "MaraeGame.log",
                              std::ios::binary | std::ios::app);
    if (!g_PackagedRuntimeLog) return;
    g_PackagedRuntimeLog << "\r\n--- Marae runtime start ---\r\n";
    g_PackagedRuntimeLog.flush();
    std::cout.rdbuf(g_PackagedRuntimeLog.rdbuf());
    std::cerr.rdbuf(g_PackagedRuntimeLog.rdbuf());
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
}

static int RunVulkanUIPanelSmokeTest()
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(640, 360, "Marae Vulkan UI smoke test", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "[VulkanTest] Não foi possível criar janela sem API.\n";
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    const bool platformReady = ImGui_ImplGlfw_InitForVulkan(window, true);
    VulkanImGuiRenderer renderer;
    bool rendererReady = platformReady && renderer.Initialize(window);
    bool rendered = rendererReady;
    for (int frame = 0; rendered && frame < 3; ++frame)
    {
        glfwPollEvents();
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::Begin("Painel Hierarquia##VulkanTest");
        ImGui::TextUnformatted("Renderizado pelo backend Vulkan do ImGui.");
        ImGui::Checkbox("Painel interativo", &rendererReady);
        ImGui::End();
        ImGui::Begin("Painel Inspector##VulkanTest");
        ImGui::TextUnformatted("Apresentação real pela swapchain Vulkan.");
        ImGui::End();
        ImGui::Render();
        rendered = renderer.Render(ImGui::GetDrawData());
        if (frame == 0)
            glfwSetWindowSize(window, 800, 450);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    renderer.Shutdown();
    if (platformReady)
        ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    std::cout << (rendered ? "[VulkanTest] Painéis ImGui apresentados em Vulkan.\n"
                           : "[VulkanTest] Falha ao apresentar painéis em Vulkan.\n");
    return rendered ? 0 : 2;
}

static GraphicsAPI LoadPreferredGraphicsAPI()
{
    const std::filesystem::path settingsPath =
        std::filesystem::path(ProjectManager::GetUserDocumentsPath()) /
        "MaraeEngine" / "editor_settings.cfg";
    std::ifstream settings(settingsPath);
    std::string line;
    while (std::getline(settings, line))
    {
        const size_t equals = line.find('=');
        if (equals != std::string::npos && line.substr(0, equals) == "GraphicsAPI")
            return line.substr(equals + 1) == "1" ? GraphicsAPI::Vulkan : GraphicsAPI::OpenGL;
    }
    return GraphicsAPI::OpenGL;
}

int main(int argc, char** argv)
{
    if (argc >= 2 && std::string(argv[1]) == "--vulkan-probe")
        return VulkanRuntimeProbe::RunDiagnostics(std::cout) ? 0 : 2;

    // -----------------------------------------------------------------------
    // Ciclo de vida do GLFW centralizado aqui.
    // Window e ProjectHub NUNCA chamam glfwInit/glfwTerminate.
    // -----------------------------------------------------------------------
    if (!glfwInit())
    {
        std::cerr << "Falha ao inicializar GLFW!\n";
        return 1;
    }

    if (argc >= 2 && std::string(argv[1]) == "--vulkan-smoke")
    {
        const bool presented = VulkanSmokeWindow::PresentOneFrame(std::cout);
        glfwTerminate();
        return presented ? 0 : 3;
    }

    if (argc >= 2 && std::string(argv[1]) == "--vulkan-ui-smoke")
    {
        const int result = RunVulkanUIPanelSmokeTest();
        glfwTerminate();
        return result;
    }

    bool showHub = true;
    bool startInPlay = false;
    ProjectConfig config;
    config.Graphics = LoadPreferredGraphicsAPI();
    std::string packagedGameError;
    const bool packagedGameDetected = ConfigurePackagedGame(config, packagedGameError);
    if (packagedGameDetected)
        StartPackagedRuntimeLog(config);
    if (packagedGameDetected && !packagedGameError.empty())
    {
        std::cerr << "[Runtime] " << packagedGameError << "\n";
        glfwTerminate();
        return 2;
    }
    if (packagedGameDetected)
    {
        showHub = false;
        startInPlay = true;
        std::cout << "[Runtime] Iniciando jogo: " << config.GameTitle << "\n";
    }

    if (argc >= 2 && std::string(argv[1]) == "--vulkan-editor-smoke")
    {
        config.Graphics = GraphicsAPI::Vulkan;
        config.PersistEditorSettings = false;
        showHub = false;
    }

    if (argc >= 2 && std::string(argv[1]) == "--physics-tests")
    {
        int result = 1;
        {
            Application app;
            result = app.RunPhysicsRegressionTests();
        }
        glfwTerminate();
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "--ui-tests")
    {
        int result = 1;
        {
            Application app;
            result = app.RunUIRegressionTests();
        }
        glfwTerminate();
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "--diagnostic-tests")
    {
        const int logResult = MaraeDiagnostics::DiagnosticLog::RunRegressionTests();
        const int frameCaptureResult = PerformanceMonitor::RunFrameCaptureRegressionTests();
        glfwTerminate();
        return (logResult == 0 && frameCaptureResult == 0) ? 0 : 1;
    }

    if (argc >= 2 && std::string(argv[1]) == "--asset-tests")
    {
        const int result = AssetDatabase::RunRegressionTests();
        glfwTerminate();
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "--recovery-tests")
    {
        int result = 1;
        {
            Application app;
            result = app.RunRecoveryRegressionTests();
        }
        glfwTerminate();
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "--scene-tests")
    {
        int result = 1;
        {
            Application app;
            result = app.RunSceneRegressionTests();
        }
        glfwTerminate();
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "--input-tests")
    {
        const int result = MaraeInput::InputMap::RunRegressionTests();
        glfwTerminate();
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "--prefab-tests")
    {
        const int result = Prefab::RunRegressionTests();
        glfwTerminate();
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "--script-debug-tests")
    {
        const int result = MaraeScripting::ScriptDebugger::RunRegressionTests();
        glfwTerminate();
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "--animation-tests")
    {
        int result = 1;
        {
            Application app;
            result = app.RunAnimationRegressionTests();
        }
        glfwTerminate();
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "--ai-tests")
    {
        const int result = AISystem::RunRegressionTests();
        glfwTerminate();
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "--frustum-tests")
    {
        const int result = SceneRenderer::RunFrustumCullingRegressionTests();
        glfwTerminate();
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "--navigation-tests")
    {
        const int result = NavigationSystem::RunRegressionTests() ? 0 : 1;
        glfwTerminate();
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "--material-tests")
    {
        int result = 1;
        {
            Application app;
            result = app.RunMaterialRegressionTests();
        }
        glfwTerminate();
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "--acoustic-tests")
    {
        int result = 1;
        {
            Application app;
            result = app.RunAcousticRegressionTests();
        }
        glfwTerminate();
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "--export-tests")
    {
        const int result = GameExporter::RunRegressionTests();
        glfwTerminate();
        return result;
    }

    if (argc >= 3 && std::string(argv[1]) == "--script-wasd-tests")
    {
        const ProjectInfo project = ProjectManager::DiscoverProject(
            argv[2], GraphicsQuality::Balanced);
        ProjectConfig config;
        config.ProjectPath = project.Path;
        config.ScenePath = project.ScenePath;
        config.Quality = project.Quality;
        config.WorldType = project.WorldType;
        int result = 1;
        {
            Application app(config);
            result = ScriptingRuntime::Instance().RunWASDInputRegressionTests(
                app.GetScene(), "Car");
        }
        glfwTerminate();
        return result;
    }

    if (argc >= 3 && std::string(argv[1]) == "--script-camera-tests")
    {
        const ProjectInfo project = ProjectManager::DiscoverProject(
            argv[2], GraphicsQuality::Balanced);
        ProjectConfig config;
        config.ProjectPath = project.Path;
        config.ScenePath = project.ScenePath;
        config.Quality = project.Quality;
        config.WorldType = project.WorldType;
        int result = 1;
        {
            Application app(config);
            result = ScriptingRuntime::Instance().RunCameraFlyRegressionTests(
                app.GetScene());
        }
        glfwTerminate();
        return result;
    }

    if (argc >= 3 && std::string(argv[1]) == "--script-api-tests")
    {
        const ProjectInfo project = ProjectManager::DiscoverProject(
            argv[2], GraphicsQuality::Balanced);
        ProjectConfig config;
        config.ProjectPath = project.Path;
        config.ScenePath = project.ScenePath;
        config.Quality = project.Quality;
        const bool world2D = argc >= 4 && std::string(argv[3]) == "2d";
        config.WorldType = world2D ? ProjectWorldType::World2D : ProjectWorldType::World3D;
        int result = 1;
        {
            Application app(config);
            result = ScriptingRuntime::Instance().RunComponentApiRegressionTests(
                app.GetScene(), world2D);
        }
        glfwTerminate();
        return result;
    }

    // Plataforma separada do editor: catálogo e execução 100% local.
    if (argc >= 2 && std::string(argv[1]) == "--platform")
    {
        LocalPlatform platform;
        const int result = platform.Run();
        glfwTerminate();
        return result;
    }

    // Fluxo opcional para testes, atalhos e integração com ferramentas:
    // o usuário comum continua iniciando pelo Hub normalmente.
    if (argc >= 3 && std::string(argv[1]) == "--project")
    {
        const std::filesystem::path projectPath(argv[2]);
        if (std::filesystem::exists(projectPath))
        {
            const ProjectInfo project = ProjectManager::DiscoverProject(
                projectPath.string(), GraphicsQuality::HighFidelity);
            config.ProjectPath = project.Path;
            config.ScenePath = project.ScenePath;
            config.Quality = project.Quality;
            config.WorldType = project.WorldType;
            showHub = false;
            startInPlay = argc >= 4 && std::string(argv[3]) == "--play";
        }
        else
        {
            std::cerr << "Projeto nao encontrado: " << projectPath.string() << "\n";
            return 2;
        }
    }

    while (true)
    {
        bool openPlatform = false;
        if (showHub)
        {
            HubResult result;
            {
                // O Hub precisa ser destruído antes de criar outra janela ImGui.
                ProjectHub hub;
                result = hub.Run();
            }
            if (!result.ShouldLaunch)
            {
                if (result.OpenPlatform)
                    openPlatform = true;
                else
                    break;
            }
            else
            {
                config.ProjectPath = result.ProjectPath;
                config.ScenePath   = result.ScenePath;
                config.Quality     = result.Quality;
                config.WorldType   = result.WorldType;
                showHub = false;
            }
        }

        if (openPlatform)
        {
            LocalPlatform platform;
            platform.Run();
            showHub = true;
            continue;
        }

        Application app(config);
        if (startInPlay)
            app.SetEditorState(Application::EditorState::Play);
        app.Run();

        const ProjectSessionResult& session = app.GetSessionResult();
        if (session.Action == ProjectSessionAction::ReturnToHub)
        {
            showHub = true;
            continue;
        }
        if (session.Action == ProjectSessionAction::OpenAnotherProject)
        {
            config = session.NextProject;
            showHub = false;
            continue;
        }
        if (session.Action == ProjectSessionAction::RestartWithGraphicsAPI)
        {
            config = session.NextProject;
            showHub = false;
            continue;
        }
        break;
    }

    glfwTerminate();
    return 0;
}
