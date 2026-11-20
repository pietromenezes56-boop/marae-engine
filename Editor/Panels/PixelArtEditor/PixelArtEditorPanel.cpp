#include "../../../Engine/Localization/Localization.h"
#include "PixelArtEditorPanel.h"
#include "PixelArtTools.h"
#include "TextureExporter.h"
#include "../../../Core/Application.h"
#include "../../../Scene/Scene/Scene.h"
#include "../../../Scene/Entity/Entity.h"
#include "../../../Renderer/Mesh/Mesh.h"
#include "../../../Renderer/MeshFactory/MeshFactory.h"
#include "../../../Renderer/Mesh/MeshCache.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <glad/glad.h>

#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <sstream>
#include <iomanip>

// ---------------------------------------------------------------------------
// Estado estatico
static bool s_VulkanUIOnly = false;
// ---------------------------------------------------------------------------
std::shared_ptr<PixelCanvas>  PixelArtEditorPanel::s_Canvas;
std::vector<AnimFrame>         PixelArtEditorPanel::s_Frames;
int                            PixelArtEditorPanel::s_CurrentFrame      = 0;
bool                           PixelArtEditorPanel::s_AnimPlaying        = false;
float                          PixelArtEditorPanel::s_AnimTimer          = 0.0f;
bool                           PixelArtEditorPanel::s_ShowAnimPanel      = true;
bool                           PixelArtEditorPanel::s_ShowOnionSkin      = false;
float                          PixelArtEditorPanel::s_OnionSkinAlpha     = 0.35f;

PixelArtEditorPanel::Tool      PixelArtEditorPanel::s_Tool               = PixelArtEditorPanel::Tool::Pencil;
glm::vec4                      PixelArtEditorPanel::s_Color              = glm::vec4(1.f, 0.f, 0.f, 1.f);
int                            PixelArtEditorPanel::s_BrushSize          = 1;
float                          PixelArtEditorPanel::s_BrushSoftness      = 0.5f;
float                          PixelArtEditorPanel::s_BrushOpacity       = 1.0f;
bool                           PixelArtEditorPanel::s_BrushCircular      = false;
bool                           PixelArtEditorPanel::s_MirrorVertical     = false;
bool                           PixelArtEditorPanel::s_MirrorHorizontal   = false;
float                          PixelArtEditorPanel::s_MirrorOffsetX      = 0.0f;
float                          PixelArtEditorPanel::s_MirrorOffsetY      = 0.0f;
std::vector<glm::vec4>         PixelArtEditorPanel::s_Palette;
std::vector<glm::vec4>         PixelArtEditorPanel::s_RecentColors;

float                          PixelArtEditorPanel::s_Zoom               = 16.0f;
ImVec2                         PixelArtEditorPanel::s_Pan                = ImVec2(0.f, 0.f);
bool                           PixelArtEditorPanel::s_ShowGrid           = true;
bool                           PixelArtEditorPanel::s_ShowCheckerboard   = true;
bool                           PixelArtEditorPanel::s_NeedsFit           = true;

std::vector<float>             PixelArtEditorPanel::s_GuideX;
std::vector<float>             PixelArtEditorPanel::s_GuideY;
bool                           PixelArtEditorPanel::s_ShowRuler          = true;
bool                           PixelArtEditorPanel::s_RulerActive        = false;
int                            PixelArtEditorPanel::s_RulerX0            = 0;
int                            PixelArtEditorPanel::s_RulerY0            = 0;
int                            PixelArtEditorPanel::s_RulerX1            = 0;
int                            PixelArtEditorPanel::s_RulerY1            = 0;

uint64_t                       PixelArtEditorPanel::s_TargetEntityID     = 0;
std::string                    PixelArtEditorPanel::s_TargetEntityName;
bool                           PixelArtEditorPanel::s_Drawing            = false;
int                            PixelArtEditorPanel::s_DrawStartX         = 0;
int                            PixelArtEditorPanel::s_DrawStartY         = 0;
int                            PixelArtEditorPanel::s_DrawLastX          = -1;
int                            PixelArtEditorPanel::s_DrawLastY          = -1;
int                            PixelArtEditorPanel::s_ShapeCurrentX      = 0;
int                            PixelArtEditorPanel::s_ShapeCurrentY      = 0;
bool                           PixelArtEditorPanel::s_HasLastStrokePoint = false;
ImVec2                         PixelArtEditorPanel::s_CanvasMin          = ImVec2(0.f, 0.f);
ImVec2                         PixelArtEditorPanel::s_CanvasMax          = ImVec2(0.f, 0.f);
std::string                    PixelArtEditorPanel::s_ExportPath;
int                            PixelArtEditorPanel::s_NewCanvasW         = 32;
int                            PixelArtEditorPanel::s_NewCanvasH         = 32;
int                            PixelArtEditorPanel::s_SpriteCounter      = 1;

static std::vector<std::array<int, 4>> BuildSymmetricSegments(
    int x0, int y0, int x1, int y1, int width, int height,
    bool mirrorVertical, bool mirrorHorizontal, float offsetX, float offsetY)
{
    const float axisX = (width - 1) * 0.5f + offsetX;
    const float axisY = (height - 1) * 0.5f + offsetY;
    std::vector<std::array<int, 4>> segments;
    const bool flipsX[] = { false, true, false, true };
    const bool flipsY[] = { false, false, true, true };
    for (int i = 0; i < 4; ++i)
    {
        if ((flipsX[i] && !mirrorVertical) || (flipsY[i] && !mirrorHorizontal))
            continue;

        int ax = flipsX[i] ? static_cast<int>(std::lround(2.0f * axisX - x0)) : x0;
        int ay = flipsY[i] ? static_cast<int>(std::lround(2.0f * axisY - y0)) : y0;
        int bx = flipsX[i] ? static_cast<int>(std::lround(2.0f * axisX - x1)) : x1;
        int by = flipsY[i] ? static_cast<int>(std::lround(2.0f * axisY - y1)) : y1;

        const std::array<int, 4> segment = { ax, ay, bx, by };
        std::array<int, 4> key = segment;
        if (key[0] > key[2] || (key[0] == key[2] && key[1] > key[3]))
        {
            std::swap(key[0], key[2]);
            std::swap(key[1], key[3]);
        }
        const bool duplicate = std::any_of(segments.begin(), segments.end(), [&](const auto& existing)
        {
            std::array<int, 4> existingKey = existing;
            if (existingKey[0] > existingKey[2] ||
                (existingKey[0] == existingKey[2] && existingKey[1] > existingKey[3]))
            {
                std::swap(existingKey[0], existingKey[2]);
                std::swap(existingKey[1], existingKey[3]);
            }
            return existingKey == key;
        });
        if (!duplicate)
            segments.push_back(segment);
    }
    return segments;
}

// ---------------------------------------------------------------------------
// Helpers de conversao de cor
// ---------------------------------------------------------------------------
ImVec4 PixelArtEditorPanel::ToImVec4(const glm::vec4& c)
{
    return ImVec4(c.r, c.g, c.b, c.a);
}

glm::vec4 PixelArtEditorPanel::ToGlmVec4(const ImVec4& c)
{
    return glm::vec4(c.x, c.y, c.z, c.w);
}

const char* PixelArtEditorPanel::ToolName(Tool t)
{
    switch (t)
    {
        case Tool::Pencil:      return "Lapis (B)";
        case Tool::SoftBrush:   return "Pincel Suave (P)";
        case Tool::Eraser:      return "Borracha (E)";
        case Tool::Fill:        return "Balde (G)";
        case Tool::Eyedropper:  return "Conta-gotas (I)";
        case Tool::Line:        return "Linha (L)";
        case Tool::Rect:        return "Retangulo (R)";
        case Tool::RectFilled:  return "Ret. Preenchido";
        case Tool::Circle:      return "Circulo (C)";
        case Tool::CircleFilled:return "Circulo preenchido (Shift+C)";
        case Tool::ReplaceColor:return "Substituir cor (O)";
        case Tool::Pan:         return "Mover / Andar (H / Espaco)";
        case Tool::Ruler:       return "Regua (M)";
        default:                return "Desconhecido";
    }
}

