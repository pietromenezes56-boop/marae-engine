#include "ModelImporter.h"
#include "../../../Scene/Scene/Scene.h"
#include "../../../Scene/Entity/Entity.h"
#include "../../../Renderer/Mesh/MeshCache.h"
#include "../../../Renderer/Texture/TextureCache.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <cctype>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include "../../../ThirdParty/json/json.hpp"

using json = nlohmann::json;

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define GUID Windows_SDK_GUID
#include <windows.h>
#include <commdlg.h>
#undef GUID
#pragma comment(lib, "comdlg32.lib")
#endif

namespace fs = std::filesystem;

struct OBJIndex
{
    int v = 0;
    int vt = 0;
    int vn = 0;
};

static std::string ToLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static std::string Trim(const std::string& value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
        ++start;

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;

    return value.substr(start, end - start);
}

static std::string GetExtension(const std::string& filepath)
{
    return ToLowerCopy(fs::path(filepath).extension().string());
}

static bool IsSafeRelativeAssetURI(const std::string& uri)
{
    const fs::path path(uri);
    if (path.empty() || path.is_absolute()) return false;
    const fs::path normalized = path.lexically_normal();
    for (const auto& part : normalized)
        if (part == "..") return false;
    return true;
}

static std::string ResolveExistingPath(const std::string& filepath)
{
    if (fs::exists(filepath))
        return filepath;

    const std::string candidates[] =
    {
        "MaraeEngine/" + filepath,
        "../MaraeEngine/" + filepath,
        "Assets/Models/" + filepath,
        "MaraeEngine/Assets/Models/" + filepath,
        std::string("Assets/Models/") + fs::path(filepath).filename().string()
    };

    for (const auto& candidate : candidates)
    {
        if (fs::exists(candidate))
            return candidate;
    }

    return filepath;
}

static OBJIndex ParseOBJIndex(const std::string& token)
{
    OBJIndex idx;
    size_t firstSlash = token.find('/');
    if (firstSlash == std::string::npos)
    {
        idx.v = std::stoi(token);
        return idx;
    }

    idx.v = std::stoi(token.substr(0, firstSlash));

    size_t secondSlash = token.find('/', firstSlash + 1);
    if (secondSlash == std::string::npos)
    {
        std::string vtStr = token.substr(firstSlash + 1);
        if (!vtStr.empty())
            idx.vt = std::stoi(vtStr);
        return idx;
    }

    std::string vtStr = token.substr(firstSlash + 1, secondSlash - (firstSlash + 1));
    if (!vtStr.empty())
        idx.vt = std::stoi(vtStr);

    std::string vnStr = token.substr(secondSlash + 1);
    if (!vnStr.empty())
        idx.vn = std::stoi(vnStr);

    return idx;
}

static void ParseMTL(
    const std::string& mtlPath,
    ImportedModelData& result)
{
    std::ifstream file(mtlPath);
    if (!file.is_open())
        return;

    std::string line;
    while (std::getline(file, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream ss(line);
        std::string prefix;
        ss >> prefix;
        prefix = ToLowerCopy(prefix);

        if (prefix == "kd")
        {
            ss >> result.Color[0] >> result.Color[1] >> result.Color[2];
            result.Color[3] = 1.0f;
        }
        else if (prefix == "map_kd")
        {
            std::string rest;
            std::getline(ss, rest);
            rest = Trim(rest);
            if (rest.empty())
                continue;

            fs::path texturePath(rest);
            if (texturePath.is_relative())
                texturePath = fs::path(mtlPath).parent_path() / texturePath;

            result.DiffuseTexturePath = texturePath.lexically_normal().string();
        }
    }
}

static void CopyIfExists(const fs::path& source, const fs::path& destination)
{
    std::error_code error;
    if (!fs::exists(source, error))
        return;

    if (fs::equivalent(source, destination, error))
        return;

    fs::create_directories(destination.parent_path(), error);
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, error);
}

bool ModelImporter::IsSupportedModel(const std::string& filepath)
{
    const std::string extension = GetExtension(filepath);
    return extension == ".obj" || extension == ".mesh2d" ||
           extension == ".fbx" || extension == ".gltf" || extension == ".glb";
}

std::string ModelImporter::OpenFileDialog()
{
#ifdef _WIN32
    char filename[MAX_PATH] = {};

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter =
        "3D Models (*.fbx;*.gltf;*.glb;*.obj)\0*.fbx;*.gltf;*.glb;*.obj\0"
        "2D Meshes (*.mesh2d)\0*.mesh2d\0"
        "All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Import Mesh 2D / Modelo";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn))
        return std::string(filename);
#endif

    return {};
}

