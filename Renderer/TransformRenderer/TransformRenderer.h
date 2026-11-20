#pragma once

#include "../../Scene/Transform/Transform.h"

#include <glm/glm.hpp>

class TransformRenderer
{
public:
    TransformRenderer();

    glm::mat4 GetModelMatrix(
        const Transform& transform);
};