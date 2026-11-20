using MaraeEngine.Internal;

namespace MaraeEngine
{
    /// <summary>
    /// API estática para métricas de tempo. Todos os valores são atualizados a cada frame
    /// pelo runtime nativo. A propriedade <see cref="timeScale"/> pode ser usada para pausar
    /// ou desacelerar o jogo.
    /// </summary>
    public static class Time
    {
        /// <summary>Tempo em segundos decorrido desde o último frame, considerando timeScale.</summary>
        public static float deltaTime => NativeDelegates.TimeGetDelta?.Invoke() ?? 0f;

        /// <summary>Delta time SEM considerar timeScale (tempo real).</summary>
        public static float unscaledDeltaTime => NativeDelegates.TimeGetUnscaledDelta?.Invoke() ?? 0f;

        /// <summary>Tempo total em segundos desde o início da sessão de Play (considera timeScale).</summary>
        public static float time => NativeDelegates.TimeGetTime?.Invoke() ?? 0f;

        /// <summary>Passo de tempo usado em FixedUpdate (padrão: 0.02s / 50 Hz).</summary>
        public static float fixedDeltaTime => NativeDelegates.TimeGetFixedDelta?.Invoke() ?? 0.02f;

        /// <summary>
        /// Escala de tempo global. 1.0 = velocidade normal, 0.5 = metade, 0 = pausado.
        /// Afeta <see cref="deltaTime"/> e todos os timers/invokes do runtime.
        /// </summary>
        public static float timeScale
        {
            get => NativeDelegates.TimeGetTimeScale?.Invoke() ?? 1f;
            set
            {
                value = System.Math.Clamp(value, 0f, 100f);
                NativeDelegates.TimeSetTimeScale?.Invoke(value);
            }
        }

        /// <summary>Atalho: retorna true se timeScale é zero (jogo pausado via tempo).</summary>
        public static bool isPaused => System.Math.Abs(timeScale) < 1e-6f;
    }
}
