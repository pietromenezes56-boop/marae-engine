#pragma once

#include "../Vector3.h"

class Matrix4
{
public:
    Matrix4();

    static Matrix4 Identity();
    static Matrix4 Translate(const Vector3& position);

    Matrix4 operator*(const Matrix4& other) const;

    float Elements[16];
};