#pragma once

#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <imgui.h>
#include "PixelCanvas.h"

class Application;

// ---------------------------------------------------------------------------
// Frame de animacao: cada frame tem seu proprio canvas independente
// ---------------------------------------------------------------------------
struct AnimFrame
{
    std::shared_ptr<PixelCanvas> Canvas;
    float                        Duration = 0.1f; // segundos por frame
};

class PixelArtEditorPanel
{
public:
    enum class Tool
    {
        Pencil = 0,
        SoftBrush,      // Pincel suave com gradiente gaussiano
        Eraser,
        Fill,
        Eyedropper,
        Line,
        Rect,
        RectFilled,
        Circle,
        CircleFilled,
        ReplaceColor,
        Pan,            // Ferramenta Mover / Navegacao
        Ruler,          // Regua: mede distancia exata em pixels
        COUNT
    };

    static void Draw(Application& app);

private:
    // Sub-paineis
    static void DrawToolbar(Application& app);
    static void DrawToolSidebar();
    static void DrawCanvasRegion();
    static void DrawHUD(Application& app);
    static void DrawAnimationPanel();

    // Logica de input e ferramentas
    static void HandleInput();
    static bool ScreenToPixel(const ImVec2& screen, int& outX, int& outY);
    static void ApplyToolAt(int px, int py, bool isStroke, bool isRelease);
    static void FinalizeShape();
    static void ApplySoftBrush(int cx, int cy);

    // Entidade
    static void CreateSpriteEntity(Application& app);
    static void ApplyToSelectedEntity(Application& app);

    // Helpers
    static void AddRecentColor(const glm::vec4& c);
    static void NewCanvas(int w, int h, const glm::vec4& bg);
    static void FitToWindow();
    static void EnsureFrames();

    static const char* ToolName(Tool t);
    static ImVec4 ToImVec4(const glm::vec4& c);
    static glm::vec4 ToGlmVec4(const ImVec4& c);

    static bool ExportSpriteSheet(const std::string& path, bool isTga = false);

    // ---- Estado do painel --------------------------------------------------

    // Canvas ativo
    static std::shared_ptr<PixelCanvas> s_Canvas;

    // Animacao
    static std::vector<AnimFrame>        s_Frames;
    static int                           s_CurrentFrame;
    static bool                          s_AnimPlaying;
    static float                         s_AnimTimer;
    static bool                          s_ShowAnimPanel;
    static bool                          s_ShowOnionSkin;
    static float                         s_OnionSkinAlpha;

    // Ferramenta / Cor
    static Tool                          s_Tool;
    static glm::vec4                     s_Color;
    static int                           s_BrushSize;
    static float                         s_BrushSoftness;
    static float                         s_BrushOpacity;
    static bool                          s_BrushCircular;
    static bool                          s_MirrorVertical;
    static bool                          s_MirrorHorizontal;
    static float                         s_MirrorOffsetX;
    static float                         s_MirrorOffsetY;

    // Paleta
    static std::vector<glm::vec4>        s_Palette;
    static std::vector<glm::vec4>        s_RecentColors;

    // Viewport / Zoom / Pan
    static float                         s_Zoom;
    static ImVec2                        s_Pan;
    static bool                          s_ShowGrid;
    static bool                          s_ShowCheckerboard;
    static bool                          s_NeedsFit;

    // Guias / Regua
    static std::vector<float>            s_GuideX;
    static std::vector<float>            s_GuideY;
    static bool                          s_ShowRuler;
    static bool                          s_RulerActive;
    static int                           s_RulerX0, s_RulerY0;
    static int                           s_RulerX1, s_RulerY1;

    // Entidade alvo
    static uint64_t                      s_TargetEntityID;
    static std::string                   s_TargetEntityName;

    // Estado de desenho
    static bool                          s_Drawing;
    static int                           s_DrawStartX, s_DrawStartY;
    static int                           s_DrawLastX,  s_DrawLastY;
    static int                           s_ShapeCurrentX, s_ShapeCurrentY;
    static bool                          s_HasLastStrokePoint;

    // Bounds do canvas na tela
    static ImVec2                        s_CanvasMin;
    static ImVec2                        s_CanvasMax;

    // Misc
    static std::string                   s_ExportPath;
    static int                           s_NewCanvasW;
    static int                           s_NewCanvasH;
    static int                           s_SpriteCounter;
};
