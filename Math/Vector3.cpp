#include "Vector3.h"

Vector3::Vector3()
    : X(0.0f),
    Y(0.0f),
    Z(0.0f)
{
}

Vector3::Vector3(float x, float y, float z)
    : X(x),
    Y(y),
    Z(z)
{
}

Vector3 Vector3::operator+(const Vector3& other) const
{
    return Vector3(
        X + other.X,
        Y + other.Y,
        Z + other.Z
    );
}

Vector3 Vector3::operator-(const Vector3& other) const
{
    return Vector3(
        X - other.X,
        Y - other.Y,
        Z - other.Z
    );
}

Vector3 Vector3::operator*(float scalar) const
{
    return Vector3(
        X * scalar,
        Y * scalar,
        Z * scalar
    );
}