#pragma once

#include "CommonStructs.h"
#include "../../../Math/Vector3.h"
#include "../../../Scene/Components/PostProcessingVolumeComponent.h"
#include <vector>

class Entity;
class Scene;

class VolumeBlender
{
public:
    static BlendedSettings Evaluate(
        const Scene& scene,
        const Vector3& cameraWorldPosition
    );

    static float ComputeVolumeWeight(
        const PostProcessingVolumeComponent& vol,
        const Vector3& volumeWorldPos,
        const Vector3& cameraWorldPos
    );
};
