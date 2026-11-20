using MaraeEngine.Internal;

namespace MaraeEngine
{
    /// <summary>Formas de collider suportadas pela física nativa.</summary>
    public enum ColliderShape
    {
        Box = 0, Sphere = 1, Capsule = 2, Cylinder = 3, Cone = 4, Mesh = 5, ConvexMesh = 6
    }

    /// <summary>Configuração e estado físico nativo da entidade.</summary>
    public sealed class Rigidbody
    {
        private readonly ulong _handle;

        internal Rigidbody(ulong handle) { _handle = handle; }

        public bool enabled
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Physics, PhysicsProperty.Enabled);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Physics, PhysicsProperty.Enabled, value);
        }

        public bool useGravity
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Physics, PhysicsProperty.UseGravity);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Physics, PhysicsProperty.UseGravity, value);
        }

        public bool isStatic
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Physics, PhysicsProperty.IsStatic);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Physics, PhysicsProperty.IsStatic, value);
        }

        public bool colliderEnabled
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Physics, PhysicsProperty.ColliderEnabled);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Physics, PhysicsProperty.ColliderEnabled, value);
        }

        /// <summary>Detecta sobreposições sem aplicar resposta física e dispara OnTrigger callbacks.</summary>
        public bool isTrigger
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Physics, PhysicsProperty.IsTrigger);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Physics, PhysicsProperty.IsTrigger, value);
        }

        public bool continuousCollision
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Physics, PhysicsProperty.ContinuousCollision);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Physics, PhysicsProperty.ContinuousCollision, value);
        }

        public bool freezeRotation
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Physics, PhysicsProperty.FreezeRotation);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Physics, PhysicsProperty.FreezeRotation, value);
        }

        public ColliderShape collider
        {
            get => (ColliderShape)ComponentAccess.GetInt(_handle, ComponentIds.Physics, PhysicsProperty.ColliderShape);
            set => ComponentAccess.SetInt(_handle, ComponentIds.Physics, PhysicsProperty.ColliderShape, (int)value);
        }

        public float mass
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Physics, PhysicsProperty.Mass);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Physics, PhysicsProperty.Mass, Math.Max(0.0001f, value));
        }

        public float restitution
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Physics, PhysicsProperty.Restitution);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Physics, PhysicsProperty.Restitution, Math.Clamp(value, 0f, 1f));
        }

        public float friction
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Physics, PhysicsProperty.Friction);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Physics, PhysicsProperty.Friction, Math.Clamp(value, 0f, 1f));
        }

        public Vector3 velocity
        {
            get => ComponentAccess.GetVector3(_handle, ComponentIds.Physics, PhysicsProperty.Velocity);
            set => ComponentAccess.SetVector3(_handle, ComponentIds.Physics, PhysicsProperty.Velocity, value);
        }

        public Vector3 angularVelocity
        {
            get => ComponentAccess.GetVector3(_handle, ComponentIds.Physics, PhysicsProperty.AngularVelocity);
            set => ComponentAccess.SetVector3(_handle, ComponentIds.Physics, PhysicsProperty.AngularVelocity, value);
        }

        /// <summary>Aplica um impulso instantâneo; altera a velocidade conforme a massa.</summary>
        public void AddImpulse(Vector3 impulse)
        {
            NativeDelegates.EntityApplyImpulse?.Invoke(_handle, impulse.X, impulse.Y, impulse.Z);
        }
    }
}
