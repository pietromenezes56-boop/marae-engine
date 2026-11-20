using MaraeEngine;

namespace MaraeExamples
{
    /// <summary>Exemplo de áudio por ID e atualização de UI 2D.</summary>
    public sealed class AudioAndUI : ScriptBehaviour
    {
        public string AudioId = "audio_23_3";

        public override void OnUpdate(float dt)
        {
            if (gameObject?.ui is UIElement ui)
            {
                ui.text = $"Tempo: {Time.time:0.00}";
                ui.fontSize = 18f;
                ui.transparency = 0f;
            }
            if (Input.GetMouseButtonDown(0))
                Audio.Play(AudioId);
        }
    }
}