std::string ModelImporter::ImportFileToAssets(const std::string& filepath)
{
    const std::string resolved = ResolveExistingPath(filepath);
    if (!fs::exists(resolved))
    {
        std::cout << "[ModelImporter] File not found: " << filepath << std::endl;
        return {};
    }

    if (!IsSupportedModel(resolved))
    {
        std::cout << "[ModelImporter] Unsupported format: " << resolved << std::endl;
        return {};
    }

    const fs::path source(resolved);
    const fs::path assetsRoot("Assets");
    const fs::path modelsDir = assetsRoot / "Models";
    const fs::path meshes2DDir = assetsRoot / "Meshes2D";

    std::error_code error;
    const fs::path canonicalSource = fs::weakly_canonical(source, error);
    const fs::path canonicalAssets = fs::weakly_canonical(assetsRoot, error);

    bool alreadyInAssets = false;
    if (!error)
    {
        const fs::path relative = fs::relative(canonicalSource, canonicalAssets, error);
        alreadyInAssets = !error && !relative.empty() && relative.string().find("..") == std::string::npos;
    }

    if (alreadyInAssets && GetExtension(source.string()) != ".fbx")
        return source.lexically_normal().string();

    const fs::path destinationDir = GetExtension(source.string()) == ".mesh2d" ? meshes2DDir : modelsDir;
    fs::create_directories(destinationDir, error);
    const fs::path destObj = destinationDir / source.filename();
    CopyIfExists(source, destObj);
    if (GetExtension(source.string()) == ".mesh2d")
        return destObj.lexically_normal().string();

    std::ifstream objFile(source);
    std::string line;
    while (objFile.is_open() && std::getline(objFile, line))
    {
        std::istringstream ss(Trim(line));
        std::string prefix;
        ss >> prefix;
        if (ToLowerCopy(prefix) != "mtllib")
            continue;

        std::string mtlName;
        std::getline(ss, mtlName);
        mtlName = Trim(mtlName);
        if (mtlName.empty())
            continue;

        const fs::path sourceMtl = source.parent_path() / mtlName;
        const fs::path destMtl = modelsDir / fs::path(mtlName).filename();
        CopyIfExists(sourceMtl, destMtl);

        std::ifstream mtlFile(sourceMtl);
        std::string mtlLine;
        while (mtlFile.is_open() && std::getline(mtlFile, mtlLine))
        {
            std::istringstream mtlStream(Trim(mtlLine));
            std::string mtlPrefix;
            mtlStream >> mtlPrefix;
            if (ToLowerCopy(mtlPrefix) != "map_kd")
                continue;

            std::string textureName;
            std::getline(mtlStream, textureName);
            textureName = Trim(textureName);
            if (textureName.empty())
                continue;

            const fs::path sourceTex = source.parent_path() / textureName;
            const fs::path destTex = modelsDir / fs::path(textureName).filename();
            CopyIfExists(sourceTex, destTex);
        }
    }

    const std::string extension = GetExtension(source.string());
    if (extension == ".gltf")
    {
        try
        {
            std::ifstream gltfFile(source);
            json document;
            gltfFile >> document;
            auto copyURI = [&](const json& item)
            {
                const std::string uri = item.value("uri", "");
                if (!uri.empty() && uri.rfind("data:", 0) != 0)
                if (IsSafeRelativeAssetURI(uri))
                    CopyIfExists(source.parent_path() / fs::path(uri), modelsDir / fs::path(uri).filename());
            };
            for (const auto& buffer : document.value("buffers", json::array())) copyURI(buffer);
            for (const auto& image : document.value("images", json::array())) copyURI(image);
        }
        catch (...) {}
    }
    else if (extension == ".fbx")
    {
        // FBX costuma referenciar texturas por caminho relativo. Copiamos as
        // imagens vizinhas para o diretório do asset, onde o Blender poderá
        // resolvê-las durante a conversão para glTF.
        for (const auto& entry : fs::directory_iterator(source.parent_path(), error))
        {
            if (!entry.is_regular_file()) continue;
            const std::string sidecarExt = GetExtension(entry.path().string());
            if (sidecarExt == ".png" || sidecarExt == ".jpg" || sidecarExt == ".jpeg" ||
                sidecarExt == ".tga" || sidecarExt == ".bmp" || sidecarExt == ".tif" || sidecarExt == ".tiff")
                CopyIfExists(entry.path(), modelsDir / entry.path().filename());
        }
        const fs::path copiedFbx = alreadyInAssets ? source : destObj;
        const fs::path converted = copiedFbx.parent_path() / (copiedFbx.stem().string() + ".marae.glb");
        const ImportedModelData convertedData = LoadFBX(copiedFbx.string());
        if (convertedData.Vertices.empty() || !fs::exists(converted))
        {
            std::cout << "[ModelImporter] FBX não gerou uma malha utilizável: " << copiedFbx << std::endl;
            return {};
        }
        const fs::path nativeAsset = modelsDir / (source.stem().string() + ".glb");
        CopyIfExists(converted, nativeAsset);
        if (!fs::exists(nativeAsset)) return {};
        return nativeAsset.lexically_normal().string();
    }

    return destObj.lexically_normal().string();
}

