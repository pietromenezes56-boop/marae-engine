#include "../../../Engine/Localization/Localization.h"
#include "AssetBrowserPanel.h"
#include "../../../Engine/Localization/Localization.h"
#include "../../../Core/Application.h"
#include "../../../Scene/Entity/Entity.h"
#include "../../Thumbnails/ThumbnailManager.h"
#include "../../../Assets/AssetDatabase/AssetDatabase.h"
#include "../../../Assets/MaterialDatabase/MaterialDatabase.h"
#include "../../../Engine/Assets/GUID/GUID.h"
#include "../../../Engine/Assets/ModelImporter/ModelImporter.h"
#include "../../../Engine/Audio/Clip/AudioAssetLibrary.h"
#include "../../../Engine/Audio/Clip/AudioClip.h"
#include "../../../Engine/Renderer/Material/MaterialSerializer.h"
#include "../../../Core/Diagnostics/DiagnosticLog.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GUID Windows_SDK_GUID
#include <windows.h>
#include <commdlg.h>
#undef GUID
#endif

namespace fs = std::filesystem;

namespace
{
    enum class AssetFilter
    {
        All = 0,
        Texture,
        Mesh3D,
        Mesh2D,
        Material,
        Audio,
        Scene,
        Prefab,
        Script
    };

    const char* FilterName(AssetFilter filter)
    {
        switch (filter)
        {
        case AssetFilter::Texture: return "Texturas";
        case AssetFilter::Mesh3D: return "Modelos 3D";
        case AssetFilter::Mesh2D: return "Meshes 2D";
        case AssetFilter::Material: return "Materiais";
        case AssetFilter::Audio: return "Sons";
        case AssetFilter::Scene: return "Cenas";
        case AssetFilter::Prefab: return "Prefabs";
        case AssetFilter::Script: return "Scripts";
        default: return "Todos";
        }
    }

    std::string LowerExtension(const fs::path& path);

