using MaraeEngine;

namespace MaraeExamples
{
    /// <summary>Exemplo mínimo de movimento 3D com input e física.</summary>
    public sealed class GameplayController : ScriptBehaviour
    {
        public float Speed = 4f;

        public override void OnFixedUpdate(float fixedDt)
        {
            if (rigidbody == null || transform == null) return;
            float x = Input.GetAxisRaw("Horizontal");
            float z = Input.GetAxisRaw("Vertical");
            transform.Translate(new Vector3(x, 0f, z) * (Speed * fixedDt), Space.World);
        }

        public override void OnUpdate(float dt)
        {
            if (Input.GetActionDown("Jump"))
                rigidbody?.AddImpulse(new Vector3(0f, 5f, 0f));
        }
    }
}
