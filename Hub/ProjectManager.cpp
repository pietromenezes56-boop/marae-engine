#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS   // silencia getenv no MSVC
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif

#include "ProjectManager.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers de serialização JSON minimalista (sem dependência externa)
// Formato:
//   { "projects": [ { "name":"...", "path":"...", "scene":"...",
//                     "thumbnail":"...", "quality":1, "lastOpened":... }, ... ] }
// ---------------------------------------------------------------------------

static std::string JsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else                out += c;
    }
    return out;
}

static std::string JsonUnescape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            ++i;
            if      (s[i] == '"')  out += '"';
            else if (s[i] == '\\') out += '\\';
            else if (s[i] == 'n')  out += '\n';
            else if (s[i] == 'r')  out += '\r';
            else                   { out += '\\'; out += s[i]; }
        }
        else out += s[i];
    }
    return out;
}

static ProjectWorldType ReadProjectWorldType(const fs::path& projectPath)
{
    std::ifstream file(projectPath / "Project.marae");
    std::string line;
    while (std::getline(file, line))
    {
        if (line.rfind("WorldType=", 0) == 0)
        {
            try
            {
                return std::stoi(line.substr(10)) == 1
                    ? ProjectWorldType::World2D
                    : ProjectWorldType::World3D;
            }
            catch (...) {}
        }
    }
    // Projetos antigos não possuíam essa informação e continuam 3D.
    return ProjectWorldType::World3D;
}

static void WriteProjectWorldType(const fs::path& projectPath, ProjectWorldType type)
{
    std::ofstream file(projectPath / "Project.marae", std::ios::trunc);
    if (file.is_open())
        file << "WorldType=" << (type == ProjectWorldType::World2D ? 1 : 0) << "\n";
}

// Extrai valor de string de uma chave simples: "key":"value"
static std::string ExtractString(const std::string& json, const std::string& key)
{
    const std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    while (pos < json.size() && std::isspace((unsigned char)json[pos])) ++pos;
    if (pos >= json.size() || json[pos++] != ':') return "";
    while (pos < json.size() && std::isspace((unsigned char)json[pos])) ++pos;
    if (pos >= json.size() || json[pos++] != '"') return "";

    std::string value;
    bool escaped = false;
    for (; pos < json.size(); ++pos)
    {
        const char c = json[pos];
        if (!escaped && c == '"') return JsonUnescape(value);
        value += c;
        escaped = !escaped && c == '\\';
        if (c != '\\') escaped = false;
    }
    return "";
}

static int64_t ExtractInt64(const std::string& json, const std::string& key)
{
    const std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.size();
    // skip whitespace
    while (pos < json.size() && json[pos] == ' ') ++pos;
    std::string num;
    while (pos < json.size() && (std::isdigit((unsigned char)json[pos]) || json[pos] == '-'))
        num += json[pos++];
    if (num.empty()) return 0;
    return std::stoll(num);
}

static std::string NormalizePath(const std::string& path)
{
    if (path.empty()) return {};

    std::error_code error;
    fs::path normalized = fs::absolute(fs::path(path), error);
    if (error) normalized = fs::path(path);
    return normalized.lexically_normal().string();
}

static bool PathsMatch(const std::string& a, const std::string& b)
{
    std::string left = NormalizePath(a);
    std::string right = NormalizePath(b);
#ifdef _WIN32
    std::transform(left.begin(), left.end(), left.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    std::transform(right.begin(), right.end(), right.begin(), [](unsigned char c) { return (char)std::tolower(c); });
#endif
    return left == right;
}

static int64_t CurrentTimestamp()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static std::string FindScenePath(const fs::path& projectDir)
{
    const fs::path mainScene = projectDir / "Assets" / "Scenes" / "Main.scene";
    std::error_code error;
    if (fs::exists(mainScene, error) && !error)
        return mainScene.string();

    fs::recursive_directory_iterator it(projectDir,
        fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    while (!error && it != end)
    {
        if (it->is_regular_file(error) && !error && it->path().extension() == ".scene")
            return it->path().string();
        it.increment(error);
        if (error) error.clear();
    }
    return {};
}

// ---------------------------------------------------------------------------

std::string ProjectManager::GetUserDocumentsPath()
{
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, path)))
        return std::string(path);
