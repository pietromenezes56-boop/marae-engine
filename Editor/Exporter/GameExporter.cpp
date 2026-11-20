#include "GameExporter.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <set>
#include <chrono>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers internos
// ---------------------------------------------------------------------------
namespace
{
    // Extensões necessárias fora da árvore Assets.
    const std::vector<std::string> k_ScriptExts   = { ".cs" };
    const std::vector<std::string> k_DllExts      = { ".dll" };

    bool HasExtension(const fs::path& p, const std::vector<std::string>& exts)
    {
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        for (const auto& e : exts)
            if (ext == e) return true;
        return false;
    }

    bool IsPathInsideOrEqual(const fs::path& path, const fs::path& directory)
    {
        const fs::path relative = path.lexically_relative(directory);
        if (relative.empty())
            return path == directory;
        const auto first = relative.begin();
        return first == relative.end() || *first != "..";
    }

    bool CopyOne(const fs::path& src, const fs::path& dst,
                 bool overwrite, ExportResult& result,
                 const std::string& tag);

    fs::path FindManagedRuntimeDirectory(const fs::path& runtimeDir)
    {
        std::vector<fs::path> projectRuntimeCandidates;
        std::vector<fs::path> bundledRuntimeCandidates;
        fs::path ancestor = runtimeDir;
        for (int i = 0; i < 8 && ancestor.has_parent_path(); ++i, ancestor = ancestor.parent_path())
        {
            projectRuntimeCandidates.push_back(ancestor / "ScriptRuntime");
            bundledRuntimeCandidates.push_back(ancestor / "Builds" / "Windows" / "MaraeEngine-Editor-Release" / "ScriptRuntime");
            bundledRuntimeCandidates.push_back(ancestor / "MaraeEngine" / "Builds" / "Windows" / "MaraeEngine-Editor-Release" / "ScriptRuntime");
        }
        auto isUsable = [](const fs::path& candidate)
        {
            return fs::is_regular_file(candidate / "MaraeEngine.Scripting.runtimeconfig.json") &&
                   fs::is_regular_file(candidate / "MaraeEngine.Scripting.dll");
        };
        // Prefira o runtime gerado pelo checkout atual. O fallback empacotado
        // pode pertencer a uma instalação antiga e divergir do ABI do editor.
        for (const fs::path& candidate : projectRuntimeCandidates)
            if (isUsable(candidate)) return candidate;
        for (const fs::path& candidate : bundledRuntimeCandidates)
            if (isUsable(candidate)) return candidate;
        return {};
    }

    fs::path FindNetHostLibrary(const fs::path& runtimeDir)
    {
        std::vector<fs::path> candidates = { runtimeDir / "nethost.dll" };
        fs::path ancestor = runtimeDir;
        for (int i = 0; i < 8 && ancestor.has_parent_path(); ++i, ancestor = ancestor.parent_path())
        {
            candidates.push_back(ancestor / "nethost.dll");
            candidates.push_back(ancestor / "Builds" / "Windows" / "MaraeEngine-Editor-Release" / "nethost.dll");
            candidates.push_back(ancestor / "MaraeEngine" / "Builds" / "Windows" / "MaraeEngine-Editor-Release" / "nethost.dll");
        }
        for (const fs::path& candidate : candidates)
            if (fs::is_regular_file(candidate)) return candidate;
        return {};
    }

    fs::path FindBrandingDirectory(const fs::path& runtimeDir)
    {
        std::vector<fs::path> candidates = { runtimeDir / "Editor" / "Branding" };
        fs::path ancestor = runtimeDir;
        for (int i = 0; i < 8 && ancestor.has_parent_path(); ++i, ancestor = ancestor.parent_path())
        {
            candidates.push_back(ancestor / "Editor" / "Branding");
            candidates.push_back(ancestor / "Builds" / "Windows" /
                "MaraeEngine-0.1.0-alpha-Windows-x64" / "MaraeEngine-Windows" / "Editor" / "Branding");
            candidates.push_back(ancestor / "MaraeEngine" / "Builds" / "Windows" /
                "MaraeEngine-0.1.0-alpha-Windows-x64" / "MaraeEngine-Windows" / "Editor" / "Branding");
        }
        for (const fs::path& candidate : candidates)
            if (fs::is_directory(candidate) && fs::is_regular_file(candidate / "MaraeIcon.png"))
                return candidate;
        return {};
    }

    class StagedBuild
    {
    public:
        explicit StagedBuild(fs::path destination)
            : m_destination(std::move(destination))
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            m_staging = m_destination.parent_path() /
                (m_destination.filename().string() + ".marae-staging-" + std::to_string(stamp));
            m_backup = m_destination.parent_path() /
                (m_destination.filename().string() + ".marae-backup-" + std::to_string(stamp));
        }

        ~StagedBuild()
        {
            if (m_ownsStaging)
            {
                std::error_code ignored;
                fs::remove_all(m_staging, ignored);
            }
        }

        void Create()
        {
            fs::create_directories(m_destination.parent_path());
            if (!fs::create_directory(m_staging))
                throw std::runtime_error("Não foi possível criar uma pasta temporária exclusiva para a build.");
            m_ownsStaging = true;
        }

        const fs::path& Path() const { return m_staging; }

