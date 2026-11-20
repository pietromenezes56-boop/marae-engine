    #include "AssetDatabase.h"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <regex>
#include <sstream>
#include "../Engine/Assets/GUID/GUID.h"
#include "../../ThirdParty/json/json.hpp"

using json = nlohmann::json;

std::vector<AssetInfo> AssetDatabase::s_Assets;
AssetScanStats AssetDatabase::s_LastScanStats;
std::vector<AssetValidationIssue> AssetDatabase::s_ValidationIssues;
std::vector<std::string> AssetDatabase::s_UnusedAssets;
static std::filesystem::path s_ProjectRoot;

static std::string PathToUTF8(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

static uint64_t LastWriteTimeValue(const std::filesystem::path& path)
{
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    if (error) return 0;
    return static_cast<uint64_t>(time.time_since_epoch().count());
}

static std::string NormalizeAssetPath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.size() > 2 && path.back() == '/') path.pop_back();
    return path;
}

static std::vector<std::string> ReadAssetDependencies(
    const std::filesystem::path& file,
    const std::filesystem::path& projectRoot)
{
    const std::string extension = file.extension().string();
    if (extension != ".scene" && extension != ".prefab" && extension != ".material" &&
        extension != ".mat")
        return {};

    std::ifstream input(file, std::ios::binary);
    if (!input) return {};
    const std::string content((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
    std::vector<std::string> dependencies;
    static const std::regex pathPattern(R"REGEX((?:Assets|Library)/[^"'\r\n]+)REGEX",
                                         std::regex::icase);
    for (std::sregex_iterator it(content.begin(), content.end(), pathPattern), end;
         it != end; ++it)
    {
        const std::string normalized = NormalizeAssetPath(it->str());
        if (std::find(dependencies.begin(), dependencies.end(), normalized) == dependencies.end())
            dependencies.push_back(normalized);
    }
    (void)projectRoot;
    return dependencies;
}

static const char* AssetTypeName(AssetType type)
{
    switch (type)
    {
    case AssetType::Texture: return "Texture";
    case AssetType::Mesh: return "Mesh";
    case AssetType::Material: return "Material";
    case AssetType::Scene: return "Scene";
    case AssetType::Prefab: return "Prefab";
    case AssetType::Audio: return "Audio";
    case AssetType::Script: return "Script";
    default: return "Unknown";
    }
}

void AssetDatabase::Initialize()
{
}

std::filesystem::path AssetDatabase::ResolveAssetPath(const std::string& path)
{
    namespace fs = std::filesystem;
    if (path.empty()) return {};

    const fs::path input(path);
    if (input.is_absolute()) return input.lexically_normal();

    // AssetInfo::Path é relativo ao projeto (ex.: Assets/Textures/parede.jpg).
    // Priorize essa raiz mesmo quando a pasta do executável também contém Assets.
    if (!s_ProjectRoot.empty())
    {
        const fs::path projectCandidate = (s_ProjectRoot / input).lexically_normal();
        std::error_code error;
        if (fs::exists(projectCandidate, error) && !error) return projectCandidate;
    }

    std::error_code error;
    if (fs::exists(input, error) && !error)
        return fs::absolute(input, error).lexically_normal();

    // Mantém o caminho projetado para que a camada chamadora possa emitir
    // diagnóstico com o mesmo valor que foi salvo no material/asset.
    return input.lexically_normal();
}

void AssetDatabase::ScanAssets(const std::string& projectRoot)
{
    namespace fs = std::filesystem;
    const fs::path root = projectRoot.empty() ? fs::current_path() : fs::path(projectRoot);
    s_ProjectRoot = root;
    const fs::path assetsRoot = root / "Assets";

    s_LastScanStats = {};

    if (!fs::exists(assetsRoot))
    {
        s_Assets.clear();
        s_ValidationIssues.clear();
        s_UnusedAssets.clear();
        return;
    }
    std::vector<AssetInfo> scannedAssets;

    for (const auto& entry : fs::recursive_directory_iterator(assetsRoot))
    {
        if (!entry.is_regular_file())
            continue;

        AssetInfo asset;

        std::error_code relativeError;
        // No Windows, generic_string() usa a página de código local e pode
        // produzir bytes inválidos para o JSON quando o nome contém acentos.
        // u8string() garante que o banco receba UTF-8 válido.
        asset.Path = NormalizeAssetPath(PathToUTF8(fs::relative(entry.path(), root, relativeError)));
        if (relativeError || asset.Path.empty())
            asset.Path = NormalizeAssetPath(PathToUTF8(entry.path()));
        asset.Name = PathToUTF8(entry.path().stem());
        asset.LastWriteTime = LastWriteTimeValue(entry.path());
        std::error_code sizeError;
        asset.FileSize = entry.file_size(sizeError);
        if (sizeError) asset.FileSize = 0;
        bool found = false;

        for (const auto& oldAsset : s_Assets)
        {
            if (oldAsset.Path == asset.Path)
            {
                asset.GUID = oldAsset.GUID;
                found = true;
                break;
            }
        }

        if (!found)
        {
            asset.GUID = GUID::Generate();
        }

        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        bool validAsset = true;

        if (extension == ".png" ||
            extension == ".jpg" ||
            extension == ".jpeg" ||
            extension == ".bmp" ||
            extension == ".tga" ||
            extension == ".hdr" ||
            extension == ".dds" ||
            extension == ".webp")
        {
            asset.Type = AssetType::Texture;
        }
        else if (extension == ".mesh2d" ||
            extension == ".obj" ||
            extension == ".fbx" ||
            extension == ".gltf" ||
            extension == ".glb")
        {
            asset.Type = AssetType::Mesh;
        }
        else if (extension == ".prefab")
        {
            asset.Type = AssetType::Prefab;
        }
        else if (extension == ".scene")
        {
            asset.Type = AssetType::Scene;
        }
        else if (extension == ".material" ||
            extension == ".mat")
        {
            asset.Type = AssetType::Material;
        }
        else if (extension == ".wav" ||
            extension == ".mp3" ||
            extension == ".ogg" ||
            extension == ".flac")
        {
            asset.Type = AssetType::Audio;
        }
        else if (extension == ".cs" ||
            extension == ".lua" ||
            extension == ".py")
        {
            asset.Type = AssetType::Script;
        }
        else
        {
            validAsset = false;
        }

       

        if (!validAsset)
        {
            continue;
        }
        asset.Dependencies = ReadAssetDependencies(entry.path(), root);
        scannedAssets.push_back(asset);
    }

    s_Assets = scannedAssets;
    s_LastScanStats.Total = s_Assets.size();
    for (const auto& asset : s_Assets)
    {
        switch (asset.Type)
        {
        case AssetType::Texture: ++s_LastScanStats.Textures; break;
        case AssetType::Mesh: ++s_LastScanStats.Meshes; break;
        case AssetType::Material: ++s_LastScanStats.Materials; break;
        case AssetType::Audio: ++s_LastScanStats.Audio; break;
        case AssetType::Scene: ++s_LastScanStats.Scenes; break;
        case AssetType::Script: ++s_LastScanStats.Scripts; break;
        case AssetType::Prefab: ++s_LastScanStats.Prefabs; break;
        default: break;
        }
    }
    Validate(root.string());
    Save(root.string());
}

const std::vector<AssetInfo>& AssetDatabase::GetAssets()
{
    return s_Assets;
}

const AssetInfo* AssetDatabase::FindByGUID(const std::string& guid)
{
    for (const auto& asset : s_Assets)
        if (asset.GUID == guid) return &asset;
    return nullptr;
}

const AssetInfo* AssetDatabase::FindByPath(const std::string& path)
{
    const std::string requested = NormalizeAssetPath(
        std::filesystem::path(path).lexically_normal().generic_string());
    for (const auto& asset : s_Assets)
    {
        if (NormalizeAssetPath(asset.Path) == requested)
            return &asset;
    }
    return nullptr;
}

const AssetScanStats& AssetDatabase::GetLastScanStats()
{
    return s_LastScanStats;
}

const std::vector<AssetValidationIssue>& AssetDatabase::GetValidationIssues()
{
    return s_ValidationIssues;
}

const std::vector<std::string>& AssetDatabase::GetUnusedAssets()
{
    return s_UnusedAssets;
}

void AssetDatabase::Validate(const std::string& projectRoot)
{
    namespace fs = std::filesystem;
    const fs::path root = projectRoot.empty()
        ? (s_ProjectRoot.empty() ? fs::current_path() : s_ProjectRoot)
        : fs::path(projectRoot);
    s_ProjectRoot = root;
    s_ValidationIssues.clear();
    s_UnusedAssets.clear();

    std::map<std::string, std::string> guidOwners;
    std::vector<std::string> referencedPaths;
    for (const auto& asset : s_Assets)
    {
        if (asset.GUID.empty())
        {
            s_ValidationIssues.push_back({ asset.Path, "Asset sem GUID estável.", true });
        }
        else if (const auto it = guidOwners.find(asset.GUID); it != guidOwners.end())
        {
            s_ValidationIssues.push_back({ asset.Path,
                "GUID duplicado; também usado por " + it->second + ".", true });
        }
        else
        {
            guidOwners.emplace(asset.GUID, asset.Path);
        }

        const fs::path assetFile = root / fs::path(asset.Path);
        if (!fs::exists(assetFile))
        {
            s_ValidationIssues.push_back({ asset.Path, "Arquivo não encontrado no projeto.", true });
            continue;
        }

        for (const std::string& dependency : asset.Dependencies)
        {
            referencedPaths.push_back(NormalizeAssetPath(dependency));
            if (dependency.rfind("Library/", 0) == 0)
                continue;
            const fs::path dependencyPath = root / fs::path(dependency);
            if (!fs::exists(dependencyPath))
            {
                s_ValidationIssues.push_back({ asset.Path,
                    "Dependência ausente: " + dependency + ".", true });
            }
        }
    }

    for (const auto& asset : s_Assets)
    {
        // Cenas, scripts e prefabs podem ser pontos de entrada ou referências
        // externas; não são classificados como “não usados” automaticamente.
        if (asset.Type == AssetType::Scene || asset.Type == AssetType::Script ||
            asset.Type == AssetType::Prefab)
            continue;
        if (std::find(referencedPaths.begin(), referencedPaths.end(),
                      NormalizeAssetPath(asset.Path)) == referencedPaths.end())
            s_UnusedAssets.push_back(asset.Path);
    }
}

bool AssetDatabase::ReimportAsset(const std::string& path, const std::string& projectRoot)
{
    namespace fs = std::filesystem;
    const fs::path root = projectRoot.empty()
        ? (s_ProjectRoot.empty() ? fs::current_path() : s_ProjectRoot)
        : fs::path(projectRoot);
    const fs::path absolute = root / fs::path(path);
    if (!fs::exists(absolute) || !fs::is_regular_file(absolute))
        return false;
    // A descoberta é determinística e preserva GUID pelo caminho.
    ScanAssets(root.string());
    return FindByPath(path) != nullptr;
}

std::string AssetDatabase::GetGUIDForPath(const std::string& path)
{
    if (path.empty())
        return {};

    const std::string requested = std::filesystem::path(path).lexically_normal().generic_string();
    for (const auto& asset : s_Assets)
    {
        const std::string candidate = std::filesystem::path(asset.Path)
            .lexically_normal().generic_string();
        if (candidate == requested)
            return asset.GUID;
    }
    return {};
}

static std::string SanitizeToUTF8(const std::string& input)
{
    // Caminhos e nomes vindos de std::filesystem já são UTF-8 nesta camada.
    // Re-encodá-los byte a byte corrompia nomes como "Sem título".
    return input;
}

void AssetDatabase::Save(const std::string& projectRoot)
{
    try
    {
        const std::filesystem::path projectRootPath = projectRoot.empty()
            ? (s_ProjectRoot.empty() ? std::filesystem::current_path() : s_ProjectRoot)
            : std::filesystem::path(projectRoot);
        s_ProjectRoot = projectRootPath;
        const std::filesystem::path library = projectRootPath / "Library";
        std::filesystem::create_directories(library);
        const std::filesystem::path databasePath = library / "AssetDatabase.json";
        const std::filesystem::path temporaryPath = databasePath.string() + ".tmp";
        const std::filesystem::path backupPath = databasePath.string() + ".bak";
        json root = json::object();
        root["Assets"] = json::array();

        for (const auto& asset : s_Assets)
        {
            json item;

            item["GUID"] = asset.GUID;
            item["Name"] = SanitizeToUTF8(asset.Name);
            item["Path"] = SanitizeToUTF8(asset.Path);
            item["LastWriteTime"] = asset.LastWriteTime;
            item["FileSize"] = asset.FileSize;
            item["Dependencies"] = asset.Dependencies;

            switch (asset.Type)
            {
            case AssetType::Texture:
                item["Type"] = "Texture";
                break;

            case AssetType::Mesh:
                item["Type"] = "Mesh";
                break;

            case AssetType::Material:
                item["Type"] = "Material";
                break;

            case AssetType::Prefab:
                item["Type"] = "Prefab";
                break;

            case AssetType::Scene:
                item["Type"] = "Scene";
                break;

            case AssetType::Audio:
                item["Type"] = "Audio";
                break;

            case AssetType::Script:
                item["Type"] = "Script";
                break;

            default:
                item["Type"] = "Unknown";
                break;
            }

            root["Assets"].push_back(item);
        }

        std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            std::cerr << "[AssetDatabase] Nao foi possivel abrir arquivo temporario para salvar: "
                      << databasePath.string() << std::endl;
            return;
        }
        file << root.dump(4) << '\n';
        file.flush();
        if (!file.good())
        {
            file.close();
            std::error_code cleanupError;
            std::filesystem::remove(temporaryPath, cleanupError);
            std::cerr << "[AssetDatabase] Falha ao gravar banco temporario: "
                      << databasePath.string() << std::endl;
            return;
        }
        file.close();

        std::error_code error;
        const bool hadOriginal = std::filesystem::is_regular_file(databasePath, error) && !error;
        error.clear();
        if (hadOriginal)
        {
            std::filesystem::copy_file(databasePath, backupPath,
                std::filesystem::copy_options::overwrite_existing, error);
            if (error)
            {
                std::filesystem::remove(temporaryPath, error);
                std::cerr << "[AssetDatabase] Falha ao criar backup do banco: "
                          << databasePath.string() << std::endl;
                return;
            }
            error.clear();
            std::filesystem::remove(databasePath, error);
            if (error)
            {
                std::filesystem::remove(temporaryPath, error);
                std::cerr << "[AssetDatabase] Falha ao preparar substituicao do banco: "
                          << databasePath.string() << std::endl;
                return;
            }
        }

        std::filesystem::rename(temporaryPath, databasePath, error);
        if (error)
        {
            std::error_code restoreError;
            if (hadOriginal && std::filesystem::is_regular_file(backupPath, restoreError))
                std::filesystem::copy_file(backupPath, databasePath,
                    std::filesystem::copy_options::overwrite_existing, restoreError);
            std::filesystem::remove(temporaryPath, restoreError);
            std::cerr << "[AssetDatabase] Falha ao ativar novo banco: "
                      << databasePath.string() << std::endl;
            return;
        }
        std::cout << "AssetDatabase salvo!" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cout << "[AssetDatabase] Erro ao salvar banco de assets: " << e.what() << std::endl;
    }
}

void AssetDatabase::Load(const std::string& projectRoot)
{
    s_Assets.clear();
    s_LastScanStats = {};
    s_ValidationIssues.clear();
    s_UnusedAssets.clear();

    try
    {
        const std::filesystem::path projectRootPath = projectRoot.empty()
            ? (s_ProjectRoot.empty() ? std::filesystem::current_path() : s_ProjectRoot)
            : std::filesystem::path(projectRoot);
        s_ProjectRoot = projectRootPath;
        const std::filesystem::path databasePath =
            projectRootPath / "Library" / "AssetDatabase.json";
        const std::filesystem::path backupPath = databasePath.string() + ".bak";
        auto readDatabase = [](const std::filesystem::path& path,
                               std::vector<AssetInfo>& loadedAssets) -> bool
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) return false;
            try
            {
                json root;
                file >> root;
                if (!root.is_object() || !root.contains("Assets") ||
                    !root["Assets"].is_array())
                    return false;
                std::vector<AssetInfo> parsed;
                parsed.reserve(root["Assets"].size());
                for (const auto& item : root["Assets"])
                {
                    if (!item.is_object()) return false;
                    AssetInfo asset;
                    if (item.contains("GUID")) asset.GUID = item["GUID"].get<std::string>();
                    if (item.contains("Name")) asset.Name = item["Name"].get<std::string>();
                    if (item.contains("Path")) asset.Path = item["Path"].get<std::string>();
                    if (item.contains("LastWriteTime")) asset.LastWriteTime = item["LastWriteTime"].get<uint64_t>();
                    if (item.contains("FileSize")) asset.FileSize = item["FileSize"].get<uintmax_t>();
                    if (item.contains("Dependencies") && item["Dependencies"].is_array())
                        asset.Dependencies = item["Dependencies"].get<std::vector<std::string>>();

                    const std::string type = item.value("Type", "Unknown");
                    if (type == "Texture") asset.Type = AssetType::Texture;
                    else if (type == "Mesh") asset.Type = AssetType::Mesh;
                    else if (type == "Material") asset.Type = AssetType::Material;
                    else if (type == "Prefab") asset.Type = AssetType::Prefab;
                    else if (type == "Scene") asset.Type = AssetType::Scene;
                    else if (type == "Audio") asset.Type = AssetType::Audio;
                    else if (type == "Script") asset.Type = AssetType::Script;
                    else asset.Type = AssetType::Unknown;
                    parsed.push_back(std::move(asset));
                }
                loadedAssets = std::move(parsed);
                return true;
            }
            catch (const std::exception& exception)
            {
                std::cerr << "[AssetDatabase] Banco invalido em " << path.string()
                          << ": " << exception.what() << std::endl;
                return false;
            }
        };

        std::vector<AssetInfo> loadedAssets;
        bool loadedFromBackup = false;
        if (!readDatabase(databasePath, loadedAssets))
        {
            if (readDatabase(backupPath, loadedAssets))
            {
                loadedFromBackup = true;
                std::error_code restoreError;
                std::filesystem::copy_file(backupPath, databasePath,
                    std::filesystem::copy_options::overwrite_existing, restoreError);
                if (restoreError)
                    std::cerr << "[AssetDatabase] Banco carregado do backup, mas o arquivo principal nao foi reparado: "
                              << restoreError.message() << std::endl;
            }
            else
            {
                std::cout << "Nenhum AssetDatabase valido encontrado.\n";
                return;
            }
        }
        s_Assets = std::move(loadedAssets);
        s_LastScanStats.Total = s_Assets.size();
        for (const AssetInfo& asset : s_Assets)
        {
            switch (asset.Type)
            {
            case AssetType::Texture: ++s_LastScanStats.Textures; break;
            case AssetType::Mesh: ++s_LastScanStats.Meshes; break;
            case AssetType::Material: ++s_LastScanStats.Materials; break;
            case AssetType::Audio: ++s_LastScanStats.Audio; break;
            case AssetType::Scene: ++s_LastScanStats.Scenes; break;
            case AssetType::Script: ++s_LastScanStats.Scripts; break;
            case AssetType::Prefab: ++s_LastScanStats.Prefabs; break;
            default: break;
            }
        }
        Validate(projectRootPath.string());
        std::cout << "AssetDatabase carregado: " << s_Assets.size() << " assets"
                  << (loadedFromBackup ? " (backup recuperado)" : "") << ".\n";
    }
    catch (const std::exception& e)
    {
        std::cout << "[AssetDatabase] Erro ao carregar banco de assets: " << e.what() << std::endl;
    }
}

