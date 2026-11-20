namespace MaraeEngine
{
    /// <summary>Parâmetros da câmera de cena, distintos da câmera de navegação do editor.</summary>
    public sealed class CameraComponent
    {
        private readonly ulong _handle;
        internal CameraComponent(ulong handle) => _handle = handle;

        public bool enabled
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Camera, CameraProperty.Enabled);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Camera, CameraProperty.Enabled, value);
        }
        public bool previewEnabled
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Camera, CameraProperty.PreviewEnabled);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Camera, CameraProperty.PreviewEnabled, value);
        }
        public bool orthographic
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Camera, CameraProperty.Orthographic);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Camera, CameraProperty.Orthographic, value);
        }
        public float fieldOfView
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Camera, CameraProperty.FieldOfView);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Camera, CameraProperty.FieldOfView, System.Math.Clamp(value, 1f, 179f));
        }
        public float nearClip
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Camera, CameraProperty.NearClip);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Camera, CameraProperty.NearClip, System.Math.Max(0.001f, value));
        }
        public float farClip
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Camera, CameraProperty.FarClip);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Camera, CameraProperty.FarClip, System.Math.Max(nearClip + 0.01f, value));
        }
        public float orthographicSize
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Camera, CameraProperty.OrthographicSize);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Camera, CameraProperty.OrthographicSize, System.Math.Max(0.001f, value));
        }
    }
}
