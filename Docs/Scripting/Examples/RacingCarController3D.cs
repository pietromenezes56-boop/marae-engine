using MaraeEngine;

namespace MaraeExamples
{
    /// <summary>Controlador arcade de carro 3D; requer corpo físico dinâmico.</summary>
    public sealed class RacingCarController3D : ScriptBehaviour
    {
        public float Acceleration = 14f;
        public float MaxSpeed = 18f;
        public float SteeringRadiansPerSecond = 1.8f;

        public override void OnFixedUpdate(float fixedDt)
        {
            if (transform == null || rigidbody == null) return;
            float throttle = Input.GetAxisRaw("Vertical");
            float steering = Input.GetAxisRaw("Horizontal");
            Vector3 velocity = rigidbody.velocity;
            Vector3 forward = transform.forward;
            velocity += forward * (throttle * Acceleration * fixedDt);
            float forwardSpeed = Vector3.Dot(velocity, forward);
            if (forwardSpeed > MaxSpeed)
                velocity -= forward * (forwardSpeed - MaxSpeed);
            if (forwardSpeed < -MaxSpeed)
                velocity -= forward * (forwardSpeed + MaxSpeed);
            rigidbody.velocity = velocity;
            if (System.Math.Abs(forwardSpeed) > 0.25f)
                transform.Rotate(0f, steering * SteeringRadiansPerSecond * fixedDt, 0f, Space.World);
        }
    }
}
