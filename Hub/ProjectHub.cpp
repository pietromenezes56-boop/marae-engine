#include "../Engine/Localization/Localization.h"
#include "../Engine/Legal/LegalNotice.h"
#include "ProjectHub.h"
#include "../Engine/Paths/RuntimePaths.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#pragma warning(disable: 4005)
#endif

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "../ThirdParty/stb/stb_image.h"
#include "../Engine/Localization/Localization.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <ctime>
#include <cctype>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Carrega logo
// ---------------------------------------------------------------------------
static unsigned int LoadMaraeLogoTexture()
{
    const fs::path logoPath = RuntimePaths::ResolveEngineAsset(
        __FILE__, "Editor/Branding/MaraeLogoTransparent.png");
    stbi_set_flip_vertically_on_load_thread(false);
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load(logoPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) return 0;
    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);
    return texture;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
static constexpr int   HUB_W     = 1100;
static constexpr int   HUB_H     = 680;
static constexpr float SIDEBAR_W = 220.0f;

// ---------------------------------------------------------------------------
// Paleta — escuro neutro, um unico acento
// ---------------------------------------------------------------------------
static const ImVec4 C_BG        = { 0.08f, 0.08f, 0.09f, 1.f };   // fundo geral
static const ImVec4 C_SIDE      = { 0.06f, 0.06f, 0.07f, 1.f };   // sidebar (ligeiramente mais escuro)
static const ImVec4 C_PANEL     = { 0.10f, 0.10f, 0.12f, 1.f };   // paineis e modais
static const ImVec4 C_INPUT     = { 0.12f, 0.12f, 0.14f, 1.f };   // campos de texto
static const ImVec4 C_SEP       = { 0.16f, 0.16f, 0.18f, 1.f };   // separadores/bordas
static const ImVec4 C_HOV       = { 0.14f, 0.14f, 0.16f, 1.f };   // hover de linha
static const ImVec4 C_SEL       = { 0.11f, 0.19f, 0.33f, 1.f };   // selecionado
static const ImVec4 C_ACC       = { 0.25f, 0.52f, 0.95f, 1.f };   // acento (unico)
static const ImVec4 C_ACC_D     = { 0.17f, 0.38f, 0.74f, 1.f };   // acento pressionado
static const ImVec4 C_TXT       = { 0.90f, 0.90f, 0.92f, 1.f };   // texto primario
static const ImVec4 C_TXT_D     = { 0.52f, 0.52f, 0.56f, 1.f };   // texto secundario
static const ImVec4 C_TXT_M     = { 0.34f, 0.34f, 0.38f, 1.f };   // texto terciario/mudo
static const ImVec4 C_DANGER    = { 0.82f, 0.22f, 0.22f, 1.f };   // vermelho

// ---------------------------------------------------------------------------
// Helpers de UI
// ---------------------------------------------------------------------------
static bool AccentBtn(const char* label, ImVec2 sz = {0,0})
{
    ImGui::PushStyleColor(ImGuiCol_Button,        C_ACC);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, C_ACC_D);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  C_ACC_D);
    bool r = ImGui::Button(label, sz);
    ImGui::PopStyleColor(3);
    return r;
}

static bool GhostBtn(const char* label, ImVec2 sz, bool active = false)
{
    ImVec4 bg = active ? C_SEL : ImVec4(0,0,0,0);
    ImVec4 fg = active ? C_ACC : C_TXT_D;
    ImGui::PushStyleColor(ImGuiCol_Button,        bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, C_HOV);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  C_SEL);
    ImGui::PushStyleColor(ImGuiCol_Text,          fg);
    bool r = ImGui::Button(label, sz);
    ImGui::PopStyleColor(4);
    return r;
}

static void DrawSeparator()
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    float  w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(p, {p.x+w, p.y},
        ImGui::ColorConvertFloat4ToU32(C_SEP), 1.f);
    ImGui::Dummy({0, 1.f});
}

// ---------------------------------------------------------------------------
// Timestamp
// ---------------------------------------------------------------------------
static const char* FmtTime(int64_t ts, char (&buf)[64])
{
    if (ts <= 0) { strncpy_s(buf, sizeof(buf), "Nunca aberto", _TRUNCATE); return buf; }
    std::time_t t = (std::time_t)ts;
    std::tm ti; localtime_s(&ti, &t);
    std::strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", &ti);
    return buf;
}

