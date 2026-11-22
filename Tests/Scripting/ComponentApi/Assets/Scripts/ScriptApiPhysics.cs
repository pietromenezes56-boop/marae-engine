using MaraeEngine;

namespace MaraeScripts
{
    public sealed class ScriptApiPhysics : ScriptBehaviour
    {
        public override void OnStart()
        {
            Rigidbody body = gameObject!.GetComponent<Rigidbody>()!;
            body.enabled = true;
            body.useGravity = false;
            body.isStatic = false;
            body.colliderEnabled = true;
            body.isTrigger = true;
            body.continuousCollision = true;
            body.freezeRotation = false;
            body.collider = ColliderShape.Sphere;
            body.mass = 2f;
            body.restitution = 0.45f;
            body.friction = 0.3f;
            body.velocity = new Vector3(1f, 2f, 3f);
            body.angularVelocity = new Vector3(0.1f, 0.2f, 0.3f);
            body.AddImpulse(new Vector3(4f, 0f, 0f));
        }
    }
}