        void Commit(bool overwrite, ExportResult& result)
        {
            bool movedPreviousBuild = false;
            if (fs::exists(m_destination))
            {
                if (!overwrite)
                    throw std::runtime_error("A pasta de saída já existe. Ative sobrescrever para publicar uma build limpa.");
                fs::rename(m_destination, m_backup);
                movedPreviousBuild = true;
            }

            try
            {
                fs::rename(m_staging, m_destination);
                m_ownsStaging = false;
            }
            catch (...)
            {
                if (movedPreviousBuild)
                {
                    std::error_code rollbackError;
                    fs::rename(m_backup, m_destination, rollbackError);
                    if (rollbackError)
                        result.Log.push_back("[ERR] A build anterior ficou preservada em " + m_backup.string() +
                                             "; restauração automática falhou: " + rollbackError.message());
                }
                throw;
            }

            if (movedPreviousBuild)
            {
                std::error_code cleanupError;
                fs::remove_all(m_backup, cleanupError);
                if (cleanupError)
                    result.Log.push_back("[WARN] Build publicada, mas não foi possível remover o backup temporário: " +
                                         m_backup.string());
            }
        }

    private:
        fs::path m_destination;
        fs::path m_staging;
        fs::path m_backup;
        bool m_ownsStaging = false;
    };

    void CopyDirectoryContents(const fs::path& sourceDir,
                               const fs::path& destinationDir,
                               bool overwrite,
                               ExportResult& result,
                               const std::string& tag)
    {
        try
        {
            if (!fs::is_directory(sourceDir))
                throw std::runtime_error("Pasta necessária não encontrada: " + sourceDir.string());
            for (const fs::directory_entry& entry : fs::recursive_directory_iterator(
                     sourceDir, fs::directory_options::skip_permission_denied))
            {
                const fs::path relative = fs::relative(entry.path(), sourceDir);
                if (entry.is_directory())
                {
                    fs::create_directories(destinationDir / relative);
                    continue;
                }
                if (entry.is_regular_file())
                    CopyOne(entry.path(), destinationDir / relative, overwrite, result, tag);
            }
        }
        catch (const std::exception& ex)
        {
            if (result.ErrorMessage.empty()) result.ErrorMessage = ex.what();
            result.Log.push_back("[ERR] Copiando pasta " + sourceDir.string() + ": " + ex.what());
        }
    }

    // Copia um único arquivo, criando diretórios de destino se necessário.
    // Retorna true se copiou, false se ignorou (já existe e igual) ou erro.
    bool CopyOne(const fs::path& src, const fs::path& dst,
                 bool overwrite, ExportResult& result,
                 const std::string& tag)
    {
        try
        {
            if (!fs::exists(src))
            {
                result.Log.push_back("[SKIP] Nao encontrado: " + src.string());
                ++result.FilesSkipped;
                return false;
            }

            if (fs::exists(dst) && !overwrite)
            {
                result.Log.push_back("[SKIP] Ja existe: " + dst.filename().string());
                ++result.FilesSkipped;
                return false;
            }

            fs::create_directories(dst.parent_path());
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
            result.Log.push_back("[OK] " + tag + ": " + dst.filename().string());
            ++result.FilesCopied;
            return true;
        }
        catch (const std::exception& ex)
        {
            result.Log.push_back("[ERR] " + std::string(ex.what()));
            if (result.ErrorMessage.empty())
                result.ErrorMessage = ex.what();
            return false;
        }
    }

    // Copia recursivamente todos os arquivos de `srcDir` cujas extensões
    // estão em `exts` para `dstDir`, mantendo a estrutura relativa.
    void CopyByExtension(const fs::path& srcDir,
                         const fs::path& dstDir,
                         const std::vector<std::string>& exts,
                         bool overwrite,
                         ExportResult& result,
                         const std::string& tag)
    {
        try
        {
            if (!fs::exists(srcDir)) return;

            for (const auto& entry : fs::recursive_directory_iterator(
                     srcDir, fs::directory_options::skip_permission_denied))
            {
                if (!entry.is_regular_file()) continue;
                if (!HasExtension(entry.path(), exts)) continue;

                const fs::path rel = fs::relative(entry.path(), srcDir);
                CopyOne(entry.path(), dstDir / rel, overwrite, result, tag);
            }
        }
        catch (const std::exception& ex)
        {
            if (result.ErrorMessage.empty()) result.ErrorMessage = ex.what();
            result.Log.push_back("[ERR] Copiando arquivos de " + srcDir.string() + ": " + ex.what());
        }
    }

    // Gera o manifest de exportação
    std::string ManifestValue(std::string value)
    {
        value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
        std::replace(value.begin(), value.end(), '\n', ' ');
        return value;
    }

    bool IsSafeGameName(const std::string& name)
    {
        if (name.empty() || name.size() > 96 || name == "." || name == "..") return false;
        return name.find_first_of("\\/:*?\"<>|\r\n") == std::string::npos;
    }

    bool IsSafeVersion(const std::string& version)
    {
        if (version.empty() || version.size() > 64) return false;
        bool hasDigit = false;
        for (unsigned char c : version)
        {
            if (std::isdigit(c)) hasDigit = true;
            else if (!(std::isalpha(c) || c == '.' || c == '-' || c == '+')) return false;
        }
        return hasDigit && version.front() != '.' && version.back() != '.';
    }

    const char* WindowModeName(int mode)
    {
        switch (mode)
        {
        case 1: return "borderless";
        case 2: return "fullscreen";
        default: return "windowed";
        }
    }

    bool WriteManifest(const fs::path& outputDir,
                       const ExportConfig& config,
                       const std::string& startScene,
                       const std::string& iconFileName,
                       ExportResult& result)
    {
        fs::path manifestPath = outputDir / "MaraeExport.manifest";
        try
        {
            std::ofstream f(manifestPath);
            if (!f.is_open()) throw std::runtime_error("Nao foi possivel criar o manifest");

            f << "name=" << ManifestValue(config.GameName) << "\n";
            if (!config.OfficialName.empty()) f << "official_name=" << ManifestValue(config.OfficialName) << "\n";
            f << "version=" << ManifestValue(config.Version) << "\n";
            if (!config.Description.empty()) f << "description=" << ManifestValue(config.Description) << "\n";
            f << "platform=" << GameExporter::PlatformName(config.Platform) << "\n";
            if (config.Platform == ExportPlatform::Desktop)
                f << "runtime=MaraeGame.exe\n";
            else if (config.Platform == ExportPlatform::Android)
                f << "runtime=android\n";
            else if (config.Platform == ExportPlatform::HTML)
                f << "runtime=html\n";
            else
                f << "runtime=MaraeGame.exe\n";
            f << "start_scene=" << startScene << "\n";
            f << "world=" << (config.World2D ? "2d" : "3d") << "\n";
            if (config.Platform == ExportPlatform::Desktop)
            {
                f << "width=" << config.WindowWidth << "\n";
                f << "height=" << config.WindowHeight << "\n";
                f << "window_mode=" << WindowModeName(config.WindowMode) << "\n";
                if (!iconFileName.empty()) f << "icon=" << iconFileName << "\n";
            }
            f << "exported_by=MaraeEngine\n";

            result.Log.push_back("[OK] manifest: MaraeExport.manifest");
            return true;
        }
        catch (const std::exception& ex)
        {
            result.Log.push_back("[ERR] manifest: " + std::string(ex.what()));
            result.ErrorMessage = ex.what();
            return false;
        }
    }

    bool WriteBuildLog(const fs::path& outputDir,
                       const ExportConfig& config,
                       const ExportResult& result)
    {
        std::ofstream log(outputDir / "MaraeBuild.log", std::ios::binary);
        if (!log) return false;
        log << "Marae Engine - Build log\n"
            << "Game: " << ManifestValue(config.GameName) << "\n"
            << "Official name: " << ManifestValue(config.OfficialName) << "\n"
            << "Version: " << ManifestValue(config.Version) << "\n"
            << "Platform: " << GameExporter::PlatformName(config.Platform) << "\n"
            << "Start scene: " << ManifestValue(config.StartScene) << "\n"
            << "Resolution: " << config.WindowWidth << "x" << config.WindowHeight << "\n"
            << "Window mode: " << WindowModeName(config.WindowMode) << "\n\n";
        for (const std::string& entry : result.Log)
            log << ManifestValue(entry) << '\n';
        log << "\nResult: validated for transactional publication\n";
        return static_cast<bool>(log);
    }

    std::string LowerExtension(const fs::path& path)
    {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension;
    }

    bool WriteMaraePlatformMetadata(const fs::path& outputDir,
                                    const ExportConfig& config,
                                    ExportResult& result)
    {
        try
        {
            const fs::path presentation = outputDir / "Presentation";
            fs::create_directories(presentation / "Gallery");
            std::ofstream meta(outputDir / "MaraePlatform.manifest", std::ios::binary);
            if (!meta.is_open()) throw std::runtime_error("Nao foi possivel criar MaraePlatform.manifest");
            meta << "format=1\n";
            meta << "project_name=" << config.GameName << "\n";
            meta << "official_name=" << (config.OfficialName.empty() ? config.GameName : config.OfficialName) << "\n";
            meta << "description=" << config.Description << "\n";
            auto resolve = [&config](const std::string& value) {
                fs::path p(value);
                if (!p.is_absolute()) p = fs::path(config.ProjectPath) / p;
                return p;
            };
            if (!config.CoverImage.empty())
            {
                const fs::path source = resolve(config.CoverImage);
                const fs::path destination = presentation / ("cover" + source.extension().string());
                if (CopyOne(source, destination, config.OverwriteOutput, result, "Capa"))
                    meta << "cover=" << fs::relative(destination, outputDir).generic_string() << "\n";
            }
            int imageIndex = 0;
            for (const std::string& image : config.PresentationImages)
            {
                if (image.empty()) continue;
                const fs::path source = resolve(image);
                const fs::path destination = presentation / "Gallery" /
                    (std::to_string(imageIndex++) + source.extension().string());
                if (CopyOne(source, destination, config.OverwriteOutput, result, "Imagem de apresentacao"))
                    meta << "image=" << fs::relative(destination, outputDir).generic_string() << "\n";
            }
            meta << "package=local\n";
            meta << "runtime=MaraeGame.exe\n";
            result.Log.push_back("[OK] manifesto da Marae Platform: MaraePlatform.manifest");
            return true;
        }
        catch (const std::exception& ex)
        {
            result.ErrorMessage = ex.what();
            result.Log.push_back("[ERR] Marae Platform: " + result.ErrorMessage);
            return false;
        }
    }

    bool WritePlatformGuide(const fs::path& outputDir,
                            const std::string& gameName,
                            ExportPlatform platform,
                            ExportResult& result)
    {
        if (platform == ExportPlatform::Desktop || platform == ExportPlatform::MaraePlatform) return true;

        try
        {
            fs::path guide = outputDir / (platform == ExportPlatform::Android
                ? "ANDROID_BUILD_INFO.txt" : "HTML_BUILD_INFO.txt");
            std::ofstream f(guide);
            if (!f.is_open()) throw std::runtime_error("Nao foi possivel criar informacoes da plataforma");
            f << "Marae Engine export\n";
            f << "game=" << gameName << "\n";
            f << "platform=" << GameExporter::PlatformName(platform) << "\n";
            f << "assets=Assets/\n";
            f << "scene= definido em MaraeExport.manifest\n";
            if (platform == ExportPlatform::Android)
                f << "status=Pacote de assets pronto para o runtime Android da Marae.\n";
            else
                f << "status=Pacote de assets pronto para o runtime HTML da Marae.\n";
            result.Log.push_back("[OK] informacoes da plataforma: " + guide.filename().string());
            return true;
        }
        catch (const std::exception& ex)
        {
            result.ErrorMessage = ex.what();
            result.Log.push_back("[ERR] plataforma: " + result.ErrorMessage);
            return false;
        }
    }

    bool WriteDesktopReadme(const fs::path& outputDir, const std::string& gameName,
                            ExportResult& result)
    {
        try
        {
            std::ofstream readme(outputDir / "LEIA-ME.txt", std::ios::binary);
            if (!readme.is_open()) throw std::runtime_error("Não foi possível criar LEIA-ME.txt");
            readme << "Jogo: " << gameName << "\r\n\r\n"
                   << "Para jogar, abra MaraeGame.exe. Mantenha os arquivos e pastas deste pacote juntos.\r\n"
                   << "Este jogo foi exportado localmente pela Marae Engine.\r\n"
                   << "Requisito: Windows x64, driver OpenGL 3.3 e .NET 8 Runtime para scripts C#.\r\n";
            result.Log.push_back("[OK] instruções: LEIA-ME.txt");
            return true;
        }
        catch (const std::exception& ex)
        {
            result.ErrorMessage = ex.what();
            result.Log.push_back("[ERR] LEIA-ME: " + result.ErrorMessage);
            return false;
        }
    }

    // Copia DLLs da pasta explícita do runtime do jogo.
    void CopyEditorDlls(const fs::path& runtimeSrcDir,
                        const fs::path& outputDir,
                        bool overwrite,
                        ExportResult& result)
    {
        try
        {
            if (!fs::exists(runtimeSrcDir)) return;

            for (const auto& entry : fs::directory_iterator(runtimeSrcDir))
            {
                if (!entry.is_regular_file()) continue;
                if (!HasExtension(entry.path(), k_DllExts)) continue;

                CopyOne(entry.path(),
                        outputDir / entry.path().filename(),
                        overwrite, result, "DLL");
            }
        }
        catch (const std::exception& ex)
        {
            if (result.ErrorMessage.empty()) result.ErrorMessage = ex.what();
            result.Log.push_back("[ERR] Copiando DLLs de " + runtimeSrcDir.string() + ": " + ex.what());
        }
    }

} // namespace