// ---------------------------------------------------------------------------
// Paleta padrao limpa
// ---------------------------------------------------------------------------
static void EnsurePalette(std::vector<glm::vec4>& palette)
{
    if (!palette.empty()) return;
    palette = {
        {0.00f, 0.00f, 0.00f, 1.0f}, // Preto
        {1.00f, 1.00f, 1.00f, 1.0f}, // Branco
        {0.85f, 0.15f, 0.15f, 1.0f}, // Vermelho
        {0.15f, 0.65f, 0.15f, 1.0f}, // Verde
        {0.15f, 0.40f, 0.90f, 1.0f}, // Azul
        {0.95f, 0.85f, 0.10f, 1.0f}, // Amarelo
        {0.10f, 0.85f, 0.85f, 1.0f}, // Ciano
        {0.85f, 0.15f, 0.85f, 1.0f}, // Magenta
        {0.95f, 0.55f, 0.10f, 1.0f}, // Laranja
        {0.55f, 0.30f, 0.10f, 1.0f}, // Marrom
        {0.65f, 0.65f, 0.65f, 1.0f}, // Cinza Claro
        {0.30f, 0.30f, 0.30f, 1.0f}, // Cinza Escuro
        {0.40f, 0.80f, 0.40f, 1.0f}, // Verde Claro
        {0.50f, 0.30f, 0.85f, 1.0f}, // Roxo
        {0.95f, 0.55f, 0.70f, 1.0f}, // Rosa
        {0.00f, 0.00f, 0.00f, 0.0f}, // Transparente
    };
}

// ---------------------------------------------------------------------------
// EnsureFrames
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::EnsureFrames()
{
    if (s_Frames.empty())
    {
        AnimFrame f;
        f.Canvas   = std::make_shared<PixelCanvas>(s_NewCanvasW, s_NewCanvasH, glm::vec4(0.f));
        f.Duration = 0.1f;
        s_Frames.push_back(std::move(f));
        s_CurrentFrame = 0;
        s_NeedsFit = true;
    }
    s_CurrentFrame = std::clamp(s_CurrentFrame, 0, (int)s_Frames.size() - 1);
    s_Canvas = s_Frames[s_CurrentFrame].Canvas;
}

// ---------------------------------------------------------------------------
// AddRecentColor
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::AddRecentColor(const glm::vec4& c)
{
    for (auto it = s_RecentColors.begin(); it != s_RecentColors.end(); ++it)
    {
        if (std::abs(it->r - c.r) < 0.01f && std::abs(it->g - c.g) < 0.01f &&
            std::abs(it->b - c.b) < 0.01f && std::abs(it->a - c.a) < 0.01f)
        {
            s_RecentColors.erase(it);
            break;
        }
    }
    s_RecentColors.insert(s_RecentColors.begin(), c);
    if (s_RecentColors.size() > 8) s_RecentColors.resize(8);
}

// ---------------------------------------------------------------------------
// NewCanvas
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::NewCanvas(int w, int h, const glm::vec4& bg)
{
    s_Frames.clear();
    s_CurrentFrame = 0;
    AnimFrame f;
    f.Canvas   = std::make_shared<PixelCanvas>(w, h, bg);
    f.Duration = 0.1f;
    s_Frames.push_back(std::move(f));
    s_Canvas  = s_Frames[0].Canvas;
    s_Pan     = ImVec2(0.f, 0.f);
    s_MirrorOffsetX = 0.0f;
    s_MirrorOffsetY = 0.0f;
    s_Drawing = false;
    s_HasLastStrokePoint = false;
    s_AnimPlaying = false;
    s_AnimTimer   = 0.0f;
    s_NeedsFit    = true;
}

// ---------------------------------------------------------------------------
// FitToWindow
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::FitToWindow()
{
    if (!s_Canvas) return;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 50.f || avail.y < 50.f)
        avail = ImVec2(800.f, 500.f);

    float zoomX  = (avail.x * 0.75f) / static_cast<float>(s_Canvas->GetWidth());
    float zoomY  = (avail.y * 0.75f) / static_cast<float>(s_Canvas->GetHeight());
    s_Zoom = std::clamp(std::floor(std::min(zoomX, zoomY)), 2.0f, 64.0f);
    s_Pan  = ImVec2(0.f, 0.f);
}

// ---------------------------------------------------------------------------
// ScreenToPixel
// ---------------------------------------------------------------------------
bool PixelArtEditorPanel::ScreenToPixel(const ImVec2& screen, int& outX, int& outY)
{
    if (!s_Canvas) return false;
    float rx = screen.x - s_CanvasMin.x;
    float ry = screen.y - s_CanvasMin.y;
    float canvasW = s_Canvas->GetWidth()  * s_Zoom;
    float canvasH = s_Canvas->GetHeight() * s_Zoom;
    if (rx < 0 || ry < 0 || rx >= canvasW || ry >= canvasH) return false;
    outX = static_cast<int>(rx / s_Zoom);
    outY = static_cast<int>(ry / s_Zoom);
    if (outX >= s_Canvas->GetWidth())  outX = s_Canvas->GetWidth()  - 1;
    if (outY >= s_Canvas->GetHeight()) outY = s_Canvas->GetHeight() - 1;
    return true;
}

// ---------------------------------------------------------------------------
// ApplySoftBrush
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::ApplySoftBrush(int cx, int cy)
{
    if (!s_Canvas) return;
    const int minOffset = -(s_BrushSize - 1) / 2;
    const int maxOffset = s_BrushSize / 2;
    const float centerOffset = (minOffset + maxOffset) * 0.5f;
    const float radius = std::max(0.5f, s_BrushSize * 0.5f);
    const float sigma = std::max(0.5f, radius * std::clamp(s_BrushSoftness, 0.1f, 1.0f));
    const float inv2Sigma2 = 1.0f / (2.0f * sigma * sigma);

    for (int dy = minOffset; dy <= maxOffset; ++dy)
    {
        for (int dx = minOffset; dx <= maxOffset; ++dx)
        {
            const int px = cx + dx;
            const int py = cy + dy;
            if (px < 0 || py < 0 || px >= s_Canvas->GetWidth() || py >= s_Canvas->GetHeight())
                continue;
            const float centeredX = dx - centerOffset;
            const float centeredY = dy - centerOffset;
            const float dist2 = centeredX * centeredX + centeredY * centeredY;
            if (s_BrushCircular && dist2 > radius * radius)
                continue;
            const float weight = std::exp(-dist2 * inv2Sigma2);
            if (weight < 0.01f) continue;
            s_Canvas->SetPixel(px, py, s_Color, weight * s_BrushOpacity);
        }
    }
}

