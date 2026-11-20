using MaraeEngine.Internal;

namespace MaraeEngine
{
    /// <summary>
    /// Componente de material padrão (PBR) associado à malha de uma entidade. Permite ajustar
    /// albedo, metalicidade e suavidade (smoothness) em tempo de execução.
    /// </summary>
    public class MaterialComponent
    {
        private readonly ulong _handle;
        internal ulong Handle => _handle;

        /// <summary>Cor albedo (cor difusa) base do material.</summary>
        public Color albedo
        {
            get
            {
                float r = 1, g = 1, b = 1, a = 1;
                NativeDelegates.ComponentMaterialGetAlbedo?.Invoke(_handle, out r, out g, out b, out a);
                return new Color(r, g, b, a);
            }
            set => NativeDelegates.ComponentMaterialSetAlbedo?.Invoke(_handle, value.R, value.G, value.B, value.A);
        }

        /// <summary>Metalicidade [0, 1]. 0 = dielétrico (borracha/plástico), 1 = metal.</summary>
        public float metallic
        {
            get => NativeDelegates.ComponentMaterialGetMetallic?.Invoke(_handle) ?? 0f;
            set => NativeDelegates.ComponentMaterialSetMetallic?.Invoke(_handle, Math.Clamp(value, 0f, 1f));
        }

        /// <summary>Suavidade [0, 1]. 0 = totalmente rugoso, 1 = espelho perfeito.</summary>
        public float smoothness
        {
            get => NativeDelegates.ComponentMaterialGetSmoothness?.Invoke(_handle) ?? 0.5f;
            set => NativeDelegates.ComponentMaterialSetSmoothness?.Invoke(_handle, Math.Clamp(value, 0f, 1f));
        }

        internal MaterialComponent(ulong handle) { _handle = handle; }
    }
}
