using MaraeEngine;

namespace MaraeExamples
{
    /// <summary>Controle simples de uma raquete vertical para uma cena 2D.</summary>
    public sealed class PongPaddle2D : ScriptBehaviour
    {
        public float Speed = 8f;
        public float MinY = -4f;
        public float MaxY = 4f;

        public override void OnUpdate(float dt)
        {
            if (transform == null) return;
            float input = Input.GetAxisRaw("Vertical");
            Vector3 position = transform.position;
            position.Y = System.Math.Clamp(position.Y + input * Speed * dt, MinY, MaxY);
            transform.position = position;
        }
    }
}
