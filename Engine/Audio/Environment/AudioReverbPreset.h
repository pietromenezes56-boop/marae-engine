#pragma once
// ---------------------------------------------------------------------------
// AudioReverbPreset — parâmetros de reverb/acústica para ambientes.
// Compatível com os parâmetros do delay node do miniaudio e processamento DSP.
// ---------------------------------------------------------------------------
#include <string>

enum class AudioReverbPresetID : uint8_t
{
    None        = 0,
    SmallRoom,
    MediumRoom,
    LargeRoom,
    Hall,
    Church,
    Cave,
    Forest,
    Tunnel,
    Underwater,
    Custom,
};

struct AudioReverbParams
{
    float RoomSize        = 0.5f;   // 0..1 — tamanho percebido da sala
    float DecayTime       = 1.5f;   // segundos
    float Density         = 0.7f;   // 0..1
    float Diffusion       = 0.7f;   // 0..1
    float WetLevel        = 0.3f;   // 0..1 — sinal com reverb
    float DryLevel        = 0.8f;   // 0..1 — sinal original
    float Damping         = 0.5f;   // 0..1 — absorção de altas frequências
    float PreDelayMs      = 20.f;   // ms — atraso das primeiras reflexões
    float EarlyReflections= 0.5f;   // 0..1 — nível das reflexões diretas
    float LPFCutoff       = 8000.f; // Hz — filtro passa-baixa no caminho wet
    float HPFCutoff       = 80.f;   // Hz — filtro passa-alta no caminho wet

    // Preenche com valores de preset
    static AudioReverbParams FromPreset(AudioReverbPresetID preset);
};

// Tabela de presets
inline AudioReverbParams AudioReverbParams::FromPreset(AudioReverbPresetID p)
{
    AudioReverbParams r;
    switch (p)
    {
        case AudioReverbPresetID::SmallRoom:
            r.RoomSize=0.25f; r.DecayTime=0.4f; r.Density=0.5f;
            r.WetLevel=0.25f; r.DryLevel=0.9f;  r.PreDelayMs=5.f;
            break;
        case AudioReverbPresetID::MediumRoom:
            r.RoomSize=0.45f; r.DecayTime=0.9f; r.Density=0.65f;
            r.WetLevel=0.30f; r.DryLevel=0.85f; r.PreDelayMs=12.f;
            break;
        case AudioReverbPresetID::LargeRoom:
            r.RoomSize=0.65f; r.DecayTime=1.8f; r.Density=0.75f;
            r.WetLevel=0.35f; r.DryLevel=0.80f; r.PreDelayMs=20.f;
            break;
        case AudioReverbPresetID::Hall:
            r.RoomSize=0.85f; r.DecayTime=3.5f; r.Density=0.85f;
            r.WetLevel=0.45f; r.DryLevel=0.75f; r.PreDelayMs=30.f;
            r.LPFCutoff=6000.f;
            break;
        case AudioReverbPresetID::Church:
            r.RoomSize=0.90f; r.DecayTime=5.0f; r.Density=0.90f;
            r.WetLevel=0.50f; r.DryLevel=0.70f; r.PreDelayMs=35.f;
            r.LPFCutoff=5500.f; r.EarlyReflections=0.3f;
            break;
        case AudioReverbPresetID::Cave:
            r.RoomSize=0.80f; r.DecayTime=4.0f; r.Density=0.95f;
            r.WetLevel=0.55f; r.DryLevel=0.65f; r.PreDelayMs=25.f;
            r.LPFCutoff=4000.f; r.Damping=0.3f;
            break;
        case AudioReverbPresetID::Forest:
            r.RoomSize=0.50f; r.DecayTime=1.2f; r.Density=0.40f;
            r.WetLevel=0.15f; r.DryLevel=0.92f; r.PreDelayMs=8.f;
            r.Damping=0.75f; r.LPFCutoff=3000.f;
            break;
        case AudioReverbPresetID::Tunnel:
            r.RoomSize=0.70f; r.DecayTime=2.5f; r.Density=0.90f;
            r.WetLevel=0.60f; r.DryLevel=0.60f; r.PreDelayMs=15.f;
            r.LPFCutoff=5000.f;
            break;
        case AudioReverbPresetID::Underwater:
            r.RoomSize=0.80f; r.DecayTime=2.0f; r.Density=1.0f;
            r.WetLevel=0.70f; r.DryLevel=0.50f; r.PreDelayMs=10.f;
            r.LPFCutoff=500.f; r.HPFCutoff=20.f; r.Damping=0.9f;
            break;
        default:
            break;
    }
    return r;
}
