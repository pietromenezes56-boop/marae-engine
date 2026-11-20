#pragma once

#include "../Hub/ProjectManager.h"
#include <string>
#include <vector>

struct GLFWwindow;

// Plataforma local da Marae. Ela não edita projetos: apenas cataloga,
// inicia e testa jogos instalados no computador do usuário.
class LocalPlatform
{
public:
    LocalPlatform();
    ~LocalPlatform();

    int Run();

private:
    bool Init();
    void Shutdown();
    void BeginFrame();
    void EndFrame();
    void Render();
    void ReloadLibrary();
    void LaunchLocalGame(const ProjectInfo& project);
    void TestProject(const ProjectInfo& project);
    void AddLocalProject();
    void RemoveFromLibrary(const ProjectInfo& project);
    std::string ExecutablePath() const;

    GLFWwindow* m_Window = nullptr;
    std::vector<ProjectInfo> m_Projects;
    std::string m_Status = "Pronto para jogar localmente.";
    bool m_Running = true;
    bool m_ShowAddDialog = false;
    char m_ProjectPath[1024] = "";
    std::string m_RemovePath;
};
