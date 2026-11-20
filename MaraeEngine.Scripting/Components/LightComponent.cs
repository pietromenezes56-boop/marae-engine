using MaraeEngine.Internal;

namespace MaraeEngine
{
    /// <summary>Tipo de luz emitida por um <see cref="LightComponent"/>.</summary>
    public enum LightType
    {
        /// <summary>Luz direcional (ex: sol), sem posição, só direção.</summary>
        Directional = 0,
        /// <summary>Luz pontual omnidirecional com alcance finito.</summary>
        Point = 1,
        /// <summary>Luz em formato de cone (projetor).</summary>
        Spot = 2,
        /// <summary>Fonte de área, aproximada pelo renderer atual.</summary>
        Area = 3
    }

    /// <summary>
    /// Componente que representa uma fonte de luz na cena. Suporta luzes Direcional, Pontual e Spot.
    /// </summary>
    public class LightComponent
    {
        private readonly ulong _handle;
        internal ulong Handle => _handle;

        public bool enabled
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Light, LightProperty.Enabled);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Light, LightProperty.Enabled, value);
        }

        public bool castShadows
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Light, LightProperty.CastShadows);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Light, LightProperty.CastShadows, value);
        }

        public bool useTemperature
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Light, LightProperty.UseTemperature);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Light, LightProperty.UseTemperature, value);
        }

        public bool distanceSoftening
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Light, LightProperty.DistanceSoftening);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Light, LightProperty.DistanceSoftening, value);
        }

        public bool indirectContribution
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Light, LightProperty.IndirectContribution);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Light, LightProperty.IndirectContribution, value);
        }

        /// <summary>Tipo geométrico da fonte de luz.</summary>
        public LightType type
        {
            get => (LightType)(NativeDelegates.ComponentLightGetType?.Invoke(_handle) ?? 0);
            set => NativeDelegates.ComponentLightSetType?.Invoke(_handle, (int)value);
        }

        /// <summary>Intensidade da luz (Lúmens para físicos, ou escala arbitrária).</summary>
        public float intensity
        {
            get => NativeDelegates.ComponentLightGetIntensity?.Invoke(_handle) ?? 1f;
            set => NativeDelegates.ComponentLightSetIntensity?.Invoke(_handle, Math.Max(0f, value));
        }

        /// <summary>Cor da luz (RGB). Alfa é ignorado.</summary>
        public Color color
        {
            get
            {
                float r = 1, g = 1, b = 1, a = 1;
                NativeDelegates.ComponentLightGetColor?.Invoke(_handle, out r, out g, out b, out a);
                return new Color(r, g, b, a);
            }
            set => NativeDelegates.ComponentLightSetColor?.Invoke(_handle, value.R, value.G, value.B, value.A);
        }

        /// <summary>
        /// Alcance máximo da luz (em unidades de cena). Apenas válido para Point e Spot.
        /// </summary>
        public float range
        {
            get => NativeDelegates.ComponentLightGetRange?.Invoke(_handle) ?? 10f;
            set => NativeDelegates.ComponentLightSetRange?.Invoke(_handle, Math.Max(0f, value));
        }

        /// <summary>Ângulo interno do cone Spot (radianos). 0 = ponto, até PI = 180°.</summary>
        public float spotAngle
        {
            get => NativeDelegates.ComponentLightGetSpotAngle?.Invoke(_handle) ?? (float)(Math.PI / 4);
            set => NativeDelegates.ComponentLightSetSpotAngle?.Invoke(_handle, Math.Clamp(value, 0f, (float)Math.PI));
        }

        public float sourceRadius
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Light, LightProperty.SourceRadius);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Light, LightProperty.SourceRadius, Math.Max(0f, value));
        }

        public float shadowSoftness
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Light, LightProperty.ShadowSoftness);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Light, LightProperty.ShadowSoftness, Math.Clamp(value, 0f, 1f));
        }

        public float shadowStrength
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Light, LightProperty.ShadowStrength);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Light, LightProperty.ShadowStrength, Math.Clamp(value, 0f, 1f));
        }

        public float contactShadowStrength
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Light, LightProperty.ContactShadowStrength);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Light, LightProperty.ContactShadowStrength, Math.Clamp(value, 0f, 1f));
        }

        public float temperatureKelvin
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Light, LightProperty.TemperatureKelvin);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Light, LightProperty.TemperatureKelvin, Math.Clamp(value, 1000f, 40000f));
        }

        internal LightComponent(ulong handle) { _handle = handle; }
    }
}