static const char* QName(GraphicsQuality q)
{
    switch(q)
    {
        case GraphicsQuality::UltraPerformance: return L("hub.quality_ultra_performance_name");
        case GraphicsQuality::Performance:      return L("hub.quality_performance_name");
        case GraphicsQuality::Balanced:         return L("hub.quality_balanced_name");
        case GraphicsQuality::HighFidelity:     return L("hub.quality_high_name");
        case GraphicsQuality::Ultra:            return L("hub.quality_ultra_name");
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Construtor / Destrutor
// ---------------------------------------------------------------------------
ProjectHub::ProjectHub()
{
    std::string docs = ProjectManager::GetUserDocumentsPath();
    fs::path def = fs::path(docs) / "MaraeEngine" / "Projects";
    strncpy_s(m_NewDirectory, def.string().c_str(), sizeof(m_NewDirectory) - 1);
}

ProjectHub::~ProjectHub() { Shutdown(); }

// ---------------------------------------------------------------------------
// Run
// ---------------------------------------------------------------------------
HubResult ProjectHub::Run()
{
    if (!Init()) return m_Result;
    LoadEditorSettings();
    glfwSwapInterval(m_Settings.VSync ? 1 : 0);
    m_Projects = ProjectManager::LoadProjects();
    ProjectManager::SortByRecent(m_Projects);

    while (m_Running && !glfwWindowShouldClose(m_Window))
    {
        static auto next = std::chrono::steady_clock::now();
        constexpr auto dt = std::chrono::milliseconds(33);
        next += dt;
        auto now = std::chrono::steady_clock::now();
        while (now < next && !glfwWindowShouldClose(m_Window))
        { glfwWaitEventsTimeout(std::chrono::duration<double>(next-now).count()); now = std::chrono::steady_clock::now(); }
        if (glfwWindowShouldClose(m_Window)) break;
        if (now - next > dt) next = now;
        BeginFrame(); RenderMainWindow(); EndFrame();
    }
    SaveEditorSettings();
    return m_Result;
}

// ---------------------------------------------------------------------------
// LoadFonts — Segoe UI do sistema
// ---------------------------------------------------------------------------
void ProjectHub::LoadFonts(float dpiScale)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float s = std::clamp(dpiScale, 1.0f, 2.0f);

    char windir[MAX_PATH] = "C:\\Windows";
    size_t envLen = 0; char* we = nullptr;
    _dupenv_s(&we, &envLen, "WINDIR");
    if (we) { strncpy_s(windir, we, MAX_PATH-1); std::free(we); }

    fs::path fonts   = fs::path(windir) / "Fonts";
    fs::path regular = fonts / "segoeui.ttf";
    fs::path semibold= fonts / "seguisb.ttf";
    fs::path bold    = fonts / "segoeuib.ttf";

    bool hasR = fs::exists(regular);
    bool hasSB= fs::exists(semibold);
    bool hasB = fs::exists(bold);

    // slot 0: fallback
    io.Fonts->AddFontDefault();

    // Regular 14px
    m_FontRegular = hasR
        ? io.Fonts->AddFontFromFileTTF(regular.string().c_str(), 14.f * s)
        : io.Fonts->AddFontDefault();

    // Semibold 14px (nomes de projeto, labels)
    m_FontBold = hasSB
        ? io.Fonts->AddFontFromFileTTF(semibold.string().c_str(), 14.f * s)
        : m_FontRegular;

    // Bold 18px (titulo da janela/secao)
    m_FontTitle = hasB
        ? io.Fonts->AddFontFromFileTTF(bold.string().c_str(), 18.f * s)
        : m_FontRegular;

    // Regular 12px (paths, timestamps, metadata)
    m_FontSmall = hasR
        ? io.Fonts->AddFontFromFileTTF(regular.string().c_str(), 12.f * s)
        : io.Fonts->AddFontDefault();

    io.FontDefault = m_FontRegular;
}

// ---------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------
bool ProjectHub::Init()
{
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    m_Window = glfwCreateWindow(HUB_W, HUB_H, "Marae Engine", nullptr, nullptr);
    if (!m_Window) { std::cerr << "[ProjectHub] glfwCreateWindow falhou\n"; return false; }

    glfwMakeContextCurrent(m_Window);
    glfwSwapInterval(0);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    { std::cerr << "[ProjectHub] gladLoadGL falhou\n"; return false; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplyTheme();

    ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Fontes carregadas APOS backend estar inicializado
    float sx=1.f, sy=1.f;
    glfwGetWindowContentScale(m_Window, &sx, &sy);
    LoadFonts(std::clamp(std::max(sx, sy), 1.0f, 2.0f));

    m_LogoTexture = LoadMaraeLogoTexture();
    return true;
}

void ProjectHub::Shutdown()
{
    if (!m_Window) return;
    if (m_LogoTexture) { glDeleteTextures(1, &m_LogoTexture); m_LogoTexture = 0; }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(m_Window);
    m_Window = nullptr;
}

// ---------------------------------------------------------------------------
// Tema — neutro, sem decoracao
// ---------------------------------------------------------------------------
void ProjectHub::ApplyTheme()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();

    st.WindowRounding   = 4.f;
    st.FrameRounding    = 3.f;
    st.GrabRounding     = 3.f;
    st.PopupRounding    = 4.f;
    st.ChildRounding    = 0.f;
    st.ScrollbarRounding= 3.f;
    st.TabRounding      = 3.f;
    st.WindowBorderSize = 0.f;
    st.FrameBorderSize  = 1.f;
    st.WindowPadding    = {12.f, 12.f};
    st.FramePadding     = {10.f,  5.f};
    st.ItemSpacing      = { 8.f,  5.f};
    st.ScrollbarSize    = 7.f;
    st.IndentSpacing    = 16.f;
    st.GrabMinSize      = 8.f;

    st.Colors[ImGuiCol_Text]                 = C_TXT;
    st.Colors[ImGuiCol_TextDisabled]         = C_TXT_D;
    st.Colors[ImGuiCol_WindowBg]             = C_BG;
    st.Colors[ImGuiCol_ChildBg]              = C_BG;
    st.Colors[ImGuiCol_PopupBg]              = C_PANEL;
    st.Colors[ImGuiCol_Border]               = C_SEP;
    st.Colors[ImGuiCol_BorderShadow]         = {0,0,0,0};
    st.Colors[ImGuiCol_FrameBg]              = C_INPUT;
    st.Colors[ImGuiCol_FrameBgHovered]       = {0.15f,0.15f,0.17f,1.f};
    st.Colors[ImGuiCol_FrameBgActive]        = {0.17f,0.17f,0.20f,1.f};
    st.Colors[ImGuiCol_TitleBg]              = C_SIDE;
    st.Colors[ImGuiCol_TitleBgActive]        = C_SIDE;
    st.Colors[ImGuiCol_CheckMark]            = C_ACC;
    st.Colors[ImGuiCol_SliderGrab]           = C_ACC;
    st.Colors[ImGuiCol_SliderGrabActive]     = C_ACC_D;
    st.Colors[ImGuiCol_Button]               = C_INPUT;
    st.Colors[ImGuiCol_ButtonHovered]        = C_HOV;
    st.Colors[ImGuiCol_ButtonActive]         = C_SEL;
    st.Colors[ImGuiCol_Header]               = C_SEL;
    st.Colors[ImGuiCol_HeaderHovered]        = C_HOV;
    st.Colors[ImGuiCol_HeaderActive]         = C_SEL;
    st.Colors[ImGuiCol_Tab]                  = C_SIDE;
    st.Colors[ImGuiCol_TabHovered]           = C_HOV;
    st.Colors[ImGuiCol_TabActive]            = C_ACC;
    st.Colors[ImGuiCol_ScrollbarBg]          = {0,0,0,0};
    st.Colors[ImGuiCol_ScrollbarGrab]        = {0.22f,0.22f,0.26f,1.f};
    st.Colors[ImGuiCol_ScrollbarGrabHovered] = {0.28f,0.28f,0.32f,1.f};
    st.Colors[ImGuiCol_ScrollbarGrabActive]  = C_ACC;
    st.Colors[ImGuiCol_Separator]            = C_SEP;
    st.Colors[ImGuiCol_ResizeGrip]           = {0,0,0,0};
    st.Colors[ImGuiCol_ResizeGripHovered]    = C_ACC;
    st.Colors[ImGuiCol_ResizeGripActive]     = C_ACC_D;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void ProjectHub::BeginFrame()
{
    glClearColor(C_BG.x, C_BG.y, C_BG.z, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}
void ProjectHub::EndFrame()
{
    ImGui::Render();
    int w, h; glfwGetFramebufferSize(m_Window, &w, &h);
    glViewport(0, 0, w, h);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_Window);
}

// ---------------------------------------------------------------------------
// Janela principal
// ---------------------------------------------------------------------------
void ProjectHub::RenderMainWindow()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f,0.f});
    ImGui::Begin("##HubRoot", nullptr,
        ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();

    if (!LegalNotice::DrawFirstRunAcceptance()) { ImGui::End(); return; }

    // ---- SIDEBAR ----
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_SIDE);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f,0.f});
    ImGui::BeginChild("##Sidebar", {SIDEBAR_W, 0.f}, false);
    ImGui::PopStyleVar();
    {
        ImGui::Dummy({0, 20.f});

        // Logo + nome
        if (m_LogoTexture)
        {
            const float lsz = 40.f;
            ImGui::SetCursorPosX((SIDEBAR_W - lsz) * 0.5f);
            ImGui::Image((ImTextureID)(intptr_t)m_LogoTexture, {lsz,lsz});
        }

        ImGui::Dummy({0, 8.f});
        if (m_FontBold) ImGui::PushFont(m_FontBold);
        const char* appName = "Marae Engine";
        ImGui::SetCursorPosX((SIDEBAR_W - ImGui::CalcTextSize(appName).x) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, C_TXT);
        ImGui::TextUnformatted(appName);
        ImGui::PopStyleColor();
        if (m_FontBold) ImGui::PopFont();

        if (m_FontSmall) ImGui::PushFont(m_FontSmall);
        const char* ver = "v0.1.0 alpha";
        ImGui::SetCursorPosX((SIDEBAR_W - ImGui::CalcTextSize(ver).x) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_M);
        ImGui::TextUnformatted(ver);
        ImGui::PopStyleColor();
        if (m_FontSmall) ImGui::PopFont();

        ImGui::Dummy({0, 20.f});

        // Divisor
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(
                {p.x+16.f, p.y}, {p.x+SIDEBAR_W-16.f, p.y},
                ImGui::ColorConvertFloat4ToU32(C_SEP), 1.f);
            ImGui::Dummy({0,8.f});
        }

        const float btnX = 0.f;
        const float btnW = SIDEBAR_W;
        const float btnH = 32.f;

        // --- Novo Projeto ---
        ImGui::SetCursorPosX(btnX);
        if (AccentBtn((std::string("  ") + L("hub.new_project") + "  ##new").c_str(), {btnW, 36.f}))
        {
            m_ShowNewModal = true; m_NewModalError.clear();
            memset(m_NewName, 0, sizeof(m_NewName));
            m_NewWorldType = ProjectWorldType::World3D;
        }
        ImGui::Dummy({0,4.f});

        // --- Abrir Pasta ---
        ImGui::SetCursorPosX(btnX);
        if (GhostBtn((std::string("  ") + L("hub.open_folder") + "  ##open").c_str(), {btnW, btnH}))
        {
            std::string picked = BrowseFolder();
            if (!picked.empty()) OpenProject(ProjectManager::DiscoverProject(picked));
        }

        ImGui::Dummy({0,16.f});
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(
                {p.x+16.f, p.y}, {p.x+SIDEBAR_W-16.f, p.y},
                ImGui::ColorConvertFloat4ToU32(C_SEP), 1.f);
            ImGui::Dummy({0,8.f});
        }

        // --- Configuracoes ---
        ImGui::SetCursorPosX(btnX);
        if (GhostBtn((std::string("  ") + L("hub.settings") + "   ##cfg").c_str(),
                     {btnW, btnH}, m_ShowSettings && !m_ShowTerms))
        { m_ShowSettings = !m_ShowSettings; m_ShowTerms = false; }

        ImGui::Dummy({0,2.f});

        // --- Termos ---
        ImGui::SetCursorPosX(btnX);
        if (GhostBtn((std::string("  ") + L("hub.terms_policies") + " ##terms").c_str(),
                     {btnW, btnH}, m_ShowTerms))
        { m_ShowTerms = !m_ShowTerms; if (m_ShowTerms) m_ShowSettings = false; }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine(0.f, 0.f);

    // Linha divisoria sidebar / conteudo
    {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const ImVec2 winPos = ImGui::GetWindowPos();
        dl->AddLine(
            {winPos.x + SIDEBAR_W, winPos.y},
            {winPos.x + SIDEBAR_W, winPos.y + vp->Size.y},
            ImGui::ColorConvertFloat4ToU32(C_SEP), 1.f);
    }

    // ---- CONTEUDO ----
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_BG);
    ImGui::BeginChild("##Content", {0.f,0.f}, false);
    {
        if (m_ShowTerms)        RenderTermsPanel();
        else if (m_ShowSettings) RenderSettingsPanel();
        else                     RenderProjectList();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (m_ShowNewModal) RenderNewProjectModal();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Lista de projetos — linhas limpas, estilo painel
// ---------------------------------------------------------------------------
void ProjectHub::RenderProjectList()
{
    // Cabecalho
    ImGui::SetCursorPos({20.f, 18.f});

    // filtro + titulo na mesma barra
    const float searchW = 200.f;
    const float availW  = ImGui::GetContentRegionAvail().x;

    if (m_FontTitle) ImGui::PushFont(m_FontTitle);
    ImGui::PushStyleColor(ImGuiCol_Text, C_TXT);
    ImGui::TextUnformatted(L("hub.recent_projects"));
    ImGui::PopStyleColor();
    if (m_FontTitle) ImGui::PopFont();

    ImGui::SameLine();
    ImGui::SetCursorPosX(availW - searchW + 20.f);
    ImGui::SetNextItemWidth(searchW);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, C_INPUT);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f);
    ImGui::InputTextWithHint("##search", "Filtrar...", m_SearchFilter, sizeof(m_SearchFilter));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.f);
    DrawSeparator();
    ImGui::Dummy({0,4.f});

    // Cabecalho de coluna (como VS Code / Linear)
    const float actionW = 190.f;
    const float listW   = availW - actionW - 20.f - 8.f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_BG);
    ImGui::BeginChild("##List", {listW, 0.f}, false);
    ImGui::PopStyleColor();
    {
        if (m_Projects.empty())
        {
            ImGui::Dummy({0,60.f});
            ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_M);
            const char* msg = "Nenhum projeto recente.";
            ImGui::SetCursorPosX((listW - ImGui::CalcTextSize(msg).x) * 0.5f);
            ImGui::TextUnformatted(msg);
            ImGui::PopStyleColor();
            ImGui::Dummy({0,10.f});
            ImGui::SetCursorPosX((listW - 180.f) * 0.5f);
            if (AccentBtn(UI("Criar novo projeto"), {180.f, 32.f}))
            { m_ShowNewModal = true; m_NewModalError.clear(); memset(m_NewName,0,sizeof(m_NewName)); }
        }
        else
        {
            std::string fl(m_SearchFilter);
            std::transform(fl.begin(), fl.end(), fl.begin(), [](unsigned char c){ return (char)std::tolower(c); });

            int openIdx = -1;
            for (int i = 0; i < (int)m_Projects.size(); ++i)
            {
                const ProjectInfo& p = m_Projects[(size_t)i];

                if (!fl.empty())
                {
                    std::string nl = p.Name;
                    std::transform(nl.begin(), nl.end(), nl.begin(), [](unsigned char c){ return (char)std::tolower(c); });
                    if (nl.find(fl) == std::string::npos) continue;
                }

                const bool exists   = ProjectManager::ProjectExists(p);
                const bool selected = (m_SelectedProject == i);

                ImGui::PushID(i);

                ImVec2 rowMin = ImGui::GetCursorScreenPos();
                float  rowW   = ImGui::GetContentRegionAvail().x;
                constexpr float ROW_H = 52.f;
                ImVec2 rowMax = {rowMin.x + rowW, rowMin.y + ROW_H};

                ImDrawList* dl = ImGui::GetWindowDrawList();

                // fundo da linha
                if (selected)
                    dl->AddRectFilled(rowMin, rowMax, ImGui::ColorConvertFloat4ToU32(C_SEL));

                // separador inferior (linha fina)
                dl->AddLine({rowMin.x, rowMax.y-1}, {rowMax.x, rowMax.y-1},
                    ImGui::ColorConvertFloat4ToU32(C_SEP));

                // Conteudo: nome (semibold) + path + meta
                const float textX = rowMin.x + 14.f;

                ImGui::SetCursorScreenPos({textX, rowMin.y + 8.f});
                if (m_FontBold) ImGui::PushFont(m_FontBold);
                ImGui::PushStyleColor(ImGuiCol_Text, exists ? C_TXT : C_DANGER);
                ImGui::TextUnformatted(p.Name.c_str());
                ImGui::PopStyleColor();
                if (m_FontBold) ImGui::PopFont();

                // tipo do projeto (2D/3D) na mesma linha do nome, aposto
                ImGui::SameLine();
                if (m_FontSmall) ImGui::PushFont(m_FontSmall);
                ImGui::PushStyleColor(ImGuiCol_Text, selected ? C_ACC : C_TXT_M);
                ImGui::Text("[%s]", p.WorldType == ProjectWorldType::World2D ? "2D" : "3D");
                ImGui::PopStyleColor();
                if (m_FontSmall) ImGui::PopFont();

                // path
                ImGui::SetCursorScreenPos({textX, rowMin.y + 24.f});
                if (m_FontSmall) ImGui::PushFont(m_FontSmall);
                ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_D);
                // trunca path se muito longa
                std::string displayPath = p.Path;
                if (displayPath.size() > 60) displayPath = "..." + displayPath.substr(displayPath.size()-57);
                ImGui::TextUnformatted(displayPath.c_str());
                ImGui::PopStyleColor();

                // timestamp e qualidade
                ImGui::SetCursorScreenPos({textX, rowMin.y + 36.f});
                ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_M);
                char ts[64]={};
                ImGui::Text("%s  |  %s", FmtTime(p.LastOpenedTimestamp, ts), QName(p.Quality));
                ImGui::PopStyleColor();
                if (m_FontSmall) ImGui::PopFont();

                // area clicavel
                ImGui::SetCursorScreenPos(rowMin);
                ImGui::InvisibleButton("##row", {rowW, ROW_H});
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))  m_SelectedProject = i;
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && exists)
                    openIdx = i;
                // hover sutil quando nao selecionado
                if (ImGui::IsItemHovered() && !selected)
                    dl->AddRectFilled(rowMin, rowMax, IM_COL32(255,255,255,6));

                ImGui::SetCursorScreenPos({rowMin.x, rowMax.y});
                ImGui::PopID();
            }
            if (openIdx >= 0) OpenProject(m_Projects[(size_t)openIdx]);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine(0.f, 8.f);

    // ---- Painel de acoes ----
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_PANEL);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.f, 12.f});
    ImGui::BeginChild("##Actions", {actionW, 0.f}, false);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    {
        const bool valid = m_SelectedProject >= 0 && m_SelectedProject < (int)m_Projects.size();

        if (!valid)
        {
            if (m_FontSmall) ImGui::PushFont(m_FontSmall);
            ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_M);
            ImGui::TextWrapped(UI("Selecione um projeto para ver as acoes."));
            ImGui::PopStyleColor();
            if (m_FontSmall) ImGui::PopFont();
        }
        else
        {
            const ProjectInfo& sel = m_Projects[(size_t)m_SelectedProject];
            const bool exists = ProjectManager::ProjectExists(sel);

            // nome no topo do painel de acoes
            if (m_FontBold) ImGui::PushFont(m_FontBold);
            ImGui::PushStyleColor(ImGuiCol_Text, C_TXT);
            ImGui::TextWrapped("%s", sel.Name.c_str());
            ImGui::PopStyleColor();
            if (m_FontBold) ImGui::PopFont();

            if (m_FontSmall) ImGui::PushFont(m_FontSmall);
            ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_M);
            ImGui::Text("%s  |  %s", sel.WorldType == ProjectWorldType::World2D ? "2D" : "3D", QName(sel.Quality));
            ImGui::PopStyleColor();
            if (m_FontSmall) ImGui::PopFont();

            ImGui::Dummy({0,8.f});
            DrawSeparator();
            ImGui::Dummy({0,8.f});

            if (AccentBtn(UI("Abrir projeto"), {-1.f, 32.f}) && exists)
                OpenProject(sel);

            if (!exists)
            {
                if (m_FontSmall) ImGui::PushFont(m_FontSmall);
                ImGui::PushStyleColor(ImGuiCol_Text, C_DANGER);
                ImGui::TextWrapped(UI("Pasta nao encontrada."));
                ImGui::PopStyleColor();
                if (m_FontSmall) ImGui::PopFont();
            }

            ImGui::Dummy({0,4.f});

            if (ImGui::Button(UI("Renomear"), {-1.f, 28.f}))
            { strncpy_s(m_RenameBuffer, sel.Name.c_str(), sizeof(m_RenameBuffer)-1); m_ShowRenameModal = true; }
            if (ImGui::Button(UI("Abrir pasta"), {-1.f, 28.f}))
            {
#ifdef _WIN32
                ShellExecuteA(nullptr, "open", sel.Path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
            }
            if (ImGui::Button(UI("Remover da lista"), {-1.f, 28.f}))
            { m_DeleteProjectFiles = false; m_ShowDeleteModal = true; }

            ImGui::Dummy({0,8.f});
            DrawSeparator();
            ImGui::Dummy({0,8.f});

            ImGui::PushStyleColor(ImGuiCol_Button,        {0.18f,0.08f,0.08f,1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, C_DANGER);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.60f,0.10f,0.10f,1.f});
            if (ImGui::Button(UI("Apagar projeto"), {-1.f, 28.f}))
            { m_DeleteProjectFiles = true; m_ShowDeleteModal = true; }
            ImGui::PopStyleColor(3);
        }
    }
    ImGui::EndChild();

    if (m_ShowRenameModal)     RenderRenameProjectModal();
    if (m_ShowDeleteModal)     RenderDeleteProjectModal();
    if (m_ShowHardwareWarning) RenderHardwareWarningModal();
}