ImportedModelData ModelImporter::LoadOBJ(const std::string& filepath)
{
    ImportedModelData result;

    const std::string targetPath = ResolveExistingPath(filepath);

    std::ifstream file(targetPath);
    if (!file.is_open())
    {
        std::cout << "[ModelImporter] Could not open OBJ file: " << filepath << std::endl;
        return result;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texCoords;
    const fs::path directory = fs::path(targetPath).parent_path();

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
            continue;

        std::istringstream ss(line);
        std::string prefix;
        ss >> prefix;

        if (prefix == "mtllib")
        {
            std::string mtlName;
            std::getline(ss, mtlName);
            mtlName = Trim(mtlName);
            if (!mtlName.empty())
                ParseMTL((directory / mtlName).string(), result);
        }
        else if (prefix == "v")
        {
            glm::vec3 pos;
            ss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        }
        else if (prefix == "vn")
        {
            glm::vec3 norm;
            ss >> norm.x >> norm.y >> norm.z;
            normals.push_back(norm);
        }
        else if (prefix == "vt")
        {
            glm::vec2 uv;
            ss >> uv.x >> uv.y;
            uv.y = 1.0f - uv.y;
            texCoords.push_back(uv);
        }
        else if (prefix == "f")
        {
            std::vector<OBJIndex> faceIndices;
            std::string token;
            while (ss >> token)
            {
                if (token.empty())
                    continue;

                try
                {
                    faceIndices.push_back(ParseOBJIndex(token));
                }
                catch (...)
                {
                }
            }

            if (faceIndices.size() < 3)
                continue;

            for (size_t i = 1; i + 1 < faceIndices.size(); ++i)
            {
                OBJIndex tri[3] = { faceIndices[0], faceIndices[i], faceIndices[i + 1] };
                Vertex v[3];

                for (int k = 0; k < 3; ++k)
                {
                    int pIdx = (tri[k].v > 0) ? tri[k].v - 1 : (int)positions.size() + tri[k].v;
                    int tIdx = (tri[k].vt > 0) ? tri[k].vt - 1 : (int)texCoords.size() + tri[k].vt;
                    int nIdx = (tri[k].vn > 0) ? tri[k].vn - 1 : (int)normals.size() + tri[k].vn;

                    glm::vec3 p = (pIdx >= 0 && pIdx < (int)positions.size()) ? positions[pIdx] : glm::vec3(0.0f);
                    glm::vec2 uv = (tIdx >= 0 && tIdx < (int)texCoords.size()) ? texCoords[tIdx] : glm::vec2(0.0f);
                    glm::vec3 n = (nIdx >= 0 && nIdx < (int)normals.size()) ? normals[nIdx] : glm::vec3(0.0f, 1.0f, 0.0f);

                    v[k].X = p.x;
                    v[k].Y = p.y;
                    v[k].Z = p.z;
                    v[k].NX = n.x;
                    v[k].NY = n.y;
                    v[k].NZ = n.z;
                    v[k].U = uv.x;
                    v[k].V = uv.y;
                }

                if (tri[0].vn == 0 && tri[1].vn == 0 && tri[2].vn == 0)
                {
                    glm::vec3 p0(v[0].X, v[0].Y, v[0].Z);
                    glm::vec3 p1(v[1].X, v[1].Y, v[1].Z);
                    glm::vec3 p2(v[2].X, v[2].Y, v[2].Z);

                    glm::vec3 faceNorm = glm::cross(p1 - p0, p2 - p0);
                    float len = glm::length(faceNorm);
                    if (len > 0.0001f)
                        faceNorm /= len;
                    else
                        faceNorm = glm::vec3(0.0f, 1.0f, 0.0f);

                    for (int k = 0; k < 3; ++k)
                    {
                        v[k].NX = faceNorm.x;
                        v[k].NY = faceNorm.y;
                        v[k].NZ = faceNorm.z;
                    }
                }

                result.Vertices.push_back(v[0]);
                result.Vertices.push_back(v[1]);
                result.Vertices.push_back(v[2]);
            }
        }
    }

    float maxRadius = 0.0f;
    for (const Vertex& vertex : result.Vertices)
    {
        const float radius = std::sqrt(vertex.X * vertex.X + vertex.Y * vertex.Y + vertex.Z * vertex.Z);
        if (radius > maxRadius)
            maxRadius = radius;
    }

    if (maxRadius > 0.0001f)
        result.BoundingRadius = maxRadius;

    std::cout << "[ModelImporter] Loaded " << result.Vertices.size()
        << " vertices from " << targetPath << std::endl;

    return result;
}

std::vector<Vertex> ModelImporter::LoadMesh2DVertices(const std::string& filepath)
{
    std::vector<Vertex> result;
    std::ifstream file(ResolveExistingPath(filepath));
    if (!file.is_open()) return result;
    try
    {
        json root; file >> root;
        const auto& points = root.at("vertices");
        const auto& triangles = root.at("triangles");
        std::vector<Vertex> source;
        for (const auto& point : points)
        {
            source.emplace_back(point.value("x", 0.0f), point.value("y", 0.0f), 0.0f,
                                0.0f, 0.0f, 1.0f, point.value("u", 0.0f), point.value("v", 0.0f));
        }
        for (const auto& triangle : triangles)
        {
            if (!triangle.is_array() || triangle.size() < 3) continue;
            const int a = triangle[0].get<int>(), b = triangle[1].get<int>(), c = triangle[2].get<int>();
            if (a < 0 || b < 0 || c < 0 || a >= static_cast<int>(source.size()) ||
                b >= static_cast<int>(source.size()) || c >= static_cast<int>(source.size())) continue;
            result.push_back(source[a]); result.push_back(source[b]); result.push_back(source[c]);
        }
    }
    catch (const std::exception& error)
    {
        std::cout << "[ModelImporter] Mesh2D invÃ¡lido: " << error.what() << std::endl;
    }
    return result;
}

std::vector<Vertex> ModelImporter::LoadOBJVertices(const std::string& filepath)
{
    return LoadOBJ(filepath).Vertices;
}

std::shared_ptr<Mesh> ModelImporter::LoadMesh(const std::string& filepath)
{
    if (!IsSupportedModel(filepath))
    {
        std::cout << "[ModelImporter] Unsupported format: " << filepath << std::endl;
        return nullptr;
    }

    const std::vector<Vertex> vertices = LoadModelData(filepath).Vertices;
    if (vertices.empty())
        return nullptr;

    auto mesh = std::make_shared<Mesh>();
    mesh->Create(vertices, GL_TRIANGLES, filepath.c_str());
    return mesh;
}

