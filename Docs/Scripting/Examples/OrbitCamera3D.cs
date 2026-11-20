using MaraeEngine;

namespace MaraeExamples
{
    /// <summary>Câmera orbital 3D que acompanha uma entidade chamada Target.</summary>
    public sealed class OrbitCamera3D : ScriptBehaviour
    {
        public string TargetName = "Target";
        public float Distance = 7f;
        public float Height = 3f;
        public float DegreesPerSecond = 35f;
        private float _angleRadians;

        public override void OnUpdate(float dt)
        {
            if (transform == null) return;
            Entity? target = Entity.Find(TargetName);
            if (target?.transform == null) return;
            _angleRadians += DegreesPerSecond * (System.MathF.PI / 180f) * dt;
            Vector3 center = target.transform.position;
            Vector3 offset = new(
                System.MathF.Sin(_angleRadians) * Distance,
                Height,
                System.MathF.Cos(_angleRadians) * Distance);
            transform.position = center + offset;
            transform.LookAt(center);
        }
    }
}
