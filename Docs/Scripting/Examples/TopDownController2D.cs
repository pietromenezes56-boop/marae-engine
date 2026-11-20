using MaraeEngine;

namespace MaraeExamples
{
    /// <summary>Movimento 2D em oito direções, independente da taxa de quadros.</summary>
    public sealed class TopDownController2D : ScriptBehaviour
    {
        public float Speed = 5f;

        public override void OnUpdate(float dt)
        {
            if (transform == null) return;
            var direction = new Vector3(
                Input.GetAxisRaw("Horizontal"), Input.GetAxisRaw("Vertical"), 0f);
            if (direction.sqrMagnitude > 1f) direction.Normalize();
            transform.Translate(direction * (Speed * dt), Space.World);
        }
    }
}
