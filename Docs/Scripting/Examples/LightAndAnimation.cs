using MaraeEngine;

namespace MaraeExamples
{
    /// <summary>Exemplo de iluminação e clip identificado por ID.</summary>
    public sealed class LightAndAnimation : ScriptBehaviour
    {
        public string AnimationId = "";

        public override void OnStart()
        {
            LightComponent? light = gameObject?.GetComponent<LightComponent>();
            if (light != null)
            {
                light.enabled = true;
                light.castShadows = true;
                light.shadowSoftness = 0.35f;
            }

            if (!string.IsNullOrWhiteSpace(AnimationId))
                gameObject?.animation.Play(AnimationId);
        }
    }
}
