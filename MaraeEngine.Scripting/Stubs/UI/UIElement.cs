using System.Numerics;
using MaraeEngine.Attributes;

namespace MaraeEngine.UI.Stubs
{
    /// <summary>
    /// Classe base stub para qualquer elemento scriptável de UI. Armazena posição, tamanho,
    /// visibilidade e cor de fundo. Renderização real pelo C++ ainda não está implementada.
    /// </summary>
    [EditorStub("Estado em memória apenas; o host de renderização de UI pode ser plugado via ScriptingRuntime.")]
    public class UIElement
    {
        /// <summary>Posição do canto superior-esquerdo, em pixels relativos ao painel pai.</summary>
        public Vector2 position { get; set; }

        /// <summary>Largura e altura do elemento, em pixels.</summary>
        public Vector2 size { get; set; }

        /// <summary>Se o elemento está visível.</summary>
        public bool visible { get; set; } = true;

        /// <summary>Cor de fundo do elemento.</summary>
        public Color backgroundColor { get; set; } = Color.White;

        /// <summary>Texto de tooltip exibido ao passar o mouse (opcional).</summary>
        public string? tooltip { get; set; }

        /// <summary>Painel pai ao qual este elemento pertence, se houver.</summary>
        public UIPanel? parent { get; internal set; }

        /// <summary>Cria um novo elemento UI com tamanho 100x100 na origem.</summary>
        public UIElement()
        {
            position = Vector2.Zero;
            size = new Vector2(100, 100);
        }
    }
}
