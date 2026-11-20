using System;
using System.Numerics;
using MaraeEngine.Internal;

namespace MaraeEngine
{
    /// <summary>Tipos de elementos UI 2D da cena.</summary>
    public enum UIElementType
    {
        ScreenGui = 0,
        Frame = 1,
        TextLabel = 2,
        TextButton = 3,
        ImageLabel = 4,
        ImageButton = 5,
        TextBox = 6,
        ScrollingFrame = 7,
        Slider = 8,
        Checkbox = 9,
        ProgressBar = 10,
        Dropdown = 11
    }

    public enum UITextAlignment { Left = 0, Center = 1, Right = 2 }

    /// <summary>Ponte gerenciada para o UIComponent nativo da Marae.</summary>
    public sealed class UIElement
    {
        private readonly Entity _entity;
        internal UIElement(Entity entity) => _entity = entity;

        public Entity entity => _entity;
        public UIElementType type
        {
            get => (UIElementType)(NativeDelegates.UIGetType?.Invoke(_entity.id) ?? -1);
            set => ComponentAccess.SetInt(_entity.id, ComponentIds.UI, UIProperty.Type, (int)value);
        }
        public bool enabled
        {
            get => NativeDelegates.UIGetEnabled?.Invoke(_entity.id) ?? false;
            set => NativeDelegates.UISetEnabled?.Invoke(_entity.id, value);
        }

