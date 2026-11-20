#pragma once

#include "Mesh.h"
#include <memory>
#include <string>
#include <unordered_map>

class MeshCache
{
public:
    static std::shared_ptr<Mesh> GetCube();
    // Quad imutável compartilhado por sprites 2D; o Modelador usa copy-on-write.
    static std::shared_ptr<Mesh> GetQuad();

    // Cada entidade precisa possuir uma malha própria: o modo Modelador pode
    // editar os vértices sem deformar todos os cubos da cena.
    static std::shared_ptr<Mesh> CreateCubeInstance();

    static std::shared_ptr<Mesh> Load(const std::string& path);

    // Remove uma entrada regravada pelo Modelador para que o próximo Load
    // leia o arquivo atualizado, inclusive ao reabrir a cena na mesma sessão.
    static void Invalidate(const std::string& path);

    static void Clear();

private:
    static std::string NormalizePath(const std::string& path);

    // Cache entries do not own imported geometry; scene entities/prefabs keep
    // shared ownership while a mesh is actually in use.
    static std::unordered_map<std::string, std::weak_ptr<Mesh>> s_Meshes;
    static std::shared_ptr<Mesh> s_QuadMesh;
};