int AssetDatabase::RunRegressionTests()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
        ("MaraeAssetDatabaseTest_" + std::to_string(
            static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count())));
    std::error_code error;
    fs::create_directories(root / "Assets" / "Scenes", error);
    if (error) return 1;

    auto check = [](bool condition, const char* name, int& failures)
    {
        std::cout << (condition ? "[AssetTest] PASS: " : "[AssetTest] FAIL: ")
                  << name << std::endl;
        if (!condition) ++failures;
    };

    int failures = 0;
    {
        std::ofstream texture(root / "Assets" / "icon.png", std::ios::binary);
        texture << "fake texture";
        std::ofstream scene(root / "Assets" / "Scenes" / "Main.scene");
        scene << "{\"mesh\":\"Assets/Missing.obj\"}";
    }
    ScanAssets(root.string());
    check(GetAssets().size() == 2, "descobre assets suportados", failures);
    const std::string guid = GetGUIDForPath("Assets/icon.png");
    check(!guid.empty(), "gera GUID para asset", failures);
    ScanAssets(root.string());
    check(GetGUIDForPath("Assets/icon.png") == guid, "preserva GUID ao reescanear", failures);
    check(!GetValidationIssues().empty(), "detecta dependencia quebrada", failures);
    check(std::find(GetUnusedAssets().begin(), GetUnusedAssets().end(), "Assets/icon.png") !=
          GetUnusedAssets().end(), "lista asset não usado", failures);
    check(ReimportAsset("Assets/icon.png", root.string()), "reimporta asset existente", failures);

    Load(root.string());
    check(GetGUIDForPath("Assets/icon.png") == guid,
          "preserva GUID apos reiniciar e carregar o banco", failures);

    const fs::path databasePath = root / "Library" / "AssetDatabase.json";
    const fs::path backupPath = databasePath.string() + ".bak";
    fs::copy_file(databasePath, backupPath, fs::copy_options::overwrite_existing, error);
    {
        std::ofstream corrupted(databasePath, std::ios::binary | std::ios::trunc);
        corrupted << "{ invalid json";
    }
    Load(root.string());
    check(GetGUIDForPath("Assets/icon.png") == guid &&
          std::filesystem::file_size(databasePath) > 0,
          "recupera GUIDs e restaura o banco quando o JSON principal esta corrompido", failures);

    const fs::path emptyProject = root / "EmptyProject";
    fs::create_directories(emptyProject, error);
    Load(emptyProject.string());
    check(GetAssets().empty() && GetValidationIssues().empty() &&
          GetUnusedAssets().empty() && GetLastScanStats().Total == 0,
          "limpa dados de diagnostico ao trocar para projeto sem banco de assets", failures);

    fs::remove_all(root, error);
    s_Assets.clear();
    s_LastScanStats = {};
    s_ValidationIssues.clear();
    s_UnusedAssets.clear();
    std::cout << "[AssetTest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU")
              << " (" << failures << " falha(s))" << std::endl;
    return failures == 0 ? 0 : 1;
}
