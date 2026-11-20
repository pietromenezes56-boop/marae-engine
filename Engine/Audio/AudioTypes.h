#pragma once
// ---------------------------------------------------------------------------
// AudioTypes.h — tipos públicos leves do sistema de áudio.
// Incluído por qualquer módulo sem puxar a implementação do AudioSystem.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>

// Handle de uma voice em reprodução
using AudioVoiceID = uint32_t;
static constexpr AudioVoiceID AUDIO_INVALID_VOICE = 0;

// Número máximo de voices simultâneas
static constexpr int AUDIO_MAX_VOICES = 64;

// Modelo de atenuação 3D
enum class AudioAttenuationModel : uint8_t
{
    None        = 0,
    Linear      = 1,
    Inverse     = 2,   // padrão OpenAL
    Exponential = 3,
};

// Resultado acústico calculado entre uma fonte e o listener.
// O ganho já combina transmissão, oclusão, obstrução e caminhos difratados.
struct AudioAcousticMix
{
    float Gain = 1.0f;
    float Occlusion = 0.0f;
    float Obstruction = 0.0f;
    float Diffraction = 0.0f;
    float Transmission = 1.0f;
    float Filtering = 0.0f; // intensidade combinada habilitada para o LPF acústico
};