// ---------------------------------------------------------------------------
// FinalizeShape
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::FinalizeShape()
{
    if (!s_Canvas) return;
    const auto shapes = BuildSymmetricSegments(
        s_DrawStartX, s_DrawStartY, s_ShapeCurrentX, s_ShapeCurrentY,
        s_Canvas->GetWidth(), s_Canvas->GetHeight(),
        s_MirrorVertical, s_MirrorHorizontal, s_MirrorOffsetX, s_MirrorOffsetY);

    for (const auto& shape : shapes)
    {
        const int x0 = shape[0], y0 = shape[1], x1 = shape[2], y1 = shape[3];
        switch (s_Tool)
        {
            case Tool::Line:
                PixelArtTools::Line(*s_Canvas, x0, y0, x1, y1, s_Color, s_BrushSize, s_BrushOpacity, s_BrushCircular);
                break;
            case Tool::Rect:
                PixelArtTools::RectOutline(*s_Canvas, x0, y0, x1, y1,
                    s_Color, s_BrushSize, s_BrushOpacity, s_BrushCircular);
                break;
            case Tool::RectFilled:
                PixelArtTools::RectFilled(*s_Canvas, x0, y0, x1, y1, s_Color, s_BrushOpacity);
                break;
            case Tool::Circle:
            case Tool::CircleFilled:
            {
                const int dx = x1 - x0, dy = y1 - y0;
                const int radius = static_cast<int>(std::sqrt(dx * dx + dy * dy));
                if (s_Tool == Tool::CircleFilled)
                    PixelArtTools::CircleFilled(*s_Canvas, x0, y0, radius, s_Color, s_BrushOpacity);
                else
                    PixelArtTools::CircleOutline(*s_Canvas, x0, y0, radius,
                        s_Color, s_BrushSize, s_BrushOpacity, s_BrushCircular);
                break;
            }
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// ApplyToolAt
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::ApplyToolAt(int px, int py, bool isStroke, bool isRelease)
{
    if (!s_Canvas) return;

    if (s_Tool == Tool::Pencil || s_Tool == Tool::SoftBrush || s_Tool == Tool::Eraser)
    {
        if (!isStroke && !isRelease)
            return;

        const int fromX = s_HasLastStrokePoint ? s_DrawLastX : px;
        const int fromY = s_HasLastStrokePoint ? s_DrawLastY : py;
        const auto segments = BuildSymmetricSegments(fromX, fromY, px, py,
            s_Canvas->GetWidth(), s_Canvas->GetHeight(),
            s_MirrorVertical, s_MirrorHorizontal, s_MirrorOffsetX, s_MirrorOffsetY);

        for (const auto& segment : segments)
        {
            const int x0 = segment[0], y0 = segment[1];
            const int x1 = segment[2], y1 = segment[3];
            if (s_Tool == Tool::Pencil)
                PixelArtTools::PencilStroke(*s_Canvas, x0, y0, x1, y1,
                    s_Color, s_BrushSize, s_BrushOpacity, s_BrushCircular);
            else if (s_Tool == Tool::Eraser)
                PixelArtTools::EraseStroke(*s_Canvas, x0, y0, x1, y1,
                    s_BrushSize, s_BrushCircular);
            else
            {
                const int dx = x1 - x0, dy = y1 - y0;
                const int steps = std::max(1, static_cast<int>(std::sqrt(
                    static_cast<float>(dx * dx + dy * dy))));
                for (int step = 0; step <= steps; ++step)
                    ApplySoftBrush(x0 + dx * step / steps, y0 + dy * step / steps);
            }
        }
        s_DrawLastX = px;
        s_DrawLastY = py;
        s_HasLastStrokePoint = true;
        return;
    }

    switch (s_Tool)
    {
        case Tool::Fill:
        {
            if (!isRelease && isStroke) break;
            const auto targets = BuildSymmetricSegments(px, py, px, py,
                s_Canvas->GetWidth(), s_Canvas->GetHeight(),
                s_MirrorVertical, s_MirrorHorizontal, s_MirrorOffsetX, s_MirrorOffsetY);
            for (const auto& target : targets)
                PixelArtTools::FloodFill(*s_Canvas, target[0], target[1], s_Color);
            break;
        }
        case Tool::ReplaceColor:
        {
            if (!isRelease && isStroke) break;
            PixelArtTools::ReplaceColor(*s_Canvas, s_Canvas->GetPixel(px, py), s_Color);
            break;
        }
        case Tool::Eyedropper:
        {
            if (!isRelease && isStroke) break;
            glm::vec4 picked = PixelArtTools::Eyedropper(*s_Canvas, px, py);
            AddRecentColor(s_Color);
            s_Color = picked;
            break;
        }
        case Tool::Ruler:
        {
            if (!isRelease && isStroke) break;
            if (!s_RulerActive)
            {
                s_RulerX0 = px; s_RulerY0 = py;
                s_RulerX1 = px; s_RulerY1 = py;
                s_RulerActive = true;
            }
            else
            {
                s_RulerX1 = px; s_RulerY1 = py;
                s_RulerActive = false;
            }
            break;
        }
        case Tool::Line:
        case Tool::Rect:
        case Tool::RectFilled:
        case Tool::Circle:
        case Tool::CircleFilled:
        {
            s_ShapeCurrentX = px;
            s_ShapeCurrentY = py;
            if (isRelease) FinalizeShape();
            break;
        }
        default: break;
    }
}

// ---------------------------------------------------------------------------
// HandleInput
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::HandleInput()
{
    if (!s_Canvas) return;
    ImGuiIO& io = ImGui::GetIO();
    bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    // Tecla Espaco para modo Mover temporario
    const bool spaceHeld = ImGui::IsKeyDown(ImGuiKey_Space);
    const bool isPanningTool = (s_Tool == Tool::Pan) || spaceHeld;

    if (isPanningTool)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    // Atalhos de Zoom no Teclado
    if (focused && !io.WantTextInput)
    {
        // Ferramentas diretas
        if (ImGui::IsKeyPressed(ImGuiKey_B)) s_Tool = Tool::Pencil;
        if (ImGui::IsKeyPressed(ImGuiKey_P)) s_Tool = Tool::SoftBrush;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) s_Tool = Tool::Eraser;
        if (ImGui::IsKeyPressed(ImGuiKey_G)) s_Tool = Tool::Fill;
        if (ImGui::IsKeyPressed(ImGuiKey_I)) s_Tool = Tool::Eyedropper;
        if (ImGui::IsKeyPressed(ImGuiKey_L)) s_Tool = Tool::Line;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) s_Tool = io.KeyShift ? Tool::RectFilled : Tool::Rect;
        if (ImGui::IsKeyPressed(ImGuiKey_C)) s_Tool = io.KeyShift ? Tool::CircleFilled : Tool::Circle;
        if (ImGui::IsKeyPressed(ImGuiKey_O)) s_Tool = Tool::ReplaceColor;
        if (ImGui::IsKeyPressed(ImGuiKey_H)) s_Tool = Tool::Pan;
        if (ImGui::IsKeyPressed(ImGuiKey_M)) s_Tool = Tool::Ruler;

        // Comandos com Ctrl
        if (io.KeyCtrl)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z)) { s_Canvas->Undo(); }
            if (ImGui::IsKeyPressed(ImGuiKey_Y)) { s_Canvas->Redo(); }
            if (ImGui::IsKeyPressed(ImGuiKey_N)) ImGui::OpenPopup("##new_canvas_modal");

            // Atalhos de Zoom solicitados: Ctrl - / Ctrl _ (Zoom Out), Ctrl + / Ctrl = (Zoom In)
            if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
            {
                s_Zoom = std::max(1.0f, s_Zoom <= 4.0f ? s_Zoom - 1.0f : std::floor(s_Zoom * 0.75f));
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
            {
                s_Zoom = std::min(64.0f, s_Zoom < 4.0f ? s_Zoom + 1.0f : std::ceil(s_Zoom * 1.33f));
            }
            if (ImGui::IsKeyPressed(ImGuiKey_0))
            {
                FitToWindow();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_1))
            {
                s_Zoom = 1.0f;
            }
        }

        // Alterar tamanho do pincel com [ e ]
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)  && s_BrushSize > 1)  s_BrushSize--;
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket) && s_BrushSize < 64) s_BrushSize++;
    }

    // Scroll do mouse: zoom suave ancorado no cursor
    if (hovered && io.MouseWheel != 0.0f)
    {
        float wheel = io.MouseWheel;
        float oldZoom = s_Zoom;
        float factor = (wheel > 0.0f) ? 1.25f : 0.8f;
        float newZoom = std::clamp(std::round(s_Zoom * factor), 1.0f, 64.0f);
        if (newZoom == oldZoom && wheel > 0.0f) newZoom = std::min(64.0f, oldZoom + 1.0f);
        if (newZoom == oldZoom && wheel < 0.0f) newZoom = std::max(1.0f, oldZoom - 1.0f);

        if (std::abs(newZoom - oldZoom) > 0.01f)
        {
            ImVec2 mouse = ImGui::GetMousePos();
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImVec2 winPos = ImGui::GetCursorScreenPos();
            float centerAreaX = winPos.x + avail.x * 0.5f;
            float centerAreaY = winPos.y + avail.y * 0.5f;
            float curCenterX = centerAreaX + s_Pan.x;
            float curCenterY = centerAreaY + s_Pan.y;

            float relX = mouse.x - curCenterX;
            float relY = mouse.y - curCenterY;
            float ratio = newZoom / oldZoom;

            float newCenterX = mouse.x - relX * ratio;
            float newCenterY = mouse.y - relY * ratio;

            s_Pan.x = newCenterX - centerAreaX;
            s_Pan.y = newCenterY - centerAreaY;
            s_Zoom = newZoom;
        }
    }

    // Navegacao com o BOTAO DO MEIO (sempre disponivel)
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        s_Pan.x += io.MouseDelta.x;
        s_Pan.y += io.MouseDelta.y;
    }

    // Navegacao ("ANDAR") COM O BOTAO ESQUERDO DO MOUSE (quando na ferramenta Pan ou segurando Espaco)
    if (isPanningTool)
    {
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            s_Pan.x += io.MouseDelta.x;
            s_Pan.y += io.MouseDelta.y;
        }
        return; // Nao desenha enquanto no modo Pan
    }

    ImVec2 mouse = ImGui::GetMousePos();
    int px = -1, py = -1;
    bool onCanvas = ScreenToPixel(mouse, px, py);

    // Atualiza medicao da regua em tempo real
    if (s_Tool == Tool::Ruler && s_RulerActive && onCanvas)
    {
        s_RulerX1 = px;
        s_RulerY1 = py;
    }

    bool isShapeOngoing = (s_Tool == Tool::Line || s_Tool == Tool::Rect ||
                           s_Tool == Tool::RectFilled || s_Tool == Tool::Circle ||
                           s_Tool == Tool::CircleFilled);

    // Clique esquerdo para Desenhar
    if (hovered && onCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        s_Drawing = true;
        s_HasLastStrokePoint = false;
        s_DrawStartX = px; s_DrawStartY = py;
        s_DrawLastX  = px; s_DrawLastY  = py;
        s_ShapeCurrentX = px; s_ShapeCurrentY = py;

        s_Canvas->PushUndoSnapshot();
        if (!isShapeOngoing)
        {
            ApplyToolAt(px, py, false, false);
        }
    }

    // Arraste de desenho
    if (s_Drawing && onCanvas && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (!isShapeOngoing)
            ApplyToolAt(px, py, true, false);
        else
        {
            s_ShapeCurrentX = px;
            s_ShapeCurrentY = py;
        }
    }

    // Soltar clique
    if (s_Drawing && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        s_Drawing = false;
        if (onCanvas)
        {
            ApplyToolAt(px, py, true, true);
        }
        s_HasLastStrokePoint = false;
    }
}

