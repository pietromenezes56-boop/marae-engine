#pragma once

#include "../../Math/Vector3.h"

class ColorComponent
{
public:
    ColorComponent()
        : Color(
            1.0f,
            1.0f,
            1.0f)
    {
    }

    Vector3 Color;
};