Entity* ModelImporter::ImportToScene(const std::string& filepath, Scene& scene, const std::string& entityName)
{
    const std::string assetPath = ImportFileToAssets(filepath);
    if (assetPath.empty())
        return nullptr;

    auto mesh = MeshCache::Load(assetPath);
    if (!mesh || !mesh->IsValid())
        return nullptr;

    const ImportedModelData data = LoadModelData(assetPath);

    Entity& entity = scene.CreateEntity();

    std::string name = entityName;
    if (name.empty())
    {
        name = fs::path(assetPath).stem().string();
        if (name.empty())
            name = "ImportedModel";
    }

    entity.SetName(name);
    entity.GetMeshComponent().MeshData = mesh;
    entity.GetMeshComponent().MeshPath = assetPath;
    entity.GetTransform().Position = Vector3(0.0f, 0.0f, 0.0f);
    entity.GetTransform().Scale = Vector3(1.0f, 1.0f, 1.0f);

    entity.GetColorComponent().Color = Vector3(data.Color[0], data.Color[1], data.Color[2]);
    entity.GetMaterialComponent().MaterialData.Color[0] = data.Color[0];
    entity.GetMaterialComponent().MaterialData.Color[1] = data.Color[1];
    entity.GetMaterialComponent().MaterialData.Color[2] = data.Color[2];
    entity.GetMaterialComponent().MaterialData.Color[3] = data.Color[3];
    entity.GetMaterialComponent().MaterialGUID = "";

    if (!data.DiffuseTexturePath.empty())
    {
        entity.GetMaterialComponent().MaterialData.TexturePath = data.DiffuseTexturePath;
        entity.GetMaterialComponent().MaterialData.TextureName =
            fs::path(data.DiffuseTexturePath).stem().string();
        entity.GetMaterialComponent().MaterialData.DiffuseTexture =
            TextureCache::Load(data.DiffuseTexturePath);
    }

    entity.PickingRadius = data.BoundingRadius;
    if (data.SkeletalAsset && !data.SkeletalAsset->Clips.empty())
    {
        AnimationComponent& animation = entity.GetAnimationComponent();
        animation.ImportedAsset = data.SkeletalAsset;
        animation.Clip = data.SkeletalAsset->Clips.front();
        animation.ImportedClipName = animation.Clip->Name;
        animation.PlayOnStart = true;
    }
    return &entity;
}

namespace
{
    struct GltfBuffer
    {
        std::vector<unsigned char> Bytes;
    };

    int GltfComponentCount(const std::string& type)
    {
        if (type == "SCALAR") return 1;
        if (type == "VEC2") return 2;
        if (type == "VEC3") return 3;
        if (type == "VEC4") return 4;
        if (type == "MAT2") return 4;
        if (type == "MAT3") return 9;
        if (type == "MAT4") return 16;
        return 1;
    }

    int GltfComponentSize(int componentType)
    {
        switch (componentType)
        {
        case 5121: return 1; // UNSIGNED_BYTE
        case 5123: return 2; // UNSIGNED_SHORT
        case 5125: return 4; // UNSIGNED_INT
        case 5126: return 4; // FLOAT
        default: return 0;
        }
    }

    bool ReadGltfValues(const json& root, const std::vector<GltfBuffer>& buffers,
                        int accessorIndex, size_t elementIndex, float* output, int outputCount)
    {
        if (!root.contains("accessors") || accessorIndex < 0 ||
            accessorIndex >= static_cast<int>(root["accessors"].size()))
            return false;
        const json& accessor = root["accessors"][accessorIndex];
        const int viewIndex = accessor.value("bufferView", -1);
        if (viewIndex < 0 || !root.contains("bufferViews") ||
            viewIndex >= static_cast<int>(root["bufferViews"].size()))
            return false;
        const json& view = root["bufferViews"][viewIndex];
        const int bufferIndex = view.value("buffer", -1);
        if (bufferIndex < 0 || bufferIndex >= static_cast<int>(buffers.size()))
            return false;

        const int componentType = accessor.value("componentType", 0);
        const int componentSize = GltfComponentSize(componentType);
        const int componentCount = GltfComponentCount(accessor.value("type", "SCALAR"));
        if (componentSize == 0 || componentCount <= 0)
            return false;

        const size_t viewOffset = view.value("byteOffset", 0u);
        const size_t accessorOffset = accessor.value("byteOffset", 0u);
        const size_t tightStride = static_cast<size_t>(componentSize * componentCount);
        const size_t stride = view.value("byteStride", tightStride);
        const size_t offset = viewOffset + accessorOffset + elementIndex * stride;
        const auto& bytes = buffers[bufferIndex].Bytes;
        if (offset + tightStride > bytes.size())
            return false;

        for (int component = 0; component < outputCount; ++component)
            output[component] = 0.0f;
        const int available = std::min(componentCount, outputCount);
        for (int component = 0; component < available; ++component)
        {
            const unsigned char* source = bytes.data() + offset + component * componentSize;
            float value = 0.0f;
            if (componentType == 5126)
                std::memcpy(&value, source, sizeof(float));
            else if (componentType == 5121)
                value = accessor.value("normalized", false) ? (*source / 255.0f) : static_cast<float>(*source);
            else if (componentType == 5123)
            {
                uint16_t v = 0; std::memcpy(&v, source, sizeof(v));
                value = accessor.value("normalized", false) ? (v / 65535.0f) : static_cast<float>(v);
            }
            else if (componentType == 5125)
            {
                uint32_t v = 0; std::memcpy(&v, source, sizeof(v));
                value = static_cast<float>(v);
            }
            output[component] = value;
        }
        return true;
    }