// ---------------------------------------------------------------------------
// Implementação pública
// ---------------------------------------------------------------------------

std::string GameExporter::RuntimeSourceDir()
{
    // O Application troca o diretório atual para a raiz do projeto. Portanto,
    // nunca use current_path() para localizar o runtime do editor.
    fs::path exeDir;
#ifdef _WIN32
    wchar_t modulePath[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath,
                                             static_cast<DWORD>(std::size(modulePath)));
    if (length > 0 && length < std::size(modulePath))
        exeDir = fs::path(modulePath).parent_path();
#endif
    if (exeDir.empty()) exeDir = fs::current_path();

    if (fs::is_regular_file(exeDir / "MaraeEngine.exe"))
        return exeDir.string();
    return "";
}

static fs::path CurrentMaraeExecutable()
{
#ifdef _WIN32
    wchar_t modulePath[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath,
                                             static_cast<DWORD>(std::size(modulePath)));
    if (length > 0 && length < std::size(modulePath)) return fs::path(modulePath);
#endif
    return {};
}

std::string GameExporter::DefaultOutputPath(const std::string& projectPath,
                                             const std::string& gameName)
{
    if (projectPath.empty()) return "";
    fs::path p = fs::path(projectPath).parent_path() / (gameName + "_Build");
    return p.string();
}

