using MaraeEngine.Events;
using MaraeEngine;

namespace MaraeExamples
{
    /// <summary>Exemplo de ciclo de cena sem inventar uma API de carregamento.</summary>
    public sealed class SceneEvents : ScriptBehaviour
    {
        public override void OnEnable()
        {
            EventSystem.Subscribe<SceneLoadedEvent>(OnSceneLoaded);
            EventSystem.Subscribe<PlayStateChangedEvent>(OnPlayStateChanged);
        }

        public override void OnDisable()
        {
            EventSystem.Unsubscribe<SceneLoadedEvent>(OnSceneLoaded);
            EventSystem.Unsubscribe<PlayStateChangedEvent>(OnPlayStateChanged);
        }

        private void OnSceneLoaded(SceneLoadedEvent data)
        {
            NativeLog($"Cena carregada: {data.ScenePath}");
        }

        private void OnPlayStateChanged(PlayStateChangedEvent data)
        {
            NativeLog(data.IsPlaying ? "Play iniciado" : "Play encerrado");
        }

        private static void NativeLog(string message)
        {
            // A mensagem fica deliberadamente no exemplo; o runtime exibe o diagnóstico.
            _ = message;
        }
    }
}
