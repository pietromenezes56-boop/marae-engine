using System;
using System.Globalization;

namespace MaraeEngine
{
    /// <summary>
    /// Estrutura de vetor 3D com componentes X, Y e Z de ponto flutuante.
    /// Usada para posições, direções, escalas e operações geométricas na engine.
    /// </summary>
    [Serializable]
    public struct Vector3 : IEquatable<Vector3>
    {
        /// <summary>Componente X do vetor.</summary>
        public float X;

        /// <summary>Componente Y do vetor.</summary>
        public float Y;

        /// <summary>Componente Z do vetor.</summary>
        public float Z;

        /// <summary>Vetor (0, 0, 0).</summary>
        public static readonly Vector3 zero = new(0f, 0f, 0f);

        /// <summary>Vetor (1, 1, 1).</summary>
        public static readonly Vector3 one = new(1f, 1f, 1f);

        /// <summary>Vetor direção para cima (0, 1, 0).</summary>
        public static readonly Vector3 up = new(0f, 1f, 0f);

        /// <summary>Vetor direção para frente (0, 0, 1).</summary>
        public static readonly Vector3 forward = new(0f, 0f, 1f);

        /// <summary>Vetor direção para a direita (1, 0, 0).</summary>
        public static readonly Vector3 right = new(1f, 0f, 0f);

        /// <summary>
        /// Inicializa uma nova instância de <see cref="Vector3"/> com os componentes especificados.
        /// </summary>
        /// <param name="x">Componente X.</param>
        /// <param name="y">Componente Y.</param>
        /// <param name="z">Componente Z.</param>
        public Vector3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        /// <summary>
        /// Inicializa uma nova instância de <see cref="Vector3"/> com X e Y especificados e Z = 0.
        /// </summary>
        /// <param name="x">Componente X.</param>
        /// <param name="y">Componente Y.</param>
        public Vector3(float x, float y)
        {
            X = x;
            Y = y;
            Z = 0f;
        }

        /// <summary>Magnitude (comprimento) do vetor.</summary>
        public float magnitude => (float)Math.Sqrt(X * X + Y * Y + Z * Z);

        /// <summary>Alias em PascalCase para magnitude.</summary>
        public float Length() => magnitude;

        /// <summary>Quadrado da magnitude do vetor (evita raiz quadrada, útil para comparações).</summary>
        public float sqrMagnitude => X * X + Y * Y + Z * Z;

        /// <summary>Retorna uma cópia normalizada do vetor (magnitude = 1).</summary>
        public Vector3 normalized
        {
            get
            {
                float mag = magnitude;
                if (mag > 1e-6f)
                    return new Vector3(X / mag, Y / mag, Z / mag);
                return zero;
            }
        }

        /// <summary>Retorna uma cópia normalizada, em formato de método.</summary>
        public Vector3 Normalized() => normalized;

        /// <summary>Normaliza o vetor atual in-place.</summary>
        public void Normalize()
        {
            float mag = magnitude;
            if (mag > 1e-6f)
            {
                X /= mag;
                Y /= mag;
                Z /= mag;
            }
            else
            {
                X = 0f;
                Y = 0f;
                Z = 0f;
            }
        }

        /// <summary>
        /// Calcula o produto escalar (dot product) entre dois vetores.
        /// </summary>
        /// <param name="a">Primeiro vetor.</param>
        /// <param name="b">Segundo vetor.</param>
        /// <returns>Produto escalar como float.</returns>
        public static float Dot(Vector3 a, Vector3 b)
        {
            return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
        }

        /// <summary>
        /// Calcula o produto vetorial (cross product) entre dois vetores.
        /// </summary>
        /// <param name="a">Primeiro vetor.</param>
        /// <param name="b">Segundo vetor.</param>
        /// <returns>Vetor perpendicular a ambos os vetores de entrada.</returns>
        public static Vector3 Cross(Vector3 a, Vector3 b)
        {
            return new Vector3(
                a.Y * b.Z - a.Z * b.Y,
                a.Z * b.X - a.X * b.Z,
                a.X * b.Y - a.Y * b.X);
        }

        /// <summary>
        /// Normaliza um vetor.
        /// </summary>
        /// <param name="value">Vetor a normalizar.</param>
        /// <returns>Vetor normalizado.</returns>
        public static Vector3 Normalize(Vector3 value)
        {
            return value.normalized;
        }

