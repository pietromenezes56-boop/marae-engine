using System;
using System.Runtime.InteropServices;
using MaraeEngine.Internal;

namespace MaraeEngine
{
    /// <summary>
    /// Componente que anexa um script C# (herdado de <see cref="ScriptBehaviour"/>) a uma entidade.
    /// Carrega o tipo gerenciado e gerencia o ciclo de vida do script.
    /// </summary>
    public class ScriptComponent
    {
        private readonly ulong _handle;
        internal ulong Handle => _handle;

        /// <summary>Se o script está habilitado (roda OnUpdate e dispara eventos).</summary>
        public bool enabled
        {
            get => NativeDelegates.ComponentScriptGetEnabled?.Invoke(_handle) ?? true;
            set => NativeDelegates.ComponentScriptSetEnabled?.Invoke(_handle, value);
        }

        /// <summary>Nome do assembly-qualified type do script (ex: "Namespace.Tipo, Assembly").</summary>
        public string typeName
        {
            get
            {
                IntPtr ptr = NativeDelegates.ComponentScriptGetTypeName?.Invoke(_handle) ?? IntPtr.Zero;
                if (ptr == IntPtr.Zero) return string.Empty;
                return Marshal.PtrToStringUni(ptr) ?? string.Empty;
            }
        }

        /// <summary>Campos públicos serializados como JSON (para hot-reload e persistência).</summary>
        public string serializedFieldsJson
        {
            get
            {
                IntPtr ptr = NativeDelegates.ComponentScriptGetFieldsJson?.Invoke(_handle) ?? IntPtr.Zero;
                if (ptr == IntPtr.Zero) return "{}";
                return Marshal.PtrToStringUni(ptr) ?? "{}";
            }
            set => NativeDelegates.ComponentScriptSetFieldsJson?.Invoke(_handle, value ?? "{}");
        }

        internal ScriptComponent(ulong handle) { _handle = handle; }
    }
}