const char* GameExporter::PlatformName(ExportPlatform platform)
{
    switch (platform)
    {
    case ExportPlatform::Android: return "Android";
    case ExportPlatform::HTML:    return "HTML";
    case ExportPlatform::MaraePlatform: return "Marae Platform";
    case ExportPlatform::Desktop:
    default:                      return "Desktop";
    }
}

int GameExporter::RunRegressionTests()
{
    int failures = 0;
    auto check = [&](bool condition, const char* name)
    {
        std::cout << (condition ? "[ExportTest] PASS: " : "[ExportTest] FAIL: ")
                  << name << std::endl;
        if (!condition) ++failures;
    };

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path testRoot = fs::temp_directory_path() /
        ("MaraeExportRegression_" + std::to_string(stamp));
    const fs::path projectDir = testRoot / "Project";
    try
    {
        fs::create_directories(projectDir / "Assets" / "Scenes");
        std::ofstream(projectDir / "Assets" / "Scenes" / "Main.scene") << "@entity\n";
        std::ofstream(projectDir / "Assets" / "Scenes" / "Alternate.scene") << "@alternate\n";
        fs::create_directories(projectDir / "Assets" / "Data");
        std::ofstream(projectDir / "Assets" / "Data" / "level.json") << "{\"seed\":42}\n";
        std::ofstream(projectDir / "keep.txt") << "preserve";

        ExportConfig unsafe;
        unsafe.ProjectPath = projectDir.string();
        unsafe.OutputPath = projectDir.string();
        unsafe.GameName = "Unsafe destination test";
        unsafe.StartScene = "Assets/Scenes/Main.scene";
        const ExportResult unsafeResult = Export(unsafe);
        std::ifstream preservedFile(projectDir / "keep.txt");
        std::string preservedContents;
        preservedFile >> preservedContents;
        check(!unsafeResult.Success && preservedContents == "preserve",
              "destino igual ao projeto falha sem apagar arquivos-fonte");

        ExportConfig parentOutput = unsafe;
        parentOutput.OutputPath = testRoot.string();
        const ExportResult parentResult = Export(parentOutput);
        check(!parentResult.Success && fs::exists(projectDir / "keep.txt"),
              "destino pai do projeto também é recusado antes de sobrescrever");

        ExportConfig html;
        html.ProjectPath = projectDir.string();
        html.OutputPath = (testRoot / "HTMLBuild").string();
        html.GameName = "HTML Runtime Test";
        html.StartScene = "Assets/Scenes/Main.scene";
        html.Platform = ExportPlatform::HTML;
        const ExportResult htmlResult = Export(html);
        check(!htmlResult.Success && htmlResult.ErrorMessage.find("runtime/build real") != std::string::npos &&
                  !fs::exists(html.OutputPath),
              "HTML sem runtime real é recusado antes de criar pacote fictício");

        ExportConfig android = html;
        android.OutputPath = (testRoot / "AndroidBuild").string();
        android.Platform = ExportPlatform::Android;
        const ExportResult androidResult = Export(android);
        check(!androidResult.Success && androidResult.ErrorMessage.find("runtime/build real") != std::string::npos &&
                  !fs::exists(android.OutputPath),
              "Android sem runtime real é recusado antes de criar pacote incompleto");

        ExportConfig desktop;
        desktop.ProjectPath = projectDir.string();
        desktop.OutputPath = (testRoot / "DesktopBuild").string();
        desktop.GameName = "Export Regression";
        desktop.OfficialName = "Export Regression Game";
        desktop.Version = "2.4.1";
        desktop.WindowWidth = 1600;
        desktop.WindowHeight = 900;
        desktop.WindowMode = 1;
        desktop.IconPath = (FindBrandingDirectory(RuntimeSourceDir()) / "MaraeIcon.png").string();
        desktop.StartScene = "Assets/Scenes/Alternate.scene";
        const ExportResult desktopResult = Export(desktop);
        std::ifstream desktopManifest(fs::path(desktop.OutputPath) / "MaraeExport.manifest");
        const std::string desktopManifestContents(
            (std::istreambuf_iterator<char>(desktopManifest)), std::istreambuf_iterator<char>());
        desktopManifest.close();
        check(desktopResult.Success && fs::exists(fs::path(desktop.OutputPath) / "MaraeGame.exe") &&
                  fs::exists(fs::path(desktop.OutputPath) / "MaraeExport.manifest") &&
                  fs::exists(fs::path(desktop.OutputPath) / "nethost.dll") &&
                  fs::exists(fs::path(desktop.OutputPath) / "Editor" / "Branding" / "MaraeIcon.png") &&
                  fs::exists(fs::path(desktop.OutputPath) / "LEIA-ME.txt") &&
                  fs::exists(fs::path(desktop.OutputPath) / "ScriptRuntime" / "MaraeEngine.Scripting.dll") &&
                  fs::exists(fs::path(desktop.OutputPath) / "Assets" / "Data" / "level.json") &&
                  desktopManifestContents.find("start_scene=Assets/Scenes/Alternate.scene") != std::string::npos &&
                  fs::exists(fs::path(desktop.OutputPath) / "Assets" / "Scenes" / "Alternate.scene") &&
                  desktopManifestContents.find("official_name=Export Regression Game") != std::string::npos &&
                  desktopManifestContents.find("version=2.4.1") != std::string::npos &&
                  desktopManifestContents.find("width=1600") != std::string::npos &&
                  desktopManifestContents.find("height=900") != std::string::npos &&
                  desktopManifestContents.find("window_mode=borderless") != std::string::npos,
              "Desktop inclui runtime, manifesto e assets com extensões não conhecidas");
        check(fs::exists(fs::path(desktop.OutputPath) / "MaraeBuild.log"),
              "exportação grava log de build junto ao pacote");
        check(fs::exists(fs::path(desktop.OutputPath) / "GameIcon.png") &&
                  desktopManifestContents.find("icon=GameIcon.png") != std::string::npos,
              "ícone configurado é copiado e registrado no manifesto");

        ExportConfig invalidVersion = desktop;
        invalidVersion.OutputPath = (testRoot / "InvalidVersion").string();
        invalidVersion.Version = "../../bad";
        const ExportResult invalidVersionResult = Export(invalidVersion);
        check(!invalidVersionResult.Success && !fs::exists(invalidVersion.OutputPath),
              "versão inválida é recusada sem criar pacote");

        ExportConfig invalidResolution = desktop;
        invalidResolution.OutputPath = (testRoot / "InvalidResolution").string();
        invalidResolution.WindowWidth = 100;
        const ExportResult invalidResolutionResult = Export(invalidResolution);
        check(!invalidResolutionResult.Success && !fs::exists(invalidResolution.OutputPath),
              "resolução inválida é recusada sem criar pacote");

        std::ofstream(fs::path(desktop.OutputPath) / "stale-from-previous-build.txt") << "stale";
        const ExportResult cleanRebuildResult = Export(desktop);
        if (!cleanRebuildResult.Success)
            std::cout << "[ExportTest] Rebuild error: " << cleanRebuildResult.ErrorMessage << std::endl;
        for (const std::string& entry : cleanRebuildResult.Log)
            if (entry.rfind("[ERR]", 0) == 0) std::cout << "[ExportTest] " << entry << std::endl;
        check(cleanRebuildResult.Success &&
                  !fs::exists(fs::path(desktop.OutputPath) / "stale-from-previous-build.txt") &&
                  fs::exists(fs::path(desktop.OutputPath) / "Assets" / "Data" / "level.json"),
              "reexportação publica uma pasta limpa sem arquivos obsoletos");

        std::ofstream(fs::path(desktop.OutputPath) / "preserve-existing.txt") << "preserve";
        ExportConfig refuseOverwrite = desktop;
        refuseOverwrite.OverwriteOutput = false;
        const ExportResult overwriteRefused = Export(refuseOverwrite);
        std::ifstream preservedBuildFile(fs::path(desktop.OutputPath) / "preserve-existing.txt");
        std::string preservedBuildContents;
        preservedBuildFile >> preservedBuildContents;
        check(!overwriteRefused.Success && preservedBuildContents == "preserve",
              "sobrescrita desativada preserva a build anterior");
    }
    catch (const std::exception& ex)
    {
        std::cout << "[ExportTest] FAIL: exceção no teste: " << ex.what() << std::endl;
        ++failures;
    }

    std::error_code cleanupError;
    fs::remove_all(testRoot, cleanupError);
    if (cleanupError)
    {
        std::cout << "[ExportTest] FAIL: limpeza da pasta temporária: " << cleanupError.message() << std::endl;
        ++failures;
    }
    std::cout << "[ExportTest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU")
              << " (" << failures << " falha(s))" << std::endl;
    return failures == 0 ? 0 : 1;
}

