#pragma once

#include <string>
#include <vector>
#include <cstdint>

// ---------------------------------------------------------------------------
// Qualidade gráfica escolhida ao criar um projeto
// ---------------------------------------------------------------------------
enum class GraphicsQuality
{
    Performance = 0,   // sombras básicas, sem nuvens
    Balanced    = 1,   // sombras 2K e nuvens médias (default)
    HighFidelity = 2,  // valor mantido para compatibilidade com projetos antigos
    UltraPerformance = 3,
    Ultra = 4
};

// Tipo estrutural do projeto. É definido uma única vez na criação e não pode
// ser alterado dentro do editor depois que o projeto foi criado.
enum class ProjectWorldType
{
    World3D = 0,
    World2D = 1
};

// ---------------------------------------------------------------------------
// Informações de um projeto na lista
// ---------------------------------------------------------------------------
struct ProjectInfo
{
    std::string Name;           // nome de exibição
    std::string OfficialName;   // nome público do jogo, diferente da pasta/projeto
    std::string Description;    // descrição pública para a Marae Platform
    std::string Path;           // caminho absoluto da pasta do projeto
    std::string ScenePath;      // caminho do arquivo .scene principal
    std::string ThumbnailPath;  // caminho da imagem de miniatura (.png)
    std::string PresentationCoverPath;
    GraphicsQuality Quality = GraphicsQuality::Balanced;
    ProjectWorldType WorldType = ProjectWorldType::World3D;
    int64_t LastOpenedTimestamp = 0; // Unix timestamp
};

// ---------------------------------------------------------------------------
// ProjectManager
// Persiste a lista de projetos em um JSON simples em:
//   <user_home>/Documents/MaraeEngine/projects.json
// ---------------------------------------------------------------------------
class ProjectManager
{
public:
    // Retorna o path do arquivo de registro de projetos
    static std::string GetRegistryPath();

    // Carrega a lista de projetos do disco
    static std::vector<ProjectInfo> LoadProjects();

    // Salva a lista de projetos no disco
    static void SaveProjects(const std::vector<ProjectInfo>& projects);

    // Cria a estrutura de pastas de um novo projeto e adiciona à lista
    // Retorna o ProjectInfo criado, ou um ProjectInfo vazio (.Path=="") em caso de erro
    static ProjectInfo CreateProject(const std::string& name,
                                     const std::string& directory,
                                     GraphicsQuality quality,
                                     ProjectWorldType worldType = ProjectWorldType::World3D);

    // Cria as informações de uma pasta de projeto já existente, procurando uma
    // cena principal quando ela existir.
    static ProjectInfo DiscoverProject(const std::string& projectPath,
                                       GraphicsQuality quality = GraphicsQuality::Balanced);

    // Inclui ou atualiza um projeto no registro persistente e o marca como
    // recém-aberto. Retorna a versão efetivamente registrada.
    static ProjectInfo RegisterProject(const ProjectInfo& project);

    // Remove um projeto da lista (não deleta os arquivos em disco)
    static void RemoveProject(std::vector<ProjectInfo>& projects,
                              const std::string& projectPath);

    // Atualiza o timestamp de último acesso de um projeto
    static void TouchProject(std::vector<ProjectInfo>& projects,
                              const std::string& projectPath);

    // Ordena a lista por último acesso (mais recente primeiro)
    static void SortByRecent(std::vector<ProjectInfo>& projects);

    // Caminho padrão de documentos do usuário
    static std::string GetUserDocumentsPath();

    // Abre o seletor nativo de uma pasta de projeto.
    static std::string BrowseForProjectFolder(const std::string& title);

    // Verifica se um projeto existe no disco
    static bool ProjectExists(const ProjectInfo& info);
};
