using MaraeEngine;

namespace MaraeScripts
{
    public sealed class ScriptApiLightCameraAnimation : ScriptBehaviour
    {
        public override void OnStart()
        {
            LightComponent light = gameObject!.GetComponent<LightComponent>()!;
            light.type = LightType.Area;
            light.enabled = false;
            light.castShadows = false;
            light.useTemperature = true;
            light.distanceSoftening = false;
            light.indirectContribution = true;
            light.intensity = 35f;
            light.color = new Color(1f, 0.5f, 0.25f);
            light.range = 18f;
            light.spotAngle = 0.9f;
            light.sourceRadius = 0.4f;
            light.shadowSoftness = 0.7f;
            light.shadowStrength = 0.6f;
            light.contactShadowStrength = 0.3f;
            light.temperatureKelvin = 4200f;

            CameraComponent camera = gameObject.GetComponent<CameraComponent>()!;
            camera.enabled = true;
            camera.previewEnabled = false;
            camera.orthographic = true;
            camera.fieldOfView = 75f;
            camera.nearClip = 0.2f;
            camera.farClip = 800f;
            camera.orthographicSize = 14f;

            AnimationComponent animation = gameObject.GetComponent<AnimationComponent>()!;
            animation.enabled = true;
            animation.playOnStart = true;
            animation.time = 0.5f;
            animation.speed = 1.5f;
            animation.weight = 0.7f;
            if (!animation.Play(animation.clipId))
                throw new System.InvalidOperationException("Animation clip ID did not resolve.");
        }

        public override void OnUpdate(float dt)
        {
            if (Input.GetKeyDown(KeyCode.B)) gameObject!.animation.Pause();
            if (Input.GetKeyDown(KeyCode.C)) gameObject!.animation.Stop();
        }
    }
}
