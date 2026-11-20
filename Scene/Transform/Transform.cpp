#include "Transform.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

Transform::Transform()
{
    LocalPosition =
        Vector3(0.0f, 0.0f, 0.0f);

    LocalRotation =
        glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    LocalScale =
        Vector3(1.0f, 1.0f, 1.0f);

    LocalEulerRotation =
        Vector3(0.0f, 0.0f, 0.0f);

    // Compatibilidade
    Position = LocalPosition;
    Rotation = LocalRotation;
    Scale = LocalScale;
    EulerRotation = LocalEulerRotation;
}

void Transform::UpdateLocalMatrix()
{
    // Os campos de compatibilidade são usados pelo Inspector, gizmos e física.
    // Mantemos os campos Local* sincronizados antes de montar a matriz.
    LocalPosition = Position;
    LocalRotation = Rotation;
    LocalScale = Scale;
    LocalEulerRotation = EulerRotation;

    glm::mat4 translation =
        glm::translate(
            glm::mat4(1.0f),
            glm::vec3(
                LocalPosition.X,
                LocalPosition.Y,
                LocalPosition.Z
            )
        );

    glm::mat4 rotation =
        glm::toMat4(
            LocalRotation
        );

    glm::mat4 scale =
        glm::scale(
            glm::mat4(1.0f),
            glm::vec3(
                LocalScale.X,
                LocalScale.Y,
                LocalScale.Z
            )
        );

    LocalMatrix =
        translation *
        rotation *
        scale;
}

void Transform::UpdateWorldMatrix(
    const glm::mat4& parentMatrix
)
{
    WorldMatrix =
        parentMatrix *
        LocalMatrix;
}
