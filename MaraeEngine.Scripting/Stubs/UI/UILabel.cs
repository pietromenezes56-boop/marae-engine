using System.Numerics;
using MaraeEngine.Attributes;

namespace MaraeEngine.UI.Stubs
{
    /// <summary>
    /// Elemento de texto não interativo (rótulo/label).
    /// </summary>
    [EditorStub("Renderização de texto não implementada no host de UI; valores em memória apenas.")]
    public class UILabel : UIElement
    {
        /// <summary>Conteúdo textual exibido.</summary>
        public string text { get; set; } = string.Empty;

        /// <summary>Tamanho da fonte em pontos.</summary>
        public int fontSize { get; set; } = 14;

        /// <summary>Cor do texto (RGBA).</summary>
        public Color textColor { get; set; } = Color.Black;

        /// <summary>Cria um novo label vazio.</summary>
        public UILabel() { }

        /// <summary>Cria um novo label com texto e posição inicial.</summary>
        public UILabel(string text, Vector2 position, int fontSize = 14)
        {
            this.text = text;
            this.position = position;
            this.fontSize = fontSize;
            size = new Vector2(System.Math.Max(1, text.Length * fontSize / 2), fontSize + 4);
        }
    }
}
