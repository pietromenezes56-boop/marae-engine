using System;
using System.Globalization;

namespace MaraeEngine
{
    /// <summary>
    /// Estrutura de quatérnio para representar rotações 3D livres de gimbal-lock.
    /// Componentes x, y, z, w (parte imaginária e parte real).
    /// </summary>
    [Serializable]
    public struct Quaternion : IEquatable<Quaternion>
    {
        /// <summary>Componente X (imaginário i).</summary>
        public float x;

        /// <summary>Componente Y (imaginário j).</summary>
        public float y;

        /// <summary>Componente Z (imaginário k).</summary>
        public float z;

        /// <summary>Componente W (real, cosseno de meio ângulo).</summary>
        public float w;

        /// <summary>Quatérnio identidade (sem rotação).</summary>
        public static readonly Quaternion identity = new(0f, 0f, 0f, 1f);

        /// <summary>Tolerância para comparações de igualdade.</summary>
        private const float K_EPSILON = 1e-6f;

        /// <summary>
        /// Inicializa um novo quatérnio com os componentes especificados.
        /// </summary>
        /// <param name="x">X.</param>
        /// <param name="y">Y.</param>
        /// <param name="z">Z.</param>
        /// <param name="w">W.</param>
        public Quaternion(float x, float y, float z, float w)
        {
            this.x = x;
            this.y = y;
            this.z = z;
            this.w = w;
        }

        /// <summary>Número de Euler (ângulos de rotação em radianos XYZ) correspondente ao quatérnio.</summary>
        public Vector3 eulerAngles => ToEulerAngles(this);

        /// <summary>Retorna o quatérnio normalizado.</summary>
        public Quaternion normalized
        {
            get
            {
                float mag = (float)Math.Sqrt(x * x + y * y + z * z + w * w);
                if (mag < K_EPSILON)
                    return identity;
                return new Quaternion(x / mag, y / mag, z / mag, w / mag);
            }
        }

        /// <summary>Normaliza o quatérnio in-place.</summary>
        public void Normalize()
        {
            float mag = (float)Math.Sqrt(x * x + y * y + z * z + w * w);
            if (mag < K_EPSILON)
            {
                this = identity;
                return;
            }
            x /= mag;
            y /= mag;
            z /= mag;
            w /= mag;
        }

        /// <summary>Normaliza um quatérnio.</summary>
        public static Quaternion Normalize(Quaternion q)
        {
            return q.normalized;
        }

        /// <summary>
        /// Cria um quatérnio a partir de ângulos de Euler (em radianos, ordem Z-X-Y / roll-pitch-yaw).
        /// </summary>
        /// <param name="x">Rotação no eixo X (pitch, radianos).</param>
        /// <param name="y">Rotação no eixo Y (yaw, radianos).</param>
        /// <param name="z">Rotação no eixo Z (roll, radianos).</param>
        public static Quaternion Euler(float x, float y, float z)
        {
            float cx = (float)Math.Cos(x * 0.5f);
            float sx = (float)Math.Sin(x * 0.5f);
            float cy = (float)Math.Cos(y * 0.5f);
            float sy = (float)Math.Sin(y * 0.5f);
            float cz = (float)Math.Cos(z * 0.5f);
            float sz = (float)Math.Sin(z * 0.5f);

            return new Quaternion(
                sx * cy * cz - cx * sy * sz,
                cx * sy * cz + sx * cy * sz,
                cx * cy * sz - sx * sy * cz,
                cx * cy * cz + sx * sy * sz);
        }

        /// <summary>Cria quatérnio a partir de vetor ângulo-euler (radianos).</summary>
        public static Quaternion Euler(Vector3 euler)
        {
            return Euler(euler.X, euler.Y, euler.Z);
        }