// ---------------------------------------------------------------------------
// ExportSpriteSheet
// ---------------------------------------------------------------------------
bool PixelArtEditorPanel::ExportSpriteSheet(const std::string& path, bool isTga)
{
    if (s_Frames.empty() || !s_Frames[0].Canvas) return false;
    const int frameCount = static_cast<int>(s_Frames.size());
    const int frameW = s_Frames[0].Canvas->GetWidth();
    const int frameH = s_Frames[0].Canvas->GetHeight();
    const int sheetW = frameW * frameCount;
    const int sheetH = frameH;

    std::vector<uint8_t> sheetPixels(static_cast<size_t>(sheetW * sheetH * 4), 0);

    for (int fi = 0; fi < frameCount; ++fi)
    {
        auto& canvas = s_Frames[fi].Canvas;
        if (!canvas) continue;
        const auto& srcPix = canvas->GetPixels();
        for (int y = 0; y < frameH; ++y)
        {
            const size_t srcOffset = static_cast<size_t>((y * frameW) * 4);
            const size_t dstOffset = static_cast<size_t>((y * sheetW + fi * frameW) * 4);
            if (srcOffset + frameW * 4 <= srcPix.size() && dstOffset + frameW * 4 <= sheetPixels.size())
            {
                std::memcpy(&sheetPixels[dstOffset], &srcPix[srcOffset], static_cast<size_t>(frameW * 4));
            }
        }
    }

    if (isTga)
        return TextureExporter::SaveTGA32(path, sheetPixels, sheetW, sheetH);
    else
        return TextureExporter::SaveBMP32(path, sheetPixels, sheetW, sheetH);
}