// ---------------------------------------------------------------------------
// Modal renomear
// ---------------------------------------------------------------------------
void ProjectHub::RenderRenameProjectModal()
{
    ImGui::OpenPopup("Renomear projeto##RN");
    ImGui::SetNextWindowSize({380.f,0.f}, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f,0.5f});
    if (!ImGui::BeginPopupModal("Renomear projeto##RN", nullptr, ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoMove))
        return;

    ImGui::TextUnformatted(UI("Nome exibido no Project Hub:"));
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputText(UI("##RNinput"), m_RenameBuffer, sizeof(m_RenameBuffer));
    ImGui::Dummy({0,8.f});
    ImGui::Separator();
    ImGui::Dummy({0,6.f});
    if (ImGui::Button(UI("Cancelar"), {110.f, 30.f})) { m_ShowRenameModal = false; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (AccentBtn(UI("Salvar"), {110.f,30.f}) &&
        m_SelectedProject >= 0 && m_SelectedProject < (int)m_Projects.size() && m_RenameBuffer[0])
    {
        m_Projects[(size_t)m_SelectedProject].Name = m_RenameBuffer;
        ProjectManager::SaveProjects(m_Projects);
        m_ShowRenameModal = false; ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Modal excluir
// ---------------------------------------------------------------------------
void ProjectHub::RenderDeleteProjectModal()
{
    ImGui::OpenPopup("Confirmar acao##DEL");
    ImGui::SetNextWindowSize({420.f,0.f}, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f,0.5f});
    if (!ImGui::BeginPopupModal("Confirmar acao##DEL", nullptr, ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoMove))
        return;

    const bool valid = m_SelectedProject >= 0 && m_SelectedProject < (int)m_Projects.size();
    if (valid)
    {
        const ProjectInfo& proj = m_Projects[(size_t)m_SelectedProject];
        ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_D);
        ImGui::TextWrapped(UI("%s"), m_DeleteProjectFiles
            ? "Apagar remove a pasta e todos os arquivos do projeto."
            : "Remover retira o projeto da lista. Os arquivos sao preservados.");
        ImGui::PopStyleColor();
        ImGui::Dummy({0,6.f});
        if (m_FontBold) ImGui::PushFont(m_FontBold);
        ImGui::TextUnformatted(proj.Name.c_str());
        if (m_FontBold) ImGui::PopFont();
        if (m_FontSmall) ImGui::PushFont(m_FontSmall);
        ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_M);
        ImGui::TextUnformatted(proj.Path.c_str());
        ImGui::PopStyleColor();
        if (m_FontSmall) ImGui::PopFont();
    }
    ImGui::Dummy({0,8.f});
    ImGui::Separator();
    ImGui::Dummy({0,6.f});
    if (ImGui::Button(UI("Cancelar"), {110.f,30.f})) { m_ShowDeleteModal = false; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        C_DANGER);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.95f,0.25f,0.25f,1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.65f,0.10f,0.10f,1.f});
    if (ImGui::Button(m_DeleteProjectFiles ? "Apagar arquivos" : "Remover da lista", {160.f,30.f}) && valid)
    {
        const ProjectInfo proj = m_Projects[(size_t)m_SelectedProject];
        if (m_DeleteProjectFiles && ProjectManager::ProjectExists(proj))
        { std::error_code ec; fs::remove_all(fs::path(proj.Path), ec); }
        ProjectManager::RemoveProject(m_Projects, proj.Path);
        ProjectManager::SaveProjects(m_Projects);
        m_SelectedProject = -1; m_ShowDeleteModal = false; ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(3);
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Modal novo projeto
// ---------------------------------------------------------------------------
void ProjectHub::RenderNewProjectModal()
{
    const std::string title = std::string(L("hub.new_project_title")) + "###NPM";
    ImGui::OpenPopup(title.c_str());
    ImGui::SetNextWindowSize({560.f,0.f}, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f,0.5f});
    if (!ImGui::BeginPopupModal(title.c_str(), &m_ShowNewModal, ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove))
        return;

    ImGui::Dummy({0,4.f});

    // Nome
    ImGui::TextUnformatted(L("hub.project_name"));
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputText(UI("##npname"), m_NewName, sizeof(m_NewName));
    ImGui::Dummy({0,8.f});

    // Diretorio
    ImGui::TextUnformatted(L("hub.directory"));
    float bW = 80.f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bW - 8.f);
    ImGui::InputText(UI("##npdir"), m_NewDirectory, sizeof(m_NewDirectory));
    ImGui::SameLine();
    if (ImGui::Button(L("hub.browse"), {bW,0.f}))
    { std::string p = BrowseFolder(); if (!p.empty()) strncpy_s(m_NewDirectory, p.c_str(), sizeof(m_NewDirectory)-1); }

    ImGui::Dummy({0,14.f});
    ImGui::Separator();
    ImGui::Dummy({0,10.f});

    // --- Tipo ---
    if (m_FontBold) ImGui::PushFont(m_FontBold);
    ImGui::TextUnformatted(UI("Tipo de projeto"));
    if (m_FontBold) ImGui::PopFont();
    if (m_FontSmall) ImGui::PushFont(m_FontSmall);
    ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_M);
    ImGui::TextUnformatted("Essa decisao nao pode ser alterada depois.");
    ImGui::PopStyleColor();
    if (m_FontSmall) ImGui::PopFont();
    ImGui::Dummy({0,6.f});

    struct WCard { ProjectWorldType t; const char* label; const char* desc; };
    static const WCard wc[] = {
        {ProjectWorldType::World2D, "2D", "Sprites, UI e jogos ortograficos."},
        {ProjectWorldType::World3D, "3D", "Viewport 3D, iluminacao e assets tridimensionais."},
    };
    const float wcW = (ImGui::GetContentRegionAvail().x - 8.f) * 0.5f;
    const float wcH = 60.f;
    for (int i=0;i<2;++i)
    {
        if (i>0) ImGui::SameLine(0.f,8.f);
        const bool sel = (m_NewWorldType == wc[i].t);
        ImVec2 mn = ImGui::GetCursorScreenPos();
        ImVec2 mx = {mn.x+wcW, mn.y+wcH};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 bg = sel ? ImGui::ColorConvertFloat4ToU32(C_SEL) : ImGui::ColorConvertFloat4ToU32(C_INPUT);
        dl->AddRectFilled(mn, mx, bg, 3.f);
        dl->AddRect(mn, mx, sel ? ImGui::ColorConvertFloat4ToU32(C_ACC) : IM_COL32(50,50,60,255), 3.f, 0, sel?2.f:1.f);
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos({mn.x+10.f, mn.y+8.f});
        if (m_FontBold) ImGui::PushFont(m_FontBold);
        ImGui::PushStyleColor(ImGuiCol_Text, sel ? C_ACC : C_TXT);
        ImGui::TextUnformatted(wc[i].label);
        ImGui::PopStyleColor();
        if (m_FontBold) ImGui::PopFont();
        ImGui::SetCursorScreenPos({mn.x+10.f, mn.y+26.f});
        if (m_FontSmall) ImGui::PushFont(m_FontSmall);
        ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_D);
        ImGui::TextUnformatted(wc[i].desc);
        ImGui::PopStyleColor();
        if (m_FontSmall) ImGui::PopFont();
        ImGui::SetCursorScreenPos(mn);
        if (ImGui::InvisibleButton("##wc", {wcW,wcH})) m_NewWorldType = wc[i].t;
        ImGui::PopID();
        ImGui::SetCursorScreenPos({mx.x, mn.y});
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + wcH + 10.f);

    ImGui::Separator();
    ImGui::Dummy({0,10.f});

    // --- Qualidade grafica ---
    if (m_FontBold) ImGui::PushFont(m_FontBold);
    ImGui::TextUnformatted(L("hub.graphics_quality"));
    if (m_FontBold) ImGui::PopFont();
    ImGui::Dummy({0,6.f});

    struct QCard { GraphicsQuality q; const char* n; const char* d; };
    static const QCard qc[] = {
        {GraphicsQuality::UltraPerformance, L("hub.quality_ultra_performance_name"), L("hub.quality_ultra_performance")},
        {GraphicsQuality::Performance,      L("hub.quality_performance_name"),       L("hub.quality_performance")},
        {GraphicsQuality::Balanced,         L("hub.quality_balanced_name"),          L("hub.quality_balanced")},
        {GraphicsQuality::HighFidelity,     L("hub.quality_high_name"),              L("hub.quality_high")},
        {GraphicsQuality::Ultra,            L("hub.quality_ultra_name"),             L("hub.quality_ultra")},
    };
    const float qW = (ImGui::GetContentRegionAvail().x - 32.f)/5.f;
    const float qH = 100.f;
    for (int i=0;i<5;++i)
    {
        if (i>0) ImGui::SameLine(0.f,8.f);
        const bool sel = (m_NewQuality == qc[i].q);
        ImVec2 mn = ImGui::GetCursorScreenPos();
        ImVec2 mx = {mn.x+qW, mn.y+qH};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 bg = sel ? ImGui::ColorConvertFloat4ToU32(C_SEL) : ImGui::ColorConvertFloat4ToU32(C_INPUT);
        dl->AddRectFilled(mn, mx, bg, 3.f);
        dl->AddRect(mn, mx, sel ? ImGui::ColorConvertFloat4ToU32(C_ACC) : IM_COL32(50,50,60,255), 3.f, 0, sel?2.f:1.f);
        ImGui::PushID(i+10);
        ImGui::SetCursorScreenPos({mn.x+6.f, mn.y+6.f});
        if (m_FontBold) ImGui::PushFont(m_FontBold);
        ImGui::PushStyleColor(ImGuiCol_Text, sel ? C_ACC : C_TXT);
        ImGui::TextUnformatted(qc[i].n);
        ImGui::PopStyleColor();
        if (m_FontBold) ImGui::PopFont();
        ImGui::SetCursorScreenPos({mn.x+6.f, mn.y+22.f});
        if (m_FontSmall) ImGui::PushFont(m_FontSmall);
        ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_D);
        ImGui::PushTextWrapPos(mx.x-4.f);
        ImGui::TextWrapped(UI("%s"), qc[i].d);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (m_FontSmall) ImGui::PopFont();
        ImGui::SetCursorScreenPos(mn);
        if (ImGui::InvisibleButton("##qc", {qW,qH})) m_NewQuality = qc[i].q;
        ImGui::PopID();
        ImGui::SetCursorScreenPos({mx.x, mn.y});
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + qH + 8.f);

    if (!m_NewModalError.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, C_DANGER);
        ImGui::TextWrapped(UI("%s"), m_NewModalError.c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy({0,4.f});
    }

    ImGui::Separator();
    ImGui::Dummy({0,8.f});
    float bw = 120.f;
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - bw*2.f - 8.f + 16.f);
    if (ImGui::Button(L("common.cancel"), {bw,32.f})) { m_ShowNewModal=false; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine(0.f,8.f);
    if (AccentBtn(L("hub.create_project"), {bw,32.f}))
    {
        m_NewModalError.clear();
        if (!strlen(m_NewName))       m_NewModalError = "Digite um nome para o projeto.";
        else if (!strlen(m_NewDirectory)) m_NewModalError = "Escolha um diretorio.";
        else
        {
            ProjectInfo created = ProjectManager::CreateProject(m_NewName, m_NewDirectory, m_NewQuality, m_NewWorldType);
            if (created.Path.empty()) m_NewModalError = "Nao foi possivel criar o projeto.\nVerifique as permissoes da pasta.";
            else { m_ShowNewModal=false; ImGui::CloseCurrentPopup(); OpenProject(created); }
        }
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Configuracoes
// ---------------------------------------------------------------------------
void ProjectHub::RenderSettingsPanel()
{
    ImGui::SetCursorPos({20.f, 18.f});
    if (m_FontTitle) ImGui::PushFont(m_FontTitle);
    ImGui::TextUnformatted(L("hub.settings_title"));
    if (m_FontTitle) ImGui::PopFont();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY()+8.f);
    DrawSeparator();
    ImGui::Dummy({0,10.f});

    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_BG);
    ImGui::BeginChild("##SettScroll", {0.f,0.f}, false);
    ImGui::PopStyleColor();
    ImGui::Indent(20.f);

    // Idioma
    if (m_FontBold) ImGui::PushFont(m_FontBold);
    ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_D);
    ImGui::TextUnformatted(L("hub.language_title"));
    ImGui::PopStyleColor();
    if (m_FontBold) ImGui::PopFont();
    ImGui::Dummy({0,4.f});

    auto& loc = Localization::Get();
    int lang = 0;
    switch (loc.GetLanguage())
    {
        case EditorLanguage::Auto:       lang = 0; break;
        case EditorLanguage::Portuguese: lang = 1; break;
        case EditorLanguage::English:    lang = 2; break;
        case EditorLanguage::Spanish:    lang = 3; break;
        default:                         lang = 0; break;
    }

    const char* langs[] = {
        L("hub.language_auto"),
        L("hub.language_pt"),
        L("hub.language_en"),
        L("hub.language_es")
    };
    ImGui::SetNextItemWidth(230.f);
    if (ImGui::Combo(UI("##lang"), &lang, langs, 4))
    {
        if (lang == 0)      loc.SetLanguage(EditorLanguage::Auto);
        else if (lang == 1) loc.SetLanguage(EditorLanguage::Portuguese);
        else if (lang == 2) loc.SetLanguage(EditorLanguage::English);
        else if (lang == 3) loc.SetLanguage(EditorLanguage::Spanish);
    }

    if (loc.GetLanguage() == EditorLanguage::Auto)
    {
        EditorLanguage sys = loc.GetResolvedLanguage();
        const char* sysName = (sys == EditorLanguage::Portuguese) ? "Portugu\xc3\xaas" :
                              (sys == EditorLanguage::Spanish)    ? "Espa\xc3\xb1ol" : "English";
        ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_M);
        ImGui::Text(UI("Sistema: %s"), sysName);
        ImGui::PopStyleColor();
    }

    std::string genStatus = loc.GetLangGenerationStatus();
    if (!genStatus.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.70f, 1.0f, 1.0f));
        ImGui::TextUnformatted(genStatus.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Dummy({0,16.f});

    // Interface
    if (m_FontBold) ImGui::PushFont(m_FontBold);
    ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_D);
    ImGui::TextUnformatted(L("hub.interface"));
    ImGui::PopStyleColor();
    if (m_FontBold) ImGui::PopFont();
    ImGui::Dummy({0,4.f});

    ImGui::Checkbox(L("hub.show_fps"), &m_Settings.ShowFPS);
    ImGui::Dummy({0,4.f});
    ImGui::TextUnformatted(L("hub.font_scale"));
    ImGui::SetNextItemWidth(200.f);
    if (ImGui::SliderFloat(UI("##fs"), &m_Settings.FontScale, 0.75f, 1.5f, "%.2fx"))
        ImGui::GetIO().FontGlobalScale = m_Settings.FontScale;

    ImGui::Dummy({0,16.f});

    // Renderizacao
    if (m_FontBold) ImGui::PushFont(m_FontBold);
    ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_D);
    ImGui::TextUnformatted(L("hub.rendering"));
    ImGui::PopStyleColor();
    if (m_FontBold) ImGui::PopFont();
    ImGui::Dummy({0,4.f});
    if (ImGui::Checkbox(L("common.vsync"), &m_Settings.VSync))
        glfwSwapInterval(m_Settings.VSync ? 1 : 0);

    ImGui::Dummy({0,16.f});

    // Salvamento
    if (m_FontBold) ImGui::PushFont(m_FontBold);
    ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_D);
    ImGui::TextUnformatted(L("hub.saving"));
    ImGui::PopStyleColor();
    if (m_FontBold) ImGui::PopFont();
    ImGui::Dummy({0,4.f});
    ImGui::Checkbox(L("hub.autosave"), &m_Settings.AutoSave);
    if (m_Settings.AutoSave)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.f);
        ImGui::InputInt(UI("##asec"), &m_Settings.AutoSaveIntervalS, 30);
        ImGui::SameLine(); ImGui::TextUnformatted(L("hub.seconds"));
        m_Settings.AutoSaveIntervalS = std::max(30, m_Settings.AutoSaveIntervalS);
    }

    ImGui::Dummy({0,20.f});

    if (AccentBtn(L("hub.save_settings"), {180.f, 32.f}))
    {
        SaveEditorSettings();
        m_SettingsSavedFeedback = true;
    }
    if (m_SettingsSavedFeedback)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, {0.3f,0.78f,0.4f,1.f});
        ImGui::TextUnformatted(UI("Salvo."));
        ImGui::PopStyleColor();
        static int ft = 0; ft++;
        if (ft > 120) { m_SettingsSavedFeedback=false; ft=0; }
    }

    ImGui::Unindent(20.f);
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// OpenProject
// ---------------------------------------------------------------------------
void ProjectHub::OpenProject(const ProjectInfo& project)
{
    if (project.WorldType == ProjectWorldType::World3D)
    {
        MEMORYSTATUSEX mem{}; mem.dwLength=sizeof(mem);
        const bool hasMem = GlobalMemoryStatusEx(&mem)!=FALSE;
        SYSTEM_INFO cpu{}; GetSystemInfo(&cpu);
        const uint64_t ramGB = hasMem ? mem.ullTotalPhys/(1024ull*1024ull*1024ull) : 0;
        const unsigned cores = cpu.dwNumberOfProcessors;
        const GLubyte* rb = glGetString(GL_RENDERER);
        const std::string gpu = rb ? reinterpret_cast<const char*>(rb) : "GPU nao identificada";
        std::string gpuL=gpu; std::transform(gpuL.begin(),gpuL.end(),gpuL.begin(),[](unsigned char c){return (char)std::tolower(c);});
        const bool sw = gpuL.find("microsoft basic")!=std::string::npos||gpuL.find("gdi generic")!=std::string::npos||gpuL.find("llvmpipe")!=std::string::npos||gpuL.find("swiftshader")!=std::string::npos;
        const bool arc = gpuL.find("arc")!=std::string::npos;
        const bool intelRisk = !arc&&(gpuL.find("intel(r) uhd")!=std::string::npos||gpuL.find("intel uhd")!=std::string::npos||gpuL.find("intel(r) hd graphics")!=std::string::npos||gpuL.find("intel hd graphics")!=std::string::npos);
        uint64_t minRam=4; unsigned minCores=2; bool needHW=false;
        switch(project.Quality){ case GraphicsQuality::Performance:minRam=4;minCores=2;break; case GraphicsQuality::Balanced:minRam=8;minCores=4;break; case GraphicsQuality::HighFidelity:minRam=12;minCores=6;break; case GraphicsQuality::Ultra:minRam=16;minCores=8;needHW=true;break; default:break; }
        const bool below = (hasMem&&ramGB<minRam)||cores<minCores||(needHW&&sw);
        if (below||intelRisk)
        {
            m_PendingHardwareProject=project; m_HardwareDriverRisk=intelRisk;
            char det[512];
            std::snprintf(det,sizeof(det),"Perfil: %s\nRAM: %s%llu GB (min %llu GB)\nCores: %u (min %u)\nGPU: %s%s%s",
                QName(project.Quality),hasMem?"":"indisponivel; ",(unsigned long long)ramGB,(unsigned long long)minRam,
                cores,minCores,gpu.c_str(),sw?" — software":"",intelRisk?"\nDriver Intel integrado com risco de falha.":"");
            m_HardwareWarningDetails=det; m_ShowHardwareWarning=true; return;
        }
    }
    LaunchProject(project);
}