    bool HasExtension(const std::string& path, std::string_view extension)
    {
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos || path.size() - dot != extension.size())
            return false;
        for (size_t i = 0; i < extension.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(path[dot + i])) != extension[i])
                return false;
        return true;
    }

    bool MatchesFilter(const AssetInfo& asset, AssetFilter filter)
    {
        if (filter == AssetFilter::All)
            return true;
        if (filter == AssetFilter::Mesh3D)
        {
            return asset.Type == AssetType::Mesh && !HasExtension(asset.Path, ".mesh2d");
        }
        if (filter == AssetFilter::Mesh2D)
        {
            return asset.Type == AssetType::Mesh && HasExtension(asset.Path, ".mesh2d");
        }
        switch (filter)
        {
        case AssetFilter::Texture: return asset.Type == AssetType::Texture;
        case AssetFilter::Material: return asset.Type == AssetType::Material;
        case AssetFilter::Audio: return asset.Type == AssetType::Audio;
        case AssetFilter::Scene: return asset.Type == AssetType::Scene;
        case AssetFilter::Prefab: return asset.Type == AssetType::Prefab;
        case AssetFilter::Script: return asset.Type == AssetType::Script;
        default: return false;
        }
    }

    std::string LowerExtension(const fs::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension;
    }

    const char* AssetTypeLabel(const AssetInfo& asset)
    {
        if (asset.Type == AssetType::Mesh)
            return HasExtension(asset.Path, ".mesh2d") ? "Meshes 2D" : "Modelo 3D";
        switch (asset.Type)
        {
        case AssetType::Texture: return "Textura";
        case AssetType::Material: return "Material";
        case AssetType::Audio: return "Som";
        case AssetType::Scene: return "Cena";
        case AssetType::Prefab: return "Prefab";
        case AssetType::Script: return "Script";
        default: return "Arquivo";
        }
    }

    fs::path ImportFolder(AssetFilter filter, const fs::path& projectRoot, const fs::path& source)
    {
        std::string folder;
        switch (filter)
        {
        case AssetFilter::Texture: folder = "Textures"; break;
        case AssetFilter::Mesh2D: folder = "Meshes2D"; break;
        case AssetFilter::Mesh3D: folder = "Models"; break;
        case AssetFilter::Material: folder = "Materials"; break;
        case AssetFilter::Audio: folder = "Audio"; break;
        case AssetFilter::Scene: folder = "Scenes"; break;
        case AssetFilter::Prefab: folder = "Prefabs"; break;
        case AssetFilter::Script: folder = "Scripts"; break;
        default:
        {
            const std::string ext = LowerExtension(source);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" || ext == ".hdr" || ext == ".dds" || ext == ".webp") folder = "Textures";
            else if (ext == ".mesh2d") folder = "Meshes2D";
            else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") folder = "Models";
            else if (ext == ".mat" || ext == ".material") folder = "Materials";
            else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") folder = "Audio";
            else if (ext == ".scene") folder = "Scenes";
            else if (ext == ".prefab") folder = "Prefabs";
            else folder = "Imported";
            break;
        }
        }
        return projectRoot / "Assets" / folder;
    }

    bool ImportAssetForFilter(AssetFilter filter, const std::string& projectRoot)
    {
#ifdef _WIN32
        char filename[32768] = {};
        const char* dialogFilter =
            "Todos suportados\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.hdr;*.dds;*.webp;*.obj;*.fbx;*.gltf;*.glb;*.mesh2d;*.mat;*.material;*.wav;*.mp3;*.ogg;*.flac;*.scene;*.prefab;*.cs;*.lua;*.py\0"
            "Todos os arquivos\0*.*\0\0";
        switch (filter)
        {
        case AssetFilter::Texture: dialogFilter = "Texturas\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.hdr;*.dds;*.webp\0\0"; break;
        case AssetFilter::Mesh3D: dialogFilter = "Modelos 3D\0*.obj;*.fbx;*.gltf;*.glb\0\0"; break;
        case AssetFilter::Mesh2D: dialogFilter = "Meshes 2D Marae\0*.mesh2d\0\0"; break;
        case AssetFilter::Material: dialogFilter = "Materiais\0*.mat;*.material\0\0"; break;
        case AssetFilter::Audio: dialogFilter = "Áudio\0*.wav;*.mp3;*.ogg;*.flac\0\0"; break;
        case AssetFilter::Scene: dialogFilter = "Cenas\0*.scene\0\0"; break;
        case AssetFilter::Prefab: dialogFilter = "Prefabs\0*.prefab\0\0"; break;
        case AssetFilter::Script: dialogFilter = "Scripts\0*.cs;*.lua;*.py\0\0"; break;
        default: break;
        }

        OPENFILENAMEA dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.lpstrFilter = dialogFilter;
        dialog.lpstrFile = filename;
        dialog.nMaxFile = static_cast<DWORD>(sizeof(filename));
        dialog.lpstrTitle = "Importar asset para a categoria selecionada";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        if (!GetOpenFileNameA(&dialog)) return false;

        const fs::path source(filename);
        const fs::path root = projectRoot.empty() ? fs::current_path() : fs::path(projectRoot);
        const fs::path destinationFolder = ImportFolder(filter, root, source);
        std::error_code error;
        fs::create_directories(destinationFolder, error);
        if (error) return false;

        fs::path destination = destinationFolder / source.filename();
        int suffix = 1;
        while (fs::exists(destination))
            destination = destinationFolder / (source.stem().string() + "_" + std::to_string(suffix++) + source.extension().string());
        fs::copy_file(source, destination, fs::copy_options::none, error);
        return !error;
#else
        (void)filter; (void)projectRoot;
        return false;
#endif
    }

    bool ContainsInsensitive(std::string_view value, std::string_view lowerQuery)
    {
        if (lowerQuery.empty())
            return true;
        if (lowerQuery.size() > value.size())
            return false;

        for (size_t start = 0; start <= value.size() - lowerQuery.size(); ++start)
        {
            size_t i = 0;
            for (; i < lowerQuery.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(value[start + i])) != lowerQuery[i])
                    break;
            if (i == lowerQuery.size())
                return true;
        }
        return false;
    }

    const char* AssetIcon(AssetType type)
    {
        switch (type)
        {
        case AssetType::Texture: return "IMG";
        case AssetType::Mesh: return "MESH";
        case AssetType::Material: return "MAT";
        case AssetType::Audio: return "SND";
        case AssetType::Scene: return "SCN";
        case AssetType::Prefab: return "PFB";
        case AssetType::Script: return "CS";
        default: return "FILE";
        }
    }

    void DrawAssetDetails(Application& app, const AssetInfo* asset)
    {
        if (!asset)
            return;

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.45f, 0.80f, 1.0f, 1.0f), "Asset selecionado");
        ImGui::Text(UI("Nome: %s"), asset->Name.c_str());
        ImGui::Text(UI("Tipo: %s"), AssetTypeLabel(*asset));
        ImGui::TextWrapped(UI("Caminho: %s"), asset->Path.c_str());
        ImGui::Text(UI("Tamanho: %llu bytes"),
                    static_cast<unsigned long long>(asset->FileSize));
        ImGui::Text(UI("Dependências: %zu"), asset->Dependencies.size());
        if (!asset->GUID.empty())
            ImGui::TextWrapped(UI("GUID: %s"), asset->GUID.c_str());

        if (asset->Type == AssetType::Material)
        {
            Material* material = MaterialDatabase::GetMaterialByGUID(asset->GUID);
            if (!material)
                material = MaterialDatabase::GetMaterialByName(asset->Name);
            if (material)
            {
                ImGui::Separator();
                ImGui::Text(UI("Material nativo"));
                ImGui::Text(UI("Shader/Tipo: %d"), static_cast<int>(material->Type));
                ImGui::ColorButton("##AssetMaterialColor", ImVec4(
                    material->Color[0], material->Color[1], material->Color[2], material->Color[3]),
                    ImGuiColorEditFlags_NoTooltip, ImVec2(34.0f, 20.0f));
                ImGui::SameLine();
                ImGui::Text(UI("Cor base"));
                if (!material->TexturePath.empty())
                    ImGui::TextWrapped(UI("Textura: %s"), material->TexturePath.c_str());
            }
        }
        else if (asset->Type == AssetType::Audio)
        {
            auto clip = AudioAssetLibrary::Get().FindByPath(asset->Path);
            if (clip)
            {
                const auto& meta = clip->GetMetadata();
                ImGui::Separator();
                ImGui::Text(UI("Som nativo"));
                ImGui::Text(UI("Formato: %s | %.2f s"), AudioClip::FormatName(meta.Format), meta.DurationSeconds);
                ImGui::Text(UI("Canais: %u | Sample rate: %u Hz"), meta.Channels, meta.SampleRate);
            }
            if (ImGui::Button(UI("Reproduzir preview##AssetAudioPreview")))
                app.GetAudioSystem().PlayPreviewClip(asset->Path);
            ImGui::SameLine();
            if (ImGui::Button(UI("Parar##AssetAudioStop")))
                app.StopAudioPreview();
        }
    }
}

