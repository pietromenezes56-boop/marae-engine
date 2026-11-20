#pragma once

#include "../../Scene/Camera/Camera.h"

#include <glm/glm.hpp>

class CameraRenderer
{
public:
    CameraRenderer();

    glm::mat4 GetViewMatrix(
        const Camera& camera);

    glm::mat4 GetProjectionMatrix(
        float fov,
        float aspectRatio,
        float nearPlane,
        float farPlane);
};