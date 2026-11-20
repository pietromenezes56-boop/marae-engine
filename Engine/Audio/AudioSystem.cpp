// ---------------------------------------------------------------------------
// AudioSystem.cpp — implementação completa com miniaudio nativo
// ---------------------------------------------------------------------------

#include "AudioSystem.h"

// miniaudio — IMPLEMENTATION está em miniaudio_impl.cpp
#include "../../ThirdParty/miniaudio/miniaudio.h"

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #define GUID Windows_SDK_GUID
  #include <windows.h>
  #undef GUID
  #include <mmsystem.h>
  #pragma comment(lib, "winmm.lib")
#endif

#include <glm/gtc/constants.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <vector>
#include <cstdint>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Constantes
// ---------------------------------------------------------------------------
static constexpr int   k_MaxVoices      = 64;
static constexpr float k_SpeedOfSound   = 343.f;  // m/s

// ---------------------------------------------------------------------------
// WAV procedural (compatibilidade com sons do Survival)
// ---------------------------------------------------------------------------
namespace
{
    void WU16(std::vector<uint8_t>& d, size_t o, uint16_t v)
    {
        d[o]   = v & 0xFF; d[o+1] = (v>>8) & 0xFF;
    }
    void WU32(std::vector<uint8_t>& d, size_t o, uint32_t v)
    {
        for (int i=0;i<4;++i) d[o+i]=(v>>(i*8))&0xFF;
    }
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
AudioSystem& AudioSystem::Get()
{
    static AudioSystem instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
bool AudioSystem::Init()
{
    if (m_Initialized) return true;

    m_Engine = new ma_engine();
    ma_engine_config cfg = ma_engine_config_init();
    cfg.channels   = 2;
    cfg.sampleRate = 44100;
    // Habilita 1 listener nativo (índice 0)
    cfg.listenerCount = 1;

    if (ma_engine_init(&cfg, m_Engine) != MA_SUCCESS)
    {
        std::cerr << "[AudioSystem] Falha ao inicializar miniaudio\n";
        delete m_Engine; m_Engine = nullptr;
        return false;
    }

    // Inicializa o mixer e cria os sound groups
    AudioMixer::Get().Init();
    if (!InitMixerGroups())
    {
        std::cerr << "[AudioSystem] Falha ao criar mixer groups\n";
    }

    m_Voices.reserve(k_MaxVoices);
    m_Stats.MaxVoices = k_MaxVoices;
    m_Initialized = true;

    std::cout << "[AudioSystem] Inicializado — miniaudio 3D nativo "
              << "(WAV/MP3/OGG/FLAC)\n";
    return true;
}

// ---------------------------------------------------------------------------
// InitMixerGroups — cria um ma_sound_group por grupo do AudioMixer
// ---------------------------------------------------------------------------
bool AudioSystem::InitMixerGroups()
{
    for (const auto& mixGroup : AudioMixer::Get().GetAllGroups())
    {
        bool alreadyCreated = false;
        for (const auto& runtimeGroup : m_Groups)
            if (runtimeGroup.GroupID == mixGroup->GetID())
            { alreadyCreated = true; break; }
        if (alreadyCreated) continue;

        // Encontra o grupo pai (nullptr = rota diretamente para o endpoint)
        ma_sound* parentGroup = nullptr;
        if (!mixGroup->GetParentID().empty())
            parentGroup = GetOrCreateGroup(mixGroup->GetParentID());

        auto* g = new ma_sound();
        ma_result r = ma_sound_group_init(m_Engine, 0, parentGroup, g);
        if (r != MA_SUCCESS)
        {
            std::cerr << "[AudioSystem] Falha ao criar group '"
                      << mixGroup->GetID() << "': "
                      << ma_result_description(r) << "\n";
            delete g;
            continue;
        }

        m_Groups.push_back({ mixGroup->GetID(), g });
    }
    return true;
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------
void AudioSystem::Shutdown()
{
    if (!m_Initialized) return;

    for (auto& v : m_Voices)
        if (v.Active && v.Sound) { ma_sound_stop(v.Sound); ma_sound_uninit(v.Sound); delete v.Sound; }
    m_Voices.clear();

    for (auto& g : m_Groups)
        if (g.Group) { ma_sound_group_uninit(g.Group); delete g.Group; }
    m_Groups.clear();

    // if (m_GlobalLPF)   { ma_lpf_node_uninit  (m_GlobalLPF,   nullptr); delete m_GlobalLPF;   m_GlobalLPF   = nullptr; }
    // if (m_GlobalHPF)   { ma_hpf_node_uninit  (m_GlobalHPF,   nullptr); delete m_GlobalHPF;   m_GlobalHPF   = nullptr; }
    // if (m_GlobalDelay) { ma_delay_node_uninit (m_GlobalDelay, nullptr); delete m_GlobalDelay; m_GlobalDelay = nullptr; }

    if (m_Engine) { ma_engine_uninit(m_Engine); delete m_Engine; m_Engine = nullptr; }
    m_Initialized = false;
}

// ---------------------------------------------------------------------------
// Update — chamado todo frame
// ---------------------------------------------------------------------------
void AudioSystem::Update(float deltaTime)
{
    if (!m_Initialized) return;

    m_CurrentTime += deltaTime;
    m_Stats.Reset();

    // 1. Mixer
    AudioMixer::Get().Update(deltaTime);
    UpdateMixerGroups();

    // 2. Listener nativo
    UpdateListenerNative();

    // 3. Ambiente acústico
    UpdateEnvironment(deltaTime);

    // 4. Voices
    UpdateVoices(deltaTime);

    // 5. Stats finais
    m_Stats.ActiveVoices = 0;
    for (const auto& v : m_Voices)
        if (v.Active) ++m_Stats.ActiveVoices;

    m_Stats.MasterVolume = m_MasterVolume;
}

// ---------------------------------------------------------------------------
// UpdateListenerNative
// ---------------------------------------------------------------------------
void AudioSystem::UpdateListenerNative()
{
    if (!m_Engine) return;
    constexpr uint32_t idx = 0;
    ma_engine_listener_set_position (m_Engine, idx, m_ListenerPos.x,     m_ListenerPos.y,     m_ListenerPos.z);
    ma_engine_listener_set_direction(m_Engine, idx, m_ListenerForward.x, m_ListenerForward.y, m_ListenerForward.z);
    ma_engine_listener_set_world_up (m_Engine, idx, m_ListenerUp.x,      m_ListenerUp.y,      m_ListenerUp.z);
    ma_engine_listener_set_velocity (m_Engine, idx, m_ListenerVel.x,     m_ListenerVel.y,     m_ListenerVel.z);
}

// ---------------------------------------------------------------------------
// UpdateEnvironment — aplica LPF/HPF globais baseados na zona acústica
// ---------------------------------------------------------------------------
void AudioSystem::UpdateEnvironment(float deltaTime)
{
    AudioEnvironmentManager::Get().Update(m_ListenerPos, deltaTime);
    const AcousticState& state = AudioEnvironmentManager::Get().GetCurrentState();

    m_Stats.EnvironmentWet  = state.Reverb.WetLevel;
    m_Stats.OcclusionAmount = state.OcclusionAmount;
    m_Stats.LPFCutoffHz     = state.LPFCutoff;
    m_Stats.HPFCutoffHz     = state.HPFCutoff;

    // LPF global — recria se o cutoff mudou mais de 50 Hz
    // if (m_Engine && std::abs(state.LPFCutoff - m_CurrentLPFCutoff) > 50.f)
    // {
    //     m_CurrentLPFCutoff = state.LPFCutoff;
    //     if (m_GlobalLPF)
    //     {
    //         ma_lpf_config lc = ma_lpf_config_init(ma_format_f32, 2, 44100,
    //                                               static_cast<double>(m_CurrentLPFCutoff), 1);
    //         ma_lpf_node_reinit(&lc, m_GlobalLPF);
    //     }
    // }
}

// ---------------------------------------------------------------------------
// UpdateMixerGroups — aplica volume efetivo ao ma_sound_group
// ---------------------------------------------------------------------------
void AudioSystem::UpdateMixerGroups()
{
    for (auto& entry : m_Groups)
    {
        const AudioMixer& mixer = AudioMixer::Get();
        const AudioMixerGroup* logicalGroup = mixer.GetGroup(entry.GroupID);
        float localVolume = logicalGroup ? logicalGroup->GetAppliedVolume() : 1.0f;
        if (logicalGroup)
        {
            const AudioMixerGroup* parent = logicalGroup->GetParentID().empty()
                ? nullptr : mixer.GetGroup(logicalGroup->GetParentID());
            if (parent)
            {
                const float parentEffective = parent->GetEffectiveVolume();
                if (parentEffective > 1e-6f)
                    localVolume = mixer.GetEffectiveVolume(entry.GroupID) / parentEffective;
                else if (mixer.GetEffectiveVolume(entry.GroupID) <= 1e-6f)
                    localVolume = 0.0f; // solo/mute local; pai já silencia o restante
            }
            else
                localVolume = mixer.GetEffectiveVolume(entry.GroupID);
        }
        if (entry.Group)
            ma_sound_group_set_volume(entry.Group,
                std::clamp(localVolume, 0.0f, 1.0f) * (logicalGroup && logicalGroup->GetParentID().empty()
                    ? m_MasterVolume : 1.0f));
    }
}

// ---------------------------------------------------------------------------
// UpdateVoices
// ---------------------------------------------------------------------------
void AudioSystem::UpdateVoices(float dt)
{
    m_Stats.VoicesThisFrame = 0;

    for (auto& v : m_Voices)
    {
        if (!v.Active) continue;

        // ---- Delay de início -------------------------------------------
        if (v.DelayPending)
        {
            v.DelayTimer -= dt;
            if (v.DelayTimer > 0.f) continue;
            v.DelayPending = false;
            if (v.Sound) ma_sound_start(v.Sound);
        }

        if (!v.Sound) continue;

        // O ganho acústico é reaplicado continuamente para que mover a fonte,
        // listener ou uma parede atualize o volume sem recriar a voice.
        ma_sound_set_volume(v.Sound,
            std::clamp(v.Params.Volume * v.AcousticGain, 0.0f, 1.0f));

        // ---- Remove voices finalizadas (não em loop) --------------------
        if (!v.Paused && !ma_sound_is_playing(v.Sound) && !v.DelayPending)
        {
            FreeVoice(v);
            continue;
        }

        // ---- Fade -------------------------------------------------------
        if (v.FadeActive)
        {
            v.FadeElapsed += dt;
            float t = std::max(0.f, std::min(v.FadeElapsed / std::max(v.FadeDuration, 0.001f), 1.f));
            float smooth = t * t * (3.f - 2.f * t);
            float vol = v.FadeStart + (v.FadeTarget - v.FadeStart) * smooth;
            ma_sound_set_volume(v.Sound, vol);

            if (t >= 1.f)
            {
                v.FadeActive = false;
                if (v.FadeStop) { FreeVoice(v); continue; }
            }
        }

        // ---- Debug info -------------------------------------------------
        {
            AudioVoiceDebugInfo info;
            info.VoiceID  = v.ID;
            info.GroupID  = v.Params.GroupID;
            info.Volume   = v.Params.Volume;
            info.Pitch    = v.Params.Pitch;
            info.Is3D     = v.Params.SpatialBlend > 0.01f;
            info.Paused   = v.Paused;
            info.Looping  = v.Params.Loop;

            glm::vec3 delta = v.Params.Position - m_ListenerPos;
            info.Distance = glm::length(delta);

            // Nome curto do clip
            fs::path p(v.Params.ClipPath);
            info.ClipName = p.filename().string();

            m_Stats.Voices.push_back(info);
        }
    }
}

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------
AudioVoiceID AudioSystem::Play(const AudioSourceParams& params)
{
    if (!m_Initialized || !m_Engine) return AUDIO_INVALID_VOICE;
    if (params.ClipPath.empty())     return AUDIO_INVALID_VOICE;

    Voice* v = AllocVoice(params.Priority);
    if (!v) return AUDIO_INVALID_VOICE;

    v->Sound = new ma_sound();

    // Flags: stream se pedido ou se arquivo grande
    ma_uint32 flags = 0;
    bool doStream = params.Stream;
    if (!doStream)
    {
        // Auto: >=256KB → stream
        std::error_code ec;
        auto sz = fs::file_size(params.ClipPath, ec);
        if (!ec && sz >= 256 * 1024) doStream = true;
    }
    if (doStream) flags |= MA_SOUND_FLAG_STREAM;

    // Grupo de roteamento
    ma_sound* group = GetOrCreateGroup(params.GroupID);

    ma_result result = ma_sound_init_from_file(
        m_Engine, params.ClipPath.c_str(), flags,
        group, nullptr, v->Sound);

    if (result != MA_SUCCESS)
    {
        std::cerr << "[AudioSystem] Falha ao carregar: " << params.ClipPath
                  << " — " << ma_result_description(result) << "\n";
        delete v->Sound; v->Sound = nullptr; v->Active = false;
        return AUDIO_INVALID_VOICE;
    }

    // Cada voice 3D recebe um LPF leve para tornar a oclusão audível também
    // pela perda de altas frequências. O filtro é inserido no grafo antes do
    // grupo de mixagem e seu cutoff é atualizado quando a geometria muda.
    if (params.SpatialBlend > 0.01f && group)
    {
        auto* filter = new ma_lpf_node();
        const ma_uint32 channels = std::max(1u, ma_engine_get_channels(m_Engine));
        const ma_uint32 sampleRate = std::max(1u, ma_engine_get_sample_rate(m_Engine));
        const ma_lpf_node_config filterConfig = ma_lpf_node_config_init(
            channels, sampleRate, 22000.0, 1);
        const ma_result filterInit = ma_lpf_node_init(
            ma_engine_get_node_graph(m_Engine), &filterConfig, nullptr, filter);
        if (filterInit == MA_SUCCESS)
        {
            ma_node* soundNode = reinterpret_cast<ma_node*>(v->Sound);
            ma_node* filterNode = reinterpret_cast<ma_node*>(&filter->baseNode);
            ma_node* groupNode = reinterpret_cast<ma_node*>(group);
            ma_node_detach_output_bus(soundNode, 0);
            const ma_result inputConnected = ma_node_attach_output_bus(soundNode, 0, filterNode, 0);
            const ma_result outputConnected = inputConnected == MA_SUCCESS
                ? ma_node_attach_output_bus(filterNode, 0, groupNode, 0) : inputConnected;
            if (inputConnected == MA_SUCCESS && outputConnected == MA_SUCCESS)
                v->AcousticFilter = static_cast<void*>(filter);
            else
            {
                ma_node_detach_output_bus(soundNode, 0);
                ma_node_attach_output_bus(soundNode, 0, groupNode, 0);
                ma_lpf_node_uninit(filter, nullptr);
                delete filter;
            }
        }
        else delete filter;
    }

    // ---- Configuração básica ----------------------------------------
    ma_sound_set_looping(v->Sound, params.Loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_pitch  (v->Sound, params.Pitch);
    ma_sound_set_volume (v->Sound, params.Volume);

    // ---- Espacialização 3D nativa ------------------------------------
    if (params.SpatialBlend > 0.01f)
    {
        ma_sound_set_spatialization_enabled(v->Sound, MA_TRUE);
        ma_sound_set_position  (v->Sound, params.Position.x, params.Position.y, params.Position.z);
        ma_sound_set_velocity  (v->Sound, params.Velocity.x, params.Velocity.y, params.Velocity.z);
        ma_sound_set_min_distance(v->Sound, params.MinDistance);
        ma_sound_set_max_distance(v->Sound, params.MaxDistance);
        ma_sound_set_doppler_factor(v->Sound, params.DopplerFactor);
        ma_sound_set_rolloff (v->Sound, 1.f);

        // Modelo de atenuação
        ma_attenuation_model atModel = ma_attenuation_model_inverse;
        switch (params.AttenuationModel)
        {
            case AudioAttenuationModel::None:        atModel = ma_attenuation_model_none;        break;
            case AudioAttenuationModel::Linear:      atModel = ma_attenuation_model_linear;      break;
            case AudioAttenuationModel::Exponential: atModel = ma_attenuation_model_exponential; break;
            default:                                 atModel = ma_attenuation_model_inverse;     break;
        }
        ma_sound_set_attenuation_model(v->Sound, atModel);

        // Cone de direção
        if (params.ConeInnerDeg < 359.f)
        {
            ma_sound_set_cone(v->Sound,
                glm::radians(params.ConeInnerDeg),
                glm::radians(params.ConeOuterDeg),
                params.ConeOuterVol);
        }

        // Blend 2D/3D via pan se SpatialBlend < 1
        if (params.SpatialBlend < 0.99f)
            ma_sound_set_pan(v->Sound, 0.f);
    }
    else
    {
        // 2D puro
        ma_sound_set_spatialization_enabled(v->Sound, MA_FALSE);
        ma_sound_set_pan(v->Sound, 0.f);
    }

    v->Params = params;
    v->Paused = false;
    ++m_Stats.VoicesThisFrame;

    // Aplica efeito de ambiente ao novo som
    ApplyEnvironmentToNewVoice(*v);

    // Inicia (com delay se necessário)
    if (v->DelayPending)
    {
        // Será iniciado em UpdateVoices quando o timer expirar
    }
    else
    {
        ma_sound_start(v->Sound);
    }

    return v->ID;
}

// ---------------------------------------------------------------------------
// ApplyEnvironmentToNewVoice — aplica occlusion / LPF ao som recém criado
// ---------------------------------------------------------------------------
void AudioSystem::ApplyEnvironmentToNewVoice(Voice& v)
{
    const AcousticState& state = AudioEnvironmentManager::Get().GetCurrentState();

    if (state.OcclusionAmount > 0.01f && v.Params.SpatialBlend > 0.01f)
    {
        // Occlusion ambiental: o cálculo geométrico específico da fonte pode
        // substituir este valor no frame seguinte.
        float occVol = 1.f - state.OcclusionAmount * (1.f - 0.2f);
        v.AcousticGain = std::clamp(occVol, 0.0f, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Stop / Pause / Resume / Fade
// ---------------------------------------------------------------------------
void AudioSystem::Stop(AudioVoiceID id)
{
    Voice* v = FindVoice(id);
    if (v) FreeVoice(*v);
}

void AudioSystem::Stop()
{
    for (auto& v : m_Voices)
        if (v.Active) FreeVoice(v);

#ifdef _WIN32
    PlaySoundA(nullptr, nullptr, 0);
#endif
}

void AudioSystem::Pause(AudioVoiceID id)
{
    Voice* v = FindVoice(id);
    if (!v || !v->Sound || v->Paused) return;
    ma_sound_stop(v->Sound);
    v->Paused = true;
}

void AudioSystem::Resume(AudioVoiceID id)
{
    Voice* v = FindVoice(id);
    if (!v || !v->Sound || !v->Paused) return;
    ma_sound_start(v->Sound);
    v->Paused = false;
}

bool AudioSystem::IsPlaying(AudioVoiceID id) const
{
    const Voice* v = FindVoice(id);
    if (!v || !v->Sound) return false;
    return ma_sound_is_playing(v->Sound) == MA_TRUE;
}

void AudioSystem::FadeVolume(AudioVoiceID id, float targetVol, float duration)
{
    Voice* v = FindVoice(id);
    if (!v || !v->Sound) return;
    v->FadeStart    = ma_sound_get_volume(v->Sound);
    v->FadeTarget   = std::max(0.f, std::min(targetVol, 1.f));
    v->FadeDuration = std::max(duration, 0.001f);
    v->FadeElapsed  = 0.f;
    v->FadeActive   = true;
    v->FadeStop     = (targetVol <= 0.f);
}

// ---------------------------------------------------------------------------
// Position / Transform
// ---------------------------------------------------------------------------
void AudioSystem::SetVoicePosition(AudioVoiceID id, const glm::vec3& pos)
{
    Voice* v = FindVoice(id);
    if (!v || !v->Sound) return;
    v->Params.Position = pos;
    ma_sound_set_position(v->Sound, pos.x, pos.y, pos.z);
}

void AudioSystem::SetVoiceTransform(AudioVoiceID id,
                                     const glm::vec3& pos,
                                     const glm::vec3& vel)
{
    Voice* v = FindVoice(id);
    if (!v || !v->Sound) return;
    v->Params.Position = pos;
    v->Params.Velocity = vel;
    ma_sound_set_position(v->Sound, pos.x, pos.y, pos.z);
    ma_sound_set_velocity(v->Sound, vel.x, vel.y, vel.z);
}

void AudioSystem::SetVoiceAcousticMix(AudioVoiceID id, const AudioAcousticMix& mix)
{
    Voice* v = FindVoice(id);
    if (!v) return;
    v->AcousticGain = std::clamp(mix.Gain, 0.0f, 1.0f);
    if (v->AcousticFilter)
    {
        const float occlusion = std::clamp(mix.Filtering, 0.0f, 1.0f);
        const float cutoffHz = std::clamp(22000.0f * (1.0f - occlusion) + 700.0f * occlusion,
                                          700.0f, 22000.0f);
        if (std::abs(cutoffHz - v->AcousticCutoffHz) >= 80.0f)
        {
            const ma_lpf_config config = ma_lpf_config_init(ma_format_f32,
                std::max(1u, ma_engine_get_channels(m_Engine)),
                std::max(1u, ma_engine_get_sample_rate(m_Engine)), cutoffHz, 1);
            if (ma_lpf_node_reinit(&config, static_cast<ma_lpf_node*>(v->AcousticFilter)) == MA_SUCCESS)
                v->AcousticCutoffHz = cutoffHz;
        }
    }
}

// ---------------------------------------------------------------------------
// Listener
// ---------------------------------------------------------------------------
void AudioSystem::SetListener(const AudioListenerComponent& listener)
{
    if (listener.AutoFollowTransform)
    {
        m_ListenerPos     = listener.WorldPosition;
        m_ListenerForward = listener.WorldForward;
        m_ListenerUp      = listener.WorldUp;
        m_ListenerVel     = listener.Velocity;
    }
    else
    {
        m_ListenerPos     = listener.ManualPosition;
        m_ListenerForward = listener.ManualForward;
        m_ListenerUp      = listener.ManualUp;
    }
}

void AudioSystem::SetListenerFromCamera(const glm::vec3& pos,
                                         const glm::vec3& forward,
                                         const glm::vec3& up,
                                         const glm::vec3& velocity)
{
    m_ListenerPos     = pos;
    m_ListenerForward = forward;
    m_ListenerUp      = up;
    m_ListenerVel     = velocity;
}

// ---------------------------------------------------------------------------
// Mixer conveniência
// ---------------------------------------------------------------------------
void AudioSystem::SetGroupVolume(const std::string& groupID, float volume)
{
    AudioMixerGroup* g = AudioMixer::Get().GetGroup(groupID);
    if (g) g->SetVolume(volume);
}

void AudioSystem::SetMasterVolume(float volume)
{
    m_MasterVolume = std::clamp(volume, 0.f, 1.f);
    if (m_Engine) ma_engine_set_volume(m_Engine, m_MasterVolume);
}

float AudioSystem::GetMasterVolume() const { return m_MasterVolume; }

// ---------------------------------------------------------------------------
// Environment
// ---------------------------------------------------------------------------
void AudioSystem::RegisterEnvironmentZone(const AudioEnvironmentZone& zone)
{
    AudioEnvironmentManager::Get().RegisterZone(zone);
}

void AudioSystem::UnregisterEnvironmentZone(uint64_t entityID)
{
    AudioEnvironmentManager::Get().UnregisterZone(entityID);
}

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------
AudioVoiceID AudioSystem::PlayPreview(const AudioSourceComponent& src)
{
    std::string path = src.ResolveClipPath();
    if (path.empty()) return AUDIO_INVALID_VOICE;
    return PlayPreviewClip(path, src.GetEffectiveVolume(), src.GetEffectivePitch());
}

AudioVoiceID AudioSystem::PlayPreviewClip(const std::string& clipPath, float volume, float pitch)
{
    if (!m_Initialized || clipPath.empty()) return AUDIO_INVALID_VOICE;
    AudioSourceParams p;
    p.ClipPath    = clipPath;
    p.Volume      = volume;
    p.Pitch       = pitch;
    p.SpatialBlend= 0.f;   // sempre 2D no editor
    p.GroupID     = MixerGroupID::SFX;
    return Play(p);
}

// ---------------------------------------------------------------------------
// Sons procedurais do Survival
// ---------------------------------------------------------------------------
void AudioSystem::PlayToneInternal(float freq, float dur, float vol)
{
#ifdef _WIN32
    constexpr int SR = 22050;
    int sc = std::max(1, (int)(SR * dur));
    uint32_t ds = sc * 2;
    std::vector<uint8_t> w(44 + ds, 0);
    const char r[]="RIFF", wv[]="WAVE", fm[]="fmt ", dt[]="data";
    std::copy(r,r+4,w.begin());
    for (int i=0;i<4;++i) w[4+i]=((36+ds)>>(i*8))&0xFF;
    std::copy(wv,wv+4,w.begin()+8);
    std::copy(fm,fm+4,w.begin()+12);
    for (int i=0;i<4;++i) w[16+i]=(16>>(i*8))&0xFF;
    w[20]=1&0xFF; w[21]=(1>>8)&0xFF;
    w[22]=1&0xFF; w[23]=(1>>8)&0xFF;
    for (int i=0;i<4;++i) w[24+i]=(SR>>(i*8))&0xFF;
    for (int i=0;i<4;++i) w[28+i]=((SR*2)>>(i*8))&0xFF;
    w[32]=2&0xFF; w[33]=(2>>8)&0xFF;
    w[34]=16&0xFF; w[35]=(16>>8)&0xFF;
    std::copy(dt,dt+4,w.begin()+36);
    for (int i=0;i<4;++i) w[40+i]=(ds>>(i*8))&0xFF;
    for(int i=0;i<sc;++i){
        float t=(float)i/SR;
        float e=std::min(1.f,t*60.f)*std::min(1.f,(dur-t)*24.f);
        float s=std::sin(6.28318530718f*freq*t)*e*vol;
        int16_t p=(int16_t)(std::clamp(s,-1.f,1.f)*32767.f);
        WU16(w,44+i*2,(uint16_t)p);
    }
    PlaySoundA(reinterpret_cast<LPCSTR>(w.data()),nullptr,SND_MEMORY|SND_ASYNC|SND_NODEFAULT);
#else
    (void)freq;(void)dur;(void)vol;
#endif
}

void AudioSystem::PlayToolSwing() { PlayToneInternal(170.f,0.075f,0.16f); }
void AudioSystem::PlayPickup()    { PlayToneInternal(640.f,0.11f, 0.20f); }
void AudioSystem::PlayCraft()     { PlayToneInternal(420.f,0.18f, 0.22f); }
void AudioSystem::PlayEat()       { PlayToneInternal(280.f,0.13f, 0.18f); }

// ---------------------------------------------------------------------------
// Pool de voices
// ---------------------------------------------------------------------------
AudioSystem::Voice* AudioSystem::AllocVoice(uint8_t priority)
{
    // Slot livre
    for (auto& v : m_Voices)
        if (!v.Active)
        {
            v.ID = m_NextID++;
            if (m_NextID == AUDIO_INVALID_VOICE) m_NextID = 1;
            v.Active = true; v.Paused = false;
            v.FadeActive = false; v.DelayPending = false;
            return &v;
        }

    // Cria novo se abaixo do limite
    if ((int)m_Voices.size() < k_MaxVoices)
    {
        Voice nv;
        nv.ID = m_NextID++;
        if (m_NextID == AUDIO_INVALID_VOICE) m_NextID = 1;
        nv.Active = true;
        m_Voices.push_back(std::move(nv));
        return &m_Voices.back();
    }

    // Voice stealing: mata a de menor prioridade (maior número = menor prioridade)
    Voice* worst = nullptr;
    for (auto& v : m_Voices)
    {
        if (!v.Active) continue;
        if (!worst || v.Params.Priority > worst->Params.Priority)
            worst = &v;
    }
    if (worst && worst->Params.Priority >= priority)
    {
        FreeVoice(*worst);
        worst->ID = m_NextID++;
        if (m_NextID == AUDIO_INVALID_VOICE) m_NextID = 1;
        worst->Active = true; worst->Paused = false;
        worst->FadeActive = false; worst->DelayPending = false;
        ++m_Stats.VoicesStolenThisFrame;
        return worst;
    }

    return nullptr; // prioridade atual é menor que todas as ativas
}

void AudioSystem::FreeVoice(Voice& v)
{
    if (v.Sound)
    {
        ma_sound_stop(v.Sound);
        ma_sound_uninit(v.Sound);
        delete v.Sound;
        v.Sound = nullptr;
    }
    if (v.AcousticFilter)
    {
        auto* filter = static_cast<ma_lpf_node*>(v.AcousticFilter);
        ma_lpf_node_uninit(filter, nullptr);
        delete filter;
        v.AcousticFilter = nullptr;
    }
    v.AcousticCutoffHz = 22000.0f;
    v.Active = false; v.Paused = false;
    v.FadeActive = false; v.DelayPending = false;
    v.ID = AUDIO_INVALID_VOICE;
}

AudioSystem::Voice* AudioSystem::FindVoice(AudioVoiceID id)
{
    if (id == AUDIO_INVALID_VOICE) return nullptr;
    for (auto& v : m_Voices)
        if (v.Active && v.ID == id) return &v;
    return nullptr;
}

const AudioSystem::Voice* AudioSystem::FindVoice(AudioVoiceID id) const
{
    if (id == AUDIO_INVALID_VOICE) return nullptr;
    for (const auto& v : m_Voices)
        if (v.Active && v.ID == id) return &v;
    return nullptr;
}

ma_sound* AudioSystem::GetOrCreateGroup(const std::string& groupID)
{
    for (auto& g : m_Groups)
        if (g.GroupID == groupID) return g.Group;

    // Cria on-demand e mantém a mesma hierarquia do mixer lógico.
    ma_sound* parentGroup = nullptr;
    if (const AudioMixerGroup* mixerGroup = AudioMixer::Get().GetGroup(groupID))
        if (!mixerGroup->GetParentID().empty())
            parentGroup = GetOrCreateGroup(mixerGroup->GetParentID());

    auto* g = new ma_sound();
    if (ma_sound_group_init(m_Engine, 0, parentGroup, g) != MA_SUCCESS)
    {
        delete g; return nullptr;
    }
    m_Groups.push_back({ groupID, g });
    return g;
}

float AudioSystem::GetGroupEffectiveVolume(const std::string& groupID) const
{
    return AudioMixer::Get().GetEffectiveVolume(groupID);
}
