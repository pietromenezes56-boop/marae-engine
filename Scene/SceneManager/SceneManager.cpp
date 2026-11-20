#include "SceneManager.h"
#include "../../Core/Application.h"
#include "../Scene/Scene.h"
#include "../../Editor/EditorObjects/EditorObjects.h"
#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace fs = std::filesystem;

SceneManager::SceneManager()
{
}

SceneManager::~SceneManager()
{
    if (m_AsyncThread && m_AsyncThread->joinable())
    {
        m_AsyncThread->join();
    }
}

std::string SceneManager::GetActiveSceneName() const
{
    if (m_ActiveScenePath.empty()) return "Sem Titulo";
    return fs::path(m_ActiveScenePath).stem().string();
}

std::string SceneManager::GetLoadingStatus() const
{
    std::lock_guard<std::mutex> lock(m_StatusMutex);
    return m_LoadingStatus;
}

void SceneManager::RefreshProjectLevels(const std::string& projectRootPath)
{
    m_ProjectLevels.clear();
    if (projectRootPath.empty()) return;

    fs::path scenesDir = fs::path(projectRootPath) / "Assets" / "Scenes";
    if (!fs::exists(scenesDir)) return;

    int order = 0;
    for (const auto& entry : fs::recursive_directory_iterator(scenesDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".scene")
        {
            SceneLevelInfo info;
            info.Name = entry.path().stem().string();
            info.AbsolutePath = entry.path().string();
            try {
                info.RelativePath = fs::relative(entry.path(), projectRootPath).string();
            } catch (...) {
                info.RelativePath = info.AbsolutePath;
            }
            std::replace(info.RelativePath.begin(), info.RelativePath.end(), '\\', '/');
            info.OrderIndex = order++;
            m_ProjectLevels.push_back(info);
        }
    }

    // Ordena de modo que "Main.scene" venha primeiro por padrão
    std::sort(m_ProjectLevels.begin(), m_ProjectLevels.end(),
        [](const SceneLevelInfo& a, const SceneLevelInfo& b) {
            if (a.Name == "Main") return true;
            if (b.Name == "Main") return false;
            return a.Name < b.Name;
        });

    for (size_t i = 0; i < m_ProjectLevels.size(); ++i)
    {
        m_ProjectLevels[i].OrderIndex = static_cast<int>(i);
    }
}

bool SceneManager::LoadNextLevel(Application& app, SceneTransitionType transition)
{
    if (m_ProjectLevels.empty()) return false;

    int currentIndex = -1;
    for (size_t i = 0; i < m_ProjectLevels.size(); ++i)
    {
        if (m_ProjectLevels[i].AbsolutePath == m_ActiveScenePath ||
            m_ProjectLevels[i].RelativePath == m_ActiveScenePath)
        {
            currentIndex = static_cast<int>(i);
            break;
        }
    }

    int nextIndex = currentIndex + 1;
    if (nextIndex >= static_cast<int>(m_ProjectLevels.size()))
    {
        // Se chegou ao fim dos níveis, reinicia ou retorna falso
        nextIndex = 0;
    }

    LoadSceneAsync(app, m_ProjectLevels[nextIndex].AbsolutePath, transition, 0.5f);
    return true;
}

void SceneManager::TriggerTransition(SceneTransitionType type, float duration)
{
    if (type == SceneTransitionType::None) return;
    m_TransitionType = type;
    m_TransitionDuration = std::max(0.05f, duration);
    m_TransitionTimer = 0.0f;
    m_TransitionAlpha = 0.0f;
    m_TransitionState = SceneTransitionState::FadingOut;
}

void SceneManager::NewScene(Application& app)
{
    app.GetScene().Clear();
    app.m_SceneHasLightingSettings = false;
    EditorObjects::CreateDefaultObjects(app.GetScene());
    app.SetGraphicsQuality(app.GetGraphicsQuality());
    m_ActiveScenePath.clear();
    m_LoadedAdditiveScenes.clear();
    m_AdditiveEntityIDs.clear();
    app.SetSelectedEntity(-1);
    app.ClearMultiSelection();
    std::cout << "[SceneManager] Nova cena inicializada com sucesso.\n";
}

