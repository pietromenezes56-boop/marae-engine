#pragma once

#include "Light.h"

class SpotLight : public Light
{
public:

    Vector3 Position;

    Vector3 Direction =
    {
        0,
        -1,
        0
    };

    float Radius = 10.0f;

    float Cutoff = 20.0f;
};