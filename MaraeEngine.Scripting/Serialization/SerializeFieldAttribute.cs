using System;

namespace MaraeEngine.Serialization
{
    /// <summary>
    /// Marca um campo como serializável pelo engine, mesmo que seja privado. Usado para exibir campos
    /// no Inspector e preservar seus valores durante hot-reload. Campos públicos já são serializados
    /// por padrão; esse atributo só é necessário para campos privados ou protected.
    /// </summary>
    [AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = true)]
    public sealed class SerializeFieldAttribute : Attribute
    {
        /// <summary>Inicializa uma nova instância do atributo.</summary>
        public SerializeFieldAttribute() { }
    }
}
