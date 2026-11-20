using MaraeEngine.Internal;

namespace MaraeEngine
{
    /// <summary>
    /// Componente de áudio 3D anexado a uma entidade. Permite iniciar/parar playback, ajustar
    /// volume, pitch e habilitar looping.
    /// </summary>
    public class AudioComponent
    {
        private readonly ulong _handle;
        internal ulong Handle => _handle;

        /// <summary>Inicia o playback do áudio anexado.</summary>
        public void Play() => NativeDelegates.ComponentAudioPlay?.Invoke(_handle);

        /// <summary>Interrompe o playback imediatamente.</summary>
        public void Stop() => NativeDelegates.ComponentAudioStop?.Invoke(_handle);

        /// <summary>Indica se o áudio está sendo reproduzido atualmente.</summary>
        public bool isPlaying => NativeDelegates.ComponentAudioGetPlaying?.Invoke(_handle) ?? false;

        /// <summary>Volume do playback [0, 1]. Valores acima de 1 amplificam (causa clipping).</summary>
        public float volume
        {
            get => NativeDelegates.ComponentAudioGetVolume?.Invoke(_handle) ?? 1f;
            set
            {
                value = Math.Clamp(value, 0f, 10f);
                NativeDelegates.ComponentAudioSetVolume?.Invoke(_handle, value);
            }
        }

        /// <summary>Se o áudio deve repetir indefinidamente após chegar ao fim.</summary>
        public bool loop
        {
            get => NativeDelegates.ComponentAudioGetLoop?.Invoke(_handle) ?? false;
            set => NativeDelegates.ComponentAudioSetLoop?.Invoke(_handle, value);
        }

        /// <summary>Tom (pitch) do áudio. 1.0 = original, 2.0 = uma oitava acima.</summary>
        public float pitch
        {
            get => NativeDelegates.ComponentAudioGetPitch?.Invoke(_handle) ?? 1f;
            set
            {
                value = Math.Clamp(value, 0.01f, 10f);
                NativeDelegates.ComponentAudioSetPitch?.Invoke(_handle, value);
            }
        }

        internal AudioComponent(ulong handle) { _handle = handle; }
    }
}
