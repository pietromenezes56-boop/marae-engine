using System;
using System.Globalization;

namespace MaraeEngine
{
    /// <summary>
    /// Estrutura de cor RGBA de ponto flutuante. Cada componente está no intervalo [0, 1].
    /// Usada para tintas, cores de luz, albedo de materiais e cores de UI.
    /// </summary>
    [Serializable]
    public struct Color : IEquatable<Color>
    {
        /// <summary>Componente vermelho [0, 1].</summary>
        public float R;

        /// <summary>Componente verde [0, 1].</summary>
        public float G;

        /// <summary>Componente azul [0, 1].</summary>
        public float B;

        /// <summary>Componente alfa (opacidade) [0, 1]. 0 = totalmente transparente.</summary>
        public float A;

        /// <summary>Cor branca (1, 1, 1, 1).</summary>
        public static readonly Color White = new(1f, 1f, 1f, 1f);

        /// <summary>Cor preta (0, 0, 0, 1).</summary>
        public static readonly Color Black = new(0f, 0f, 0f, 1f);

        /// <summary>Cor vermelha (1, 0, 0, 1).</summary>
        public static readonly Color Red = new(1f, 0f, 0f, 1f);

        /// <summary>Cor verde (0, 1, 0, 1).</summary>
        public static readonly Color Green = new(0f, 1f, 0f, 1f);

        /// <summary>Cor azul (0, 0, 1, 1).</summary>
        public static readonly Color Blue = new(0f, 0f, 1f, 1f);

        /// <summary>Cor amarela (1, 1, 0, 1).</summary>
        public static readonly Color Yellow = new(1f, 1f, 0f, 1f);

        /// <summary>Cor ciano (0, 1, 1, 1).</summary>
        public static readonly Color Cyan = new(0f, 1f, 1f, 1f);

        /// <summary>Cor magenta (1, 0, 1, 1).</summary>
        public static readonly Color Magenta = new(1f, 0f, 1f, 1f);

        /// <summary>Cor cinza (0.5, 0.5, 0.5, 1).</summary>
        public static readonly Color Gray = new(0.5f, 0.5f, 0.5f, 1f);

        /// <summary>Cor cinza claro (0.75, 0.75, 0.75, 1).</summary>
        public static readonly Color LightGray = new(0.75f, 0.75f, 0.75f, 1f);

        /// <summary>Cor cinza escuro (0.25, 0.25, 0.25, 1).</summary>
        public static readonly Color DarkGray = new(0.25f, 0.25f, 0.25f, 1f);

        /// <summary>Transparente total (0, 0, 0, 0).</summary>
        public static readonly Color Clear = new(0f, 0f, 0f, 0f);

        /// <summary>Cor laranja (1, 0.5, 0, 1).</summary>
        public static readonly Color Orange = new(1f, 0.5f, 0f, 1f);

        /// <summary>Cor roxa (0.5, 0, 0.5, 1).</summary>
        public static readonly Color Purple = new(0.5f, 0f, 0.5f, 1f);

        /// <summary>
        /// Inicializa uma nova cor RGB com alpha = 1 (totalmente opaca).
        /// </summary>
        /// <param name="r">Vermelho [0, 1].</param>
        /// <param name="g">Verde [0, 1].</param>
        /// <param name="b">Azul [0, 1].</param>
        public Color(float r, float g, float b)
        {
            R = r;
            G = g;
            B = b;
            A = 1f;
        }

        /// <summary>
        /// Inicializa uma nova cor RGBA.
        /// </summary>
        /// <param name="r">Vermelho [0, 1].</param>
        /// <param name="g">Verde [0, 1].</param>
        /// <param name="b">Azul [0, 1].</param>
        /// <param name="a">Alfa [0, 1].</param>
        public Color(float r, float g, float b, float a)
        {
            R = r;
            G = g;
            B = b;
            A = a;
        }

        /// <summary>
        /// Multiplica duas cores componente a componente (tintagem).
        /// </summary>
        public static Color operator *(Color a, Color b)
        {
            return new Color(a.R * b.R, a.G * b.G, a.B * b.B, a.A * b.A);
        }

        /// <summary>Multiplica a cor por um escalar (escurece/clareia).</summary>
        public static Color operator *(Color a, float b)
        {
            return new Color(a.R * b, a.G * b, a.B * b, a.A * b);
        }

        /// <summary>Multiplica um escalar por uma cor (comutatividade).</summary>
        public static Color operator *(float b, Color a)
        {
            return new Color(a.R * b, a.G * b, a.B * b, a.A * b);
        }

        /// <summary>Soma duas cores componente a componente.</summary>
        public static Color operator +(Color a, Color b)
        {
            return new Color(a.R + b.R, a.G + b.G, a.B + b.B, a.A + b.A);
        }

        /// <summary>Subtrai duas cores componente a componente.</summary>
        public static Color operator -(Color a, Color b)
        {
            return new Color(a.R - b.R, a.G - b.G, a.B - b.B, a.A - b.A);
        }

        /// <summary>Compara duas cores por igualdade exata de bits (epsilon baixo).</summary>
        public static bool operator ==(Color a, Color b)
        {
            return Math.Abs(a.R - b.R) < 1e-5f
                && Math.Abs(a.G - b.G) < 1e-5f
                && Math.Abs(a.B - b.B) < 1e-5f
                && Math.Abs(a.A - b.A) < 1e-5f;
        }

        /// <summary>Compara duas cores por diferença.</summary>
        public static bool operator !=(Color a, Color b)
        {
            return !(a == b);
        }

        /// <summary>Constrói uma cor a partir de componentes em byte [0,255].</summary>
        public static Color FromByte(byte r, byte g, byte b, byte a = 255)
        {
            return new Color(r / 255f, g / 255f, b / 255f, a / 255f);
        }

        /// <summary>
        /// Interpola linearmente entre duas cores.
        /// </summary>
        /// <param name="a">Cor inicial.</param>
        /// <param name="b">Cor final.</param>
        /// <param name="t">Fator de interpolação [0, 1].</param>
        public static Color Lerp(Color a, Color b, float t)
        {
            t = Math.Clamp(t, 0f, 1f);
            return new Color(
                a.R + (b.R - a.R) * t,
                a.G + (b.G - a.G) * t,
                a.B + (b.B - a.B) * t,
                a.A + (b.A - a.A) * t);
        }

        /// <inheritdoc/>
        public readonly bool Equals(Color other)
        {
            return this == other;
        }

        /// <inheritdoc/>
        public override readonly bool Equals(object? obj)
        {
            return obj is Color other && Equals(other);
        }

        /// <inheritdoc/>
        public override readonly int GetHashCode()
        {
            return HashCode.Combine(R, G, B, A);
        }

        /// <inheritdoc/>
        public override readonly string ToString()
        {
            return string.Format(CultureInfo.InvariantCulture, "RGBA({0:F3}, {1:F3}, {2:F3}, {3:F3})", R, G, B, A);
        }
    }
}
