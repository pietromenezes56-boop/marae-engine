#include "../Localization/Localization.h"
#include "LegalNotice.h"
#include "../Localization/Localization.h"
#include "../../Hub/ProjectManager.h"
#include <imgui.h>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <cstdint>
#include <vector>
#include <system_error>
#include <ctime>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

static void OpenUrl(const char* url);

namespace
{
    // Fingerprint estável, usado para detectar alterações acidentais ou não
    // autorizadas no documento oficial distribuído com a engine.
    std::string FileFingerprint(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return {};

        uint64_t hash = 1469598103934665603ULL;
        char byte = 0;
        while (file.get(byte))
        {
            hash ^= static_cast<unsigned char>(byte);
            hash *= 1099511628211ULL;
        }

        std::ostringstream result;
        result << std::hex << std::uppercase << std::setfill('0')
               << std::setw(16) << hash;
        return result.str();
    }

    void MakeReadOnly(const fs::path& path)
    {
        std::error_code error;
        fs::permissions(path,
            fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read,
            fs::perm_options::replace, error);

#ifdef _WIN32
        // O atributo do Windows complementa as permissões portáveis acima.
        SetFileAttributesA(path.string().c_str(), FILE_ATTRIBUTE_READONLY);
#endif
    }

    bool IsOfficialFile(const fs::path& path, const char* expectedFingerprint)
    {
        return fs::exists(path) && FileFingerprint(path) == expectedFingerprint;
    }

    constexpr const char* kLegalVersion = "1.3";
    constexpr const char* kOfficialFingerprint = "EA83059B7CE46CAC";

    fs::path FindLegalDocumentPath()
    {
        std::vector<fs::path> starts;
        starts.push_back(fs::current_path());

#ifdef _WIN32
        char modulePath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
        if (length > 0 && length < MAX_PATH)
            starts.emplace_back(fs::path(modulePath).parent_path());
#endif

        for (const fs::path& start : starts)
        {
            fs::path candidate = start;
            for (int level = 0; level < 8; ++level)
            {
                const fs::path projectCopy = candidate / "MaraeEngine" / "TERMS_OF_USE_v1.3.md";
                if (IsOfficialFile(projectCopy, kOfficialFingerprint))
                    return projectCopy;

                const fs::path officialCopy = candidate / "TERMS_OF_USE_v1.3.md";
                if (IsOfficialFile(officialCopy, kOfficialFingerprint))
                    return officialCopy;

                if (candidate == candidate.parent_path())
                    break;
                candidate = candidate.parent_path();
            }
        }

        return fs::current_path() / "TERMS_OF_USE_v1.3.md";
    }
    fs::path CopyTermsToExternalFile()
    {
        const fs::path source = FindLegalDocumentPath();
        const fs::path directory = fs::path(ProjectManager::GetUserDocumentsPath()) / "MaraeEngine";
        std::error_code error;
        fs::create_directories(directory, error);
        const fs::path target = directory / "TERMS_OF_USE_v1.3_COPY.md";
        std::ifstream input(source, std::ios::binary);
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        if (!input.is_open() || !output.is_open()) return {};
        output << input.rdbuf();
        return target;
    }

    fs::path AcceptancePath()
    {
        fs::path directory = fs::path(ProjectManager::GetUserDocumentsPath()) / "MaraeEngine";
        std::error_code error;
        fs::create_directories(directory, error);
        return directory / "legal_acceptance.cfg";
    }

    bool HasAcceptedCurrentVersion()
    {
        std::ifstream file(AcceptancePath());
        std::string line;
        bool terms = false;
        bool privacy = false;
        while (std::getline(file, line))
        {
            if (line == "TermsVersion=" + std::string(kLegalVersion)) terms = true;
            if (line == "PrivacyVersion=" + std::string(kLegalVersion)) privacy = true;
        }
        return terms && privacy;
    }

    void SaveAcceptance()
    {
        std::ofstream file(AcceptancePath(), std::ios::trunc);
        if (!file.is_open()) return;
        file << "TermsVersion=" << kLegalVersion << "\n";
        file << "PrivacyVersion=" << kLegalVersion << "\n";
        file << "AcceptedAt=" << std::time(nullptr) << "\n";
        file << "AcceptanceMethod=ExplicitCheckbox\n";
    }

