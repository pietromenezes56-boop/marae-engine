using System.Numerics;
using MaraeEngine;

namespace MaraeScripts
{
    public sealed class ScriptApiUI : ScriptBehaviour
    {
        public override void OnStart()
        {
            UIElement element = gameObject!.GetComponent<UIElement>()!;
            element.type = UIElementType.TextButton;
            element.enabled = true;
            element.rect = new Vector4(24f, 48f, 320f, 72f);
            element.anchorX = 0.5f;
            element.anchorY = 0.25f;
            element.rotation = 12f;
            element.backgroundColor = new Color(0.1f, 0.2f, 0.8f, 0.9f);
            element.textColor = Color.White;
            element.borderColor = Color.Cyan;
            element.transparency = 0.2f;
            element.cornerRadius = 8f;
            element.borderThickness = 2f;
            element.fontSize = 28f;
            element.zIndex = 12;
            element.textAlignment = UITextAlignment.Center;
            element.clipsContent = true;
            element.text = "C# UI conectado";
            element.font = "Default";
            element.imagePath = "ui/test-icon.png";
        }
    }
}
