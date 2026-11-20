#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <iterator>

// Elementos 2D de interface inspirados no modelo ScreenGui/GuiObject:
// Frame, labels, caixas de texto e botões de texto/imagem.
// Os controles de widget (ScrollingFrame, Slider, Checkbox, ProgressBar,
// Dropdown) ampliam o modelo para uma UI de jogo de verdade, como no
// Roblox Studio / Unity Canvas.
enum class UIElementType
{
    ScreenGui = 0,
    Frame,
    TextLabel,
    TextButton,
    ImageLabel,
    ImageButton,
    TextBox,
    ScrollingFrame,
    Slider,
    Checkbox,
    ProgressBar,
    Dropdown
};

enum class UITextAlignment
{
    Left = 0,
    Center,
    Right
};

enum class UILayoutMode
{
    None = 0,
    Horizontal,
    Vertical,
    Grid
};

struct UIComponent
{
    bool Enabled = true;
    UIElementType Type = UIElementType::Frame;
    float PositionX = 0.0f;
    float PositionY = 0.0f;
    float Width = 240.0f;
    float Height = 64.0f;
    float AnchorX = 0.0f;
    float AnchorY = 0.0f;
    // Responsive anchors are opt-in so old scenes retain their pixel layout.
    bool UseResponsiveAnchors = false;
    float AnchorMinX = 0.0f, AnchorMinY = 0.0f;
    float AnchorMaxX = 0.0f, AnchorMaxY = 0.0f;
    float OffsetMinX = 0.0f, OffsetMinY = 0.0f;
    float OffsetMaxX = 240.0f, OffsetMaxY = 64.0f;
    float Rotation = 0.0f;
    int ZIndex = 0;
    float BackgroundColor[4] = { 0.12f, 0.15f, 0.22f, 1.0f };
    float TextColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float Transparency = 0.0f;
    float CornerRadius = 0.0f;
    float BorderColor[4] = { 0.25f, 0.55f, 0.90f, 1.0f };
    float BorderThickness = 0.0f;
    UITextAlignment TextAlignment = UITextAlignment::Left;
    bool ClipsContent = false;
    UILayoutMode LayoutMode = UILayoutMode::None;
    float LayoutPadding = 8.0f;
    float LayoutSpacing = 8.0f;
    int LayoutColumns = 2;
    bool StretchLayoutChildren = false;
    bool ScrollViewEnabled = false;
    float ScrollOffsetY = 0.0f; // estado de runtime; não altera cenas ao rolar
    bool RichTextEnabled = false;
    int Theme = 0; // 0=customizado, 1=Marae, 2=claro, 3=retro
    std::string Text;
    std::string ImagePath;
    std::string Font = "Default";
    float FontSize = 14.0f; // tamanho base da fonte em pixels (editor)

    // ── Cores de estado (hover/pressionado) e preenchimento ──────────────
    // Usadas por botões e widgets. A cor de fundo padrão permanece em
    // BackgroundColor para manter compatibilidade com cenas antigas.
    float HoverColor[4] = { 0.16f, 0.22f, 0.34f, 1.0f };
    float PressedColor[4] = { 0.10f, 0.14f, 0.22f, 1.0f };
    // Cor de preenchimento (Slider/Checkbox/ProgressBar/Dropdown).
    float FillColor[4] = { 0.25f, 0.55f, 0.90f, 1.0f };
    // Cor do trilho/fundo do widget (Slider/ProgressBar).
    float TrackColor[4] = { 0.09f, 0.11f, 0.16f, 1.0f };

    // ── Nine-slice (imagens com bordas esticáveis) ───────────────────────
    bool NineSlice = false;
    float SliceLeft = 12.0f;
    float SliceTop = 12.0f;
    float SliceRight = 12.0f;
    float SliceBottom = 12.0f;

    // ── Valor de controle (Slider/ProgressBar) ───────────────────────────
    float Value = 0.0f;
    float MinValue = 0.0f;
    float MaxValue = 1.0f;

    // ── Checkbox ─────────────────────────────────────────────────────────
    bool Checked = false;

    // ── Dropdown / ComboBox ──────────────────────────────────────────────
    std::vector<std::string> Options;
    int SelectedIndex = 0;

    // ── Estado de runtime (não serializado / não altera a cena) ──────────
    bool IsHovered = false;
    bool IsPressed = false;
    bool IsFocused = false;
    bool DropdownOpen = false;
    int HoverOptionIndex = -1; // opção sob o cursor quando o dropdown está aberto
    // Cursor/seleção do TextBox (índices em caracteres).
    int CaretIndex = 0;
    int SelectionAnchor = 0;

    // Normaliza Value para o intervalo [MinValue, MaxValue] (0..1).
    float NormalizedValue() const
    {
        const float span = MaxValue - MinValue;
        if (span <= 0.0001f)
            return 0.0f;
        return std::clamp((Value - MinValue) / span, 0.0f, 1.0f);
    }

    struct ResolvedRect { float X, Y, Width, Height; };

