using System;

namespace UnityEngine
{
    /// <summary>
    /// Classe de compatibilidade para scripts no estilo Unity.
    /// Herda diretamente do ScriptBehaviour da Marae Engine.
    /// </summary>
    public abstract class MonoBehaviour : MaraeEngine.ScriptBehaviour
    {
    }

    /// <summary>
    /// Fornece acesso a delta time e tempo de jogo para scripts que utilizam a sintaxe do Unity.
    /// </summary>
    public static class Time
    {
        public static float deltaTime => MaraeEngine.Time.deltaTime;
        public static float unscaledDeltaTime => MaraeEngine.Time.unscaledDeltaTime;
        public static float time => MaraeEngine.Time.time;
        public static float fixedDeltaTime => MaraeEngine.Time.fixedDeltaTime;
        public static float timeScale
        {
            get => MaraeEngine.Time.timeScale;
            set => MaraeEngine.Time.timeScale = value;
        }
    }

    /// <summary>
    /// Redireciona chamadas de Debug.Log do Unity para o console e log da Marae Engine.
    /// </summary>
    public static class Debug
    {
        public static void Log(object message) => MaraeEngine.Internal.NativeDelegates.Log?.Invoke(0, message?.ToString() ?? "null");
        public static void LogWarning(object message) => MaraeEngine.Internal.NativeDelegates.Log?.Invoke(1, message?.ToString() ?? "null");
        public static void LogError(object message) => MaraeEngine.Internal.NativeDelegates.Log?.Invoke(3, message?.ToString() ?? "null");
    }
}

namespace MaraeEngine
{
    /// <summary>
    /// Alias opcional de MonoBehaviour dentro do namespace MaraeEngine.
    /// </summary>
    public abstract class MonoBehaviour : ScriptBehaviour
    {
    }
}