#endif
    const char* home = std::getenv("USERPROFILE");
    if (home) return std::string(home) + "/Documents";
    home = std::getenv("HOME");
    if (home) return std::string(home) + "/Documents";
    return ".";}

std::string ProjectManager::BrowseForProjectFolder(const std::string& title)
{
#ifdef _WIN32
    BROWSEINFOA browseInfo = {};
    browseInfo.lpszTitle = title.c_str();
    browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST itemId = SHBrowseForFolderA(&browseInfo);
    if (!itemId) return {};

    char path[MAX_PATH] = {};
    const bool foundPath = SHGetPathFromIDListA(itemId, path) != FALSE;
    CoTaskMemFree(itemId);
    return foundPath ? std::string(path) : std::string();
#else
    (void)title;
    return {};
#endif
}

std::string ProjectManager::GetRegistryPath()
{
    fs::path dir = fs::path(GetUserDocumentsPath()) / "MaraeEngine";
    fs::create_directories(dir);
    return (dir / "projects.json").string();
}

std::vector<ProjectInfo> ProjectManager::LoadProjects()
{
    std::vector<ProjectInfo> projects;

    std::string path = GetRegistryPath();
    std::ifstream f(path);
    if (!f.is_open()) return projects;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Localiza o array e lê somente os objetos de projeto. A implementação
    // anterior começava no objeto-raiz e, por isso, descartava o primeiro item.
    const auto projectsKey = content.find("\"projects\"");
    const auto arrayStart = projectsKey == std::string::npos ? std::string::npos
        : content.find('[', projectsKey);
    if (arrayStart == std::string::npos) return projects;

    size_t cursor = arrayStart + 1;
    while (true)
    {
        auto start = content.find('{', cursor);
        if (start == std::string::npos) break;

        size_t end = start;
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        for (; end < content.size(); ++end)
        {
            const char c = content[end];
            if (inString)
            {
                if (!escaped && c == '"') inString = false;
                escaped = !escaped && c == '\\';
                if (c != '\\') escaped = false;
                continue;
            }
            if (c == '"') { inString = true; continue; }
            if (c == '{') ++depth;
            if (c == '}' && --depth == 0) break;
        }
        if (end == content.size()) break;
        cursor = end + 1;

        std::string obj = content.substr(start, end - start + 1);

        ProjectInfo info;
        info.Name           = ExtractString(obj, "name");
        info.OfficialName   = ExtractString(obj, "officialName");
        info.Description    = ExtractString(obj, "description");
        info.Path           = ExtractString(obj, "path");
        info.ScenePath      = ExtractString(obj, "scene");
        info.ThumbnailPath  = ExtractString(obj, "thumbnail");
        info.PresentationCoverPath = ExtractString(obj, "presentationCover");
        info.Quality        = (GraphicsQuality)ExtractInt64(obj, "quality");
        info.WorldType      = ExtractInt64(obj, "worldType") == 1
            ? ProjectWorldType::World2D
            : ProjectWorldType::World3D;
        info.LastOpenedTimestamp = ExtractInt64(obj, "lastOpened");

        if (!info.Name.empty() && !info.Path.empty())
            projects.push_back(std::move(info));
    }

    return projects;
}

