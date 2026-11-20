#pragma once
// ---------------------------------------------------------------------------
// AudioEnvironmentComponent — zona acústica com reverb, filtros e occlusion.
//
// Quando o AudioListener entra no volume desta entidade, o AudioSystem
// aplica os parâmetros acústicos desta zona com transição suave.
//
// Occlusion: quando uma fonte de áudio é separada do listener por paredes,
// o sistema aplica LPF e redução de volume proporcionalmente.
// ---------------------------------------------------------------------------
#include "AudioReverbPreset.h"
#include <string>
#include <glm/glm.hpp>

struct AudioEnvironmentComponent
{
    bool   Enabled            = true;

    // ---- Reverb ----------------------------------------------------------
    bool   ReverbEnabled      = true;
    AudioReverbPresetID PresetID = AudioReverbPresetID::MediumRoom;
    AudioReverbParams   Reverb; // usado quando PresetID == Custom

    // Carrega o preset escolhido nos campos Reverb
    void ApplyPreset()
    {
        if (PresetID != AudioReverbPresetID::Custom)
            Reverb = AudioReverbParams::FromPreset(PresetID);
    }

    // ---- Filtros de ambiente ---------------------------------------------
    bool  LPFEnabled          = false;
    float LPFCutoffHz         = 4000.f;  // Hz

    bool  HPFEnabled          = false;
    float HPFCutoffHz         = 80.f;    // Hz

    // ---- Occlusion -------------------------------------------------------
    bool  OcclusionEnabled    = false;
    float OcclusionAmount     = 0.0f;    // 0=sem oclusão, 1=totalmente ocluído
    float OcclusionLPFCutoff  = 800.f;   // Hz aplicado quando ocluído
    float OcclusionVolumeScale= 0.3f;    // volume multiplicador quando ocluído

    // ---- Volume geral da zona --------------------------------------------
    float VolumeScale         = 1.0f;

    // ---- Transição suave -------------------------------------------------
    float TransitionTime      = 1.5f;    // segundos para blend entre zonas

    // ---- Prioridade (zona de maior prioridade vence sobreposições) -------
    int   Priority            = 0;

    // ---- Identificação ---------------------------------------------------
    std::string ZoneName      = "AudioEnvironment";
};
