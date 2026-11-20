#pragma once

#include "../../Math/Vector3.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

class Transform
{
public:

    Transform();

    // Local Transform
    Vector3 LocalPosition;

    glm::quat LocalRotation;

    Vector3 LocalScale;

    Vector3 LocalEulerRotation;

    // Compatibilidade (serão removidos no futuro)
    Vector3 Position;

    glm::quat Rotation;

    Vector3 Scale;

    Vector3 EulerRotation;

    // Matrizes (vamos usar depois)
    glm::mat4 LocalMatrix =
        glm::mat4(1.0f);

    glm::mat4 WorldMatrix =
        glm::mat4(1.0f);

    // Update local matrix from Position/Rotation/Scale
    void UpdateLocalMatrix();

    // Update world matrix using parent matrix
    void UpdateWorldMatrix(const glm::mat4& parentMatrix);
};