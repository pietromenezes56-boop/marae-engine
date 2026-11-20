using System;

namespace MaraeEngine
{
    public static class Mathf
    {
        public const float PI = MathF.PI;
        public const float Deg2Rad = MathF.PI / 180.0f;
        public const float Rad2Deg = 180.0f / MathF.PI;
        public static float Abs(float v) => MathF.Abs(v);
        public static float Sign(float v) => MathF.Sign(v);
        public static float Min(float a, float b) => MathF.Min(a, b);
        public static float Max(float a, float b) => MathF.Max(a, b);
        public static int Min(int a, int b) => Math.Min(a, b);
        public static int Max(int a, int b) => Math.Max(a, b);
        public static float Clamp(float v, float min, float max) => Math.Clamp(v, min, max);
        public static int Clamp(int v, int min, int max) => Math.Clamp(v, min, max);
        public static float Clamp01(float v) => Math.Clamp(v, 0.0f, 1.0f);
        public static float Lerp(float a, float b, float t) => a + (b - a) * Clamp01(t);
        public static float InverseLerp(float a, float b, float v) => Math.Abs(b - a) < 0.000001f ? 0.0f : Clamp01((v - a) / (b - a));
        public static float MoveTowards(float current, float target, float maxDelta) => Abs(target - current) <= maxDelta ? target : current + Sign(target - current) * maxDelta;
        public static float Repeat(float t, float length) => Clamp(t - MathF.Floor(t / length) * length, 0.0f, length);
        public static float PingPong(float t, float length) { t = Repeat(t, length * 2.0f); return length - Abs(t - length); }
        public static float SmoothStep(float from, float to, float t) { t = Clamp01(t); t = t * t * (3.0f - 2.0f * t); return Lerp(from, to, t); }
        public static float Sqrt(float v) => MathF.Sqrt(v);
        public static float Pow(float a, float b) => MathF.Pow(a, b);
        public static float Sin(float v) => MathF.Sin(v);
        public static float Cos(float v) => MathF.Cos(v);
        public static float Tan(float v) => MathF.Tan(v);
        public static float Atan2(float y, float x) => MathF.Atan2(y, x);
        public static float Floor(float v) => MathF.Floor(v);
        public static float Ceil(float v) => MathF.Ceiling(v);
        public static float Round(float v) => MathF.Round(v);
        public static bool Approximately(float a, float b) => Abs(b - a) <= 0.00001f * Max(1.0f, Max(Abs(a), Abs(b)));
    }
}