    uint32_t ReadGltfIndex(const json& root, const std::vector<GltfBuffer>& buffers,
                           int accessorIndex, size_t elementIndex)
    {
        float value = 0.0f;
        if (!ReadGltfValues(root, buffers, accessorIndex, elementIndex, &value, 1))
            return 0;
        return static_cast<uint32_t>(value);
    }

    bool ReadGltfMatrix(const json& root, const std::vector<GltfBuffer>& buffers,
                        int accessorIndex, size_t elementIndex, glm::mat4& matrix)
    {
        float values[16]{};
        if (!ReadGltfValues(root, buffers, accessorIndex, elementIndex, values, 16))
            return false;
        std::memcpy(&matrix[0][0], values, sizeof(values));
        return true;
    }

    std::vector<unsigned char> DecodeBase64(const std::string& input)
    {
        static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::vector<unsigned char> output;
        int value = 0, bits = -8;
        for (unsigned char c : input)
        {
            if (c == '=') break;
            const size_t index = alphabet.find(c);
            if (index == std::string::npos) continue;
            value = (value << 6) + static_cast<int>(index);
            bits += 6;
            if (bits >= 0)
            {
                output.push_back(static_cast<unsigned char>((value >> bits) & 0xFF));
                bits -= 8;
            }
        }
        return output;
    }

    std::vector<unsigned char> ReadBinaryFile(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return {};
        return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), {});
    }

    bool LoadGltfBufferURI(const std::string& uri, const fs::path& directory,
                           std::vector<unsigned char>& output)
    {
        if (uri.rfind("data:", 0) == 0)
        {
            const size_t comma = uri.find(',');
            if (comma == std::string::npos) return false;
            output = DecodeBase64(uri.substr(comma + 1));
            return !output.empty();
        }
        if (!IsSafeRelativeAssetURI(uri)) return false;
        output = ReadBinaryFile(directory / fs::path(uri));
        return !output.empty();
    }

    std::string ImageMimeExtension(const std::string& mime)
    {
        if (mime.find("jpeg") != std::string::npos) return ".jpg";
        if (mime.find("png") != std::string::npos) return ".png";
        return ".bin";
    }
}

