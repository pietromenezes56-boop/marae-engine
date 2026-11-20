namespace MaraeEngine.Collision.Stubs
{
    /// <summary>
    /// Representa um collider (forma de colisão) anexado a uma entidade. Usado nos callbacks de
    /// trigger (<c>OnTriggerEnter/Stay/Exit</c>).
    /// </summary>
    public class Collider
    {
        /// <summary>Entidade dona deste collider.</summary>
        public Entity? attachedEntity { get; }

        /// <summary>Cria um novo collider stub.</summary>
        public Collider(Entity? attachedEntity)
        {
            this.attachedEntity = attachedEntity;
        }
    }
}
