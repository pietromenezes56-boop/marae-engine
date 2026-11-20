#pragma once
// ---------------------------------------------------------------------------
// AudioDebugStats — estatísticas coletadas a cada frame pelo AudioSystem.
// Usadas pelo AudioDebugger panel no editor.
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <cstdint>

struct AudioVoiceDebugInfo
{
    uint32_t    VoiceID      = 0;
    std::string ClipName;
    std::string GroupID;
    float       Volume       = 1.f;
    float       Pitch        = 1.f;
    float       Distance     = 0.f;   // distância ao listener
    bool        Is3D         = false;
    bool        Paused       = false;
    bool        Looping      = false;
};

struct AudioDebugStats
{
    int   ActiveVoices      = 0;
    int   MaxVoices         = 0;
    int   VoicesThisFrame   = 0;   // novas voices criadas neste frame
    int   VoicesStolenThisFrame = 0;

    float MasterVolume      = 1.f;
    float EnvironmentWet    = 0.f;
    float OcclusionAmount   = 0.f;
    float LPFCutoffHz       = 22000.f;
    float HPFCutoffHz       = 20.f;

    std::string ActiveEnvironmentZone;

    std::vector<AudioVoiceDebugInfo> Voices;

    void Reset()
    {
        ActiveVoices       = 0;
        VoicesThisFrame    = 0;
        VoicesStolenThisFrame = 0;
        EnvironmentWet     = 0.f;
        OcclusionAmount    = 0.f;
        Voices.clear();
    }
};