bool SceneManager::SaveActiveScene(Application& app, const std::string& customPath)
{
    std::string path = customPath.empty() ? m_ActiveScenePath : customPath;
    if (path.empty()) return false;

    // Garante diretórios
    try {
        fs::create_directories(fs::path(path).parent_path());
    } catch (...) {}

    const std::string previousPath = app.m_ProjectConfig.ScenePath;
    app.m_ProjectConfig.ScenePath = path;
    const bool saved = app.SaveScene() && fs::is_regular_file(path);
    if (saved)
    {
        m_ActiveScenePath = path;
        app.m_ProjectConfig.ScenePath = path;
    }
    else
    {
        app.m_ProjectConfig.ScenePath = previousPath;
    }
    return saved;
}

bool SceneManager::ExecuteLoadSceneInternal(Application& app, const std::string& path, bool isAdditive)
{
    std::vector<uint64_t> previousEntityIDs;
    if (isAdditive)
    {
        for (const Entity& entity : app.GetScene().GetEntities())
            previousEntityIDs.push_back(entity.GetID());
    }

    if (!app.LoadScene(path, isAdditive))
        return false;

    if (isAdditive)
    {
        std::unordered_set<uint64_t> previousIDs(previousEntityIDs.begin(), previousEntityIDs.end());
        std::vector<uint64_t> loadedIDs;
        for (const Entity& entity : app.GetScene().GetEntities())
        {
            if (!previousIDs.contains(entity.GetID()))
                loadedIDs.push_back(entity.GetID());
        }

        m_LoadedAdditiveScenes.push_back(path);
        m_AdditiveEntityIDs[path] = std::move(loadedIDs);
        app.MarkDirty();
        std::cout << "[SceneManager] Cena aditiva carregada: " << path << std::endl;
    }
    else
    {
        m_LoadedAdditiveScenes.clear();
        m_AdditiveEntityIDs.clear();
        m_ActiveScenePath = path;
        app.m_ProjectConfig.ScenePath = path;
        std::cout << "[SceneManager] Cena principal carregada: " << path << std::endl;
    }

    app.GetScene().UpdateTransforms();
    return true;
}

bool SceneManager::LoadScene(Application& app, const std::string& path)
{
    if (!fs::exists(path))
    {
        std::cerr << "[SceneManager] Arquivo de cena nao encontrado: " << path << std::endl;
        return false;
    }

    return ExecuteLoadSceneInternal(app, path, false);
}

bool SceneManager::LoadSceneAdditive(Application& app, const std::string& path)
{
    if (!fs::exists(path))
    {
        std::cerr << "[SceneManager] Arquivo aditivo nao encontrado: " << path << std::endl;
        return false;
    }

    if (m_AdditiveEntityIDs.contains(path))
    {
        std::cerr << "[SceneManager] Cena aditiva ja esta carregada: " << path << std::endl;
        return false;
    }
    return ExecuteLoadSceneInternal(app, path, true);
}

void SceneManager::UnloadAdditiveScene(Application& app, const std::string& scenePath)
{
    auto it = std::find(m_LoadedAdditiveScenes.begin(), m_LoadedAdditiveScenes.end(), scenePath);
    if (it != m_LoadedAdditiveScenes.end())
    {
        const auto loaded = m_AdditiveEntityIDs.find(scenePath);
        if (loaded != m_AdditiveEntityIDs.end())
        {
            for (uint64_t entityID : loaded->second)
            {
                for (int index = static_cast<int>(app.GetScene().GetEntities().size()) - 1;
                     index >= 0; --index)
                {
                    if (app.GetScene().GetEntities()[static_cast<size_t>(index)].GetID() == entityID)
                    {
                        app.GetScene().DeleteEntity(index);
                        break;
                    }
                }
            }
            m_AdditiveEntityIDs.erase(loaded);
            app.GetScene().UpdateTransforms();
            app.MarkDirty();
        }
        m_LoadedAdditiveScenes.erase(it);
        std::cout << "[SceneManager] Cena aditiva descarregada: " << scenePath << std::endl;
    }
}

