namespace MaraeEngine.Collision.Stubs
{
    /// <summary>
    /// Dados de uma colisão física enviados aos callbacks de script.
    /// </summary>
    public struct Collision
    {
        /// <summary>Entidade com a qual colidimos.</summary>
        public Entity? other;

        /// <summary>Ponto de contato no mundo (posição da colisão).</summary>
        public Vector3 contactPoint;

        /// <summary>Normal da superfície de colisão (direção da resposta).</summary>
        public Vector3 normal;

        /// <summary>Magnitude do impulso aplicado (força da colisão).</summary>
        public float impulse;

        /// <summary>Cria uma nova estrutura de colisão.</summary>
        public Collision(Entity? other, Vector3 contactPoint, Vector3 normal, float impulse)
        {
            this.other = other;
            this.contactPoint = contactPoint;
            this.normal = normal;
            this.impulse = impulse;
        }
    }
}
