#pragma once

#include "../../Math/Vector3.h"

class Camera
{
public:
    Camera();

    Vector3 Position;
    Vector3 Rotation;

    float MoveSpeed = 0.03f;

    // Parâmetros da projeção usados quando a câmera representa uma câmera de
    // cena. A câmera fly mantém estes valores padrão.
    float FieldOfView = 60.0f;
    float NearClip = 0.1f;
    float FarClip = 10000.0f;
    bool Orthographic = false;
    float OrthographicSize = 10.0f;
};
