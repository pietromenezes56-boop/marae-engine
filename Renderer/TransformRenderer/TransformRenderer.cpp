#include "TransformRenderer.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <glm/gtc/matrix_transform.hpp>

TransformRenderer::TransformRenderer()
{
}

glm::mat4 TransformRenderer::GetModelMatrix(
    const Transform& transform)
{
    // Use the computed world matrix for rendering
    return transform.WorldMatrix;
}