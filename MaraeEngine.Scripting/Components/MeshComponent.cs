using System;
using System.Runtime.InteropServices;
using MaraeEngine.Internal;

namespace MaraeEngine
{
    /// <summary>
    /// Componente que representa uma malha renderizável (Mesh) anexada a uma entidade.
    /// Controla o nome do asset, visibilidade e cor base (tint) da malha.
    /// </summary>
    public class MeshComponent
    {
        private readonly ulong _handle;
        internal ulong Handle => _handle;

        /// <summary>Nome ou caminho do asset de malha associado.</summary>
        public string meshName
        {
            get
            {
                IntPtr ptr = NativeDelegates.ComponentMeshGetName?.Invoke(_handle) ?? IntPtr.Zero;
                if (ptr == IntPtr.Zero) return string.Empty;
                return Marshal.PtrToStringUni(ptr) ?? string.Empty;
            }
            set => NativeDelegates.ComponentMeshSetName?.Invoke(_handle, value ?? string.Empty);
        }

        /// <summary>Se a malha está visível no pipeline de renderização.</summary>
        public bool visible
        {
            get => NativeDelegates.ComponentMeshGetVisible?.Invoke(_handle) ?? true;
            set => NativeDelegates.ComponentMeshSetVisible?.Invoke(_handle, value);
        }

        /// <summary>Cor (tint) aplicada à malha antes da iluminação.</summary>
        public Color color
        {
            get
            {
                float r = 1, g = 1, b = 1, a = 1;
                NativeDelegates.ComponentMeshGetColor?.Invoke(_handle, out r, out g, out b, out a);
                return new Color(r, g, b, a);
            }
            set => NativeDelegates.ComponentMeshSetColor?.Invoke(_handle, value.R, value.G, value.B, value.A);
        }

        internal MeshComponent(ulong handle) { _handle = handle; }
    }
}