// ---------------------------------------------------------------------------
// DrawToolbar
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::DrawToolbar(Application& app)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));

    if (ImGui::Button(UI("+ Novo Canvas"))) ImGui::OpenPopup("##new_canvas_modal");
    ImGui::SameLine();

    // Modal de Novo Canvas
    if (ImGui::BeginPopupModal("##new_canvas_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text(UI("Criar Novo Canvas"));
        ImGui::Separator();
        static const char* sizes[] = { "8x8", "16x16", "32x32", "64x64", "128x128", "256x256", "512x512", "Personalizado" };
        static int sizeIdx = 2;
        ImGui::SetNextItemWidth(140.f);
        ImGui::Combo(UI("Tamanho##nc"), &sizeIdx, sizes, 8);
        int presets[][2] = {{8,8},{16,16},{32,32},{64,64},{128,128},{256,256},{512,512},{0,0}};
        if (sizeIdx < 7) { s_NewCanvasW = presets[sizeIdx][0]; s_NewCanvasH = presets[sizeIdx][1]; }
        if (sizeIdx == 7)
        {
            ImGui::InputInt(UI("Largura##ncw"), &s_NewCanvasW); s_NewCanvasW = std::clamp(s_NewCanvasW, 1, 4096);
            ImGui::InputInt(UI("Altura##nch"),  &s_NewCanvasH); s_NewCanvasH = std::clamp(s_NewCanvasH, 1, 4096);
        }
        static int bgIdx = 0;
        static const char* bgOpts[] = { "Transparente", "Branco", "Preto" };
        ImGui::Combo(UI("Fundo##ncbg"), &bgIdx, bgOpts, 3);
        ImGui::Dummy({0.f, 6.f});
        if (ImGui::Button(UI("Criar Canvas"), ImVec2(100, 0)))
        {
            glm::vec4 bg = bgIdx == 1 ? glm::vec4(1.f) : bgIdx == 2 ? glm::vec4(0.f,0.f,0.f,1.f) : glm::vec4(0.f);
            NewCanvas(s_NewCanvasW, s_NewCanvasH, bg);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(UI("Cancelar"), ImVec2(80, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (s_Canvas)
    {
        if (ImGui::Button(UI("Limpar")))
        {
            s_Canvas->PushUndoSnapshot();
            s_Canvas->Clear(glm::vec4(0.f));
        }
        ImGui::SameLine();

        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        if (ImGui::Button(UI("Desfazer"))) { s_Canvas->Undo(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(UI("Desfazer [Ctrl+Z]"));
        ImGui::SameLine();

        if (ImGui::Button(UI("Refazer")))  { s_Canvas->Redo(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(UI("Refazer [Ctrl+Y]"));
        ImGui::SameLine();

        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        if (ImGui::Button(UI("Rot90")))
        {
            s_Canvas->PushUndoSnapshot();
            s_Canvas->Rotate90CW();
        }
        ImGui::SameLine();

        if (ImGui::Button(UI("Flip H")))
        {
            s_Canvas->PushUndoSnapshot();
            s_Canvas->FlipHorizontal();
        }
        ImGui::SameLine();

        if (ImGui::Button(UI("Flip V")))
        {
            s_Canvas->PushUndoSnapshot();
            s_Canvas->FlipVertical();
        }
        ImGui::SameLine();

        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // Controles rapidos de Zoom
        if (ImGui::Button(UI("Zoom -")))
        {
            s_Zoom = std::max(1.0f, s_Zoom <= 4.0f ? s_Zoom - 1.0f : std::floor(s_Zoom * 0.75f));
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(UI("Diminuir Zoom [Ctrl + -]"));
        ImGui::SameLine();

        if (ImGui::Button(UI("Zoom +")))
        {
            s_Zoom = std::min(64.0f, s_Zoom < 4.0f ? s_Zoom + 1.0f : std::ceil(s_Zoom * 1.33f));
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(UI("Aumentar Zoom [Ctrl + +]"));
        ImGui::SameLine();

        if (ImGui::Button(UI("100%"))) { s_Zoom = 1.0f; s_Pan = ImVec2(0.f, 0.f); }
        ImGui::SameLine();

        if (ImGui::Button(UI("Ajustar"))) { FitToWindow(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(UI("Ajustar Canvas a Tela [Ctrl + 0]"));
        ImGui::SameLine();

        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        if (ImGui::Button(UI("Exportar")))
        {
            s_ExportPath = "Assets/Textures/pixel_export.bmp";
            ImGui::OpenPopup("##export_modal");
        }
        ImGui::SameLine();

        if (ImGui::BeginPopupModal("##export_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text(UI("Exportar Textura Atual"));
            ImGui::Separator();
            static char exportBuf[512] = "";
            if (exportBuf[0] == '\0') std::snprintf(exportBuf, sizeof(exportBuf), "%s", s_ExportPath.c_str());
            ImGui::SetNextItemWidth(360.f);
            ImGui::InputText(UI("Caminho##exp"), exportBuf, 512);
            ImGui::Dummy({0.f, 4.f});
            if (ImGui::Button(UI("Salvar BMP"), ImVec2(100, 0)))
            {
                TextureExporter::SaveBMP32(exportBuf, s_Canvas->GetPixels(),
                    s_Canvas->GetWidth(), s_Canvas->GetHeight());
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(UI("Salvar TGA"), ImVec2(100, 0)))
            {
                std::string tgaPath = exportBuf;
                auto dot = tgaPath.rfind('.');
                if (dot != std::string::npos) tgaPath = tgaPath.substr(0, dot);
                tgaPath += ".tga";
                TextureExporter::SaveTGA32(tgaPath, s_Canvas->GetPixels(),
                    s_Canvas->GetWidth(), s_Canvas->GetHeight());
                ImGui::CloseCurrentPopup();
            }

            ImGui::Dummy({0.f, 6.f});
            ImGui::Text(UI("Exportar Animacao (Sprite Sheet):"));
            ImGui::Separator();
            if (ImGui::Button(UI("Exportar Sprite Sheet BMP"), ImVec2(180, 0)))
            {
                ExportSpriteSheet("Assets/Textures/spritesheet_export.bmp", false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(UI("Exportar Sprite Sheet TGA"), ImVec2(180, 0)))
            {
                ExportSpriteSheet("Assets/Textures/spritesheet_export.tga", true);
                ImGui::CloseCurrentPopup();
            }

            ImGui::Dummy({0.f, 4.f});
            if (ImGui::Button(UI("Fechar"), ImVec2(80, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // Integracao com Cena
        if (ImGui::Button(UI("Criar Sprite 2D")))
            CreateSpriteEntity(app);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(UI("Cria uma entidade 2D nova com esta textura"));
        ImGui::SameLine();

        bool hasSelection = (app.GetSelectedEntityIndex() >= 0);
        if (!hasSelection) ImGui::BeginDisabled();
        if (ImGui::Button(UI("Aplicar na Selecao")))
            ApplyToSelectedEntity(app);
        if (!hasSelection)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(UI("Selecione um objeto no Hierarchy primeiro."));
        }
        else
        {
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(UI("Aplica esta textura ao material do objeto selecionado"));
        }
    }

    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// DrawToolSidebar
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::DrawToolSidebar()
{
    EnsurePalette(s_Palette);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

    // ---- FERRAMENTAS ----
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "FERRAMENTAS");
    ImGui::Separator();

    const struct { Tool t; const char* label; const char* tooltip; } tools[] = {
        { Tool::Pencil,     "Lapis [B]",        "Desenho pixel art preciso [B]" },
        { Tool::SoftBrush,  "Pincel [P]",       "Pincel suave e realista [P]" },
        { Tool::Eraser,     "Borracha [E]",     "Apaga pixels [E]" },
        { Tool::Fill,       "Balde [G]",        "Preenchimento de area [G]" },
        { Tool::Eyedropper, "Conta-gotas [I]",  "Captura cor do pixel [I]" },
        { Tool::Line,       "Linha [L]",        "Desenha linhas retas [L]" },
        { Tool::Rect,       "Retangulo [R]",    "Retangulo vazado [R]" },
        { Tool::RectFilled, "Ret. Cheio",       "Retangulo preenchido [Shift+R]" },
        { Tool::Circle,     "Circulo [C]",      "Desenha circulo [C]" },
        { Tool::CircleFilled,"Circulo cheio",   "Circulo preenchido [Shift+C]" },
        { Tool::ReplaceColor,"Substituir cor [O]", "Troca no canvas todos os pixels com a cor clicada [O]" },
        { Tool::Pan,        "Mover [H]",        "Andar / Mover canvas [H / Espaco + Botao Esquerdo]" },
        { Tool::Ruler,      "Regua [M]",        "Mede distancia em pixels [M]" },
    };
    const int toolCount = static_cast<int>(Tool::COUNT);

    for (int i = 0; i < toolCount; ++i)
    {
        bool active = (s_Tool == tools[i].t);
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.10f, 0.50f, 0.90f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.60f, 1.00f, 1.0f));
        }

        if (ImGui::Button(tools[i].label, ImVec2(148, 26)))
            s_Tool = tools[i].t;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(UI("%s"), tools[i].tooltip);

        if (active) ImGui::PopStyleColor(2);
    }

    ImGui::Dummy(ImVec2(0, 4));

    // ---- PROPRIEDADES DO PINCEL ----
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "PINCEL");
    ImGui::Separator();

    ImGui::Text(UI("Tamanho: %d px"), s_BrushSize);
    if (ImGui::Button(UI("-##subsize"), ImVec2(28, 22)) && s_BrushSize > 1) s_BrushSize--;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(86.f);
    ImGui::SliderInt(UI("##bslider"), &s_BrushSize, 1, 64, "");
    ImGui::SameLine();
    if (ImGui::Button(UI("+##addsize"), ImVec2(28, 22)) && s_BrushSize < 64) s_BrushSize++;

    if (s_Tool == Tool::SoftBrush)
    {
        ImGui::Text(UI("Suavidade:"));
        ImGui::SetNextItemWidth(148.f);
        ImGui::SliderFloat(UI("##softness"), &s_BrushSoftness, 0.05f, 1.0f, "%.2f");
    }

    ImGui::Checkbox(UI("Pincel circular"), &s_BrushCircular);
    ImGui::SetNextItemWidth(148.0f);
    ImGui::SliderFloat(UI("Opacidade##brush"), &s_BrushOpacity, 0.05f, 1.0f, "%.2f");

    ImGui::Dummy(ImVec2(0, 4));

    // ---- COR ATIVA ----
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "COR");
    ImGui::Separator();

    ImVec4 col = ToImVec4(s_Color);
    if (ImGui::ColorButton("##curcolor_btn", col, ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(148, 28)))
        ImGui::OpenPopup("##color_picker_popup");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(UI("Clique para abrir o seletor de cores"));

    if (ImGui::BeginPopup("##color_picker_popup"))
    {
        ImVec4 editCol = ToImVec4(s_Color);
        if (ImGui::ColorPicker4("##cp", &editCol.x, ImGuiColorEditFlags_AlphaBar))
            s_Color = ToGlmVec4(editCol);
        ImGui::EndPopup();
    }

    // Paleta
    ImGui::Text(UI("Paleta:"));
    for (size_t i = 0; i < s_Palette.size(); ++i)
    {
        ImVec4 pc = ToImVec4(s_Palette[i]);
        char label[16];
        std::snprintf(label, sizeof(label), "##pal%zu", i);
        if (ImGui::ColorButton(label, pc,
            ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoBorder,
            ImVec2(33, 20)))
        {
            AddRecentColor(s_Color);
            s_Color = s_Palette[i];
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(UI("Botao esquerdo: selecionar\nBotao direito: substituir cor"));
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                s_Palette[i] = s_Color;
        }
        if ((i + 1) % 4 != 0) ImGui::SameLine(0, 4);
    }

    // Cores recentes
    if (!s_RecentColors.empty())
    {
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::Text(UI("Recentes:"));
        for (size_t i = 0; i < s_RecentColors.size(); ++i)
        {
            char label[16];
            std::snprintf(label, sizeof(label), "##rec%zu", i);
            ImVec4 rc = ToImVec4(s_RecentColors[i]);
            if (ImGui::ColorButton(label, rc,
                ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoBorder,
                ImVec2(33, 16)))
            {
                s_Color = s_RecentColors[i];
            }
            if ((i + 1) % 4 != 0) ImGui::SameLine(0, 4);
        }
    }

    if (s_Palette.size() < 32)
    {
        if (ImGui::Button(UI("Adicionar cor atual"), ImVec2(148, 22)))
            s_Palette.push_back(s_Color);
    }
    else
        ImGui::TextDisabled(UI("Paleta cheia (32 cores)"));

    ImGui::Dummy(ImVec2(0, 4));

    // ---- VISUALIZACAO ----
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "EXIBICAO");
    ImGui::Separator();
    ImGui::Checkbox(UI("Grade"),  &s_ShowGrid);
    ImGui::Checkbox(UI("Xadrez"), &s_ShowCheckerboard);
    ImGui::Checkbox(UI("Regua"),  &s_ShowRuler);
    ImGui::Checkbox(UI("Casca de Cebola"), &s_ShowOnionSkin);
    if (s_ShowOnionSkin)
    {
        ImGui::SetNextItemWidth(148.f);
        ImGui::SliderFloat(UI("##onion_alpha_sb"), &s_OnionSkinAlpha, 0.1f, 0.8f, "Opac: %.2f");
    }

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), UI("SIMETRIA DE DESENHO"));
    ImGui::Separator();
    ImGui::Checkbox(UI("Espelhar esquerda / direita"), &s_MirrorVertical);
    ImGui::Checkbox(UI("Espelhar cima / baixo"), &s_MirrorHorizontal);
    if (s_Canvas && s_MirrorVertical)
    {
        const float axisRangeX = std::max(0.5f, (s_Canvas->GetWidth() - 1) * 0.5f);
        ImGui::SetNextItemWidth(148.0f);
        ImGui::SliderFloat(UI("Eixo vertical##symx"), &s_MirrorOffsetX,
            -axisRangeX, axisRangeX, "%+.1f px");
    }
    if (s_Canvas && s_MirrorHorizontal)
    {
        const float axisRangeY = std::max(0.5f, (s_Canvas->GetHeight() - 1) * 0.5f);
        ImGui::SetNextItemWidth(148.0f);
        ImGui::SliderFloat(UI("Eixo horizontal##symy"), &s_MirrorOffsetY,
            -axisRangeY, axisRangeY, "%+.1f px");
    }
    if (s_MirrorVertical || s_MirrorHorizontal)
        ImGui::TextWrapped(UI("O espelho atualiza junto com o traço. Use as linhas coloridas no canvas para alinhar o personagem."));

    if (ImGui::Button(UI("+ Guia V"), ImVec2(72, 22)))
    {
        if (s_Canvas) s_GuideX.push_back(s_Canvas->GetWidth() * 0.5f);
    }
    ImGui::SameLine();
    if (ImGui::Button(UI("+ Guia H"), ImVec2(72, 22)))
    {
        if (s_Canvas) s_GuideY.push_back(s_Canvas->GetHeight() * 0.5f);
    }
    if (!s_GuideX.empty() || !s_GuideY.empty())
    {
        if (ImGui::Button(UI("Limpar Guias"), ImVec2(148, 20)))
        {
            s_GuideX.clear();
            s_GuideY.clear();
        }
    }

    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// DrawCanvasRegion
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::DrawCanvasRegion()
{
    if (!s_Canvas)
    {
        ImGui::TextDisabled("(Nenhum canvas ativo - clique em '+ Novo Canvas' no topo)");
        return;
    }

    s_Canvas->UpdateGPU();

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2 winPos    = ImGui::GetCursorScreenPos();
    ImVec2 avail     = ImGui::GetContentRegionAvail();

    if (avail.x < 10.f || avail.y < 10.f) return;

    if (s_NeedsFit)
    {
        FitToWindow();
        s_NeedsFit = false;
    }

    float canvasW = s_Canvas->GetWidth()  * s_Zoom;
    float canvasH = s_Canvas->GetHeight() * s_Zoom;

    // Centro da area disponivel
    ImVec2 centerPos(winPos.x + avail.x * 0.5f + s_Pan.x,
                     winPos.y + avail.y * 0.5f + s_Pan.y);

    s_CanvasMin = ImVec2(std::floor(centerPos.x - canvasW * 0.5f),
                         std::floor(centerPos.y - canvasH * 0.5f));
    s_CanvasMax = ImVec2(s_CanvasMin.x + canvasW, s_CanvasMin.y + canvasH);

    dl->PushClipRect(winPos, ImVec2(winPos.x + avail.x, winPos.y + avail.y), true);

    // 0. Fundo escuro do workspace
    dl->AddRectFilled(winPos, ImVec2(winPos.x + avail.x, winPos.y + avail.y), IM_COL32(28, 28, 34, 255));

    // 1. Sombra suave sob o canvas
    dl->AddRectFilled(
        ImVec2(s_CanvasMin.x + 4, s_CanvasMin.y + 4),
        ImVec2(s_CanvasMax.x + 4, s_CanvasMax.y + 4),
        IM_COL32(10, 10, 14, 180));

    // 2. Checkerboard de transparencia
    if (s_ShowCheckerboard)
    {
        const float csize = std::max(4.0f, s_Zoom * 0.5f);
        const ImU32 cA = IM_COL32(46, 46, 54, 255);
        const ImU32 cB = IM_COL32(36, 36, 42, 255);
        for (float cy = s_CanvasMin.y; cy < s_CanvasMax.y; cy += csize)
        {
            for (float cx = s_CanvasMin.x; cx < s_CanvasMax.x; cx += csize)
            {
                int ci = (int)((cx - s_CanvasMin.x) / csize) + (int)((cy - s_CanvasMin.y) / csize);
                ImU32 cc = (ci % 2 == 0) ? cA : cB;
                float ex = std::min(cx + csize, s_CanvasMax.x);
                float ey = std::min(cy + csize, s_CanvasMax.y);
                dl->AddRectFilled(ImVec2(cx, cy), ImVec2(ex, ey), cc);
            }
        }
    }

    // 2.5 Casca de Cebola (Onion Skinning - quadro anterior translucido)
    if (s_ShowOnionSkin && s_CurrentFrame > 0 && s_CurrentFrame < (int)s_Frames.size())
    {
        auto& prevCanvas = s_Frames[s_CurrentFrame - 1].Canvas;
        if (prevCanvas)
        {
            prevCanvas->UpdateGPU();
            const int ghostAlpha = std::clamp(static_cast<int>(s_OnionSkinAlpha * 255.0f), 10, 240);
            const ImU32 ghostCol = IM_COL32(170, 210, 255, ghostAlpha);
            if (!s_VulkanUIOnly)
                dl->AddImage(
                    static_cast<ImTextureID>(prevCanvas->GetTextureID()),
                    s_CanvasMin, s_CanvasMax,
                    ImVec2(0, 0), ImVec2(1, 1),
                    ghostCol);
        }
    }

    // 3. Textura desenhada do canvas
    if (!s_VulkanUIOnly)
    {
        dl->AddImage(
            static_cast<ImTextureID>(s_Canvas->GetTextureID()),
            s_CanvasMin, s_CanvasMax,
            ImVec2(0, 0), ImVec2(1, 1));
    }
    else
    {
        dl->AddRectFilled(s_CanvasMin, s_CanvasMax, IM_COL32(38, 42, 52, 255));
        dl->AddRect(s_CanvasMin, s_CanvasMax, IM_COL32(85, 115, 160, 255));
        dl->AddText(ImVec2(s_CanvasMin.x + 12.0f, s_CanvasMin.y + 12.0f),
                    IM_COL32(205, 215, 232, 255),
                    "Canvas GL: edição disponível; preview de textura requer OpenGL.");
    }

    // 4. Previa de formas geometricas durante arraste
    if (s_Drawing && (s_Tool == Tool::Line || s_Tool == Tool::Rect ||
                      s_Tool == Tool::RectFilled || s_Tool == Tool::Circle ||
                      s_Tool == Tool::CircleFilled))
    {
        auto ToScreen = [&](int ppx, int ppy) -> ImVec2 {
            return ImVec2(s_CanvasMin.x + (ppx + 0.5f) * s_Zoom,
                          s_CanvasMin.y + (ppy + 0.5f) * s_Zoom);
        };
        ImU32 previewCol = IM_COL32(
            (int)(s_Color.r * 255), (int)(s_Color.g * 255),
            (int)(s_Color.b * 255), 220);
        const auto previews = BuildSymmetricSegments(
            s_DrawStartX, s_DrawStartY, s_ShapeCurrentX, s_ShapeCurrentY,
            s_Canvas->GetWidth(), s_Canvas->GetHeight(),
            s_MirrorVertical, s_MirrorHorizontal, s_MirrorOffsetX, s_MirrorOffsetY);
        for (const auto& preview : previews)
        {
            const ImVec2 a = ToScreen(preview[0], preview[1]);
            const ImVec2 b = ToScreen(preview[2], preview[3]);
            if (s_Tool == Tool::Line)
                dl->AddLine(a, b, previewCol, std::max(1.5f, s_BrushSize * s_Zoom * 0.5f));
            else if (s_Tool == Tool::Rect || s_Tool == Tool::RectFilled)
            {
                const ImVec2 lo(std::min(a.x, b.x), std::min(a.y, b.y));
                const ImVec2 hi(std::max(a.x, b.x), std::max(a.y, b.y));
                if (s_Tool == Tool::Rect)
                    dl->AddRect(lo, hi, previewCol, 0.f, 0, 1.5f);
                else
                    dl->AddRectFilled(lo, hi, previewCol & 0x80FFFFFF);
            }
            else
            {
                const float dx = b.x - a.x, dy = b.y - a.y;
                const float radius = std::sqrt(dx * dx + dy * dy);
                if (s_Tool == Tool::CircleFilled)
                    dl->AddCircleFilled(a, radius, previewCol & 0x80FFFFFF, 64);
                else
                    dl->AddCircle(a, radius, previewCol, 64, 1.5f);
            }
        }
    }

    // 5. Grade de pixels
    if (s_ShowGrid && s_Zoom >= 6.0f)
    {
        ImU32 gridCol = IM_COL32(90, 90, 105, 70);
        for (int gx = 0; gx <= s_Canvas->GetWidth(); ++gx)
        {
            float sx = s_CanvasMin.x + gx * s_Zoom;
            dl->AddLine(ImVec2(sx, s_CanvasMin.y), ImVec2(sx, s_CanvasMax.y), gridCol, 1.0f);
        }
        for (int gy = 0; gy <= s_Canvas->GetHeight(); ++gy)
        {
            float sy = s_CanvasMin.y + gy * s_Zoom;
            dl->AddLine(ImVec2(s_CanvasMin.x, sy), ImVec2(s_CanvasMax.x, sy), gridCol, 1.0f);
        }
    }

    // Eixos de simetria: indicam exatamente onde o traço será refletido.
    if (s_MirrorVertical)
    {
        const float axisX = (s_Canvas->GetWidth() - 1) * 0.5f + s_MirrorOffsetX;
        const float screenX = s_CanvasMin.x + (axisX + 0.5f) * s_Zoom;
        dl->AddLine(ImVec2(screenX, s_CanvasMin.y), ImVec2(screenX, s_CanvasMax.y),
            IM_COL32(60, 220, 255, 210), 2.0f);
    }
    if (s_MirrorHorizontal)
    {
        const float axisY = (s_Canvas->GetHeight() - 1) * 0.5f + s_MirrorOffsetY;
        const float screenY = s_CanvasMin.y + (axisY + 0.5f) * s_Zoom;
        dl->AddLine(ImVec2(s_CanvasMin.x, screenY), ImVec2(s_CanvasMax.x, screenY),
            IM_COL32(255, 190, 60, 210), 2.0f);
    }

    // 6. Indicador de cursor (brush outline)
    ImVec2 mouse = ImGui::GetMousePos();
    int hpx = -1, hpy = -1;
    if (ScreenToPixel(mouse, hpx, hpy) && !ImGui::IsKeyDown(ImGuiKey_Space) && s_Tool != Tool::Pan)
    {
        const int brushMin = -(s_BrushSize - 1) / 2;
        const int brushMax = s_BrushSize / 2;
        float pLeft   = s_CanvasMin.x + (hpx + brushMin) * s_Zoom;
        float pTop    = s_CanvasMin.y + (hpy + brushMin) * s_Zoom;
        float pRight  = s_CanvasMin.x + (hpx + brushMax + 1) * s_Zoom;
        float pBottom = s_CanvasMin.y + (hpy + brushMax + 1) * s_Zoom;

        if (s_Tool == Tool::SoftBrush)
        {
            if (s_BrushCircular)
            {
                ImVec2 center(s_CanvasMin.x + (hpx + 0.5f) * s_Zoom,
                              s_CanvasMin.y + (hpy + 0.5f) * s_Zoom);
                dl->AddCircle(center, std::max(0.5f, s_BrushSize * s_Zoom * 0.5f),
                    IM_COL32(255, 255, 255, 180), 32, 1.5f);
            }
            else
                dl->AddRect(ImVec2(pLeft, pTop), ImVec2(pRight, pBottom),
                    IM_COL32(255, 255, 255, 180), 0.f, 0, 1.5f);
        }
        else if (s_Tool == Tool::Pencil || s_Tool == Tool::Eraser)
        {
            if (s_BrushCircular)
            {
                const ImVec2 center(s_CanvasMin.x + (hpx + 0.5f) * s_Zoom,
                                    s_CanvasMin.y + (hpy + 0.5f) * s_Zoom);
                dl->AddCircle(center, std::max(0.5f, s_BrushSize * s_Zoom * 0.5f),
                    IM_COL32(255, 255, 255, 180), 32, 1.5f);
            }
            else
                dl->AddRect(ImVec2(pLeft, pTop), ImVec2(pRight, pBottom), IM_COL32(255, 255, 255, 180), 0.f, 0, 1.5f);
        }
    }

    // 7. Regua Interativa
    if (s_ShowRuler && (s_RulerActive || (s_RulerX0 != s_RulerX1 || s_RulerY0 != s_RulerY1)))
    {
        auto ToScrn = [&](int ppx, int ppy) -> ImVec2 {
            return ImVec2(s_CanvasMin.x + (ppx + 0.5f) * s_Zoom,
                          s_CanvasMin.y + (ppy + 0.5f) * s_Zoom);
        };
        ImVec2 ra = ToScrn(s_RulerX0, s_RulerY0);
        ImVec2 rb = ToScrn(s_RulerX1, s_RulerY1);
        ImU32 rulerCol = IM_COL32(255, 215, 0, 240);

        dl->AddLine(ra, rb, rulerCol, 2.0f);
        dl->AddCircleFilled(ra, 4.f, rulerCol);
        dl->AddCircleFilled(rb, 4.f, rulerCol);

        int ddx = s_RulerX1 - s_RulerX0;
        int ddy = s_RulerY1 - s_RulerY0;
        float dist = std::sqrt((float)(ddx*ddx + ddy*ddy));
        char label[64];
        std::snprintf(label, sizeof(label), "%.1f px  (dX: %d, dY: %d)", dist, ddx, ddy);
        ImVec2 mid((ra.x + rb.x) * 0.5f + 6, (ra.y + rb.y) * 0.5f - 14);
        dl->AddRectFilled(ImVec2(mid.x - 2, mid.y - 2), ImVec2(mid.x + 160, mid.y + 16), IM_COL32(20, 20, 25, 220));
        dl->AddText(mid, IM_COL32(255, 215, 0, 255), label);
    }

    // 8. Guias fixas
    {
        ImU32 guideCol = IM_COL32(0, 195, 255, 180);
        for (float gx : s_GuideX)
        {
            float sx = s_CanvasMin.x + gx * s_Zoom;
            if (sx >= s_CanvasMin.x && sx <= s_CanvasMax.x)
                dl->AddLine(ImVec2(sx, s_CanvasMin.y), ImVec2(sx, s_CanvasMax.y), guideCol, 1.5f);
        }
        for (float gy : s_GuideY)
        {
            float sy = s_CanvasMin.y + gy * s_Zoom;
            if (sy >= s_CanvasMin.y && sy <= s_CanvasMax.y)
                dl->AddLine(ImVec2(s_CanvasMin.x, sy), ImVec2(s_CanvasMax.x, sy), guideCol, 1.5f);
        }
    }

    // 9. Borda elegante do canvas
    dl->AddRect(s_CanvasMin, s_CanvasMax, IM_COL32(80, 80, 95, 255), 0.f, 0, 2.0f);

    dl->PopClipRect();

    // Captura de input para a area total do canvas
    ImGui::SetCursorScreenPos(winPos);
    ImGui::InvisibleButton("##canvas_input", avail,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
}

// ---------------------------------------------------------------------------
// DrawAnimationPanel
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::DrawAnimationPanel()
{
    if (!s_ShowAnimPanel || s_Frames.empty()) return;

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.14f, 0.16f, 0.22f, 1.0f));
    bool open = ImGui::CollapsingHeader(UI("Animacao de Sprites (Frames)"), ImGuiTreeNodeFlags_DefaultOpen);
    ImGui::PopStyleColor();
    if (!open) return;

    const float frameThumbSize = 42.f;

    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));

    if (s_AnimPlaying)
    {
        if (ImGui::Button(UI("Parar##stop"), ImVec2(80, 0)))
            s_AnimPlaying = false;
    }
    else
    {
        if (ImGui::Button(UI("Reproduzir##play"), ImVec2(80, 0)))
        {
            s_AnimPlaying = true;
            s_AnimTimer   = 0.0f;
        }
    }
    ImGui::SameLine();

    if (ImGui::ArrowButton("##prev", ImGuiDir_Left) && !s_AnimPlaying)
    {
        s_CurrentFrame = ((s_CurrentFrame - 1) + (int)s_Frames.size()) % (int)s_Frames.size();
        s_Canvas = s_Frames[s_CurrentFrame].Canvas;
    }
    ImGui::SameLine();

    ImGui::Text(UI("Frame %d / %d"), s_CurrentFrame + 1, (int)s_Frames.size());
    ImGui::SameLine();

    if (ImGui::ArrowButton("##next", ImGuiDir_Right) && !s_AnimPlaying)
    {
        s_CurrentFrame = (s_CurrentFrame + 1) % (int)s_Frames.size();
        s_Canvas = s_Frames[s_CurrentFrame].Canvas;
    }
    ImGui::SameLine();

    // Adicionar frame duplicando o atual
    if (ImGui::Button(UI("+ Novo Frame"), ImVec2(100, 0)))
    {
        AnimFrame newFrame;
        auto& cur = s_Frames[s_CurrentFrame];
        newFrame.Canvas = std::make_shared<PixelCanvas>(
            cur.Canvas->GetWidth(), cur.Canvas->GetHeight(), glm::vec4(0.f));
        {
            const auto& srcPix = cur.Canvas->GetPixels();
            const int W = cur.Canvas->GetWidth();
            const int H = cur.Canvas->GetHeight();
            for (int yy = 0; yy < H; ++yy)
            {
                for (int xx = 0; xx < W; ++xx)
                {
                    size_t idx = static_cast<size_t>((yy * W + xx) * 4);
                    glm::vec4 pix(srcPix[idx]   / 255.f,
                                  srcPix[idx+1] / 255.f,
                                  srcPix[idx+2] / 255.f,
                                  srcPix[idx+3] / 255.f);
                    newFrame.Canvas->SetPixel(xx, yy, pix);
                }
            }
            newFrame.Canvas->MarkDirtyFull();
            newFrame.Canvas->UpdateGPU();
        }
        newFrame.Duration = cur.Duration;
        s_Frames.insert(s_Frames.begin() + s_CurrentFrame + 1, std::move(newFrame));
        s_CurrentFrame++;
        s_Canvas = s_Frames[s_CurrentFrame].Canvas;
    }
    ImGui::SameLine();

    bool canRemove = (int)s_Frames.size() > 1;
    if (!canRemove) ImGui::BeginDisabled();
    if (ImGui::Button(UI("Excluir Frame"), ImVec2(100, 0)))
    {
        s_Frames.erase(s_Frames.begin() + s_CurrentFrame);
        s_CurrentFrame = std::clamp(s_CurrentFrame, 0, (int)s_Frames.size() - 1);
        s_Canvas = s_Frames[s_CurrentFrame].Canvas;
    }
    if (!canRemove) ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::SetNextItemWidth(75.f);
    ImGui::DragFloat(UI("Duracao(s)##fdur"), &s_Frames[s_CurrentFrame].Duration, 0.01f, 0.01f, 5.0f, "%.2f s");
    ImGui::SameLine();

    float totalDur = 0.f;
    for (auto& f : s_Frames) totalDur += f.Duration;
    float fps = s_Frames.empty() ? 0.f : (float)s_Frames.size() / totalDur;
    ImGui::Text(UI("Taxa: ~%.1f fps"), fps);
    ImGui::SameLine(0, 16.f);
    ImGui::Checkbox(UI("Casca de Cebola##anim_onion"), &s_ShowOnionSkin);

    ImGui::PopStyleVar();

    // Thumbnails dos frames
    ImGui::BeginChild("##framelist",
        ImVec2(0.f, frameThumbSize + 28.f), false,
        ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    for (int fi = 0; fi < (int)s_Frames.size(); ++fi)
    {
        ImGui::PushID(fi);
        auto& fr = s_Frames[fi];
        fr.Canvas->UpdateGPU();

        bool isCur = (fi == s_CurrentFrame);
        if (isCur)
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.10f, 0.60f, 1.00f, 1.f));

        ImVec2 thumbSz(frameThumbSize, frameThumbSize);
        char btnId[32];
        std::snprintf(btnId, sizeof(btnId), "##btnframe_%d", fi);
        const bool frameClicked = s_VulkanUIOnly
            ? ImGui::Button(btnId, thumbSz)
            : ImGui::ImageButton(btnId,
                static_cast<ImTextureID>(fr.Canvas->GetTextureID()), thumbSz);
        if (frameClicked)
        {
            if (!s_AnimPlaying)
            {
                s_CurrentFrame = fi;
                s_Canvas = s_Frames[s_CurrentFrame].Canvas;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(UI("Frame %d  (%.2f s)"), fi + 1, fr.Duration);

        if (isCur)
            ImGui::PopStyleColor();

        char lblFrame[16];
        std::snprintf(lblFrame, sizeof(lblFrame), "F%d", fi + 1);
        ImGui::TextDisabled("%s", lblFrame);

        ImGui::SameLine(0.f, 6.f);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::EndGroup();
}

// ---------------------------------------------------------------------------
// DrawHUD
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::DrawHUD(Application& app)
{
    if (!s_Canvas)
    {
        ImGui::TextDisabled("Sem canvas ativo");
        return;
    }

    ImVec2 mouse = ImGui::GetMousePos();
    int px = -1, py = -1;
    bool onCanvas = ScreenToPixel(mouse, px, py);

    glm::vec4 pixelColor = onCanvas ? s_Canvas->GetPixel(px, py) : glm::vec4(0.f);

    ImGui::Text(UI("%dx%d px | Zoom: %.0fx (%.0f%%) | %s"),
        s_Canvas->GetWidth(), s_Canvas->GetHeight(), s_Zoom, s_Zoom * 100.0f, ToolName(s_Tool));

    if (onCanvas)
    {
        ImGui::SameLine(0, 14.f);
        ImGui::Text(UI("Cursor: X: %d, Y: %d  |  RGBA: (%.2f, %.2f, %.2f, %.2f)"),
            px, py, pixelColor.r, pixelColor.g, pixelColor.b, pixelColor.a);
        ImGui::SameLine(0, 8.f);
        ImGui::ColorButton("##hudcol", ToImVec4(pixelColor),
            ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoBorder,
            ImVec2(14, 14));
    }

    if (s_Tool == Tool::Ruler && s_RulerActive)
    {
        int ddx = s_RulerX1 - s_RulerX0;
        int ddy = s_RulerY1 - s_RulerY0;
        float dist = std::sqrt((float)(ddx*ddx + ddy*ddy));
        ImGui::SameLine(0, 14.f);
        ImGui::TextColored(ImVec4(1.f, 0.85f, 0.2f, 1.f),
            "Regua: %.1f px (dX: %d, dY: %d)", dist, ddx, ddy);
    }

    if (!s_TargetEntityName.empty())
    {
        ImGui::SameLine(0, 14.f);
        ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.45f, 1.f), "Alvo: %s", s_TargetEntityName.c_str());
    }
}

// ---------------------------------------------------------------------------
// CreateSpriteEntity
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::CreateSpriteEntity(Application& app)
{
    if (!s_Canvas) return;

    Scene&  scene = app.GetScene();
    Entity& ent   = scene.CreateEntity();

    ent.SetName("Sprite2D_" + std::to_string(s_SpriteCounter++));
    ent.SetEditorOnly(false);
    if (app.Is2DWorldMode())
    {
        ent.PhysicsEnabled = false;
        ent.UseGravity = false;
        ent.IsStatic = true;
        ent.ColliderEnabled = true;
    }
    ent.Tag = "Sprite2D";

    ent.GetMeshComponent().MeshData = app.Is2DWorldMode()
        ? MeshCache::GetQuad() : std::make_shared<Mesh>();
    if (!app.Is2DWorldMode())
        ent.GetMeshComponent().MeshData->Create(
            MeshFactory::CreateQuad(), GL_TRIANGLES,
            "PixelArtEditorPanel::CreateSpriteEntity");

    float aspect = static_cast<float>(s_Canvas->GetWidth()) /
                   static_cast<float>(s_Canvas->GetHeight());
    ent.GetTransform().Position = Vector3(0.f, 0.f, 0.f);
    ent.GetTransform().Scale    = Vector3(aspect, 1.f, 1.f);

    ent.GetMaterialComponent().MaterialData.Color[0] = 1.f;
    ent.GetMaterialComponent().MaterialData.Color[1] = 1.f;
    ent.GetMaterialComponent().MaterialData.Color[2] = 1.f;
    ent.GetMaterialComponent().MaterialData.Color[3] = 1.f;
    ent.GetMaterialComponent().MaterialData.DiffuseTexture = s_Canvas->GetTexture();

    auto& entities = scene.GetEntities();
    app.SetSelectedEntity(static_cast<int>(entities.size()) - 1);
    s_TargetEntityID   = ent.GetID();
    s_TargetEntityName = ent.GetName();

    app.MarkDirty();
}

// ---------------------------------------------------------------------------
// ApplyToSelectedEntity
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::ApplyToSelectedEntity(Application& app)
{
    if (!s_Canvas) return;
    if (app.GetSelectedEntityIndex() < 0) return;

    Entity& ent = app.GetSelectedEntity();
    if (ent.IsEditorOnly()) return;

    ent.GetMaterialComponent().MaterialData.DiffuseTexture = s_Canvas->GetTexture();
    ent.GetMaterialComponent().MaterialData.Color[0] = 1.f;
    ent.GetMaterialComponent().MaterialData.Color[1] = 1.f;
    ent.GetMaterialComponent().MaterialData.Color[2] = 1.f;
    ent.GetMaterialComponent().MaterialData.Color[3] = 1.f;

    s_TargetEntityID   = ent.GetID();
    s_TargetEntityName = ent.GetName();

    app.MarkDirty();
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
void PixelArtEditorPanel::Draw(Application& app)
{
    s_VulkanUIOnly = app.GetWindow().GetGraphicsAPI() == GraphicsAPI::Vulkan;
    EnsurePalette(s_Palette);
    EnsureFrames();

    // Atualizacao da animacao
    if (s_AnimPlaying && (int)s_Frames.size() > 1)
    {
        float dt = ImGui::GetIO().DeltaTime;
        s_AnimTimer += dt;
        float curDur = s_Frames[s_CurrentFrame].Duration;
        if (s_AnimTimer >= curDur)
        {
            s_AnimTimer -= curDur;
            s_CurrentFrame = (s_CurrentFrame + 1) % (int)s_Frames.size();
            s_Canvas = s_Frames[s_CurrentFrame].Canvas;
        }
    }

    ImGui::SetNextWindowSize(ImVec2(1050, 700), ImGuiCond_FirstUseEver);

    // IMPORTANTE: ImGuiWindowFlags_NoScrollbar e ImGuiWindowFlags_NoScrollWithMouse
    // impedem que a janela principal role para cima e para baixo ao usar o scroll do mouse!
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::Begin(UI("Pintor de Texturas 2D"), nullptr, windowFlags);

    // Barra superior com ferramentas gerais
    DrawToolbar(app);
    ImGui::Separator();

    // Divisao principal: Sidebar a esquerda + Canvas ao centro
    const float sidebarWidth = 160.f;
    const float hudHeight    = 24.f;
    const float animHeight   = s_ShowAnimPanel ? 110.f : 0.f;
    float canvasAreaH = -hudHeight - animHeight - 8.f;

    // Sidebar de Ferramentas
    ImGui::BeginChild("##sidebar", ImVec2(sidebarWidth, canvasAreaH),
                      true, 0);
    DrawToolSidebar();
    ImGui::EndChild();

    ImGui::SameLine();

    // Area do Canvas
    ImGui::BeginChild("##canvas_region",
                      ImVec2(0.f, canvasAreaH),
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    HandleInput();
    DrawCanvasRegion();
    ImGui::EndChild();

    // HUD inferior com coordenadas e cor
    ImGui::Separator();
    DrawHUD(app);

    // Painel de animacao de frames
    DrawAnimationPanel();

    ImGui::End();
}