    void RenderLegalMarkdown(const std::string& markdown)
    {
        std::istringstream lines(markdown);
        std::string line;
        int noticeBlock = 0;
        while (std::getline(lines, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            std::string::size_type boldMarker = line.find("**");
            while (boldMarker != std::string::npos)
            {
                line.erase(boldMarker, 2);
                boldMarker = line.find("**");
            }

            if (line.empty())
            {
                ImGui::Spacing();
                continue;
            }

            if (line.rfind("# ", 0) == 0)
            {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.32f, 0.78f, 1.0f, 1.0f));
                ImGui::TextWrapped(UI("%s"), line.substr(2).c_str());
                ImGui::PopStyleColor();
                ImGui::Separator();
                continue;
            }

            if (line.rfind("## ", 0) == 0 || line.rfind("### ", 0) == 0)
            {
                const size_t prefix = line.rfind("### ", 0) == 0 ? 4u : 3u;
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.86f, 1.0f, 1.0f));
                ImGui::TextWrapped(UI("%s"), line.substr(prefix).c_str());
                ImGui::PopStyleColor();
                continue;
            }

            if (line.rfind("> ", 0) == 0)
            {
                ImGui::PushID(noticeBlock++);
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.82f, 0.32f, 1.0f));
                ImGui::TextWrapped(UI("%s"), line.substr(2).c_str());
                ImGui::PopStyleColor();
                ImGui::Separator();
                ImGui::PopID();
                continue;
            }

            const bool hasPlaceholder = line.find("[PREENCHER]") != std::string::npos;
            const bool important = line.find("IMPORTANTE") != std::string::npos ||
                line.find("Atenção") != std::string::npos ||
                line.find("direito") != std::string::npos ||
                line.find("não poderá") != std::string::npos;
            if (hasPlaceholder)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.32f, 1.0f));
            else if (important)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.84f, 0.42f, 1.0f));

            const size_t urlStart = line.find("http://");
            const size_t httpsStart = line.find("https://");
            const size_t linkStart = httpsStart != std::string::npos ? httpsStart : urlStart;
            if (linkStart != std::string::npos)
            {
                size_t linkEnd = line.find_first_of(" \\\"\\)", linkStart);
                if (linkEnd == std::string::npos) linkEnd = line.size();
                const std::string prefix = line.substr(0, linkStart);
                const std::string url = line.substr(linkStart, linkEnd - linkStart);
                if (!prefix.empty()) ImGui::TextWrapped(UI("%s"), prefix.c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.70f, 1.0f, 1.0f));
                if (ImGui::SmallButton(url.c_str())) OpenUrl(url.c_str());
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::TextWrapped(UI("%s"), line.c_str());
            }

            if (hasPlaceholder || important)
                ImGui::PopStyleColor();
        }
    }

    bool RenderConsolidatedDocument(float reservedBottom = 0.0f)
    {
        const bool integrityValid = LegalNotice::IsIntegrityValid();
        std::ifstream file(FindLegalDocumentPath());
        std::ostringstream content;
        if (file.is_open())
            content << file.rdbuf();
        else
            content << "Documento legal oficial indisponível.";

        ImGui::BeginChild("##ConsolidatedTermsDocument", ImVec2(0.0f, reservedBottom), true,
            ImGuiWindowFlags_HorizontalScrollbar);
        if (!integrityValid)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.25f, 1.0f));
            ImGui::TextWrapped(UI("Documento legal não verificado. O aceite ficará bloqueado até que a versão oficial v1.3 seja restaurada."));
            ImGui::PopStyleColor();
            ImGui::Separator();
        }
        RenderLegalMarkdown(content.str());
        const bool reachedEnd = integrityValid &&
            (ImGui::GetScrollMaxY() <= 1.0f ||
             ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f);
        ImGui::EndChild();
        return reachedEnd;
    }
}

static void OpenUrl(const char* url)
{
#ifdef _WIN32
    std::string cmd = std::string("start \"\" \"") + url + "\"";
    std::system(cmd.c_str());
#endif
}

void LegalNotice::EnsureLegalFilesExist()
{
    // A privacidade integra o documento consolidado; não é criado um segundo
    // arquivo legal separado. O documento oficial deve ser distribuído
    // junto com a engine e nunca é recriado com conteúdo antigo.
    MakeReadOnly(FindLegalDocumentPath());
}

bool LegalNotice::IsIntegrityValid()
{
    return IsOfficialFile(FindLegalDocumentPath(), kOfficialFingerprint);
}