ImportedModelData ModelImporter::LoadGLTF(const std::string& filepath)
{
    ImportedModelData result;
    const fs::path source = fs::path(ResolveExistingPath(filepath));
    const std::vector<unsigned char> fileBytes = ReadBinaryFile(source);
    if (fileBytes.empty()) return result;

    json root;
    std::vector<unsigned char> glbBinary;
    try
    {
        if (fileBytes.size() >= 12 && std::memcmp(fileBytes.data(), "glTF", 4) == 0)
        {
            size_t cursor = 12;
            while (cursor + 8 <= fileBytes.size())
            {
                uint32_t chunkLength = 0, chunkType = 0;
                std::memcpy(&chunkLength, fileBytes.data() + cursor, 4);
                std::memcpy(&chunkType, fileBytes.data() + cursor + 4, 4);
                cursor += 8;
                if (cursor + chunkLength > fileBytes.size()) break;
                if (chunkType == 0x4E4F534A)
                    root = json::parse(reinterpret_cast<const char*>(fileBytes.data() + cursor),
                                       reinterpret_cast<const char*>(fileBytes.data() + cursor + chunkLength));
                else if (chunkType == 0x004E4942)
                    glbBinary.assign(fileBytes.begin() + cursor, fileBytes.begin() + cursor + chunkLength);
                cursor += chunkLength;
            }
        }
        else
        {
            root = json::parse(fileBytes.begin(), fileBytes.end());
        }
    }
    catch (const std::exception& error)
    {
        std::cout << "[ModelImporter] glTF inválido: " << error.what() << std::endl;
        return result;
    }

    std::vector<GltfBuffer> buffers;
    for (size_t index = 0; root.contains("buffers") && index < root["buffers"].size(); ++index)
    {
        GltfBuffer buffer;
        const json& descriptor = root["buffers"][index];
        const std::string uri = descriptor.value("uri", "");
        if (uri.empty()) buffer.Bytes = glbBinary;
        else LoadGltfBufferURI(uri, source.parent_path(), buffer.Bytes);
        buffers.push_back(std::move(buffer));
    }

    if (root.contains("materials") && !root["materials"].empty())
    {
        const json& material = root["materials"][0];
        const json& pbr = material.value("pbrMetallicRoughness", json::object());
        if (pbr.contains("baseColorFactor") && pbr["baseColorFactor"].is_array())
            for (size_t i = 0; i < 4 && i < pbr["baseColorFactor"].size(); ++i)
                result.Color[i] = pbr["baseColorFactor"][i].get<float>();

        const int textureIndex = pbr.value("baseColorTexture", json::object()).value("index", -1);
        if (textureIndex >= 0 && root.contains("textures") && textureIndex < static_cast<int>(root["textures"].size()))
        {
            const int imageIndex = root["textures"][textureIndex].value("source", -1);
            if (imageIndex >= 0 && root.contains("images") && imageIndex < static_cast<int>(root["images"].size()))
            {
                const json& image = root["images"][imageIndex];
                const std::string uri = image.value("uri", "");
                if (!uri.empty() && uri.rfind("data:", 0) != 0 && IsSafeRelativeAssetURI(uri))
                    result.DiffuseTexturePath = (source.parent_path() / fs::path(uri)).lexically_normal().string();
            }
        }
    }

    if (!root.contains("meshes")) return result;
    for (const auto& mesh : root["meshes"])
    {
        for (const auto& primitive : mesh.value("primitives", json::array()))
        {
            if (primitive.value("mode", 4) != 4) continue;
            const json attributes = primitive.value("attributes", json::object());
            const int positionAccessor = attributes.value("POSITION", -1);
            if (positionAccessor < 0 || positionAccessor >= static_cast<int>(root["accessors"].size())) continue;
            const size_t vertexCount = root["accessors"][positionAccessor].value("count", 0u);
            const int normalAccessor = attributes.value("NORMAL", -1);
            const int uvAccessor = attributes.value("TEXCOORD_0", -1);
            const int indexAccessor = primitive.value("indices", -1);
            const int jointsAccessor = attributes.value("JOINTS_0", -1);
            const int weightsAccessor = attributes.value("WEIGHTS_0", -1);
            std::vector<uint32_t> indices;
            if (indexAccessor >= 0)
            {
                if (indexAccessor >= static_cast<int>(root["accessors"].size())) continue;
                const size_t count = root["accessors"][indexAccessor].value("count", 0u);
                for (size_t i = 0; i < count; ++i) indices.push_back(ReadGltfIndex(root, buffers, indexAccessor, i));
            }
            else
                for (size_t i = 0; i < vertexCount; ++i) indices.push_back(static_cast<uint32_t>(i));

            for (size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                if (indices[i] >= vertexCount || indices[i + 1] >= vertexCount || indices[i + 2] >= vertexCount)
                    continue;
                Vertex triangle[3]{};
                for (int corner = 0; corner < 3; ++corner)
                {
                    const size_t vertexIndex = indices[i + corner];
                    float position[3] = {}, normal[3] = { 0.0f, 1.0f, 0.0f }, uv[2] = {};
                    ReadGltfValues(root, buffers, positionAccessor, vertexIndex, position, 3);
                    if (normalAccessor >= 0) ReadGltfValues(root, buffers, normalAccessor, vertexIndex, normal, 3);
                    if (uvAccessor >= 0) ReadGltfValues(root, buffers, uvAccessor, vertexIndex, uv, 2);
                    triangle[corner] = Vertex(position[0], position[1], position[2], normal[0], normal[1], normal[2], uv[0], 1.0f - uv[1]);
                    if (jointsAccessor >= 0 && weightsAccessor >= 0)
                    {
                        float jointValues[4]{}, weightValues[4]{};
                        if (ReadGltfValues(root, buffers, jointsAccessor, vertexIndex, jointValues, 4) &&
                            ReadGltfValues(root, buffers, weightsAccessor, vertexIndex, weightValues, 4))
                        {
                            float totalWeight = 0.0f;
                            for (int influence = 0; influence < 4; ++influence)
                            {
                                const int joint = static_cast<int>(std::clamp(jointValues[influence], 0.0f, 65535.0f));
                                triangle[corner].JointIndices[influence] = static_cast<uint16_t>(
                                    joint < 48 ? joint : 0);
                                triangle[corner].JointWeights[influence] = std::max(0.0f, weightValues[influence]);
                                if (joint >= 48) triangle[corner].JointWeights[influence] = 0.0f;
                                totalWeight += triangle[corner].JointWeights[influence];
                            }
                            if (totalWeight > 1e-6f)
                                for (float& weight : triangle[corner].JointWeights) weight /= totalWeight;
                            else
                                std::fill(std::begin(triangle[corner].JointWeights), std::end(triangle[corner].JointWeights), 0.0f);
                        }
                    }
                }
                if (normalAccessor < 0)
                {
                    glm::vec3 a(triangle[0].X, triangle[0].Y, triangle[0].Z);
                    glm::vec3 b(triangle[1].X, triangle[1].Y, triangle[1].Z);
                    glm::vec3 c(triangle[2].X, triangle[2].Y, triangle[2].Z);
                    glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
                    if (!std::isfinite(normal.x)) normal = glm::vec3(0.0f, 1.0f, 0.0f);
                    for (Vertex& vertex : triangle) { vertex.NX = normal.x; vertex.NY = normal.y; vertex.NZ = normal.z; }
                }
                result.Vertices.insert(result.Vertices.end(), triangle, triangle + 3);
            }
        }
    }

    // Ler hierarquia, skin e canais do glTF. A malha continua em um único
    // asset da Marae; os índices de osso nos vértices apontam para a paleta da skin.
    if (root.contains("nodes") && root["nodes"].is_array())
    {
        auto asset = std::make_shared<SkeletalAnimationAsset>();
        const auto& gltfNodes = root["nodes"];
        asset->Nodes.resize(gltfNodes.size());
        std::vector<glm::mat4> localMatrices(gltfNodes.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < gltfNodes.size(); ++i)
        {
            const json& node = gltfNodes[i];
            AnimationNode& output = asset->Nodes[i];
            output.Name = node.value("name", std::string("Node_") + std::to_string(i));
            if (node.contains("translation") && node["translation"].is_array() && node["translation"].size() >= 3)
                output.BindTranslation = glm::vec3(node["translation"][0].get<float>(), node["translation"][1].get<float>(), node["translation"][2].get<float>());
            if (node.contains("rotation") && node["rotation"].is_array() && node["rotation"].size() >= 4)
                output.BindRotation = glm::normalize(glm::quat(node["rotation"][3].get<float>(), node["rotation"][0].get<float>(), node["rotation"][1].get<float>(), node["rotation"][2].get<float>()));
            if (node.contains("scale") && node["scale"].is_array() && node["scale"].size() >= 3)
                output.BindScale = glm::vec3(node["scale"][0].get<float>(), node["scale"][1].get<float>(), node["scale"][2].get<float>());
            if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() == 16)
            {
                float values[16]{};
                for (int c = 0; c < 16; ++c) values[c] = node["matrix"][c].get<float>();
                std::memcpy(&localMatrices[i][0][0], values, sizeof(values));
                glm::vec3 skew; glm::vec4 perspective;
                glm::decompose(localMatrices[i], output.BindScale, output.BindRotation,
                               output.BindTranslation, skew, perspective);
                output.BindRotation = glm::normalize(output.BindRotation);
            }
            else
            {
                localMatrices[i] = glm::translate(glm::mat4(1.0f), output.BindTranslation) *
                    glm::mat4_cast(output.BindRotation) * glm::scale(glm::mat4(1.0f), output.BindScale);
            }
            if (node.contains("mesh") && asset->MeshNodeIndex < 0)
                asset->MeshNodeIndex = static_cast<int>(i);
            if (node.contains("children") && node["children"].is_array())
                for (const auto& childValue : node["children"])
                {
                    const int child = childValue.get<int>();
                    if (child >= 0 && child < static_cast<int>(asset->Nodes.size())) asset->Nodes[child].Parent = static_cast<int>(i);
                }
        }
        if (root.contains("skins") && root["skins"].is_array() && !root["skins"].empty())
        {
            const json& skin = root["skins"][0];
            if (skin.contains("joints") && skin["joints"].is_array())
                for (const auto& joint : skin["joints"])
                {
                    if (asset->JointNodeIndices.size() >= 48) break;
                    asset->JointNodeIndices.push_back(joint.get<int>());
                }
            const int inverseAccessor = skin.value("inverseBindMatrices", -1);
            for (size_t i = 0; i < asset->JointNodeIndices.size(); ++i)
            {
                glm::mat4 matrix(1.0f);
                if (inverseAccessor >= 0) ReadGltfMatrix(root, buffers, inverseAccessor, i, matrix);
                asset->InverseBindMatrices.push_back(matrix);
            }
        }
        if (root.contains("animations") && root["animations"].is_array())
        {
            for (size_t animationIndex = 0; animationIndex < root["animations"].size(); ++animationIndex)
            {
                const json& sourceAnimation = root["animations"][animationIndex];
                auto clip = std::make_shared<AnimationClip>();
                clip->Name = sourceAnimation.value("name", std::string("Animation_") + std::to_string(animationIndex));
                clip->ID = "anim_" + std::to_string(std::hash<std::string>{}(source.string() + "#" + clip->Name));
                clip->Loop = true;
                std::unordered_map<int, size_t> trackByNode;
                const auto& samplers = sourceAnimation.value("samplers", json::array());
                for (const auto& channel : sourceAnimation.value("channels", json::array()))
                {
                    const int samplerIndex = channel.value("sampler", -1);
                    const json target = channel.value("target", json::object());
                    const int nodeIndex = target.value("node", -1);
                    const std::string path = target.value("path", std::string());
                    if (samplerIndex < 0 || samplerIndex >= static_cast<int>(samplers.size()) ||
                        nodeIndex < 0 || nodeIndex >= static_cast<int>(asset->Nodes.size())) continue;
                    const json& sampler = samplers[samplerIndex];
                    const int inputAccessor = sampler.value("input", -1), outputAccessor = sampler.value("output", -1);
                    if (inputAccessor < 0 || outputAccessor < 0 || inputAccessor >= static_cast<int>(root["accessors"].size()) || outputAccessor >= static_cast<int>(root["accessors"].size())) continue;
                    const size_t keyCount = root["accessors"][inputAccessor].value("count", 0u);
                    const bool cubicSpline = sampler.value("interpolation", std::string("LINEAR")) == "CUBICSPLINE";
                    size_t trackIndex;
                    const auto found = trackByNode.find(nodeIndex);
                    if (found == trackByNode.end())
                    {
                        trackIndex = clip->NodeTracks.size();
                        trackByNode[nodeIndex] = trackIndex;
                        AnimationNodeTrack track; track.NodeIndex = nodeIndex;
                        clip->NodeTracks.push_back(std::move(track));
                    }
                    else trackIndex = found->second;
                    AnimationNodeTrack& track = clip->NodeTracks[trackIndex];
                    for (size_t key = 0; key < keyCount; ++key)
                    {
                        float timeValue[1]{}; ReadGltfValues(root, buffers, inputAccessor, key, timeValue, 1);
                        clip->Duration = std::max(clip->Duration, timeValue[0]);
                        if (path == "translation" || path == "scale")
                        {
                            float v[3]{}; if (!ReadGltfValues(root, buffers, outputAccessor, key * (cubicSpline ? 3u : 1u) + (cubicSpline ? 1u : 0u), v, 3)) continue;
                            KeyframeVec3 frame{timeValue[0], glm::vec3(v[0], v[1], v[2])};
                            (path == "translation" ? track.Translation.Keys : track.Scale.Keys).push_back(frame);
                        }
                        else if (path == "rotation")
                        {
                            float v[4]{}; if (!ReadGltfValues(root, buffers, outputAccessor, key * (cubicSpline ? 3u : 1u) + (cubicSpline ? 1u : 0u), v, 4)) continue;
                            track.Rotation.Keys.push_back({timeValue[0], glm::normalize(glm::quat(v[3], v[0], v[1], v[2]))});
                        }
                    }
                }
                if (!clip->NodeTracks.empty()) asset->Clips.push_back(std::move(clip));
            }
        }
        // glTF também permite animação de objeto sem armature/skin. Como a
        // Marae representa cada importação como uma entidade, convertemos o
        // canal do nó da malha para o AnimationClip de transform existente.
        if (asset->JointNodeIndices.empty() && asset->MeshNodeIndex >= 0)
        {
            const AnimationNode& meshNode = asset->Nodes[static_cast<size_t>(asset->MeshNodeIndex)];
            for (const auto& clip : asset->Clips)
            {
                if (!clip) continue;
                const auto track = std::find_if(clip->NodeTracks.begin(), clip->NodeTracks.end(),
                    [&](const AnimationNodeTrack& candidate) { return candidate.NodeIndex == asset->MeshNodeIndex; });
                if (track != clip->NodeTracks.end())
                {
                    clip->Position = track->Translation;
                    clip->Rotation = track->Rotation;
                    clip->Scale = track->Scale;
                }
                if (clip->Position.Keys.empty()) clip->Position.Keys.push_back({0.0f, meshNode.BindTranslation});
                if (clip->Rotation.Keys.empty()) clip->Rotation.Keys.push_back({0.0f, meshNode.BindRotation});
                if (clip->Scale.Keys.empty()) clip->Scale.Keys.push_back({0.0f, meshNode.BindScale});
                clip->NodeTracks.clear();
            }
        }
        if (!asset->Clips.empty() || !asset->JointNodeIndices.empty()) result.SkeletalAsset = std::move(asset);
    }

    for (const Vertex& vertex : result.Vertices)
        result.BoundingRadius = std::max(result.BoundingRadius,
            std::sqrt(vertex.X * vertex.X + vertex.Y * vertex.Y + vertex.Z * vertex.Z));
    return result;
}

