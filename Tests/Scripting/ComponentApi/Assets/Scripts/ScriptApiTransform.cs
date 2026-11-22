using MaraeEngine;

namespace MaraeScripts
{
    public sealed class ScriptApiTransform : ScriptBehaviour
    {
        public override void OnStart()
        {
            if (gameObject == null) return;
            gameObject.name = "Transform API passed";
            gameObject.tag = "script-api-test";
            gameObject.transform.position = new Vector3(1.25f, 2.5f, -3.75f);
            gameObject.transform.localScale = new Vector3(2f, 3f, 4f);
            gameObject.GetComponent<MeshComponent>()!.visible = false;
            gameObject.GetComponent<MeshComponent>()!.color = new Color(0.2f, 0.4f, 0.6f, 0.8f);
            MaterialComponent material = gameObject.GetComponent<MaterialComponent>()!;
            material.albedo = new Color(0.7f, 0.3f, 0.1f, 1f);
            material.metallic = 0.65f;
            material.smoothness = 0.85f;
        }
    }
}
