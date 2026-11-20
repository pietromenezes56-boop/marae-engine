#include "CameraRenderer.h"

#include <glm/gtc/matrix_transform.hpp>

CameraRenderer::CameraRenderer()
{
}

glm::mat4 CameraRenderer::GetViewMatrix(
    const Camera& camera)
{
    glm::mat4 view(1.0f);

    view = glm::rotate(
        view,
        glm::radians(camera.Rotation.X),
        glm::vec3(1.0f, 0.0f, 0.0f)
    );

    view = glm::rotate(
        view,
        glm::radians(camera.Rotation.Y),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    view = glm::rotate(
        view,
        glm::radians(camera.Rotation.Z),
        glm::vec3(0.0f, 0.0f, 1.0f)
    );

    view = glm::translate(
        view,
        glm::vec3(
            -camera.Position.X,
            -camera.Position.Y,
            -camera.Position.Z
        )
    );

    return view;
}

glm::mat4 CameraRenderer::GetProjectionMatrix(
    float fov,
    float aspectRatio,
    float nearPlane,
    float farPlane)
{
    return glm::perspective(
        glm::radians(fov),
        aspectRatio,
        nearPlane,
        farPlane
    );
}
