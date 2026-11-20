#pragma once

#include "../../Renderer/Mesh/Mesh.h"
#include <memory>
#include <string>

class MeshComponent
{
public:
    MeshComponent();

    std::shared_ptr<Mesh> MeshData;

    // Empty means the built-in cube (or a procedural mesh).
    std::string MeshPath;

    // Tipo do primitivo interno. Necessário para reconstruir a geometria
    // corretamente ao recarregar uma cena sem asset externo.
    std::string PrimitiveType = "Cube";
};