        /// <summary>
        /// Interpola linearmente entre dois vetores.
        /// </summary>
        /// <param name="a">Vetor inicial (t = 0).</param>
        /// <param name="b">Vetor final (t = 1).</param>
        /// <param name="t">Parâmetro de interpolação [0, 1].</param>
        /// <returns>Vetor interpolado.</returns>
        public static Vector3 Lerp(Vector3 a, Vector3 b, float t)
        {
            t = Math.Clamp(t, 0f, 1f);
            return new Vector3(
                a.X + (b.X - a.X) * t,
                a.Y + (b.Y - a.Y) * t,
                a.Z + (b.Z - a.Z) * t);
        }

        /// <summary>
        /// Interpola linearmente sem limitar t ao intervalo [0, 1].
        /// </summary>
        public static Vector3 LerpUnclamped(Vector3 a, Vector3 b, float t)
        {
            return new Vector3(
                a.X + (b.X - a.X) * t,
                a.Y + (b.Y - a.Y) * t,
                a.Z + (b.Z - a.Z) * t);
        }

        /// <summary>
        /// Calcula a distância entre dois pontos.
        /// </summary>
        /// <param name="a">Ponto A.</param>
        /// <param name="b">Ponto B.</param>
        /// <returns>Distância entre A e B.</returns>
        public static float Distance(Vector3 a, Vector3 b)
        {
            return (b - a).magnitude;
        }

        /// <summary>Soma dois vetores componente a componente.</summary>
        public static Vector3 operator +(Vector3 a, Vector3 b)
        {
            return new Vector3(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
        }

        /// <summary>Subtrai dois vetores componente a componente.</summary>
        public static Vector3 operator -(Vector3 a, Vector3 b)
        {
            return new Vector3(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
        }

        /// <summary>Nega o vetor (inverte o sinal de todos os componentes).</summary>
        public static Vector3 operator -(Vector3 a)
        {
            return new Vector3(-a.X, -a.Y, -a.Z);
        }

        /// <summary>Multiplica o vetor por um escalar.</summary>
        public static Vector3 operator *(Vector3 a, float d)
        {
            return new Vector3(a.X * d, a.Y * d, a.Z * d);
        }

        /// <summary>Multiplica um escalar por um vetor (comutatividade).</summary>
        public static Vector3 operator *(float d, Vector3 a)
        {
            return new Vector3(a.X * d, a.Y * d, a.Z * d);
        }

        /// <summary>Multiplica dois vetores componente a componente (Hadamard).</summary>
        public static Vector3 operator *(Vector3 a, Vector3 b)
        {
            return new Vector3(a.X * b.X, a.Y * b.Y, a.Z * b.Z);
        }

        /// <summary>Divide o vetor por um escalar.</summary>
        public static Vector3 operator /(Vector3 a, float d)
        {
            if (Math.Abs(d) < 1e-30f)
                return zero;
            return new Vector3(a.X / d, a.Y / d, a.Z / d);
        }

        /// <summary>Compara se dois vetores são aproximadamente iguais.</summary>
        public static bool operator ==(Vector3 a, Vector3 b)
        {
            return (a - b).sqrMagnitude < 1e-8f;
        }

        /// <summary>Compara se dois vetores são diferentes.</summary>
        public static bool operator !=(Vector3 a, Vector3 b)
        {
            return !(a == b);
        }

        /// <inheritdoc/>
        public readonly bool Equals(Vector3 other)
        {
            return this == other;
        }

        /// <inheritdoc/>
        public override readonly bool Equals(object? obj)
        {
            return obj is Vector3 other && Equals(other);
        }

        /// <inheritdoc/>
        public override readonly int GetHashCode()
        {
            return HashCode.Combine(X, Y, Z);
        }

        /// <inheritdoc/>
        public override readonly string ToString()
        {
            return string.Format(CultureInfo.InvariantCulture, "({0}, {1}, {2})", X, Y, Z);
        }

        /// <summary>
        /// Retorna a representação de string formatada do vetor.
        /// </summary>
        /// <param name="format">Formato numérico para cada componente.</param>
        public readonly string ToString(string format)
        {
            return string.Format(CultureInfo.InvariantCulture,
                "({0}, {1}, {2})",
                X.ToString(format, CultureInfo.InvariantCulture),
                Y.ToString(format, CultureInfo.InvariantCulture),
                Z.ToString(format, CultureInfo.InvariantCulture));
        }

        /// <summary>Conversão implícita de tuple (x, y, z) para Vector3.</summary>
        public static implicit operator Vector3((float x, float y, float z) tuple)
        {
            return new Vector3(tuple.x, tuple.y, tuple.z);
        }

        /// <summary>Conversão explícita de Vector3 para tuple.</summary>
        public static explicit operator (float, float, float)(Vector3 v)
        {
            return (v.X, v.Y, v.Z);
        }
    }
}
