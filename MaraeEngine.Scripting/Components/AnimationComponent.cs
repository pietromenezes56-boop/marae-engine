namespace MaraeEngine
{
    /// <summary>Controla a reprodução do AnimationClip associado à entidade.</summary>
    public sealed class AnimationComponent
    {
        private readonly ulong _handle;
        internal AnimationComponent(ulong handle) => _handle = handle;

        public bool enabled
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Animation, AnimationProperty.Enabled);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Animation, AnimationProperty.Enabled, value);
        }
        public bool isPlaying
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Animation, AnimationProperty.Playing);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Animation, AnimationProperty.Playing, value);
        }
        public bool playOnStart
        {
            get => ComponentAccess.GetBool(_handle, ComponentIds.Animation, AnimationProperty.PlayOnStart);
            set => ComponentAccess.SetBool(_handle, ComponentIds.Animation, AnimationProperty.PlayOnStart, value);
        }
        public float time
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Animation, AnimationProperty.Time);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Animation, AnimationProperty.Time, value < 0f ? 0f : value);
        }
        public float speed
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Animation, AnimationProperty.Speed);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Animation, AnimationProperty.Speed, value);
        }
        public float weight
        {
            get => ComponentAccess.GetFloat(_handle, ComponentIds.Animation, AnimationProperty.Weight);
            set => ComponentAccess.SetFloat(_handle, ComponentIds.Animation, AnimationProperty.Weight, System.Math.Clamp(value, 0f, 1f));
        }
        public string clipName => ComponentAccess.GetString(_handle, ComponentIds.Animation, 0);
        /// <summary>ID estável do clip, copiável no Inspector e salvo com a cena.</summary>
        public string clipId => ComponentAccess.GetString(_handle, ComponentIds.Animation, 1);
        /// <summary>Nome do estado ativo do controller; vazio se a máquina estiver desativada.</summary>
        public string currentState => ComponentAccess.GetString(_handle, ComponentIds.Animation, 2);

        public void Play() => ComponentAccess.Invoke(_handle, ComponentIds.Animation, AnimationProperty.Play);
        /// <summary>Executa o clip anexado se o ID informado corresponder a ele.</summary>
        public bool Play(string animationId)
        {
            if (string.IsNullOrWhiteSpace(animationId) ||
                !string.Equals(animationId.Trim(), clipId, System.StringComparison.Ordinal))
                return false;
            Play();
            return true;
        }
        public void Pause() => ComponentAccess.Invoke(_handle, ComponentIds.Animation, AnimationProperty.Pause);
        public void Stop() => ComponentAccess.Invoke(_handle, ComponentIds.Animation, AnimationProperty.Stop);

        /// <summary>Define um parâmetro bool usado nas transições da máquina de estados.</summary>
        public void SetBool(string parameter, bool value)
        {
            if (!string.IsNullOrWhiteSpace(parameter))
                Internal.NativeDelegates.AnimationSetBool?.Invoke(_handle, parameter.Trim(), value);
        }
    }
}
