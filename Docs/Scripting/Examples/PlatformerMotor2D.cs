using MaraeEngine;
using MaraeEngine.Collision.Stubs;

namespace MaraeExamples
{
    /// <summary>Movimento horizontal e salto de um personagem com Rigidbody 2D.</summary>
    public sealed class PlatformerMotor2D : ScriptBehaviour
    {
        public float Speed = 6f;
        public float JumpSpeed = 7f;
        private bool _grounded;

        public override void OnFixedUpdate(float fixedDt)
        {
            if (rigidbody == null) return;
            Vector3 velocity = rigidbody.velocity;
            velocity.X = Input.GetAxisRaw("Horizontal") * Speed;
            rigidbody.velocity = velocity;
        }

        public override void OnUpdate(float dt)
        {
            if (!_grounded || !Input.GetKeyDown(KeyCode.Space) || rigidbody == null) return;
            Vector3 velocity = rigidbody.velocity;
            velocity.Y = JumpSpeed;
            rigidbody.velocity = velocity;
            _grounded = false;
        }

        public override void OnCollisionEnter(Collision collision)
        {
            if (collision.normal.Y > 0.5f) _grounded = true;
        }

        public override void OnCollisionStay(Collision collision)
        {
            if (collision.normal.Y > 0.5f) _grounded = true;
        }

        public override void OnCollisionExit(Collision collision) => _grounded = false;
    }
}
