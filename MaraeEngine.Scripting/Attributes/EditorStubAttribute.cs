using System;

namespace MaraeEngine.Attributes
{
    /// <summary>
    /// Marca um membro ou tipo como implementação "stub" (apenas fachada) no contexto do editor.
    /// O IntelliSense e ferramentas do editor podem exibir a mensagem informando que a
    /// funcionalidade completa será adicionada posteriormente.
    /// </summary>
    [AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct | AttributeTargets.Method
        | AttributeTargets.Property | AttributeTargets.Interface, AllowMultiple = false, Inherited = true)]
    public sealed class EditorStubAttribute : Attribute
    {
        /// <summary>Mensagem explicativa exibida no editor.</summary>
        public string Message { get; }

        /// <summary>Inicializa uma nova instância do atributo stub.</summary>
        /// <param name="message">Mensagem descritiva do status de implementação.</param>
        public EditorStubAttribute(string message)
        {
            Message = message ?? "Stub: implementação pendente";
        }
    }
}