void ProjectManager::SaveProjects(const std::vector<ProjectInfo>& projects)
{
    std::string path = GetRegistryPath();
    const fs::path registryPath(path);
    const fs::path temporaryPath = registryPath.string() + ".tmp";
    const fs::path backupPath = registryPath.string() + ".bak";

    // Nunca truncar o registro ativo diretamente: uma interrupção durante a
    // gravação poderia deixar projects.json vazio ou parcialmente escrito.
    std::ofstream f(temporaryPath, std::ios::trunc);
    if (!f.is_open())
    {
        std::cerr << "[ProjectManager] Falha ao abrir temporario para salvar "
                  << temporaryPath.string() << std::endl;
        return;
    }

    f << "{\n  \"projects\": [\n";
    for (size_t i = 0; i < projects.size(); ++i)
    {
        const auto& p = projects[i];
        f << "    {\n";
        f << "      \"name\":        \"" << JsonEscape(p.Name)          << "\",\n";
        f << "      \"officialName\": \"" << JsonEscape(p.OfficialName)   << "\",\n";
        f << "      \"description\":  \"" << JsonEscape(p.Description)    << "\",\n";
        f << "      \"path\":        \"" << JsonEscape(p.Path)          << "\",\n";
        f << "      \"scene\":       \"" << JsonEscape(p.ScenePath)     << "\",\n";
        f << "      \"thumbnail\":   \"" << JsonEscape(p.ThumbnailPath) << "\",\n";
        f << "      \"presentationCover\": \"" << JsonEscape(p.PresentationCoverPath) << "\",\n";
        f << "      \"quality\":     "  << (int)p.Quality              << ",\n";
        f << "      \"worldType\":   "  << (int)p.WorldType              << ",\n";
        f << "      \"lastOpened\":  "  << p.LastOpenedTimestamp       << "\n";
        f << "    }";
        if (i + 1 < projects.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";

    f.flush();
    if (!f.good())
    {
        f.close();
        std::error_code cleanupError;
        fs::remove(temporaryPath, cleanupError);
        std::cerr << "[ProjectManager] Falha ao concluir gravacao temporaria de "
                  << path << std::endl;
        return;
    }
    f.close();

    std::error_code error;
    const bool hadOriginal = fs::exists(registryPath, error) && !error;
    error.clear();

    // Mantém uma cópia recuperável do último registro válido.
    if (hadOriginal)
    {
        fs::copy_file(registryPath, backupPath,
                      fs::copy_options::overwrite_existing, error);
        if (error)
        {
            fs::remove(temporaryPath, error);
            std::cerr << "[ProjectManager] Falha ao criar backup de "
                      << path << std::endl;
            return;
        }
        error.clear();
        fs::remove(registryPath, error);
        if (error)
        {
            fs::remove(temporaryPath, error);
            std::cerr << "[ProjectManager] Falha ao preparar substituicao de "
                      << path << std::endl;
            return;
        }
    }

    fs::rename(temporaryPath, registryPath, error);
    if (error)
    {
        std::error_code restoreError;
        if (hadOriginal && fs::exists(backupPath, restoreError))
        {
            fs::copy_file(backupPath, registryPath,
                          fs::copy_options::overwrite_existing, restoreError);
        }
        fs::remove(temporaryPath, restoreError);
        std::cerr << "[ProjectManager] Falha ao ativar novo registro "
                  << path << std::endl;
    }
}

ProjectInfo ProjectManager::CreateProject(const std::string& name,
                                           const std::string& directory,
                                           GraphicsQuality quality,
                                           ProjectWorldType worldType)
{
    if (name.empty() || directory.empty())
        return {};

    // Sanitiza o nome para uso como nome de pasta
    std::string folderName = name;
    for (char& c : folderName)
        if (c == ' ' || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"')
            c = '_';

    fs::path projectDir = fs::path(directory) / folderName;

    try
    {
        // Cria estrutura de pastas do projeto
        fs::create_directories(projectDir);
        fs::create_directories(projectDir / "Assets" / "Scenes");
        fs::create_directories(projectDir / "Assets" / "Materials");
        fs::create_directories(projectDir / "Assets" / "Textures");
        fs::create_directories(projectDir / "Assets" / "Models");
        fs::create_directories(projectDir / "Assets" / "Prefabs");
        fs::create_directories(projectDir / "Scripts");
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[ProjectManager] Erro ao criar projeto: " << ex.what() << std::endl;
        return {};
    }

    // Cria arquivo de cena vazio
    std::string scenePath = (projectDir / "Assets" / "Scenes" / "Main.scene").string();
    { std::ofstream f(scenePath); }  // cria arquivo vazio
    WriteProjectWorldType(projectDir, worldType);

    // Monta o ProjectInfo
    ProjectInfo info;
    info.Name           = name;
    info.Path           = projectDir.string();
    info.ScenePath      = scenePath;
    info.ThumbnailPath  = "";  // sem thumbnail por enquanto
    info.Quality        = quality;
    info.WorldType      = worldType;
    info.LastOpenedTimestamp = CurrentTimestamp();

    return info;
}

ProjectInfo ProjectManager::DiscoverProject(const std::string& projectPath,
                                            GraphicsQuality quality)
{
    ProjectInfo info;
    info.Path = NormalizePath(projectPath);
    if (info.Path.empty()) return info;

    fs::path path(info.Path);
    info.Name = path.filename().string();
    if (info.Name.empty()) info.Name = info.Path;
    info.ScenePath = FindScenePath(path);
    info.Quality = quality;
    info.WorldType = ReadProjectWorldType(path);

    // Pacotes exportados para a Marae Platform carregam metadados públicos
    // sem alterar o nome técnico da pasta/projeto.
    const fs::path platformManifest = path / "MaraePlatform.manifest";
    std::ifstream manifest(platformManifest);
    std::string line;
    while (std::getline(manifest, line))
    {
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "official_name") info.OfficialName = value;
        else if (key == "description") info.Description = value;
        else if (key == "cover") info.PresentationCoverPath =
            (path / fs::path(value)).lexically_normal().string();
    }
    if (!info.OfficialName.empty()) info.ThumbnailPath = info.PresentationCoverPath;
    return info;
}

ProjectInfo ProjectManager::RegisterProject(const ProjectInfo& project)
{
    ProjectInfo registered = project;
    registered.Path = NormalizePath(registered.Path);
    if (registered.Path.empty()) return {};
    if (registered.Name.empty())
        registered.Name = fs::path(registered.Path).filename().string();
    if (registered.OfficialName.empty()) registered.OfficialName = registered.Name;
    if (registered.ScenePath.empty())
        registered.ScenePath = FindScenePath(fs::path(registered.Path));

    std::vector<ProjectInfo> projects = LoadProjects();
    auto existing = std::find_if(projects.begin(), projects.end(), [&](const ProjectInfo& item)
    {
        return PathsMatch(item.Path, registered.Path);
    });

    if (existing != projects.end())
    {
        // Mantém a qualidade escolhida anteriormente para projetos já conhecidos.
        registered.Quality = existing->Quality;
        registered.WorldType = existing->WorldType;
        if (registered.Name.empty()) registered.Name = existing->Name;
        if (registered.ScenePath.empty()) registered.ScenePath = existing->ScenePath;
        if (registered.ThumbnailPath.empty()) registered.ThumbnailPath = existing->ThumbnailPath;
        if (registered.OfficialName.empty()) registered.OfficialName = existing->OfficialName;
        if (registered.Description.empty()) registered.Description = existing->Description;
        if (registered.PresentationCoverPath.empty()) registered.PresentationCoverPath = existing->PresentationCoverPath;
        *existing = registered;
    }
    else
    {
        projects.push_back(registered);
        existing = std::prev(projects.end());
    }

    existing->LastOpenedTimestamp = CurrentTimestamp();
    registered = *existing;
    SortByRecent(projects);
    SaveProjects(projects);
    return registered;
}

void ProjectManager::RemoveProject(std::vector<ProjectInfo>& projects,
                                    const std::string& projectPath)
{
    projects.erase(std::remove_if(projects.begin(), projects.end(),
        [&](const ProjectInfo& p){ return PathsMatch(p.Path, projectPath); }),
        projects.end());
}

void ProjectManager::TouchProject(std::vector<ProjectInfo>& projects,
                                   const std::string& projectPath)
{
    for (auto& p : projects)
    {
        if (PathsMatch(p.Path, projectPath))
        {
            p.LastOpenedTimestamp = CurrentTimestamp();
            break;
        }
    }
}

void ProjectManager::SortByRecent(std::vector<ProjectInfo>& projects)
{
    std::sort(projects.begin(), projects.end(),
        [](const ProjectInfo& a, const ProjectInfo& b){
            return a.LastOpenedTimestamp > b.LastOpenedTimestamp;
        });
}

bool ProjectManager::ProjectExists(const ProjectInfo& info)
{
    return !info.Path.empty() && fs::exists(info.Path);
}