void ProjectHub::RenderHardwareWarningModal()
{
    ImGui::OpenPopup("Verificacao de hardware##HW");
    ImGui::SetNextWindowSize({460.f,0.f},ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),ImGuiCond_Always,{0.5f,0.5f});
    if (!ImGui::BeginPopupModal("Verificacao de hardware##HW",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoMove)) return;
    ImGui::TextWrapped(UI("%s"), m_HardwareDriverRisk
        ? "GPU Intel integrada com falha critica de driver conhecida. Pode causar tela preta ou crash."
        : "Hardware abaixo do recomendado para este perfil. O projeto pode ficar lento ou nao abrir.");
    ImGui::Dummy({0,6.f});
    if (m_FontSmall) ImGui::PushFont(m_FontSmall);
    ImGui::PushStyleColor(ImGuiCol_Text, C_TXT_D);
    ImGui::TextWrapped(UI("%s"),m_HardwareWarningDetails.c_str());
    ImGui::PopStyleColor();
    if (m_FontSmall) ImGui::PopFont();
    ImGui::Dummy({0,8.f}); ImGui::Separator(); ImGui::Dummy({0,6.f});
    if (ImGui::Button(UI("Voltar"),{130.f,30.f})) { m_ShowHardwareWarning=false; m_HardwareDriverRisk=false; ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, C_DANGER);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0.95f,0.25f,0.25f,1.f});
    if (ImGui::Button(UI("Continuar mesmo assim"),{190.f,30.f}))
    { const ProjectInfo p=m_PendingHardwareProject; m_ShowHardwareWarning=false; m_HardwareDriverRisk=false; ImGui::CloseCurrentPopup(); LaunchProject(p); }
    ImGui::PopStyleColor(2);
    ImGui::EndPopup();
}