        /// <summary>
        /// Cria um quatérnio a partir de um eixo unitário e ângulo em radianos.
        /// </summary>
        /// <param name="angle">Ângulo em radianos.</param>
        /// <param name="axis">Eixo unitário de rotação.</param>
        public static Quaternion AngleAxis(float angle, Vector3 axis)
        {
            axis.Normalize();
            float half = angle * 0.5f;
            float s = (float)Math.Sin(half);
            return new Quaternion(axis.X * s, axis.Y * s, axis.Z * s, (float)Math.Cos(half));
        }

        /// <summary>
        /// Cria um quatérnio que olha na direção do vetor forward, com up sugerido.
        /// </summary>
        /// <param name="forward">Direção para frente (não precisa ser normalizado).</param>
        /// <param name="up">Direção para cima do mundo.</param>
        public static Quaternion LookRotation(Vector3 forward, Vector3? up = null)
        {
            Vector3 upVec = up ?? Vector3.up;
            Vector3 f = forward.normalized;
            Vector3 s = Vector3.Cross(upVec, f).normalized;
            Vector3 u = Vector3.Cross(f, s);

            float m00 = s.X, m01 = s.Y, m02 = s.Z;
            float m10 = u.X, m11 = u.Y, m12 = u.Z;
            float m20 = f.X, m21 = f.Y, m22 = f.Z;

            float trace = m00 + m11 + m22;
            if (trace > 0f)
            {
                float s2 = (float)Math.Sqrt(trace + 1f) * 2f;
                return new Quaternion((m12 - m21) / s2, (m20 - m02) / s2, (m01 - m10) / s2, 0.25f * s2);
            }
            else if (m00 > m11 && m00 > m22)
            {
                float s2 = (float)Math.Sqrt(1f + m00 - m11 - m22) * 2f;
                return new Quaternion(0.25f * s2, (m10 + m01) / s2, (m20 + m02) / s2, (m12 - m21) / s2);
            }
            else if (m11 > m22)
            {
                float s2 = (float)Math.Sqrt(1f + m11 - m00 - m22) * 2f;
                return new Quaternion((m10 + m01) / s2, 0.25f * s2, (m21 + m12) / s2, (m20 - m02) / s2);
            }
            else
            {
                float s2 = (float)Math.Sqrt(1f + m22 - m00 - m11) * 2f;
                return new Quaternion((m20 + m02) / s2, (m21 + m12) / s2, 0.25f * s2, (m01 - m10) / s2);
            }
        }

        /// <summary>
        /// Extrai os ângulos de Euler (em radianos) de um quatérnio normalizado.
        /// Ordem: X (pitch), Y (yaw), Z (roll).
        /// </summary>
        /// <param name="q">Quatérnio (preferencialmente normalizado).</param>
        /// <returns>Vetor com ângulos em radianos (X=pitch, Y=yaw, Z=roll).</returns>
        public static Vector3 ToEulerAngles(Quaternion q)
        {
            q.Normalize();

            float sinPitch = -2f * (q.y * q.z - q.w * q.x);
            float pitch, yaw, roll;

            if (Math.Abs(sinPitch) >= 1f - K_EPSILON)
            {
                pitch = (float)Math.CopySign(Math.PI * 0.5, sinPitch);
                yaw = (float)Math.Atan2(2f * (q.x * q.z - q.w * q.y), 1f - 2f * (q.y * q.y + q.z * q.z));
                roll = 0f;
            }
            else
            {
                pitch = (float)Math.Asin(sinPitch);
                yaw = (float)Math.Atan2(2f * (q.x * q.z + q.w * q.y), 1f - 2f * (q.x * q.x + q.y * q.y));
                roll = (float)Math.Atan2(2f * (q.x * q.y + q.w * q.z), 1f - 2f * (q.x * q.x + q.z * q.z));
            }

            return new Vector3(pitch, yaw, roll);
        }

