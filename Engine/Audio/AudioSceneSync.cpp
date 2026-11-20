#include "AudioSceneSync.h"

#include "AudioAcoustics.h"
#include "AudioSystem.h"
#include "Listener/AudioListenerComponent.h"
#include "Source/AudioSourceComponent.h"
#include "../../Scene/Scene/Scene.h"
#include "../../Scene/Entity/Entity.h"
#include "../../Scene/Camera/Camera.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace
{
    // Resolve o clip da fonte (ClipID via library ou ClipPath direto) para um
    // caminho absoluto. Caminhos relativos são procurados na raiz do projeto
    // e, como fallback, no diretório de trabalho — mesmo comportamento do
    // resolvedor de arquivos de projeto do editor.
    std::string ResolveSourceClipPath(const AudioSourceComponent& audio,
                                      const std::string& projectRoot)
    {
        const std::string raw = audio.ResolveClipPath();
        if (raw.empty())
            return {};

        const std::filesystem::path input(raw);
        if (input.is_absolute())
            return input.string();

        const std::filesystem::path root = !projectRoot.empty()
            ? std::filesystem::path(projectRoot)
            : std::filesystem::current_path();
        const std::filesystem::path inProject = root / input;
        if (std::filesystem::exists(inProject))
            return std::filesystem::weakly_canonical(inProject).string();

        const std::filesystem::path inWorkingDirectory = std::filesystem::current_path() / input;
        if (std::filesystem::exists(inWorkingDirectory))
            return std::filesystem::weakly_canonical(inWorkingDirectory).string();

        return inProject.string();
    }
}

void AudioSceneSync::Reset()
{
    m_StartedEntities.clear();
    m_HasListenerPosition = false;
}

void AudioSceneSync::Update(Scene& scene,
                            const Camera& listenerCamera,
                            float deltaTime,
                            bool gameplayActive,
                            const std::string& projectRoot)
{
    AudioSystem& audioSystem = AudioSystem::Get();

    // Listener derivado da mesma câmera que o viewport está usando.
    const glm::vec3 listenerPosition(listenerCamera.Position.X,
                                     listenerCamera.Position.Y,
                                     listenerCamera.Position.Z);

    // Forward da câmera: -Z rotacionada pela orientação.
    const float yawRad   = glm::radians(listenerCamera.Rotation.Y);
    const float pitchRad = glm::radians(listenerCamera.Rotation.X);
    const glm::vec3 listenerForward = glm::normalize(glm::vec3(
        std::sin(yawRad) * std::cos(pitchRad),
       -std::sin(pitchRad),
       -std::cos(yawRad) * std::cos(pitchRad)));
    const glm::vec3 listenerUp(0.0f, 1.0f, 0.0f);

    glm::vec3 listenerVelocity(0.0f);
    if (m_HasListenerPosition)
    {
        listenerVelocity = (listenerPosition - m_PrevListenerPosition) /
                           std::max(deltaTime, 0.0001f);
    }
    m_PrevListenerPosition = listenerPosition;
    m_HasListenerPosition = true;

    // Sincroniza as voices vinculadas a entidades durante o Play.
    // O handle fica no componente para que cada fonte preserve seu loop,
    // pausa, posição, velocidade e bus independentemente.
    if (gameplayActive)
    {
        for (Entity& entity : scene.GetEntities())
        {
            if (!entity.HasAudio())
                continue;

            AudioComponent& audio = entity.GetAudioComponent();
            if (!entity.Active || !audio.Enabled || audio.ResolveClipPath().empty())
            {
                if (audio.RuntimeVoiceID != AUDIO_INVALID_VOICE)
                {
                    audioSystem.Stop(audio.RuntimeVoiceID);
                    audio.RuntimeVoiceID = AUDIO_INVALID_VOICE;
                }
                continue;
            }

            const Transform& transform = entity.GetTransform();
            const glm::vec3 position(transform.Position.X,
                                     transform.Position.Y,
                                     transform.Position.Z);
            const glm::vec3 velocity(entity.Velocity.X,
                                     entity.Velocity.Y,
                                     entity.Velocity.Z);

            if (audio.RuntimeVoiceID == AUDIO_INVALID_VOICE &&
                audio.PlayOnStart &&
                m_StartedEntities.find(entity.GetID()) == m_StartedEntities.end())
            {
                AudioSourceParams params;
                params.ClipPath = ResolveSourceClipPath(audio, projectRoot);
                params.Position = position;
                params.Velocity = velocity;
                params.Volume = audio.GetEffectiveVolume();
                params.Pitch = audio.GetEffectivePitch();
                params.SpatialBlend = std::clamp(audio.SpatialBlend, 0.0f, 1.0f);
                params.MinDistance = std::max(0.01f, audio.MinDistance);
                params.MaxDistance = std::max(params.MinDistance + 0.01f, audio.MaxDistance);
                params.DopplerFactor = std::clamp(audio.DopplerFactor, 0.0f, 4.0f);
                params.Loop = audio.Loop;
                params.Priority = audio.Priority;
                params.GroupID = audio.MixerGroupID;
                params.AttenuationModel = audio.AttenuationModel;
                params.ConeInnerDeg = audio.ConeInnerAngle;
                params.ConeOuterDeg = audio.ConeOuterAngle;
                params.ConeOuterVol = audio.ConeOuterVolume;
                audio.RuntimeVoiceID = audioSystem.Play(params);
                m_StartedEntities.insert(entity.GetID());
            }

            if (audio.RuntimeVoiceID != AUDIO_INVALID_VOICE)
            {
                if (!audioSystem.IsPlaying(audio.RuntimeVoiceID))
                {
                    audio.RuntimeVoiceID = AUDIO_INVALID_VOICE;
                }
                else
                {
                    audioSystem.SetVoiceTransform(
                        audio.RuntimeVoiceID, position, velocity);
                    const AudioAcousticMix acoustic = AudioAcoustics::Evaluate(
                        scene, entity, listenerPosition);
                    audioSystem.SetVoiceAcousticMix(audio.RuntimeVoiceID, acoustic);
                }
            }
        }
    }

    audioSystem.SetListenerFromCamera(
        listenerPosition, listenerForward, listenerUp, listenerVelocity);
    audioSystem.Update(deltaTime);
}