ImportedModelData ModelImporter::LoadFBX(const std::string& filepath)
{
    ImportedModelData result;
    const fs::path source = fs::path(ResolveExistingPath(filepath));
    if (!fs::exists(source)) return result;

    fs::path blender;
#ifdef _WIN32
    const fs::path candidates[] = {
        "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe",
        "C:/Program Files/Blender Foundation/Blender 4.3/blender.exe",
        "C:/Program Files/Blender Foundation/Blender 4.2/blender.exe"
    };
    for (const auto& candidate : candidates)
        if (fs::exists(candidate)) { blender = candidate; break; }
#endif
    if (blender.empty())
    {
        std::cout << "[ModelImporter] Blender não encontrado para converter FBX: " << source << std::endl;
        return result;
    }

    // O conversor legado usa uma linha de comando externa; não aceite nomes
    // que possam quebrar as aspas desse comando.
    if (source.string().find('"') != std::string::npos || blender.string().find('"') != std::string::npos)
        return result;

    const fs::path output = source.parent_path() / (source.stem().string() + ".marae.glb");
    const fs::path script = fs::temp_directory_path() / "marae_fbx_to_glb.py";
    {
        std::ofstream file(script, std::ios::trunc);
        file << "import bpy, sys\n"
                "args = sys.argv[sys.argv.index('--') + 1:]\n"
                "src, dst = args[0], args[1]\n"
                "bpy.ops.wm.read_factory_settings(use_empty=True)\n"
                "bpy.ops.import_scene.fbx(filepath=src)\n"
                "bpy.ops.export_scene.gltf(filepath=dst, export_format='GLB', export_animations=True, export_skins=True)\n";
    }

    const auto quote = [](const fs::path& path) {
        return std::string("\"") + path.string() + "\"";
    };
    const std::string command = quote(blender) + " --background --python " + quote(script) +
        " -- " + quote(source) + " " + quote(output);
    const int exitCode = std::system(command.c_str());
    std::error_code ignored;
    fs::remove(script, ignored);
    if (exitCode != 0 || !fs::exists(output))
    {
        std::cout << "[ModelImporter] Falha ao converter FBX com Blender: " << source << std::endl;
        return result;
    }
    std::cout << "[ModelImporter] FBX convertido para glTF: " << output << std::endl;
    return LoadGLTF(output.string());
}

ImportedModelData ModelImporter::LoadModelData(const std::string& filepath)
{
    const std::string extension = GetExtension(filepath);
    if (extension == ".mesh2d")
    {
        ImportedModelData result;
        result.Vertices = LoadMesh2DVertices(filepath);
        return result;
    }
    if (extension == ".gltf" || extension == ".glb") return LoadGLTF(filepath);
    if (extension == ".fbx") return LoadFBX(filepath);
    return LoadOBJ(filepath);
}