void AssetBrowserPanel::Draw(Application& app)
{
    std::string title = std::string(L("panel.assets")) + "###Assets";
    ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);

    static AssetFilter filter = AssetFilter::All;
    static char searchBuffer[128] = {};
    static std::string scannedProjectRoot;
    static bool refreshRequested = false;
    static bool validateRequested = false;
    static bool includeSubfolders = false;

    const std::string projectRoot = app.GetProjectConfig().ProjectPath;
    if (m_ProjectRoot != projectRoot)
    {
        m_ProjectRoot = projectRoot;
        m_CurrentPath.clear();
        m_SelectedAsset = nullptr;
        app.SetSelectedAsset(nullptr);
    }
    if (ImGui::Button(UI("Atualizar biblioteca##AssetRefresh")))
        refreshRequested = true;
    ImGui::SameLine();
    if (ImGui::Button(UI("Validar##AssetValidate")))
        validateRequested = true;
    ImGui::SameLine();
    if (ImGui::Button(UI("Importar asset##AssetImport")))
    {
        if (ImportAssetForFilter(filter, projectRoot))
            refreshRequested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(UI("Novo script C###AssetNewScript")))
    {
        const fs::path scriptFolder = projectRoot.empty()
            ? fs::path("Assets") / "Scripts"
            : fs::path(projectRoot) / "Assets" / "Scripts";
        std::error_code createError;
        fs::create_directories(scriptFolder, createError);
        if (!createError)
        {
            std::string className = "CarController";
            fs::path scriptPath = scriptFolder / (className + ".cs");
            for (int suffix = 1; fs::exists(scriptPath); ++suffix)
            {
                className = "CarController_" + std::to_string(suffix);
                scriptPath = scriptFolder / (className + ".cs");
            }

            std::ofstream scriptFile(scriptPath, std::ios::binary);
            if (scriptFile)
            {
                scriptFile << "using MaraeEngine;\n\n"
                    << "// Anexe este script ao chassi. W/S aceleram e freiam; A/D viram.\n"
                    << "public sealed class " << className << " : ScriptBehaviour\n"
                    << "{\n"
                    << "    public float Speed = 8f;\n"
                    << "    public float TurnSpeed = 90f;\n\n"
                    << "    public override void OnUpdate(float deltaTime)\n"
                    << "    {\n"
                    << "        if (transform is null) return;\n"
                    << "        float throttle = 0f;\n"
                    << "        if (Input.IsKeyDown(KeyCode.W)) throttle += 1f;\n"
                    << "        if (Input.IsKeyDown(KeyCode.S)) throttle -= 1f;\n"
                    << "        float steering = 0f;\n"
                    << "        if (Input.IsKeyDown(KeyCode.A)) steering -= 1f;\n"
                    << "        if (Input.IsKeyDown(KeyCode.D)) steering += 1f;\n\n"
                    << "        if (throttle != 0f)\n"
                    << "            transform.Translate(0f, 0f, throttle * Speed * deltaTime, Space.Self);\n"
                    << "        if (steering != 0f)\n"
                    << "            transform.Rotate(0f, steering * TurnSpeed * deltaTime, 0f, Space.World);\n"
                    << "    }\n"
                    << "}\n";
                if (scriptFile)
                {
                    refreshRequested = true;
                    const fs::path relativePath = fs::relative(scriptPath, fs::path(projectRoot), createError);
                    app.OpenScriptInInternalEditor(createError ? scriptPath.string() : relativePath.generic_string());
                }
            }
        }
        if (createError)
            ImGui::SetTooltip(UI("Não foi possível criar Assets/Scripts: %s"), createError.message().c_str());
    }
    if (filter != AssetFilter::All)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("Importação: %s", FilterName(filter));
    }
    ImGui::SameLine();
    if (ImGui::Button(UI("Novo material##AssetNewMaterial")))
    {
        const fs::path materialFolder = projectRoot.empty()
            ? fs::path("Assets") / "Materials"
            : fs::path(projectRoot) / "Assets" / "Materials";
        fs::create_directories(materialFolder);

        fs::path materialPath = materialFolder / "NewMaterial.mat";
        int suffix = 1;
        while (fs::exists(materialPath))
            materialPath = materialFolder / ("NewMaterial_" + std::to_string(suffix++) + ".mat");

        Material material;
        material.Name = materialPath.stem().string();
        material.GUID = GUID::Generate();
        material.Color[0] = 1.0f;
        material.Color[1] = 1.0f;
        material.Color[2] = 1.0f;
        material.Color[3] = 1.0f;
        MaterialDatabase::RegisterMaterial(material);
        MaterialSerializer::Save(material, materialPath.string());
        refreshRequested = true;
    }

    if (scannedProjectRoot != projectRoot || refreshRequested)
    {
        m_SelectedAsset = nullptr;
        app.SetSelectedAsset(nullptr);
        AssetDatabase::ScanAssets(projectRoot);
        MaterialDatabase::ScanMaterials();
        const fs::path audioFolder = projectRoot.empty()
            ? fs::path("Assets") : fs::path(projectRoot) / "Assets";
        AudioAssetLibrary::Get().Clear();
        if (!projectRoot.empty())
            AudioAssetLibrary::Get().Load(projectRoot);
        AudioAssetLibrary::Get().ScanDirectory(audioFolder.string(), projectRoot);
        if (!projectRoot.empty())
            AudioAssetLibrary::Get().Save(projectRoot);
        scannedProjectRoot = projectRoot;
        refreshRequested = false;
        validateRequested = false;
    }
    if (validateRequested)
    {
        AssetDatabase::Validate(projectRoot);
        const auto& issues = AssetDatabase::GetValidationIssues();
        MaraeDiagnostics::DiagnosticLog::Get().Add(
            issues.empty() ? MaraeDiagnostics::LogLevel::Success : MaraeDiagnostics::LogLevel::Warning,
            issues.empty() ? "Asset Database validado sem problemas."
                            : "Asset Database encontrou " + std::to_string(issues.size()) + " problema(s).",
            "Assets", {}, 0, 0, {}, projectRoot);
        validateRequested = false;
    }

    ImGui::TextColored(ImVec4(0.45f, 0.80f, 1.0f, 1.0f), UI("Recursos do projeto"));
    ImGui::TextDisabled(UI("Assets do jogo: arquivos, materiais, cenas, scripts e dependências."));
    const AssetScanStats& stats = AssetDatabase::GetLastScanStats();
    ImGui::TextDisabled(UI("%zu assets | Texturas %zu | Meshes %zu | Cenas %zu | Scripts %zu"),
                        stats.Total, stats.Textures, stats.Meshes, stats.Scenes, stats.Scripts);

    const auto& validationIssues = AssetDatabase::GetValidationIssues();
    if (validationIssues.empty())
    {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f),
                           UI("Banco validado: nenhuma referência quebrada."));
    }
    else if (ImGui::TreeNode(UI("Problemas encontrados##AssetIssues")))
    {
        for (const auto& issue : validationIssues)
        {
            ImGui::PushStyleColor(ImGuiCol_Text,
                issue.Error ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                            : ImVec4(1.0f, 0.8f, 0.3f, 1.0f));
            ImGui::TextWrapped("%s: %s", issue.AssetPath.c_str(), issue.Message.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::TreePop();
    }
    const auto& unusedAssets = AssetDatabase::GetUnusedAssets();
    if (!unusedAssets.empty() && ImGui::TreeNode(UI("Assets não usados##AssetUnused")))
    {
        ImGui::TextDisabled(UI("Isto é apenas uma sugestão; nada será removido automaticamente."));
        for (const auto& path : unusedAssets)
            ImGui::BulletText("%s", path.c_str());
        ImGui::TreePop();
    }

    // A navegação é apenas visual: os arquivos continuam no caminho Assets/ e
    // mantêm os mesmos GUIDs usados por cenas, prefabs e scripts.
    const fs::path assetsRoot = projectRoot.empty() ? fs::path{} : fs::path(projectRoot) / "Assets";
    fs::path currentFolder = assetsRoot / m_CurrentPath;
    std::error_code folderError;
    if (!projectRoot.empty() && !fs::is_directory(currentFolder, folderError))
    {
        m_CurrentPath.clear();
        currentFolder = assetsRoot;
    }

    ImGui::TextDisabled(UI("Localização"));
    if (ImGui::SmallButton(UI("Assets##AssetRoot")))
        m_CurrentPath.clear();
    fs::path breadcrumb;
    fs::path selectedBreadcrumb = m_CurrentPath;
    bool breadcrumbSelected = false;
    for (const auto& part : m_CurrentPath)
    {
        if (part.empty() || part == ".")
            continue;
        ImGui::SameLine();
        ImGui::TextDisabled("/");
        breadcrumb /= part;
        ImGui::SameLine();
        const std::string label = part.string() + "##AssetCrumb_" + breadcrumb.generic_string();
        if (ImGui::SmallButton(label.c_str()))
        {
            selectedBreadcrumb = breadcrumb;
            breadcrumbSelected = true;
        }
    }
    if (breadcrumbSelected)
        m_CurrentPath = selectedBreadcrumb;

    ImGui::BeginChild("##AssetFolderStrip", ImVec2(0.0f, 58.0f), true);
    if (!m_CurrentPath.empty())
    {
        if (ImGui::SmallButton(UI("..##AssetParentFolder")))
            m_CurrentPath = m_CurrentPath.parent_path();
        ImGui::SameLine();
    }
    size_t folderCount = 0;
    folderError.clear();
    if (!projectRoot.empty() && fs::is_directory(currentFolder, folderError))
    {
        for (fs::directory_iterator it(currentFolder, folderError), end; !folderError && it != end; it.increment(folderError))
        {
            std::error_code entryError;
            if (!it->is_directory(entryError) || entryError)
                continue;
            const std::string folderName = it->path().filename().string();
            const std::string folderButton = "[+] " + folderName + "##AssetFolder_" + folderName;
            if (ImGui::SmallButton(folderButton.c_str()))
                m_CurrentPath /= it->path().filename();
            ImGui::SameLine();
            ++folderCount;
        }
    }
    if (folderCount == 0)
        ImGui::TextDisabled(UI("Nenhuma subpasta neste local."));
    ImGui::EndChild();

    ImGui::Checkbox(UI("Incluir subpastas##AssetRecursiveView"), &includeSubfolders);
    ImGui::SameLine();

    const char* filters[] = { "Todos", "Texturas", "Modelos 3D", "Meshes 2D", "Materiais", "Sons", "Cenas", "Prefabs", "Scripts" };
    int filterIndex = static_cast<int>(filter);
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::Combo(UI("Categoria##AssetFilter"), &filterIndex, filters, IM_ARRAYSIZE(filters)))
        filter = static_cast<AssetFilter>(std::clamp(filterIndex, 0, 8));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##AssetSearch", "Buscar por nome ou caminho...",
                             searchBuffer, sizeof(searchBuffer));

    // Normaliza a consulta uma vez por frame. A busca não concatena nome+caminho
    // nem cria cópias temporárias para cada asset do projeto.
    char normalizedSearchBuffer[sizeof(searchBuffer)] = {};
    const size_t searchLength = std::strlen(searchBuffer);
    std::transform(searchBuffer, searchBuffer + searchLength, normalizedSearchBuffer,
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string_view normalizedSearch(normalizedSearchBuffer, searchLength);
    const auto matchesSearch = [normalizedSearch](const AssetInfo& asset)
    {
        return ContainsInsensitive(asset.Name, normalizedSearch) ||
               ContainsInsensitive(asset.Path, normalizedSearch);
    };

    const auto isInCurrentFolder = [this](const AssetInfo& asset)
    {
        fs::path assetPath = fs::path(asset.Path).lexically_normal();
        fs::path relativeToAssets = assetPath.lexically_relative(fs::path("Assets"));
        if (relativeToAssets.empty())
            relativeToAssets = assetPath.filename();
        const fs::path parent = relativeToAssets.parent_path();
        if (includeSubfolders)
        {
            const fs::path relativeParent = parent.lexically_relative(m_CurrentPath);
            if (relativeParent.empty())
                return true;
            return !relativeParent.is_absolute() &&
                   relativeParent.begin()->string() != "..";
        }
        return parent == m_CurrentPath.lexically_normal();
    };

    ImGui::Separator();

    const auto& assets = AssetDatabase::GetAssets();
    const float uiFontSize = ImGui::GetFontSize();
    const float cellSize = std::clamp(uiFontSize * 5.5f, 96.0f, 132.0f);
    const float spacing = std::max(10.0f, uiFontSize * 0.8f);
    const float panelWidth = ImGui::GetContentRegionAvail().x;
    const int columnCount = std::max(1, static_cast<int>(panelWidth / (cellSize + spacing)));

    size_t visibleAssetCount = 0;
    for (const auto& asset : assets)
        if (MatchesFilter(asset, filter) && matchesSearch(asset) && isInCurrentFolder(asset))
            ++visibleAssetCount;
    ImGui::TextDisabled(UI("%s | %zu recurso(s)"), FilterName(filter), visibleAssetCount);
    ImGui::Columns(columnCount, "##NativeAssetGrid", false);

    for (size_t index = 0; index < assets.size(); ++index)
    {
        const AssetInfo& asset = assets[index];
        if (!MatchesFilter(asset, filter) || !matchesSearch(asset) || !isInCurrentFolder(asset))
            continue;

        const bool selected = app.GetSelectedAsset() == &asset;
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.68f, 1.0f));

        const std::string buttonID = "##Asset_" + std::to_string(index);
        const bool thumbnailVisible = ImGui::IsRectVisible(ImVec2(cellSize, cellSize));
        GLuint textureID = asset.Type == AssetType::Texture
            ? (thumbnailVisible
                ? ThumbnailManager::GetTextureThumbnail(asset.Path)
                : ThumbnailManager::GetIcon(AssetType::Texture))
            : ThumbnailManager::GetIcon(asset.Type);

        const bool assetClicked = app.GetWindow().GetGraphicsAPI() == GraphicsAPI::OpenGL
            ? ImGui::ImageButton(buttonID.c_str(), (ImTextureID)(intptr_t)textureID,
                                 ImVec2(cellSize, cellSize), ImVec2(0, 1), ImVec2(1, 0))
            : ImGui::Button(buttonID.c_str(), ImVec2(cellSize, cellSize));
        if (assetClicked)
        {
            m_SelectedAsset = &asset;
            app.SetSelectedAsset(&asset);
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            asset.Type == AssetType::Mesh)
        {
            Entity* imported = ModelImporter::ImportToScene(asset.Path, app.GetScene());
            if (imported)
                app.SetSelectedEntity(static_cast<int>(app.GetScene().GetEntities().size()) - 1);
        }

        if (ImGui::BeginDragDropSource())
        {
            // Não envie AssetInfo diretamente: ele contém std::string e o
            // payload do ImGui é apenas uma cópia de bytes. O objeto copiado
            // ficava com ponteiros internos inválidos ao chegar ao destino.
            ImGui::SetDragDropPayload("ASSET_PATH", asset.Path.c_str(), asset.Path.size() + 1);
            ImGui::Text(UI("%s"), asset.Name.c_str());
            ImGui::TextDisabled("%s", FilterName(filter));
            ImGui::EndDragDropSource();
        }

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
        ImGui::TextColored(ImVec4(0.45f, 0.80f, 1.0f, 1.0f), "%s", AssetIcon(asset.Type));
        ImGui::TextWrapped(UI("%s"), asset.Name.c_str());

        if (selected)
            ImGui::PopStyleColor();
        ImGui::NextColumn();
    }

    ImGui::Columns(1);
    if (m_SelectedAsset)
    {
        const std::string selectedPath = m_SelectedAsset->Path;
        if (ImGui::Button(UI("Reimportar selecionado##AssetReimport")))
        {
            const bool reimported = AssetDatabase::ReimportAsset(selectedPath, projectRoot);
            MaraeDiagnostics::DiagnosticLog::Get().Add(
                reimported ? MaraeDiagnostics::LogLevel::Success : MaraeDiagnostics::LogLevel::Error,
                reimported ? "Asset reimportado: " + selectedPath
                           : "Não foi possível reimportar: " + selectedPath,
                "Assets", selectedPath, 0, 0, {}, projectRoot,
                reimported ? "A biblioteca foi atualizada." :
                             "Confira se o arquivo ainda existe e se o formato é suportado.");
            m_SelectedAsset = nullptr;
            app.SetSelectedAsset(nullptr);
            refreshRequested = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(UI("Releia o asset e valide as dependências."));
    }
    DrawAssetDetails(app, app.GetSelectedAsset());
    ImGui::End();
}

const AssetInfo* AssetBrowserPanel::GetSelectedAsset() const
{
    return m_SelectedAsset;
}
