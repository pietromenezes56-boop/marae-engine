using System;
using System.Globalization;
using System.Runtime.InteropServices;

namespace MaraeEngine
{
    /// <summary>Consulta caminhos no NavMesh criado ao iniciar o Play.</summary>
    public static class Navigation
    {
        /// <summary>
        /// Encontra um caminho sobre colisores estáticos. Retorna vazio se não houver
        /// NavMesh válido ou se os pontos não forem conectáveis.
        /// </summary>
        public static Vector3[] FindPath(Vector3 start, Vector3 destination)
        {
            var callback = Internal.NativeDelegates.NavigationFindPath;
            if (callback == null) return Array.Empty<Vector3>();
            IntPtr result = callback(start.X, start.Y, start.Z,
                destination.X, destination.Y, destination.Z);
            string? text = Marshal.PtrToStringUni(result);
            if (string.IsNullOrWhiteSpace(text)) return Array.Empty<Vector3>();

            string[] points = text.Split(';', StringSplitOptions.RemoveEmptyEntries);
            var path = new Vector3[points.Length];
            for (int i = 0; i < points.Length; ++i)
            {
                string[] values = points[i].Split(',');
                if (values.Length != 3 ||
                    !float.TryParse(values[0], NumberStyles.Float, CultureInfo.InvariantCulture, out float x) ||
                    !float.TryParse(values[1], NumberStyles.Float, CultureInfo.InvariantCulture, out float y) ||
                    !float.TryParse(values[2], NumberStyles.Float, CultureInfo.InvariantCulture, out float z))
                    return Array.Empty<Vector3>();
                path[i] = new Vector3(x, y, z);
            }
            return path;
        }
    }
}
