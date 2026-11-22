using MaraeEngine;

namespace MaraeScripts
{
    public sealed class ScriptApiAudio : ScriptBehaviour
    {
        public override void OnStart()
        {
            AudioComponent audio = gameObject!.GetComponent<AudioComponent>()!;
            audio.volume = 0.6f;
            audio.pitch = 1.25f;
            audio.loop = true;
            audio.Play();
        }
    }
}
