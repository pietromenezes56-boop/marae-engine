using MaraeEngine.Internal;

namespace MaraeEngine
{
    /// <summary>
    /// Reprodução de fontes de áudio identificadas pelo ID exibido no Inspector.
    /// </summary>
    public static class Audio
    {
        /// <summary>
        /// Toca a fonte cujo campo ID do som corresponde a sourceId.
        /// Exemplo: Audio.Play("audio_12");
        /// </summary>
        public static bool Play(string sourceId)
        {
            if (string.IsNullOrWhiteSpace(sourceId))
                return false;
            return NativeDelegates.AudioPlayBySourceID?.Invoke(sourceId) ?? false;
        }

        /// <summary>Alias explícito para facilitar leitura em scripts.</summary>
        public static bool PlayById(string sourceId) => Play(sourceId);
    }
}
