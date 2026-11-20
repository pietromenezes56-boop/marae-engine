#pragma once

#include "Light.h"

class PointLight : public Light
{
public:

    Vector3 Position;

    float Radius = 10.0f;
};