void SceneManager::LoadSceneAsync(Application& app,
                                  const std::string& path,
                                  SceneTransitionType transition,
                                  float transitionDuration,
                                  SceneLoadedCallback onComplete)
{
    if (m_IsAsyncLoading.load())
    {
        std::cerr << "[SceneManager] Já existe um carregamento em andamento!\n";
        return;
    }

    if (!fs::exists(path))
    {
        std::cerr << "[SceneManager] Arquivo de cena não existe: " << path << std::endl;
        if (onComplete) onComplete(path, false);
        return;
    }

    if (m_AsyncThread && m_AsyncThread->joinable())
    {
        m_AsyncThread->join();
    }

    m_PendingScenePath = path;
    m_PendingCallback = onComplete;
    m_IsAsyncLoading = true;
    m_LoadingProgress = 0.0f;
    m_AsyncReadDone = false;
    m_AsyncReadSuccess = false;

    {
        std::lock_guard<std::mutex> lock(m_StatusMutex);
        m_LoadingStatus = "Iniciando transicao...";
    }

    // Configura a transição
    m_TransitionType = transition;
    m_TransitionDuration = std::max(0.05f, transitionDuration);
    m_TransitionTimer = 0.0f;
    m_TransitionAlpha = 0.0f;

    if (transition != SceneTransitionType::None)
    {
        m_TransitionState = SceneTransitionState::FadingOut;
    }
    else
    {
        m_TransitionState = SceneTransitionState::Loading;
    }

    // Dispara a leitura preliminar assíncrona do arquivo
    m_AsyncThread = std::make_unique<std::thread>([this, path]()
    {
        {
            std::lock_guard<std::mutex> lock(m_StatusMutex);
            m_LoadingStatus = "Lendo arquivo de cena...";
        }
        m_LoadingProgress = 0.2f;

        std::ifstream file(path);
        if (!file.is_open())
        {
            m_AsyncReadSuccess = false;
            m_AsyncReadDone = true;
            m_IsAsyncLoading = false;
            return;
        }

        std::stringstream buffer;
        std::string line;
        int lineCount = 0;
        while (std::getline(file, line))
        {
            buffer << line << "\n";
            ++lineCount;
            if (lineCount % 200 == 0)
            {
                m_LoadingProgress = std::min(0.85f, 0.2f + (lineCount / 1000.0f));
            }
        }

        m_AsyncFileContent = buffer.str();
        {
            std::lock_guard<std::mutex> lock(m_StatusMutex);
            m_LoadingStatus = "Preparando recursos e geometrias...";
        }
        m_LoadingProgress = 0.95f;

        // Pequena pausa para garantir suavidade da transição
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        m_AsyncReadSuccess = true;
        m_AsyncReadDone = true;
    });
}