    ResolvedRect ResolveRect(float canvasWidth, float canvasHeight) const
    {
        canvasWidth = std::max(0.0f, canvasWidth);
        canvasHeight = std::max(0.0f, canvasHeight);
        if (!UseResponsiveAnchors)
            return { PositionX - AnchorX * Width, PositionY - AnchorY * Height,
                     std::max(0.0f, Width), std::max(0.0f, Height) };

        const float x0 = std::clamp(AnchorMinX, 0.0f, 1.0f) * canvasWidth + OffsetMinX;
        const float y0 = std::clamp(AnchorMinY, 0.0f, 1.0f) * canvasHeight + OffsetMinY;
        const float x1 = std::clamp(AnchorMaxX, 0.0f, 1.0f) * canvasWidth + OffsetMaxX;
        const float y1 = std::clamp(AnchorMaxY, 0.0f, 1.0f) * canvasHeight + OffsetMaxY;
        return { x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0) };
    }

    void ResolveThemeColors(float* background, float* border, float* text) const
    {
        std::copy(std::begin(BackgroundColor), std::end(BackgroundColor), background);
        std::copy(std::begin(BorderColor), std::end(BorderColor), border);
        std::copy(std::begin(TextColor), std::end(TextColor), text);
        const float* presetBackground = nullptr;
        const float* presetBorder = nullptr;
        const float* presetText = nullptr;
        static constexpr float maraeBackground[] = { 0.075f, 0.09f, 0.13f, 1.0f };
        static constexpr float maraeBorder[] = { 0.15f, 0.43f, 0.78f, 1.0f };
        static constexpr float maraeText[] = { 0.92f, 0.95f, 1.0f, 1.0f };
        static constexpr float lightBackground[] = { 0.91f, 0.93f, 0.96f, 1.0f };
        static constexpr float lightBorder[] = { 0.45f, 0.51f, 0.60f, 1.0f };
        static constexpr float lightText[] = { 0.08f, 0.10f, 0.14f, 1.0f };
        static constexpr float retroBackground[] = { 0.10f, 0.14f, 0.10f, 1.0f };
        static constexpr float retroBorder[] = { 0.60f, 0.82f, 0.35f, 1.0f };
        static constexpr float retroText[] = { 0.84f, 0.96f, 0.62f, 1.0f };
        if (Theme == 1) { presetBackground = maraeBackground; presetBorder = maraeBorder; presetText = maraeText; }
        else if (Theme == 2) { presetBackground = lightBackground; presetBorder = lightBorder; presetText = lightText; }
        else if (Theme == 3) { presetBackground = retroBackground; presetBorder = retroBorder; presetText = retroText; }
        if (presetBackground)
        {
            std::copy(presetBackground, presetBackground + 4, background);
            std::copy(presetBorder, presetBorder + 4, border);
            std::copy(presetText, presetText + 4, text);
        }
    }

    void EnableResponsiveAnchors(float parentWidth = 960.0f, float parentHeight = 540.0f)
    {
        const float left = PositionX - AnchorX * Width;
        const float top = PositionY - AnchorY * Height;
        UseResponsiveAnchors = true;
        AnchorMinX = AnchorMaxX = AnchorX;
        AnchorMinY = AnchorMaxY = AnchorY;
        OffsetMinX = left - AnchorX * parentWidth;
        OffsetMinY = top - AnchorY * parentHeight;
        OffsetMaxX = left + Width - AnchorX * parentWidth;
        OffsetMaxY = top + Height - AnchorY * parentHeight;
    }

    bool ContainsPoint(float x, float y, float canvasWidth = 960.0f, float canvasHeight = 540.0f) const
    {
        const ResolvedRect rect = ResolveRect(canvasWidth, canvasHeight);
        return Enabled && x >= rect.X && x <= rect.X + rect.Width &&
               y >= rect.Y && y <= rect.Y + rect.Height;
    }
};

inline const char* UIElementTypeName(UIElementType type)
{
    switch (type)
    {
    case UIElementType::ScreenGui: return "ScreenGui";
    case UIElementType::Frame: return "Frame";
    case UIElementType::TextLabel: return "TextLabel";
    case UIElementType::TextButton: return "TextButton";
    case UIElementType::ImageLabel: return "ImageLabel";
    case UIElementType::ImageButton: return "ImageButton";
    case UIElementType::TextBox: return "TextBox";
    case UIElementType::ScrollingFrame: return "ScrollingFrame";
    case UIElementType::Slider: return "Slider";
    case UIElementType::Checkbox: return "Checkbox";
    case UIElementType::ProgressBar: return "ProgressBar";
    case UIElementType::Dropdown: return "Dropdown";
    default: return "GuiObject";
    }
}

// Um elemento desenha um fundo sólido (com cantos/borda)?
// Widgets que desenham a própria moldura (Slider/ProgressBar/Checkbox) ficam
// de fora para não receberem um retângulo cheio por cima.
inline bool UIElementHasBackground(UIElementType type)
{
    switch (type)
    {
    case UIElementType::TextLabel:
    case UIElementType::ImageLabel:
    case UIElementType::Slider:
    case UIElementType::ProgressBar:
    case UIElementType::Checkbox:
        return false;
    default:
        return true;
    }
}

// Um elemento desenha texto por padrão?
inline bool UIElementHasText(UIElementType type)
{
    switch (type)
    {
    case UIElementType::TextLabel:
    case UIElementType::TextButton:
    case UIElementType::TextBox:
    case UIElementType::Checkbox:
    case UIElementType::Slider:
    case UIElementType::ProgressBar:
    case UIElementType::Dropdown:
        return true;
    default:
        return false;
    }
}

inline bool UIElementHasImage(UIElementType type)
{
    return type == UIElementType::ImageLabel || type == UIElementType::ImageButton;
}

inline bool UIElementIsButton(UIElementType type)
{
    return type == UIElementType::TextButton || type == UIElementType::ImageButton;
}

// Elementos que capturam o ponteiro/teclado (bloqueiam cliques no mundo).
inline bool UIElementIsInteractive(UIElementType type)
{
    switch (type)
    {
    case UIElementType::Frame:
    case UIElementType::ScrollingFrame:
    case UIElementType::TextButton:
    case UIElementType::ImageButton:
    case UIElementType::TextBox:
    case UIElementType::Slider:
    case UIElementType::Checkbox:
    case UIElementType::Dropdown:
        return true;
    default:
        return false;
    }
}
