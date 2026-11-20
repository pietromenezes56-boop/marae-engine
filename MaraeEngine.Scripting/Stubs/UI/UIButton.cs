using System;
using System.Numerics;
using MaraeEngine.Attributes;

namespace MaraeEngine.UI.Stubs
{
    /// <summary>
    /// Botão clicável de UI com evento <see cref="onClick"/> disparado quando o usuário interage.
    /// </summary>
    [EditorStub("onClick pode ser assinado por scripts; input do mouse ainda é processado pelo host C++.")]
    public class UIButton : UIElement
    {
        /// <summary>Texto mostrado no centro do botão.</summary>
        public string text { get; set; } = "Button";

        /// <summary>Cor do texto do botão.</summary>
        public Color textColor { get; set; } = Color.Black;

        /// <summary>Evento disparado quando o botão é clicado (pressionado + solto).</summary>
        public event Action? onClick;

        /// <summary>Evento disparado quando o mouse entra no botão (hover start).</summary>
        public event Action? onEnter;

        /// <summary>Evento disparado quando o mouse sai do botão (hover end).</summary>
        public event Action? onExit;

        /// <summary>Cria um novo botão com tamanho default 120x32.</summary>
        public UIButton()
        {
            size = new Vector2(120, 32);
            backgroundColor = Color.LightGray;
        }

        /// <summary>
        /// Método interno usado pelo host de UI para disparar o evento onClick (chame de scripts
        /// para simular clique em testes).
        /// </summary>
        public void SimulateClick() => onClick?.Invoke();

        internal void FireOnEnter() => onEnter?.Invoke();
        internal void FireOnExit() => onExit?.Invoke();
    }
}
