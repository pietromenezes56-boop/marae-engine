namespace MaraeEngine
{
    /// <summary>Configurações ambientais e de névoa do componente Global Light da cena.</summary>
    public sealed class GlobalLightComponent
    {
        private readonly ulong _handle;
        internal GlobalLightComponent(ulong handle) => _handle = handle;

        public bool enabled
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.GlobalLight, GlobalLightProperty.Enabled);
            set => ComponentAccess.SetBool(_handle, ComponentIds.GlobalLight, GlobalLightProperty.Enabled, value);
        }
        public float ambientIntensity
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.AmbientIntensity);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.AmbientIntensity, System.Math.Max(0f, value));
        }
        public Color ambientColor
        {
            get => ComponentAccess.GetColor(_handle, ComponentIds.GlobalLight, GlobalLightProperty.AmbientColor);
            set => ComponentAccess.SetColor(_handle, ComponentIds.GlobalLight, GlobalLightProperty.AmbientColor, value);
        }
        public float exposure
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.Exposure);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.Exposure, value);
        }
        public float shadowStrength
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.ShadowStrength);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.ShadowStrength, System.Math.Clamp(value, 0f, 1f));
        }
        public bool fogEnabled
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.GlobalLight, GlobalLightProperty.FogEnabled);
            set => ComponentAccess.SetBool(_handle, ComponentIds.GlobalLight, GlobalLightProperty.FogEnabled, value);
        }
        public Color fogColor
        {
            get => ComponentAccess.GetColor(_handle, ComponentIds.GlobalLight, GlobalLightProperty.FogColor);
            set => ComponentAccess.SetColor(_handle, ComponentIds.GlobalLight, GlobalLightProperty.FogColor, value);
        }
        public float fogDensity
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.FogDensity);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.FogDensity, System.Math.Max(0f, value));
        }
        public float fogStart
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.FogStart);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.FogStart, System.Math.Max(0f, value));
        }
        public float fogEnd
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.FogEnd);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.FogEnd, System.Math.Max(fogStart + 0.01f, value));
        }
        public bool cloudsEnabled
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.GlobalLight, GlobalLightProperty.CloudsEnabled);
            set => ComponentAccess.SetBool(_handle, ComponentIds.GlobalLight, GlobalLightProperty.CloudsEnabled, value);
        }
        public bool cloudMovementEnabled
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.GlobalLight, GlobalLightProperty.CloudMovementEnabled);
            set => ComponentAccess.SetBool(_handle, ComponentIds.GlobalLight, GlobalLightProperty.CloudMovementEnabled, value);
        }
        public float cloudAmount
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.CloudAmount);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.CloudAmount, System.Math.Clamp(value, 0f, 1f));
        }
        public float cloudSpeed
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.CloudSpeed);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.GlobalLight, GlobalLightProperty.CloudSpeed, value);
        }
    }
}
