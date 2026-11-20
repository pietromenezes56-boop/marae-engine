#include "MeshComponent.h"
#include <iostream>

MeshComponent::MeshComponent()
    : MeshData(std::make_shared<Mesh>())
{
}