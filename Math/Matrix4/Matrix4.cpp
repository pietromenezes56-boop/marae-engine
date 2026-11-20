#include "Matrix4.h"

Matrix4::Matrix4()
{
    for (int i = 0; i < 16; i++)
    {
        Elements[i] = 0.0f;
    }

    Elements[0] = 1.0f;
    Elements[5] = 1.0f;
    Elements[10] = 1.0f;
    Elements[15] = 1.0f;
}

Matrix4 Matrix4::Identity()
{
    return Matrix4();
}

Matrix4 Matrix4::Translate(const Vector3& position)
{
    Matrix4 result;

    result.Elements[12] = position.X;
    result.Elements[13] = position.Y;
    result.Elements[14] = position.Z;

    return result;
}

Matrix4 Matrix4::operator*(const Matrix4& other) const
{
    Matrix4 result;

    for (int row = 0; row < 4; row++)
    {
        for (int col = 0; col < 4; col++)
        {
            result.Elements[row * 4 + col] = 0.0f;

            for (int k = 0; k < 4; k++)
            {
                result.Elements[row * 4 + col] +=
                    Elements[row * 4 + k] *
                    other.Elements[k * 4 + col];
            }
        }
    }

    return result;
}