using MaraeEngine;

namespace MaraeScripts
{
    public sealed class ScriptApiGlobalLight : ScriptBehaviour
    {
        public override void OnStart()
        {
            GlobalLightComponent light = gameObject!.GetComponent<GlobalLightComponent>()!;
            light.enabled = true;
            light.ambientIntensity = 0.55f;
            light.ambientColor = new Color(0.25f, 0.35f, 0.5f);
            light.exposure = 0.4f;
            light.shadowStrength = 0.8f;
            light.fogEnabled = true;
            light.fogColor = new Color(0.6f, 0.7f, 0.8f);
            light.fogDensity = 0.02f;
            light.fogStart = 5f;
            light.fogEnd = 125f;
            light.cloudsEnabled = false;
            light.cloudMovementEnabled = true;
            light.cloudAmount = 0.65f;
            light.cloudSpeed = 1.25f;
        }
    }
}
