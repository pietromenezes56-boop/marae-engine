#include "AudioAcoustics.h"

#include "../../Scene/Scene/Scene.h"
#include "../../Scene/Entity/Entity.h"
#include "../../Scene/Components/AudioComponent.h"

#include <algorithm>
#include <cmath>

namespace
{
    bool SegmentIntersectsAABB(const glm::vec3& start,
                               const glm::vec3& end,
                               const glm::vec3& boxMin,
                               const glm::vec3& boxMax)
    {
        const glm::vec3 direction = end - start;
        float tMin = 0.0f;
        float tMax = 1.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(direction[axis]) < 0.00001f)
            {
                if (start[axis] < boxMin[axis] || start[axis] > boxMax[axis])
                    return false;
                continue;
            }
            const float invDirection = 1.0f / direction[axis];
            float t1 = (boxMin[axis] - start[axis]) * invDirection;
            float t2 = (boxMax[axis] - start[axis]) * invDirection;
            if (t1 > t2) std::swap(t1, t2);
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) return false;
        }
        return tMax >= 0.0f && tMin <= 1.0f;
    }
}

namespace AudioAcoustics
{
    AudioAcousticMix Evaluate(const Scene& scene,
                              const Entity& source,
                              const glm::vec3& listenerPosition)
    {
        AudioAcousticMix mix;
        const AudioComponent& audio = const_cast<Entity&>(source).GetAudioComponent();
        if (!audio.AcousticPropagationEnabled || !audio.GeometricAcousticsEnabled ||
            !audio.AcousticRayTracingEnabled)
            return mix;

        const Transform& sourceTransform = const_cast<Entity&>(source).GetTransform();
        const glm::vec3 sourcePosition(sourceTransform.Position.X,
                                        sourceTransform.Position.Y,
                                        sourceTransform.Position.Z);

        for (const Entity& obstacle : scene.GetEntities())
        {
            if (&obstacle == &source || !obstacle.Active || obstacle.IsEditorOnly() ||
                !obstacle.AudioAcousticGeometryEnabled || !obstacle.AudioOccluder ||
                obstacle.HasUI())
                continue;

            const Transform& obstacleTransform = const_cast<Entity&>(obstacle).GetTransform();
            const glm::vec3 center(obstacleTransform.Position.X,
                                   obstacleTransform.Position.Y,
                                   obstacleTransform.Position.Z);
            glm::vec3 halfExtents(std::max(0.05f, std::abs(obstacleTransform.Scale.X) * 0.5f),
                                  std::max(0.05f, std::abs(obstacleTransform.Scale.Y) * 0.5f),
                                  std::max(0.05f, std::abs(obstacleTransform.Scale.Z) * 0.5f));
            const glm::vec3 boxMin = center - halfExtents;
            const glm::vec3 boxMax = center + halfExtents;
            if (!SegmentIntersectsAABB(sourcePosition, listenerPosition, boxMin, boxMax))
                continue;

            const float absorption = std::clamp(obstacle.AudioAbsorption, 0.0f, 1.0f);
            const float transmission = std::clamp(obstacle.AudioTransmission, 0.0f, 1.0f);
            mix.Obstruction = std::max(mix.Obstruction, absorption);
            mix.Diffraction = std::max(mix.Diffraction,
                                       std::clamp(obstacle.AudioDiffraction, 0.0f, 1.0f));

            if (obstacle.AudioTransmissionEnabled)
            {
                mix.Transmission *= std::clamp(1.0f - absorption * (1.0f - transmission), 0.05f, 1.0f);
            }
            else
            {
                mix.Occlusion = std::max(mix.Occlusion, absorption);
                mix.Transmission *= std::clamp(1.0f - absorption, 0.03f, 1.0f);
            }
        }

        if (mix.Occlusion > 0.0f && audio.OcclusionEnabled)
            mix.Gain *= 1.0f - mix.Occlusion * 0.75f;
        if (mix.Obstruction > 0.0f && audio.ObstructionEnabled)
            mix.Gain *= 1.0f - mix.Obstruction * 0.35f;
        if (audio.OcclusionEnabled)
            mix.Filtering = std::max(mix.Filtering, mix.Occlusion);
        if (audio.ObstructionEnabled)
            mix.Filtering = std::max(mix.Filtering, mix.Obstruction * 0.55f);
        if (audio.DiffractionEnabled && audio.AcousticPathfindingEnabled && mix.Diffraction > 0.0f)
        {
            // Estimativa de caminho alternativo: recupera parte do sinal sem
            // fingir que a parede deixou de existir.
            const float diffractedPath = 0.18f + 0.42f * mix.Diffraction;
            mix.Gain = std::max(mix.Gain, diffractedPath);
        }
        if (audio.DynamicPropagationEnabled)
            mix.Gain *= mix.Transmission;
        else
            mix.Gain = std::min(mix.Gain, 1.0f);
        mix.Gain = std::clamp(mix.Gain, 0.0f, 1.0f);
        return mix;
    }
}
