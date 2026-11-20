using System;
using System.Collections;
using System.Collections.Generic;
using System.Numerics;
using MaraeEngine.Attributes;

namespace MaraeEngine.UI.Stubs
{
    /// <summary>
    /// Container hierárquico de UI: pode conter outros <see cref="UIElement"/> incluindo
    /// outros paineis, botões e labels.
    /// </summary>
    [EditorStub("Hierarquia em memória; renderização do painel e filhos será implementada via IUIRenderer no C++.")]
    public class UIPanel : UIElement, IEnumerable<UIElement>
    {
        private readonly List<UIElement> _children = new();

        /// <summary>Elementos filhos deste painel.</summary>
        public IReadOnlyList<UIElement> Children => _children;

        /// <summary>Número de filhos imediatos.</summary>
        public int childCount => _children.Count;

        /// <summary>Cria um novo painel vazio.</summary>
        public UIPanel()
        {
            backgroundColor = new Color(0.9f, 0.9f, 0.9f, 0.9f);
            size = new Vector2(400, 300);
        }

        /// <summary>Adiciona um elemento como filho deste painel.</summary>
        public void Add(UIElement element)
        {
            if (element == null) throw new ArgumentNullException(nameof(element));
            if (element.parent != null)
                throw new InvalidOperationException("Elemento já pertence a outro UIPanel.");
            element.parent = this;
            _children.Add(element);
        }

        /// <summary>Remove um elemento filho, se presente.</summary>
        public bool Remove(UIElement element)
        {
            bool ok = _children.Remove(element);
            if (ok && element != null) element.parent = null;
            return ok;
        }

        /// <summary>Retorna o filho no índice especificado.</summary>
        /// <exception cref="ArgumentOutOfRangeException">Se o índice for inválido.</exception>
        public UIElement GetChild(int index)
        {
            if (index < 0 || index >= _children.Count)
                throw new ArgumentOutOfRangeException(nameof(index));
            return _children[index];
        }

        /// <summary>Remove todos os filhos do painel.</summary>
        public void Clear()
        {
            foreach (UIElement child in _children) child.parent = null;
            _children.Clear();
        }

        /// <summary>Cria e adiciona um novo botão como filho (método de conveniência).</summary>
        public UIButton CreateButton(string text, Vector2 position, Action? clickHandler = null)
        {
            UIButton btn = new()
            {
                text = text,
                position = position
            };
            if (clickHandler != null) btn.onClick += clickHandler;
            Add(btn);
            return btn;
        }

        /// <summary>Cria e adiciona um novo label como filho.</summary>
        public UILabel CreateLabel(string text, Vector2 position, int fontSize = 14)
        {
            UILabel label = new(text, position, fontSize);
            Add(label);
            return label;
        }

        /// <inheritdoc/>
        public IEnumerator<UIElement> GetEnumerator() => _children.GetEnumerator();
        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    }
}