void SceneManager::Update(Application& app, float deltaTime)
{
    if (m_TransitionState == SceneTransitionState::Idle)
    {
        return;
    }

    switch (m_TransitionState)
    {
    case SceneTransitionState::FadingOut:
    {
        m_TransitionTimer += deltaTime;
        m_TransitionAlpha = std::clamp(m_TransitionTimer / m_TransitionDuration, 0.0f, 1.0f);

        if (m_TransitionTimer >= m_TransitionDuration)
        {
            m_TransitionAlpha = 1.0f;
            m_TransitionState = SceneTransitionState::Loading;
        }
        break;
    }

    case SceneTransitionState::Loading:
    {
        // Se a leitura assíncrona terminou, aplica a nova cena na thread principal
        if (m_AsyncReadDone.load())
        {
            {
                std::lock_guard<std::mutex> lock(m_StatusMutex);
                m_LoadingStatus = "Instanciando entidades...";
            }
            m_LoadingProgress = 1.0f;

            if (m_AsyncReadSuccess.load())
            {
                const bool loaded = ExecuteLoadSceneInternal(app, m_PendingScenePath, false);
                if (m_PendingCallback)
                {
                    m_PendingCallback(m_PendingScenePath, loaded);
                }
                if (!loaded)
                    std::cerr << "[SceneManager] Falha ao aplicar cena assincrona.\n";
                m_AsyncReadSuccess = loaded;
            }
            else
            {
                std::cerr << "[SceneManager] Falha ao carregar cena assincrona.\n";
                if (m_PendingCallback)
                {
                    m_PendingCallback(m_PendingScenePath, false);
                }
            }

            m_PendingCallback = nullptr;
            m_IsAsyncLoading = false;

            if (m_TransitionType != SceneTransitionType::None)
            {
                m_TransitionState = SceneTransitionState::FadingIn;
                m_TransitionTimer = m_TransitionDuration;
            }
            else
            {
                m_TransitionState = SceneTransitionState::Idle;
                m_TransitionAlpha = 0.0f;
            }
        }
        break;
    }

    case SceneTransitionState::FadingIn:
    {
        m_TransitionTimer -= deltaTime;
        m_TransitionAlpha = std::clamp(m_TransitionTimer / m_TransitionDuration, 0.0f, 1.0f);

        if (m_TransitionTimer <= 0.0f)
        {
            m_TransitionAlpha = 0.0f;
            m_TransitionState = SceneTransitionState::Idle;
            if (m_AsyncThread && m_AsyncThread->joinable())
            {
                m_AsyncThread->join();
            }
        }
        break;
    }

    default:
        break;
    }
}

void SceneManager::RenderTransitionOverlay(float screenWidth, float screenHeight)
{
    if (m_TransitionState == SceneTransitionState::Idle || m_TransitionAlpha <= 0.001f)
    {
        return;
    }

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (!draw) return;

    // Cor do overlay baseado no tipo
    ImU32 overlayColor = IM_COL32(0, 0, 0, static_cast<int>(m_TransitionAlpha * 255.0f));
    if (m_TransitionType == SceneTransitionType::FadeWhite)
    {
        overlayColor = IM_COL32(255, 255, 255, static_cast<int>(m_TransitionAlpha * 255.0f));
    }
    else if (m_TransitionType == SceneTransitionType::CrossFade)
    {
        overlayColor = IM_COL32(18, 24, 38, static_cast<int>(m_TransitionAlpha * 230.0f));
    }

    // Cobre toda a janela
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(screenWidth, screenHeight), overlayColor);

    // Se estiver carregando no escuro, desenha um spinner elegante e o status
    if (m_TransitionState == SceneTransitionState::Loading ||
        (m_IsAsyncLoading.load() && m_TransitionAlpha >= 0.8f))
    {
        const ImVec2 center(screenWidth * 0.5f, screenHeight * 0.5f);
        const float time = static_cast<float>(ImGui::GetTime());
        const float start = time * 4.0f;
        const float radius = 28.0f;

        draw->PathClear();
        for (int i = 0; i <= 36; ++i)
        {
            const float a = start + 6.28318530718f * static_cast<float>(i) / 36.0f;
            draw->PathLineTo(ImVec2(
                center.x + std::cos(a) * radius,
                center.y + std::sin(a) * radius));
        }
        draw->PathStroke(IM_COL32(65, 145, 255, 255), false, 4.0f);

        // Barra de progresso
        const float barWidth = 260.0f;
        const float barHeight = 5.0f;
        const ImVec2 barMin(center.x - barWidth * 0.5f, center.y + 45.0f);
        const ImVec2 barMax(center.x + barWidth * 0.5f, center.y + 45.0f + barHeight);

        draw->AddRectFilled(barMin, barMax, IM_COL32(40, 45, 55, 200), 3.0f);

        const float progress = std::clamp(m_LoadingProgress.load(), 0.0f, 1.0f);
        draw->AddRectFilled(barMin, ImVec2(barMin.x + barWidth * progress, barMax.y),
                            IM_COL32(65, 155, 255, 255), 3.0f);

        // Texto de status
        std::string status = GetLoadingStatus();
        ImVec2 textSize = ImGui::CalcTextSize(status.c_str());
        draw->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y + 60.0f),
                      IM_COL32(200, 220, 255, 255), status.c_str());
    }
}