bool LegalNotice::DrawFirstRunAcceptance()
{
    EnsureLegalFilesExist();
    if (HasAcceptedCurrentVersion() && IsIntegrityValid())
        return true;

    static bool confirmed = false;
    const bool isPT = Localization::Get().GetResolvedLanguage() == EditorLanguage::Portuguese;
    const char* title = isPT ? "Aceite obrigatório — Marae Engine"
                             : "Required acceptance — Marae Engine";

    ImGui::OpenPopup(title);
    ImGui::SetNextWindowSize(ImVec2(920.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal(title, nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        ImGui::TextColored(ImVec4(0.20f, 0.72f, 1.0f, 1.0f), "%s", isPT
            ? "Leia atentamente antes de continuar"
            : "Read carefully before continuing");
        ImGui::Separator();
        ImGui::TextWrapped(UI("%s"), isPT
            ? "A instalação ou abertura da Marae Engine não substitui o aceite explícito. Para usar a engine, você precisa confirmar que leu e concorda com os Termos de Uso e a Política de Privacidade abaixo."
            : "To use Marae Engine, read the consolidated document, scroll to the end, and explicitly confirm your agreement.");
        ImGui::Spacing();

        const bool reachedEnd = RenderConsolidatedDocument(-122.0f);

        ImGui::Spacing();
        if (!reachedEnd)
            ImGui::BeginDisabled();
        ImGui::Checkbox(isPT
            ? "Li e concordo com os Termos de Uso e a Politica de Privacidade da Marae Engine."
            : "I have read and agree to Marae Engine's Terms of Use and Privacy Policy.", &confirmed);
        if (!reachedEnd)
            ImGui::EndDisabled();
        if (!reachedEnd)
            ImGui::TextDisabled("%s", isPT
                ? "Role o documento até o final para habilitar o aceite."
                : "Scroll to the end of the document to enable acceptance.");
        ImGui::TextDisabled("%s", isPT
            ? "A concordância fica registrada localmente para esta versão dos documentos."
            : "Your acceptance is stored locally for this document version.");
        ImGui::Spacing();

        if (!confirmed || !reachedEnd)
            ImGui::BeginDisabled();
        if (ImGui::Button(isPT ? "Aceitar e continuar" : "Accept and continue", ImVec2(210.0f, 36.0f)))
        {
            SaveAcceptance();
            ImGui::CloseCurrentPopup();
        }
        if (!confirmed || !reachedEnd)
            ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("v%s", kLegalVersion);
        ImGui::EndPopup();
    }

    return HasAcceptedCurrentVersion() && IsIntegrityValid();
}

void LegalNotice::DrawModal(bool* p_open)
{
    if (!p_open || !*p_open) return;

    EnsureLegalFilesExist();

    const bool isPT = Localization::Get().GetResolvedLanguage() == EditorLanguage::Portuguese;
    ImGui::SetNextWindowSize(ImVec2(880.0f, 600.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin(isPT ? "Marae Engine — Termos de Uso, Licença & Contato" : "Marae Engine — Terms of Service, License & Contact", p_open,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        // Os documentos são exibidos pela própria engine e não podem ser
        // abertos por este painel em um editor externo.
        ImGui::TextDisabled("%s", isPT
            ? "Documentos oficiais protegidos contra edição não autorizada"
            : "Official documents protected against unauthorized editing");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.85f, 0.45f, 1.0f));
        ImGui::TextDisabled("%s", "0% Royalties • 100% Creator Ownership");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", "• maraeengine@gmail.com");

        if (!LegalNotice::IsIntegrityValid())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.25f, 1.0f));
            ImGui::TextWrapped(UI("%s"), isPT
                ? "Atenção: a cópia externa de um documento legal não corresponde à versão oficial. O conteúdo oficial exibido acima permanece protegido e embutido na Marae Engine."
                : "Warning: an external legal document does not match the official version. The official content shown above remains protected and embedded in Marae Engine.");
            ImGui::PopStyleColor();
        }

        ImGui::Separator();
        ImGui::Spacing();

        // Exibe o documento consolidado v1.3 em um único lugar.
        RenderConsolidatedDocument();

        ImGui::Separator();
        if (ImGui::Button(isPT ? "Abrir documento no navegador" : "Open document in browser"))
        {
            const fs::path copy = CopyTermsToExternalFile();
            if (!copy.empty()) OpenUrl(copy.string().c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button(isPT ? "Copiar Termos externamente" : "Copy Terms externally"))
        {
            const fs::path copy = CopyTermsToExternalFile();
            if (!copy.empty())
            {
                std::ifstream copied(copy, std::ios::binary);
                std::ostringstream text;
                text << copied.rdbuf();
                ImGui::SetClipboardText(text.str().c_str());
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("maraeengine@gmail.com");

        ImGui::End();
    }
}

void LegalNotice::DrawPanel()
{
    EnsureLegalFilesExist();

    const bool isPT = Localization::Get().GetResolvedLanguage() == EditorLanguage::Portuguese;

    // Header do painel
    ImGui::SetCursorPos(ImVec2(20.0f, 20.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.92f, 0.92f, 1.0f));
    ImGui::SetWindowFontScale(1.2f);
    ImGui::TextUnformatted(isPT ? "Termos de Uso, Licença & Contato" : "Terms of Service, License & Contact");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
    ImGui::Separator();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);

    // Estado de proteção — não há edição ou abertura externa por este painel.
    ImGui::SetCursorPosX(20.0f);
    ImGui::TextDisabled("%s", isPT
        ? "Termos e privacidade: somente leitura / alteração oficial via atualização da engine"
        : "Terms and privacy: read-only / official changes require an engine update");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", "0% Royalties • 100% Creator Ownership • maraeengine@gmail.com");

    if (!LegalNotice::IsIntegrityValid())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.25f, 1.0f));
        ImGui::TextWrapped(UI("%s"), isPT
            ? "Atenção: foi detectada uma alteração ou ausência em um arquivo legal externo. A versão oficial continua sendo exibida pela engine."
            : "Warning: a change or missing external legal file was detected. The official version remains displayed by the engine.");
        ImGui::PopStyleColor();
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
    ImGui::SetCursorPosX(20.0f);

    // Documento consolidado com uma única área de rolagem.
    RenderConsolidatedDocument();
}
