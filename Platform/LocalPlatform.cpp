#include "../Engine/Localization/Localization.h"
#include "LocalPlatform.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

static bool ValidateProjectForExecution(const ProjectInfo& project, std::string& reason)
{
    if (project.Path.empty() || !fs::exists(project.Path))
    {
        reason = "Projeto local não encontrado.";
        return false;
    }
    fs::path scenePath = project.ScenePath;
    if (scenePath.empty()) scenePath = fs::path(project.Path) / "Assets" / "Scenes" / "Main.scene";
    if (!scenePath.is_absolute()) scenePath = fs::path(project.Path) / scenePath;
    if (!fs::exists(scenePath))
    {
        reason = "Nada a executar: o projeto não possui uma cena principal.";
        return false;
    }
    std::ifstream sceneFile(scenePath, std::ios::binary);
    std::ostringstream contents;
    contents << sceneFile.rdbuf();
    const std::string scene = contents.str();
    if (scene.find("@entity") == std::string::npos)
    {
        reason = "Nada a executar: a cena está vazia.";
        return false;
    }
    const bool hasCamera = scene.find("@camera") != std::string::npos;
    const bool hasScript = scene.find("@script") != std::string::npos;
    if (!hasCamera && !hasScript)
    {
        reason = "Nada a executar: a cena não possui câmera nem script ativo.";
        return false;
    }
    return true;
}

static void ApplyPlatformTheme()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 8.0f;
    s.ChildRounding = 6.0f;
    s.FrameRounding = 5.0f;
    s.ItemSpacing = ImVec2(10.0f, 8.0f);
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.045f, 0.085f, 1.0f);
    s.Colors[ImGuiCol_ChildBg] = ImVec4(0.055f, 0.070f, 0.125f, 1.0f);
    s.Colors[ImGuiCol_Button] = ImVec4(0.08f, 0.30f, 0.62f, 1.0f);
    s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.12f, 0.42f, 0.82f, 1.0f);
    s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.05f, 0.22f, 0.48f, 1.0f);
}

LocalPlatform::LocalPlatform() = default;

LocalPlatform::~LocalPlatform()
{
    Shutdown();
}

int LocalPlatform::Run()
{
    if (!Init()) return 1;
    ReloadLibrary();
    while (m_Running && m_Window && !glfwWindowShouldClose(m_Window))
    {
        glfwPollEvents();
        BeginFrame();
        Render();
        EndFrame();
    }
    return 0;
}

bool LocalPlatform::Init()
{
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    m_Window = glfwCreateWindow(1180, 720, "Marae Platform — Jogos locais", nullptr, nullptr);
    if (!m_Window) return false;
    glfwMakeContextCurrent(m_Window);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ApplyPlatformTheme();
    ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    return true;
}

void LocalPlatform::Shutdown()
{
    if (!m_Window) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(m_Window);
    m_Window = nullptr;
}

void LocalPlatform::BeginFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void LocalPlatform::EndFrame()
{
    ImGui::Render();
    int w = 1, h = 1;
    glfwGetFramebufferSize(m_Window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.018f, 0.025f, 0.055f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_Window);
}

void LocalPlatform::ReloadLibrary()
{
    m_Projects = ProjectManager::LoadProjects();
    ProjectManager::SortByRecent(m_Projects);
}

std::string LocalPlatform::ExecutablePath() const
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0) return {};
    return fs::path(std::wstring(path, length)).string();
}

void LocalPlatform::LaunchLocalGame(const ProjectInfo& project)
{
    std::string validationError;
    if (!ValidateProjectForExecution(project, validationError))
    {
        m_Status = validationError;
        return;
    }
    const fs::path projectPath(project.Path);
    const std::vector<fs::path> candidates = {
        projectPath / "Builds" / "Windows" / "MaraeGame.exe",
        projectPath.parent_path() / (projectPath.filename().string() + "_Build") / "MaraeGame.exe"
    };
    for (const fs::path& exe : candidates)
    {
        if (!fs::exists(exe)) continue;
#ifdef _WIN32
        const HINSTANCE result = ShellExecuteA(nullptr, "open", exe.string().c_str(), nullptr,
                                                exe.parent_path().string().c_str(), SW_SHOWNORMAL);
        if ((INT_PTR)result > 32)
        {
            m_Status = "Jogo iniciado localmente: " + project.Name;
            return;
        }
#endif
    }
    m_Status = "Nenhum build local encontrado. Exporte o projeto para Desktop primeiro.";
}

