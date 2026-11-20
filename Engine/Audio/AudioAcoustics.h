#pragma once
// ---------------------------------------------------------------------------
// AudioAcoustics — propagação sonora geométrica entre fontes e listener.
//
// Avalia oclusão, obstrução, transmissão e difração aproximada entre uma
// entidade emissora e a posição do listener, percorrendo os obstáculos
// acústicos da cena (AudioAcousticGeometryEnabled + AudioOccluder).
// ---------------------------------------------------------------------------

#include "AudioTypes.h"
#include <glm/glm.hpp>

class Scene;
class Entity;

namespace AudioAcoustics
{
    // Calcula o mix acústico de uma fonte em relação ao listener.
    // Retorna Gain=1 (sem atenuação) quando a propagação está desativada.
    AudioAcousticMix Evaluate(const Scene& scene,
                              const Entity& source,
                              const glm::vec3& listenerPosition);
}
