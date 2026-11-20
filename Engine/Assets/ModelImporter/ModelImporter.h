#pragma once

#include <string>
#include <vector>
#include <memory>
#include "../../../Renderer/Vertex/Vertex.h"
#include "../../../Renderer/Mesh/Mesh.h"
#include "../../Animation/Animation.h"

class Scene;
class Entity;

struct ImportedModelData
{
    std::vector<Vertex> Vertices;
    float Color[4] = { 0.8f, 0.8f, 0.8f, 1.0f };
    std::string DiffuseTexturePath;
    float BoundingRadius = 1.5f;
    std::shared_ptr<SkeletalAnimationAsset> SkeletalAsset;
};

class ModelImporter
{
public:
    static bool IsSupportedModel(const std::string& filepath);

    static std::string OpenFileDialog();

    // Copies an external model (and its .mtl / textures) into Assets/Models.
    static std::string ImportFileToAssets(const std::string& filepath);

    static ImportedModelData LoadOBJ(const std::string& filepath);

    static ImportedModelData LoadGLTF(const std::string& filepath);
    static ImportedModelData LoadFBX(const std::string& filepath);
    static ImportedModelData LoadModelData(const std::string& filepath);

    // Mesh 2D nativo: JSON com vertices [x,y,z,u,v] e triangles [a,b,c].
    static std::vector<Vertex> LoadMesh2DVertices(const std::string& filepath);

    static std::vector<Vertex> LoadOBJVertices(const std::string& filepath);

    static std::shared_ptr<Mesh> LoadMesh(const std::string& filepath);

    static Entity* ImportToScene(const std::string& filepath, Scene& scene, const std::string& entityName = "");
};
