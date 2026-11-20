#pragma once

#include "Light.h"

class DirectionalLight : public Light
{
public:

    Vector3 Direction =
    {
        -0.4f,
        -1.0f,
        -0.4f
    };
};