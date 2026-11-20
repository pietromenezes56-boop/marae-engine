#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>

enum class AssetType
{
    Unknown,
    Texture,
    Mesh,
    Material,
    Scene,
    Prefab,
    Audio,
    Script
};

struct AssetInfo
{
    std::string GUID;
    std::string Name;
    std::string Path;
    AssetType Type = AssetType::Unknown;
    uint64_t LastWriteTime = 0;
    uintmax_t FileSize = 0;
    std::vector<std::string> Dependencies;
};

struct AssetValidationIssue
{
    std::string AssetPath;
    std::string Message;
    bool Error = false;
};

struct AssetScanStats
{
    size_t Total = 0;
    size_t Textures = 0;
    size_t Meshes = 0;
    size_t Materials = 0;
    size_t Audio = 0;
    size_t Scenes = 0;
    size_t Scripts = 0;
    size_t Prefabs = 0;
};

class AssetDatabase
{
public:

    static void Initialize();

    static void ScanAssets(const std::string& projectRoot = {});

    // Resolve caminhos relativos de Assets a partir da raiz do projeto ativo,
    // não do diretório de trabalho do executável/editor.
    static std::filesystem::path ResolveAssetPath(const std::string& path);

    static const std::vector<AssetInfo>& GetAssets();

    static const AssetInfo* FindByGUID(const std::string& guid);
    static const AssetInfo* FindByPath(const std::string& path);
    static const AssetScanStats& GetLastScanStats();
    static const std::vector<AssetValidationIssue>& GetValidationIssues();
    static const std::vector<std::string>& GetUnusedAssets();
    static void Validate(const std::string& projectRoot = {});
    static bool ReimportAsset(const std::string& path, const std::string& projectRoot = {});

    // Testa descoberta, GUID estável, dependência quebrada e reimportação.
    static int RunRegressionTests();

    // Retorna o ID estável do asset usando o caminho relativo ao projeto.
    // Scripts e componentes podem referenciar o asset sem depender do nome.
    static std::string GetGUIDForPath(const std::string& path);

    static void Save(const std::string& projectRoot = {});

    static void Load(const std::string& projectRoot = {});

    

private:

    static std::vector<AssetInfo> s_Assets;
    static AssetScanStats s_LastScanStats;
    static std::vector<AssetValidationIssue> s_ValidationIssues;
    static std::vector<std::string> s_UnusedAssets;
   
};
