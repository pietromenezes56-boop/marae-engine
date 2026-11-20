using MaraeEngine;
using MaraeEngine.Collision.Stubs;

namespace MaraeExamples
{
    /// <summary>Exemplo de coleta local: itens com tag Collectible são removidos ao tocar.</summary>
    public sealed class SurvivalPickup : ScriptBehaviour
    {
        public int Collected { get; private set; }

        public override void OnTriggerEnter(Collider other)
        {
            Entity? item = other.attachedEntity;
            if (item == null || item.tag != "Collectible") return;
            Collected++;
            item.Destroy();
        }
    }
}
