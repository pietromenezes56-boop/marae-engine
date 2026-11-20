#pragma once
// ---------------------------------------------------------------------------
// AudioComponent.h — alias de compatibilidade para AudioSourceComponent.
//
// O sistema de áudio novo usa AudioSourceComponent (Engine/Audio/Source/).
// Este header mantém compatibilidade com código existente que inclui
// "AudioComponent.h" e usa os campos antigos.
// ---------------------------------------------------------------------------
#include "../../Engine/Audio/Source/AudioSourceComponent.h"
#include "../../Engine/Audio/AudioTypes.h"

// Alias para compatibilidade com código antigo
using AudioComponent = AudioSourceComponent;
