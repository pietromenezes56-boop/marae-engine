#pragma once

#include "CommonStructs.h"
#include "../../../Scene/Scene/Scene.h"
#include "../../../Math/Vector3.h"

class PostProcessingVolumeBlender
{
public:
    static BlendedSettings Blend(const class Scene& scene, const Vector3& cameraWorldPos);

private:
    static void SphereContains(const Vector3& center, float radius, const Vector3& p, float& signedDist);
    static void BoxContains(const Vector3& min, const Vector3& max, const Vector3& p, float& signedDist);
    static float Smoothstep(float edge0, float edge1, float x);
};