        /// <summary>
        /// Interpolação esférica entre dois quaternions (preserva velocidade angular, caminho mais curto).
        /// </summary>
        /// <param name="a">Rotação inicial.</param>
        /// <param name="b">Rotação final.</param>
        /// <param name="t">Parâmetro [0, 1].</param>
        public static Quaternion Slerp(Quaternion a, Quaternion b, float t)
        {
            t = Math.Clamp(t, 0f, 1f);
            a.Normalize();
            b.Normalize();

            float cosHalfTheta = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
            if (cosHalfTheta < 0)
            {
                b = new Quaternion(-b.x, -b.y, -b.z, -b.w);
                cosHalfTheta = -cosHalfTheta;
            }

            if (cosHalfTheta > 1f - K_EPSILON)
            {
                return new Quaternion(
                    a.x + (b.x - a.x) * t,
                    a.y + (b.y - a.y) * t,
                    a.z + (b.z - a.z) * t,
                    a.w + (b.w - a.w) * t).normalized;
            }

            float halfTheta = (float)Math.Acos(cosHalfTheta);
            float sinHalfTheta = (float)Math.Sqrt(1f - cosHalfTheta * cosHalfTheta);
            if (Math.Abs(sinHalfTheta) < K_EPSILON)
            {
                return new Quaternion(
                    (a.x + b.x) * 0.5f,
                    (a.y + b.y) * 0.5f,
                    (a.z + b.z) * 0.5f,
                    (a.w + b.w) * 0.5f);
            }

            float ratioA = (float)Math.Sin((1f - t) * halfTheta) / sinHalfTheta;
            float ratioB = (float)Math.Sin(t * halfTheta) / sinHalfTheta;

            return new Quaternion(
                a.x * ratioA + b.x * ratioB,
                a.y * ratioA + b.y * ratioB,
                a.z * ratioA + b.z * ratioB,
                a.w * ratioA + b.w * ratioB);
        }

        /// <summary>
        /// Combina duas rotações (aplica q1 depois q2, matematicamente q2 * q1).
        /// </summary>
        public static Quaternion operator *(Quaternion a, Quaternion b)
        {
            return new Quaternion(
                a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
                a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
        }

        /// <summary>
        /// Rotaciona um vetor por um quatérnio.
        /// </summary>
        public static Vector3 operator *(Quaternion rotation, Vector3 point)
        {
            float rx = rotation.x, ry = rotation.y, rz = rotation.z, rw = rotation.w;
            float vx = point.X, vy = point.Y, vz = point.Z;

            float cx = ry * vz - rz * vy;
            float cy = rz * vx - rx * vz;
            float cz = rx * vy - ry * vx;

            float tx = 2f * cx;
            float ty = 2f * cy;
            float tz = 2f * cz;

            return new Vector3(
                vx + rw * tx + ry * tz - rz * ty,
                vy + rw * ty + rz * tx - rx * tz,
                vz + rw * tz + rx * ty - ry * tx);
        }

        /// <summary>Retorna o inverso da rotação (conjugado, para quatérnios unitários).</summary>
        public static Quaternion Inverse(Quaternion q)
        {
            return new Quaternion(-q.x, -q.y, -q.z, q.w);
        }

        /// <summary>Compara dois quaternions (considera o duplo-revestimento: q == -q).</summary>
        public static bool operator ==(Quaternion a, Quaternion b)
        {
            float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
            return Math.Abs(Math.Abs(dot) - 1f) < 1e-5f;
        }

        /// <summary>Compara dois quaternions por diferença.</summary>
        public static bool operator !=(Quaternion a, Quaternion b)
        {
            return !(a == b);
        }

        /// <inheritdoc/>
        public readonly bool Equals(Quaternion other)
        {
            return this == other;
        }

        /// <inheritdoc/>
        public override readonly bool Equals(object? obj)
        {
            return obj is Quaternion other && Equals(other);
        }

        /// <inheritdoc/>
        public override readonly int GetHashCode()
        {
            return HashCode.Combine(x, y, z, w);
        }

        /// <inheritdoc/>
        public override readonly string ToString()
        {
            return string.Format(CultureInfo.InvariantCulture, "Quat({0:F3}, {1:F3}, {2:F3}, {3:F3})", x, y, z, w);
        }
    }
}