        public Vector4 rect
        {
            get
            {
                float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
                NativeDelegates.UIGetRect?.Invoke(_entity.id, out x, out y, out w, out h);
                return new Vector4(x, y, w, h);
            }
            set
            {
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.X, value.X);
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.Y, value.Y);
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.Width, Math.Max(0f, value.Z));
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.Height, Math.Max(0f, value.W));
            }
        }

        public float anchorX
        {
            get => ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.AnchorX);
            set => ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.AnchorX, Math.Clamp(value, 0f, 1f));
        }
        public float anchorY
        {
            get => ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.AnchorY);
            set => ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.AnchorY, Math.Clamp(value, 0f, 1f));
        }
        /// <summary>Ativa layout relativo ao canvas (960×540) com âncoras e offsets responsivos.</summary>
        public bool useResponsiveAnchors
        {
            get => ComponentAccess.GetBool(_entity.id, ComponentIds.UI, UIProperty.UseResponsiveAnchors);
            set => ComponentAccess.SetBool(_entity.id, ComponentIds.UI, UIProperty.UseResponsiveAnchors, value);
        }
        public Vector2 anchorMin
        {
            get => new Vector2(ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.AnchorMinX),
                               ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.AnchorMinY));
            set
            {
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.AnchorMinX, Math.Clamp(value.X, 0f, 1f));
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.AnchorMinY, Math.Clamp(value.Y, 0f, 1f));
            }
        }
        public Vector2 anchorMax
        {
            get => new Vector2(ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.AnchorMaxX),
                               ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.AnchorMaxY));
            set
            {
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.AnchorMaxX, Math.Clamp(value.X, 0f, 1f));
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.AnchorMaxY, Math.Clamp(value.Y, 0f, 1f));
            }
        }
        /// <summary>Offsets do retângulo relativos às âncoras, em pixels do canvas de referência.</summary>
        public Vector4 anchorOffsets
        {
            get => new Vector4(
                ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.OffsetMinX),
                ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.OffsetMinY),
                ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.OffsetMaxX),
                ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.OffsetMaxY));
            set
            {
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.OffsetMinX, value.X);
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.OffsetMinY, value.Y);
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.OffsetMaxX, value.Z);
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.OffsetMaxY, value.W);
            }
        }
        public float rotation
        {
            get => ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.Rotation);
            set => ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.Rotation, value);
        }
        public float transparency
        {
            get => ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.Transparency);
            set => ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.Transparency, Math.Clamp(value, 0f, 1f));
        }
        public float cornerRadius
        {
            get => ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.CornerRadius);
            set => ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.CornerRadius, Math.Max(0f, value));
        }
        public float borderThickness
        {
            get => ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.BorderThickness);
            set => ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.BorderThickness, Math.Max(0f, value));
        }
        public float fontSize
        {
            get => ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.FontSize);
            set => ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.FontSize, Math.Max(1f, value));
        }
        public int zIndex
        {
            get => ComponentAccess.GetInt(_entity.id, ComponentIds.UI, UIProperty.ZIndex);
            set => ComponentAccess.SetInt(_entity.id, ComponentIds.UI, UIProperty.ZIndex, value);
        }
        public UITextAlignment textAlignment
        {
            get => (UITextAlignment)ComponentAccess.GetInt(_entity.id, ComponentIds.UI, UIProperty.TextAlignment);
            set => ComponentAccess.SetInt(_entity.id, ComponentIds.UI, UIProperty.TextAlignment, (int)value);
        }
        public bool clipsContent
        {
            get => ComponentAccess.GetBool(_entity.id, ComponentIds.UI, UIProperty.ClipsContent);
            set => ComponentAccess.SetBool(_entity.id, ComponentIds.UI, UIProperty.ClipsContent, value);
        }
        public Color backgroundColor
        {
            get => ComponentAccess.GetColor(_entity.id, ComponentIds.UI, UIProperty.BackgroundColor);
            set => ComponentAccess.SetColor(_entity.id, ComponentIds.UI, UIProperty.BackgroundColor, value);
        }
        public Color textColor
        {
            get => ComponentAccess.GetColor(_entity.id, ComponentIds.UI, UIProperty.TextColor);
            set => ComponentAccess.SetColor(_entity.id, ComponentIds.UI, UIProperty.TextColor, value);
        }
        public Color borderColor
        {
            get => ComponentAccess.GetColor(_entity.id, ComponentIds.UI, UIProperty.BorderColor);
            set => ComponentAccess.SetColor(_entity.id, ComponentIds.UI, UIProperty.BorderColor, value);
        }
        public string text
        {
            get => ComponentAccess.GetString(_entity.id, ComponentIds.UI, UIProperty.Text);
            set => ComponentAccess.SetString(_entity.id, ComponentIds.UI, UIProperty.Text, value);
        }
        public string font
        {
            get => ComponentAccess.GetString(_entity.id, ComponentIds.UI, UIProperty.Font);
            set => ComponentAccess.SetString(_entity.id, ComponentIds.UI, UIProperty.Font, value);
        }
        public string imagePath
        {
            get => ComponentAccess.GetString(_entity.id, ComponentIds.UI, UIProperty.ImagePath);
            set => ComponentAccess.SetString(_entity.id, ComponentIds.UI, UIProperty.ImagePath, value);
        }

        /// <summary>True quando o cursor está dentro de um TextButton/ImageButton.</summary>
        public bool IsPointerOver => NativeDelegates.UIIsPointerOver?.Invoke(_entity.id) ?? false;
        public bool isButton => type == UIElementType.TextButton || type == UIElementType.ImageButton;

        // ── Nine-slice (imagens com bordas esticáveis) ───────────────────────
        public bool nineSlice
        {
            get => ComponentAccess.GetBool(_entity.id, ComponentIds.UI, UIProperty.NineSlice);
            set => ComponentAccess.SetBool(_entity.id, ComponentIds.UI, UIProperty.NineSlice, value);
        }
        public Vector4 sliceBorders
        {
            get => new Vector4(
                ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.SliceLeft),
                ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.SliceTop),
                ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.SliceRight),
                ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.SliceBottom));
            set
            {
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.SliceLeft, Math.Max(0f, value.X));
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.SliceTop, Math.Max(0f, value.Y));
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.SliceRight, Math.Max(0f, value.Z));
                ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.SliceBottom, Math.Max(0f, value.W));
            }
        }

        // ── Rolagem (ScrollingFrame) ─────────────────────────────────────────
        public bool scrollViewEnabled
        {
            get => ComponentAccess.GetBool(_entity.id, ComponentIds.UI, UIProperty.ScrollViewEnabled);
            set => ComponentAccess.SetBool(_entity.id, ComponentIds.UI, UIProperty.ScrollViewEnabled, value);
        }
        public float scrollOffsetY
        {
            get => ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.ScrollOffsetY);
            set => ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.ScrollOffsetY, Math.Max(0f, value));
        }

        // ── Valor (Slider / ProgressBar) ─────────────────────────────────────
        public float value
        {
            get => ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.Value);
            set => ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.Value, value);
        }
        public float minValue
        {
            get => ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.MinValue);
            set => ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.MinValue, value);
        }
        public float maxValue
        {
            get => ComponentAccess.GetFloat(_entity.id, ComponentIds.UI, UIProperty.MaxValue);
            set => ComponentAccess.SetFloat(_entity.id, ComponentIds.UI, UIProperty.MaxValue, value);
        }
        /// <summary>Valor normalizado em [0,1] dentro do intervalo MinValue..MaxValue.</summary>
        public float normalizedValue
        {
            get
            {
                float span = maxValue - minValue;
                if (span <= 0.0001f) return 0f;
                return Math.Clamp((value - minValue) / span, 0f, 1f);
            }
        }

        // ── Checkbox ─────────────────────────────────────────────────────────
        public bool isChecked
        {
            get => ComponentAccess.GetBool(_entity.id, ComponentIds.UI, UIProperty.Checked);
            set => ComponentAccess.SetBool(_entity.id, ComponentIds.UI, UIProperty.Checked, value);
        }

        // ── Dropdown / ComboBox ──────────────────────────────────────────────
        public int selectedIndex
        {
            get => ComponentAccess.GetInt(_entity.id, ComponentIds.UI, UIProperty.SelectedIndex);
            set => ComponentAccess.SetInt(_entity.id, ComponentIds.UI, UIProperty.SelectedIndex, Math.Max(0, value));
        }
        public bool dropdownOpen
        {
            get => ComponentAccess.GetBool(_entity.id, ComponentIds.UI, UIProperty.DropdownOpen);
            set => ComponentAccess.SetBool(_entity.id, ComponentIds.UI, UIProperty.DropdownOpen, value);
        }
        public string[] options
        {
            get
            {
                string joined = ComponentAccess.GetString(_entity.id, ComponentIds.UI, UIProperty.Options);
                return string.IsNullOrEmpty(joined) ? Array.Empty<string>() : joined.Split('\n');
            }
            set => ComponentAccess.SetString(_entity.id, ComponentIds.UI, UIProperty.Options,
                value == null ? string.Empty : string.Join("\n", value));
        }

        // ── Cores de estado e preenchimento ──────────────────────────────────
        public Color hoverColor
        {
            get => ComponentAccess.GetColor(_entity.id, ComponentIds.UI, UIProperty.HoverColor);
            set => ComponentAccess.SetColor(_entity.id, ComponentIds.UI, UIProperty.HoverColor, value);
        }
        public Color pressedColor
        {
            get => ComponentAccess.GetColor(_entity.id, ComponentIds.UI, UIProperty.PressedColor);
            set => ComponentAccess.SetColor(_entity.id, ComponentIds.UI, UIProperty.PressedColor, value);
        }
        public Color fillColor
        {
            get => ComponentAccess.GetColor(_entity.id, ComponentIds.UI, UIProperty.FillColor);
            set => ComponentAccess.SetColor(_entity.id, ComponentIds.UI, UIProperty.FillColor, value);
        }
        public Color trackColor
        {
            get => ComponentAccess.GetColor(_entity.id, ComponentIds.UI, UIProperty.TrackColor);
            set => ComponentAccess.SetColor(_entity.id, ComponentIds.UI, UIProperty.TrackColor, value);
        }

        // ── Estado de runtime (somente leitura) ──────────────────────────────
        /// <summary>True enquanto o ponteiro está sobre o elemento.</summary>
        public bool isHovered => ComponentAccess.GetBool(_entity.id, ComponentIds.UI, UIProperty.IsHovered);
        /// <summary>True enquanto o elemento está pressionado.</summary>
        public bool isPressed => ComponentAccess.GetBool(_entity.id, ComponentIds.UI, UIProperty.IsPressed);
        /// <summary>True quando um TextBox está com foco de teclado.</summary>
        public bool isFocused => ComponentAccess.GetBool(_entity.id, ComponentIds.UI, UIProperty.IsFocused);
        /// <summary>Índice da opção sob o cursor quando um Dropdown está aberto (-1 caso contrário).</summary>
        public int hoverOptionIndex => ComponentAccess.GetInt(_entity.id, ComponentIds.UI, UIProperty.HoverOptionIndex);
        /// <summary>Posição do cursor de texto (em caracteres) do TextBox.</summary>
        public int caretIndex
        {
            get => ComponentAccess.GetInt(_entity.id, ComponentIds.UI, UIProperty.CaretIndex);
            set => ComponentAccess.SetInt(_entity.id, ComponentIds.UI, UIProperty.CaretIndex, Math.Max(0, value));
        }
    }
}