void ProjectHub::LaunchProject(const ProjectInfo& project)
{
    const ProjectInfo reg = ProjectManager::RegisterProject(project);
    if (reg.Path.empty()) return;
    m_Projects = ProjectManager::LoadProjects();
    ProjectManager::SortByRecent(m_Projects);
    m_Result.ShouldLaunch = true;
    m_Result.ProjectPath  = reg.Path;
    m_Result.ScenePath    = reg.ScenePath;
    m_Result.Quality      = reg.Quality;
    m_Result.WorldType    = reg.WorldType;
    m_Running = false;
    glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
}

std::string ProjectHub::BrowseFolder()
{
    return ProjectManager::BrowseForProjectFolder("Selecione a pasta do projeto");
}

std::string ProjectHub::GetEditorSettingsPath()
{
    fs::path dir = fs::path(ProjectManager::GetUserDocumentsPath()) / "MaraeEngine";
    fs::create_directories(dir);
    return (dir/"editor_settings.cfg").string();
}

void ProjectHub::SaveEditorSettings()
{
    std::ofstream f(GetEditorSettingsPath());
    if (!f) return;
    f<<"AutoSave="<<m_Settings.AutoSave<<"\n";
    f<<"AutoSaveInterval="<<m_Settings.AutoSaveIntervalS<<"\n";
    f<<"FontScale="<<m_Settings.FontScale<<"\n";
    f<<"ShowFPS="<<m_Settings.ShowFPS<<"\n";
    f<<"VSync="<<m_Settings.VSync<<"\n";
}

void ProjectHub::LoadEditorSettings()
{
    std::ifstream f(GetEditorSettingsPath());
    if (!f) return;
    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty()||line[0]=='#') continue;
        auto eq=line.find('='); if (eq==std::string::npos) continue;
        std::string k=line.substr(0,eq), v=line.substr(eq+1);
        if      (k=="AutoSave")         m_Settings.AutoSave         =(v=="1");
        else if (k=="AutoSaveInterval") m_Settings.AutoSaveIntervalS=std::stoi(v);
        else if (k=="FontScale")        m_Settings.FontScale        =std::stof(v);
        else if (k=="ShowFPS")          m_Settings.ShowFPS          =(v=="1");
        else if (k=="VSync")            m_Settings.VSync            =(v=="1");
    }
}

void ProjectHub::RenderTermsPanel() { LegalNotice::DrawPanel(); }
