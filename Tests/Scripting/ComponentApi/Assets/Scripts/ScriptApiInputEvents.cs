using MaraeEngine;
using MaraeEngine.Collision.Stubs;

namespace MaraeScripts
{
    public sealed class ScriptApiInputEvents : ScriptBehaviour
    {
        public override void OnStart()
        {
            gameObject!.transform.position = Vector3.zero;
            Time.timeScale = 0.75f;
        }

        public override void OnUpdate(float dt)
        {
            Vector3 p = gameObject!.transform.position;
            if (Input.GetKeyDown(KeyCode.Space)) p.X += 1f;
            if (Input.GetMouseButtonDown(0)) p.X += 10f;
            if (Input.GetKey(KeyCode.D)) p.Y += Input.GetAxisRaw("Horizontal");
            p.Z += dt;
            gameObject.transform.position = p;
        }

        public override void OnFixedUpdate(float fixedDt)
        {
            Vector3 p = gameObject!.transform.position;
            p.Z += fixedDt;
            gameObject.transform.position = p;
        }

        public override void OnCollisionEnter(Collision collision)
        {
            gameObject!.transform.position = collision.contactPoint;
        }

        public override void OnTriggerEnter(Collider other)
        {
            gameObject!.transform.position = new Vector3(other.attachedEntity == null ? -1f : 9f, 8f, 7f);
        }
    }
}