void LocalPlatform::TestProject(const ProjectInfo& project)
{
    std::string validationError;
    if (!ValidateProjectForExecution(project, validationError))
    {
        m_Status = validationError;
        return;
    }
#ifdef _WIN32
    const std::string exe = ExecutablePath();
    if (!exe.empty())
    {
        const std::string args = "--project \"" + project.Path + "\" --play";
        const HINSTANCE result = ShellExecuteA(nullptr, "open", exe.c_str(), args.c_str(),
                                                fs::path(exe).parent_path().string().c_str(), SW_SHOWNORMAL);
        if ((INT_PTR)result > 32)
        {
            m_Status = "Teste local aberto no runtime da Marae: " + project.Name;
            return;
        }
    }
#endif
    m_Status = "Não foi possível abrir o teste local.";
}

void LocalPlatform::AddLocalProject()
{
    const fs::path path(m_ProjectPath);
    if (!fs::exists(path))
    {
        m_Status = "Pasta do projeto não encontrada.";
        return;
    }
    ProjectInfo project = ProjectManager::DiscoverProject(path.string());
    if (project.Path.empty())
    {
        m_Status = "A pasta escolhida não parece ser um projeto Marae.";
        return;
    }
    ProjectManager::RegisterProject(project);
    m_Status = "Projeto adicionado à biblioteca local.";
    m_ShowAddDialog = false;
    ReloadLibrary();
}

void LocalPlatform::RemoveFromLibrary(const ProjectInfo& project)
{
    ProjectManager::RemoveProject(m_Projects, project.Path);
    ProjectManager::SaveProjects(m_Projects);
    m_Status = "Removido da biblioteca local. Os arquivos do projeto foram preservados.";
}

void LocalPlatform::Render()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    ImGui::Begin(UI("Marae Platform##root"), nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings);

    ImGui::TextColored(ImVec4(0.30f, 0.65f, 1.0f, 1.0f), "MARAE");
    ImGui::SameLine();
    ImGui::Text(UI("Platform"));
    ImGui::SameLine(ImGui::GetWindowWidth() - 250.0f);
    if (ImGui::Button(UI("Adicionar projeto local"))) m_ShowAddDialog = true;
    ImGui::Separator();
    ImGui::Text(UI("Jogos instalados neste computador"));
    ImGui::TextDisabled("Execução 100%% local — nenhum jogo é enviado para a nuvem.");
    ImGui::Spacing();

    if (m_Projects.empty())
    {
        ImGui::BeginChild("empty", ImVec2(0, 180), true);
        ImGui::Text(UI("Sua biblioteca está vazia."));
        ImGui::TextWrapped(UI("Adicione um projeto Marae ou exporte um jogo para Desktop para jogar aqui."));
        ImGui::EndChild();
    }
    else
    {
        int index = 0;
        for (const ProjectInfo& project : m_Projects)
        {
            ImGui::PushID(index++);
            ImGui::BeginChild("card", ImVec2(0, 112), true);
            const std::string publicName = project.OfficialName.empty() ? project.Name : project.OfficialName;
            ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1.0f), "%s", publicName.c_str());
            ImGui::TextDisabled("Projeto: %s", project.Name.c_str());
            if (!project.Description.empty())
                ImGui::TextWrapped(UI("%s"), project.Description.c_str());
            if (!project.PresentationCoverPath.empty())
                ImGui::TextDisabled("Capa: %s", fs::path(project.PresentationCoverPath).filename().string().c_str());
            if (ImGui::Button(UI("Jogar local"))) LaunchLocalGame(project);
            ImGui::SameLine();
            if (ImGui::Button(UI("Testar no editor"))) TestProject(project);
            ImGui::SameLine();
            if (ImGui::Button(UI("Remover da biblioteca"))) m_RemovePath = project.Path;
            ImGui::EndChild();
            ImGui::PopID();
            ImGui::Spacing();
        }
        if (!m_RemovePath.empty())
        {
            const auto it = std::find_if(m_Projects.begin(), m_Projects.end(),
                [this](const ProjectInfo& item) { return item.Path == m_RemovePath; });
            if (it != m_Projects.end())
                RemoveFromLibrary(*it);
            m_RemovePath.clear();
        }
    }
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.60f, 1.0f), "%s", m_Status.c_str());
    if (ImGui::Button(UI("Atualizar biblioteca"))) ReloadLibrary();
    ImGui::SameLine();
    if (ImGui::Button(UI("Sair"))) m_Running = false;
    ImGui::End();

    if (m_ShowAddDialog)
    {
        ImGui::OpenPopup("Adicionar projeto Marae##add");
        m_ShowAddDialog = false;
    }
    if (ImGui::BeginPopupModal("Adicionar projeto Marae##add", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped(UI("Informe a pasta raiz do projeto Marae. A plataforma apenas registra o caminho local."));
        ImGui::InputText(UI("Pasta"), m_ProjectPath, sizeof(m_ProjectPath));
        if (ImGui::Button(UI("Adicionar"), ImVec2(120, 0))) AddLocalProject();
        ImGui::SameLine();
        if (ImGui::Button(UI("Cancelar"), ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