ExportResult GameExporter::Export(const ExportConfig& config,
                                   ProgressCallback    onProgress)
{
    ExportResult result;

    // ---- Validação básica ------------------------------------------------
    if (config.ProjectPath.empty())
    {
        result.ErrorMessage = "ProjectPath nao configurado.";
        result.Log.push_back("[ERR] " + result.ErrorMessage);
        return result;
    }
    if (config.OutputPath.empty())
    {
        result.ErrorMessage = "OutputPath nao configurado.";
        result.Log.push_back("[ERR] " + result.ErrorMessage);
        return result;
    }
    if (!IsSafeGameName(config.GameName))
    {
        result.ErrorMessage = "O nome do jogo está vazio ou contém caracteres inválidos.";
        result.Log.push_back("[ERR] " + result.ErrorMessage);
        return result;
    }
    if (!IsSafeVersion(config.Version))
    {
        result.ErrorMessage = "Versão inválida. Use letras, números, ponto, hífen ou + (ex.: 1.0.0).";
        result.Log.push_back("[ERR] " + result.ErrorMessage);
        return result;
    }
    if (config.Platform == ExportPlatform::Desktop &&
        (config.WindowWidth < 320 || config.WindowWidth > 7680 ||
         config.WindowHeight < 200 || config.WindowHeight > 4320 ||
         config.WindowMode < 0 || config.WindowMode > 2))
    {
        result.ErrorMessage = "Configuração de janela inválida: resolução ou modo fora do intervalo suportado.";
        result.Log.push_back("[ERR] " + result.ErrorMessage);
        return result;
    }

    const fs::path projectDir(config.ProjectPath);
    const fs::path outputDir = fs::absolute(fs::path(config.OutputPath)).lexically_normal();
    const fs::path assetsDir  = projectDir / "Assets";
    fs::path iconSource;
    std::string packagedIconName;
    if (!config.IconPath.empty())
    {
        iconSource = fs::path(config.IconPath);
        if (!iconSource.is_absolute()) iconSource = projectDir / iconSource;
        const std::string extension = LowerExtension(iconSource);
        if (!fs::is_regular_file(iconSource) ||
            (extension != ".png" && extension != ".jpg" && extension != ".jpeg" &&
             extension != ".bmp" && extension != ".tga"))
        {
            result.ErrorMessage = "Ícone inválido. Selecione um arquivo PNG, JPG, BMP ou TGA existente.";
            result.Log.push_back("[ERR] " + result.ErrorMessage);
            return result;
        }
        packagedIconName = "GameIcon" + extension;
    }

    // Não apague nem escreva dentro da pasta-fonte do projeto. A validação
    // acontece antes de remove_all para proteger o projeto contra destino
    // incorreto, inclusive quando o output é pai da pasta do projeto.
    try
    {
        if (!fs::exists(projectDir) || !fs::is_directory(projectDir))
            throw std::runtime_error("A pasta do projeto não existe ou não é uma pasta.");
        std::error_code pathError;
        const fs::path canonicalProject = fs::weakly_canonical(projectDir, pathError);
        if (pathError) throw std::runtime_error("Não foi possível validar a pasta do projeto.");
        const fs::path canonicalOutput = fs::weakly_canonical(outputDir, pathError);
        if (pathError) throw std::runtime_error("Não foi possível validar o destino da exportação.");
        if (fs::exists(outputDir) && !fs::is_directory(outputDir))
            throw std::runtime_error("O destino da exportação existe e não é uma pasta.");
        if (IsPathInsideOrEqual(canonicalOutput, canonicalProject) ||
            IsPathInsideOrEqual(canonicalProject, canonicalOutput))
            throw std::runtime_error("O destino precisa ficar fora da pasta do projeto e não pode contê-la.");
    }
    catch (const std::exception& ex)
    {
        result.ErrorMessage = ex.what();
        result.Log.push_back("[ERR] Pré-validação: " + result.ErrorMessage);
        return result;
    }

    if (config.Platform == ExportPlatform::Android || config.Platform == ExportPlatform::HTML)
    {
        result.ErrorMessage = std::string("Exportação ") + PlatformName(config.Platform) +
            " indisponível: a Marae ainda não tem um runtime/build real para esta plataforma. "
            "Nenhum pacote incompleto foi gerado.";
        result.Log.push_back("[ERR] " + result.ErrorMessage);
        return result;
    }

    const std::string runtimeSource = RuntimeSourceDir();
    const fs::path executableSource = CurrentMaraeExecutable();
    if (runtimeSource.empty() || executableSource.empty() || !fs::is_regular_file(executableSource))
    {
        result.ErrorMessage = "Executável da Marae em execução não encontrado para gerar o runtime Desktop.";
        result.Log.push_back("[ERR] " + result.ErrorMessage);
        return result;
    }
    const fs::path runtimeDir(runtimeSource);
    const fs::path managedRuntimeSource = FindManagedRuntimeDirectory(runtimeDir);
    const fs::path netHostSource = FindNetHostLibrary(runtimeDir);
    const fs::path brandingSource = FindBrandingDirectory(runtimeDir);
    if (managedRuntimeSource.empty() || netHostSource.empty() || brandingSource.empty())
    {
        result.ErrorMessage = "Runtime Desktop incompleto: ScriptRuntime, nethost.dll ou Branding da janela não encontrado. "
            "O export foi cancelado para não gerar um jogo sem suporte a scripts.";
        result.Log.push_back("[ERR] " + result.ErrorMessage);
        return result;
    }

    if (config.StartScene.empty())
    {
        result.ErrorMessage = "Selecione uma cena inicial antes de exportar.";
        result.Log.push_back("[ERR] " + result.ErrorMessage);
        return result;
    }
    const fs::path requestedScene(config.StartScene);
    const fs::path resolvedScene = requestedScene.is_absolute() ? requestedScene : projectDir / requestedScene;
    if (!fs::exists(resolvedScene) || !fs::is_regular_file(resolvedScene))
    {
        result.ErrorMessage = "A cena inicial não foi encontrada: " + config.StartScene;
        result.Log.push_back("[ERR] " + result.ErrorMessage);
        return result;
    }
    std::error_code scenePathError;
    const fs::path canonicalAssets = fs::weakly_canonical(assetsDir, scenePathError);
    if (scenePathError)
    {
        result.ErrorMessage = "Não foi possível validar a pasta Assets do projeto.";
        result.Log.push_back("[ERR] " + result.ErrorMessage);
        return result;
    }
    const fs::path canonicalScene = fs::weakly_canonical(resolvedScene, scenePathError);
    if (scenePathError || !IsPathInsideOrEqual(canonicalScene, canonicalAssets))
    {
        result.ErrorMessage = "A cena inicial precisa estar dentro de Assets para entrar na build reproduzível.";
        result.Log.push_back("[ERR] " + result.ErrorMessage);
        return result;
    }
    const fs::path packagedSceneRelative = canonicalScene.lexically_relative(canonicalAssets);
    if (packagedSceneRelative.empty() || *packagedSceneRelative.begin() == "..")
    {
        result.ErrorMessage = "O caminho relativo da cena inicial é inválido.";
        result.Log.push_back("[ERR] " + result.ErrorMessage);
        return result;
    }

    auto progress = [&](const std::string& msg, float pct)
    {
        result.Log.push_back(msg);
        if (onProgress) onProgress(msg, pct);
    };

    progress("Iniciando exportacao para: " + config.OutputPath, 0.0f);

    // ---- Gera em staging para nunca deixar uma build parcial publicada ----
    StagedBuild stagedBuild(outputDir);
    fs::path buildDir;
    try
    {
        progress("Preparando pasta temporária limpa...", 0.02f);
        stagedBuild.Create();
        buildDir = stagedBuild.Path();
    }
    catch (const std::exception& ex)
    {
        result.ErrorMessage = ex.what();
        result.Log.push_back("[ERR] Nao foi possivel criar output: " + result.ErrorMessage);
        return result;
    }

    // ---- Runtime (somente Desktop; Android/HTML usam o pacote de assets) --
    progress("Copiando runtime...", 0.05f);
    if (config.Platform == ExportPlatform::Desktop || config.Platform == ExportPlatform::MaraePlatform)
    {
        const fs::path srcDir(runtimeSource);

            // Executável
            CopyOne(executableSource,
                    buildDir / "MaraeGame.exe",
                    config.OverwriteOutput, result, "Runtime");

            // Todas as DLLs da pasta do runtime
            CopyEditorDlls(srcDir, buildDir, true, result);
        CopyOne(netHostSource, buildDir / "nethost.dll",
                config.OverwriteOutput, result, "Host .NET");
        try
        {
            CopyDirectoryContents(managedRuntimeSource, buildDir / "ScriptRuntime",
                                  config.OverwriteOutput, result, "Runtime C#");
        }
        catch (const std::exception& ex)
        {
            result.ErrorMessage = ex.what();
            result.Log.push_back("[ERR] Runtime C#: " + result.ErrorMessage);
            return result;
        }
        // O runtime de desenvolvimento pode manter os assemblies gerenciados e
        // nethost na saída Release, enquanto o executável corrente é Debug.
        CopyEditorDlls(managedRuntimeSource.parent_path(), buildDir, true, result);
        CopyDirectoryContents(brandingSource, buildDir / "Editor" / "Branding",
                              true, result, "Branding do runtime");

        if (!fs::exists(buildDir / "MaraeGame.exe") ||
            fs::file_size(buildDir / "MaraeGame.exe") == 0 ||
            !fs::exists(buildDir / "nethost.dll") ||
            fs::file_size(buildDir / "nethost.dll") == 0 ||
            !fs::exists(buildDir / "ScriptRuntime" / "MaraeEngine.Scripting.dll") ||
            !fs::exists(buildDir / "ScriptRuntime" / "MaraeEngine.Scripting.runtimeconfig.json"))
        {
            result.ErrorMessage = "Exportação Desktop incompleta: runtime, host ou bibliotecas C# ausentes.";
            result.Log.push_back("[ERR] " + result.ErrorMessage);
            return result;
        }
    }
    else
        progress("Preparando pacote local...", 0.05f);

    // Copia toda a árvore Assets sem filtrar extensões: cenas, meshes geradas,
    // dados auxiliares e assets futuros também precisam acompanhar o jogo.
    progress("Copiando assets do projeto...", 0.15f);
    CopyDirectoryContents(assetsDir, buildDir / "Assets", true, result, "Asset");
    if (!packagedIconName.empty())
        CopyOne(iconSource, buildDir / packagedIconName, true, result, "Ícone do jogo");

    // O modelador grava algumas malhas derivadas em Library/Generated e a
    // cena pode referenciá-las por esse caminho relativo ao projeto.
    const fs::path generatedLibrary = projectDir / "Library" / "Generated";
    if (fs::is_directory(generatedLibrary))
        CopyDirectoryContents(generatedLibrary, buildDir / "Library" / "Generated",
                              true, result, "Malha gerada");

    // ---- Shaders fora de Assets -----------------------------------------
    progress("Copiando shaders...", 0.75f);
    CopyByExtension(projectDir / "Shaders", buildDir / "Shaders",
                    { ".glsl", ".vert", ".frag", ".hlsl" }, config.OverwriteOutput, result, "Shader");

    // ---- Fontes ----------------------------------------------------------
    progress("Copiando fontes...", 0.78f);
    // Fontes dentro de Assets já foram copiadas com a árvore completa.

    // ---- Scripts ---------------------------------------------------------
    progress("Copiando scripts...", 0.80f);
    {
        fs::path scriptsDir = projectDir / "Scripts";
        CopyByExtension(scriptsDir,
                        buildDir / "Scripts",
                        k_ScriptExts, config.OverwriteOutput, result, "Script");
        // Scripts também podem estar em Assets/Scripts
        CopyByExtension(assetsDir / "Scripts",
                        buildDir / "Scripts",
                        k_ScriptExts, config.OverwriteOutput, result, "Script");
    }

    // ---- Manifest --------------------------------------------------------
    progress("Gerando manifest...", 0.92f);
    {
        // Normaliza start_scene para usar barras normais
        std::string startScene = (fs::path("Assets") / packagedSceneRelative).generic_string();
        std::replace(startScene.begin(), startScene.end(), '\\', '/');
        if (!WriteManifest(buildDir, config, startScene, packagedIconName, result))
            return result;
        if (config.Platform == ExportPlatform::MaraePlatform)
        {
            if (!WriteMaraePlatformMetadata(buildDir, config, result)) return result;
        }
        if (!WritePlatformGuide(buildDir, config.GameName, config.Platform, result))
            return result;
        if (config.Platform == ExportPlatform::Desktop &&
            !WriteDesktopReadme(buildDir, config.GameName, result))
            return result;
    }

    // Confirma que a cena escolhida e os binários requeridos estão realmente
    // na pasta nova antes de substituir uma versão anterior.
    const fs::path packagedScene = buildDir / "Assets" / packagedSceneRelative;
    if (!fs::is_regular_file(packagedScene) || fs::file_size(packagedScene) == 0)
    {
        result.ErrorMessage = "A cena inicial não entrou na build: " + config.StartScene;
        result.Log.push_back("[ERR] Validação: " + result.ErrorMessage);
        return result;
    }
    if (!result.ErrorMessage.empty())
    {
        result.Log.push_back("[ERR] Build descartada: houve falha ao copiar um ou mais arquivos.");
        return result;
    }
    result.Log.push_back("[OK] Build validada; publicando pacote.");
    if (!WriteBuildLog(buildDir, config, result))
    {
        result.ErrorMessage = "Não foi possível gravar MaraeBuild.log na pasta da build.";
        result.Log.push_back("[ERR] " + result.ErrorMessage);
        return result;
    }
    try
    {
        stagedBuild.Commit(config.OverwriteOutput, result);
    }
    catch (const std::exception& ex)
    {
        result.ErrorMessage = ex.what();
        result.Log.push_back("[ERR] Não foi possível publicar a build: " + result.ErrorMessage);
        return result;
    }

    // ---- Resumo ----------------------------------------------------------
    progress("Exportacao concluida!", 1.0f);
    {
        std::ostringstream summary;
        summary << "Total: " << result.FilesCopied << " copiados, "
                << result.FilesSkipped << " ignorados.";
        result.Log.push_back(summary.str());
    }

    result.Success = result.ErrorMessage.empty();
    return result;
}
