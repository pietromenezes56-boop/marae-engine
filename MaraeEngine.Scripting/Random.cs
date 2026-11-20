using System;

namespace MaraeEngine
{
    public static class Random
    {
        private static readonly System.Random Generator = new System.Random();
        public static float value => (float)Generator.NextDouble();
        public static int Range(int minInclusive, int maxExclusive) => Generator.Next(minInclusive, maxExclusive);
        public static float Range(float minInclusive, float maxInclusive) => minInclusive + (float)Generator.NextDouble() * (maxInclusive - minInclusive);
        public static Vector3 InsideUnitSphere()
        {
            while (true)
            {
                Vector3 v = new Vector3(Range(-1.0f, 1.0f), Range(-1.0f, 1.0f), Range(-1.0f, 1.0f));
                if (v.Length() <= 1.0f) return v;
            }
        }
        public static Vector3 OnUnitSphere()
        {
            Vector3 v = InsideUnitSphere();
            return v.Length() > 0.00001f ? v.Normalized() : Vector3.forward;
        }
    }
}
