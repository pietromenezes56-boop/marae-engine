#include "Prefab.h"

#include "../Entity/Entity.h"
#include "../Scene/Scene.h"
#include "../Components/LightComponent.h"
#include "../Components/AudioComponent.h"
#include "../Components/MaterialComponent.h"
#include "../Components/ColorComponent.h"
#include "../../Renderer/Mesh/MeshCache.h"
#include "../../Engine/Assets/ModelImporter/ModelImporter.h"

#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Helpers internos
// ---------------------------------------------------------------------------
namespace
{
    const std::string k_HierarchyHeader = "@prefab_hierarchy_v3";

    void WriteEntityData(std::ostream& file, const Entity& entity, bool isChild = false)
    {
        const Transform& t = const_cast<Entity&>(entity).GetTransform();

        file << entity.GetName() << "\n";

        // Se for nó filho em uma hierarquia, gravamos o transform local para preservar o layout do prefab;
        // caso contrário (raiz), grava o transform padrão.
        if (isChild)
        {
            file << t.LocalPosition.X << " " << t.LocalPosition.Y << " " << t.LocalPosition.Z << "\n";
            file << t.LocalRotation.x << " " << t.LocalRotation.y << " " << t.LocalRotation.z << " " << t.LocalRotation.w << "\n";
            file << t.LocalScale.X    << " " << t.LocalScale.Y    << " " << t.LocalScale.Z    << "\n";
        }
        else
        {
            file << t.Position.X << " " << t.Position.Y << " " << t.Position.Z << "\n";
            file << t.Rotation.x << " " << t.Rotation.y << " " << t.Rotation.z << " " << t.Rotation.w << "\n";
            file << t.Scale.X    << " " << t.Scale.Y    << " " << t.Scale.Z    << "\n";
        }

        // Cor
        const ColorComponent& col = const_cast<Entity&>(entity).GetColorComponent();
        file << col.Color.X << " " << col.Color.Y << " " << col.Color.Z << "\n";

        // Mesh
        file << const_cast<Entity&>(entity).GetMeshComponent().MeshPath << "\n";

        // Physics
        file << "@physics "
             << (entity.PhysicsEnabled    ? 1 : 0) << " "
             << (entity.UseGravity        ? 1 : 0) << " "
             << (entity.IsStatic          ? 1 : 0) << " "
             << (entity.ColliderEnabled   ? 1 : 0) << " "
             << static_cast<int>(entity.Collider)  << " "
             << entity.Mass        << " "
             << entity.Restitution << " "
             << entity.Friction    << " "
             << entity.PickingRadius << " "
             << entity.AngularVelocity.X << " "
             << entity.AngularVelocity.Y << " "
             << entity.AngularVelocity.Z << " "
             << (entity.ContinuousCollision ? 1 : 0) << " "
             << (entity.FreezeRotation      ? 1 : 0) << " "
             << (entity.IsTrigger ? 1 : 0) << "\n";

        // Material
        file << "@material "
             << static_cast<int>(const_cast<Entity&>(entity).GetMaterialComponent().MaterialData.Type)
             << "\n";

        const MaterialComponent& materialComponent = const_cast<Entity&>(entity).GetMaterialComponent();
        std::vector<uint32_t> assignedFaces;
        assignedFaces.reserve(materialComponent.FaceTexturePaths.size());
        for (const auto& [faceIndex, path] : materialComponent.FaceTexturePaths)
            if (!path.empty()) assignedFaces.push_back(faceIndex);
        std::sort(assignedFaces.begin(), assignedFaces.end());
        file << "@face_textures " << static_cast<int>(materialComponent.TextureMode) << " "
             << assignedFaces.size();
        for (uint32_t faceIndex : assignedFaces)
            file << " " << faceIndex << " " << std::quoted(materialComponent.FaceTexturePaths.at(faceIndex));
        file << "\n";

        // Entity metadata
        file << "@entity "
             << (entity.Active          ? 1 : 0) << " "
             << std::clamp(entity.Opacity, 0.0f, 1.0f) << " "
             << (entity.RaycastTarget   ? 1 : 0) << " "
             << entity.Layer << " "
             << std::quoted(entity.Tag) << " "
             << (entity.CastShadows ? 1 : 0) << "\n";

        // Light (opcional)
        if (entity.HasLight())
        {
            const LightComponent& lc = const_cast<Entity&>(entity).GetLightComponent();
            file << "@light "
                 << (lc.Enabled ? 1 : 0) << " "
                 << static_cast<int>(lc.Type) << " "
                 << lc.Color[0] << " " << lc.Color[1] << " " << lc.Color[2] << " "
                 << lc.Intensity << " " << lc.Range << " "
                 << lc.DirectionLocal[0] << " " << lc.DirectionLocal[1] << " " << lc.DirectionLocal[2] << " "
                 << lc.InnerAngleDeg << " " << lc.OuterAngleDeg << " "
                 << (lc.CastShadows ? 1 : 0) << " "
                 << (lc.DistanceSoftening ? 1 : 0) << "\n";
        }

        // Audio (opcional)
        if (entity.HasAudio())
        {
            const AudioComponent& ac = const_cast<Entity&>(entity).GetAudioComponent();
            file << "@audio "
                 << (ac.Enabled       ? 1 : 0) << " "
                 << (ac.Loop          ? 1 : 0) << " "
                 << (ac.PlayOnStart   ? 1 : 0) << " "
                 << ac.Volume << " " << ac.Pitch << " "
                 << ac.MinDistance << " " << ac.MaxDistance << " "
                 << std::quoted(ac.ClipPath) << "\n";
        }
    }

    void SaveNodeRecursive(std::ostream& file, const Scene& scene, const Entity& entity, bool isChild = false)
    {
        file << "@node_begin\n";
        WriteEntityData(file, entity, isChild);

        const auto& childIDs = entity.GetChildrenIDs();
        std::vector<const Entity*> validChildren;
        for (uint64_t cid : childIDs)
        {
            const Entity* child = const_cast<Scene&>(scene).FindEntityByID(cid);
            if (child && !child->IsEditorOnly())
            {
                validChildren.push_back(child);
            }
        }

        file << "@children_count " << validChildren.size() << "\n";
        for (const Entity* child : validChildren)
        {
            SaveNodeRecursive(file, scene, *child, true);
        }
        file << "@node_end\n";
    }

    bool ReadEntityFieldsFromStream(std::istream& file, Entity& entity, std::string& nextHeaderLine)
    {
        std::string name;
        if (!std::getline(file, name)) return false;
        entity.SetName(name);

        // Transform
        Transform& t = entity.GetTransform();
        {
            std::string posLine, rotLine, sclLine;
            std::getline(file, posLine);
            std::getline(file, rotLine);
            std::getline(file, sclLine);

            { std::stringstream ss(posLine); ss >> t.Position.X >> t.Position.Y >> t.Position.Z; }
            { std::stringstream ss(rotLine); ss >> t.Rotation.x >> t.Rotation.y >> t.Rotation.z >> t.Rotation.w; }
            { std::stringstream ss(sclLine); ss >> t.Scale.X    >> t.Scale.Y    >> t.Scale.Z; }

            t.LocalPosition     = t.Position;
            t.LocalRotation     = t.Rotation;
            t.LocalScale        = t.Scale;
            t.EulerRotation     = t.LocalEulerRotation = {
                glm::degrees(glm::eulerAngles(t.Rotation).x),
                glm::degrees(glm::eulerAngles(t.Rotation).y),
                glm::degrees(glm::eulerAngles(t.Rotation).z)
            };
        }

        // Cor
        {
            std::string colorLine;
            std::getline(file, colorLine);
            std::stringstream ss(colorLine);
            ColorComponent& col = entity.GetColorComponent();
            ss >> col.Color.X >> col.Color.Y >> col.Color.Z;
            entity.GetMaterialComponent().MaterialData.Color[0] = col.Color.X;
            entity.GetMaterialComponent().MaterialData.Color[1] = col.Color.Y;
            entity.GetMaterialComponent().MaterialData.Color[2] = col.Color.Z;
            entity.GetMaterialComponent().MaterialData.Color[3] = 1.0f;
        }

        // Mesh
        {
            std::string meshPath;
            std::getline(file, meshPath);
            if (!meshPath.empty() && ModelImporter::IsSupportedModel(meshPath))
            {
                entity.GetMeshComponent().MeshData = MeshCache::Load(meshPath);
                entity.GetMeshComponent().MeshPath = meshPath;
            }
            if (meshPath.empty())
            {
                entity.GetMeshComponent().MeshData = MeshCache::CreateCubeInstance();
                entity.GetMeshComponent().MeshPath.clear();
            }
            else if (!entity.GetMeshComponent().MeshData ||
                !entity.GetMeshComponent().MeshData->IsValid())
            {
                entity.GetMeshComponent().MeshData = MeshCache::CreateCubeInstance();
                entity.GetMeshComponent().MeshPath.clear();
            }
        }

        // Metadata (@physics, @material, @entity, @light, @audio, @children_count, @node_end)
        std::string physicsLine, materialLine, faceTexturesLine, entityLine, lightLine, audioLine;
        std::string line;
        while (std::getline(file, line))
        {
            if (line.rfind("@physics",  0) == 0) physicsLine  = line;
            else if (line.rfind("@material", 0) == 0) materialLine = line;
            else if (line.rfind("@face_textures", 0) == 0) faceTexturesLine = line;
            else if (line.rfind("@entity",   0) == 0) entityLine   = line;
            else if (line.rfind("@light",    0) == 0) lightLine    = line;
            else if (line.rfind("@audio",    0) == 0) audioLine    = line;
            else if (line.rfind("@children_count", 0) == 0 || line.rfind("@node_end", 0) == 0 || line.rfind("@node_begin", 0) == 0)
            {
                nextHeaderLine = line;
                break;
            }
        }

        if (!physicsLine.empty())
        {
            std::stringstream ss(physicsLine.substr(8));
            int enabled = 1, gravity = 1, stat = 0, collider = 1, shape = 0, cc = 1, fr = 0, trigger = 0;
            ss >> enabled >> gravity >> stat >> collider >> shape
               >> entity.Mass >> entity.Restitution >> entity.Friction >> entity.PickingRadius
               >> entity.AngularVelocity.X >> entity.AngularVelocity.Y >> entity.AngularVelocity.Z
               >> cc >> fr;
            ss >> trigger;
            entity.PhysicsEnabled  = enabled  != 0;
            entity.UseGravity      = gravity  != 0;
            entity.IsStatic        = stat     != 0;
            entity.ColliderEnabled = collider != 0;
            entity.Collider = static_cast<Entity::ColliderShape>(std::clamp(shape, 0, 5));
            entity.ContinuousCollision = cc != 0;
            entity.FreezeRotation      = fr != 0;
            entity.IsTrigger = trigger != 0;
        }

        if (!materialLine.empty())
        {
            std::stringstream ss(materialLine.substr(9));
            int type = 0; ss >> type;
            entity.GetMaterialComponent().MaterialData.Type =
                static_cast<MaterialType>(std::clamp(type, 0, 12));
        }

        if (!faceTexturesLine.empty())
        {
            std::stringstream ss(faceTexturesLine.substr(14));
            int mode = 0;
            size_t count = 0;
            if (ss >> mode >> count)
            {
                MaterialComponent& component = entity.GetMaterialComponent();
                component.TextureMode = static_cast<FaceTextureMode>(std::clamp(mode, 0, 1));
                component.FaceTexturePaths.clear();
                component.FaceTextures.clear();
                count = std::min<size_t>(count, 1'000'000);
                for (size_t i = 0; i < count; ++i)
                {
                    uint32_t faceIndex = 0;
                    std::string path;
                    if (!(ss >> faceIndex >> std::quoted(path))) break;
                    if (!path.empty()) component.FaceTexturePaths.emplace(faceIndex, std::move(path));
                }
            }
        }

        if (!entityLine.empty())
        {
            std::stringstream ss(entityLine.substr(7));
            int active = 1, raycast = 1, layer = 0, castShadows = 1;
            float opacity = 1.0f;
            std::string tag;
            ss >> active >> opacity >> raycast >> layer >> std::quoted(tag);
            ss >> castShadows;
            entity.Active        = active  != 0;
            entity.Opacity       = std::clamp(opacity, 0.0f, 1.0f);
            entity.CastShadows  = castShadows != 0;
            entity.RaycastTarget = raycast != 0;
            entity.Layer         = layer;
            entity.Tag           = tag;
        }

        if (!lightLine.empty())
        {
            std::stringstream ss(lightLine.substr(6));
            int enabled = 1, type = 0;
            entity.AddLight();
            LightComponent& lc = entity.GetLightComponent();
            ss >> enabled >> type
               >> lc.Color[0] >> lc.Color[1] >> lc.Color[2]
               >> lc.Intensity >> lc.Range
               >> lc.DirectionLocal[0] >> lc.DirectionLocal[1] >> lc.DirectionLocal[2]
               >> lc.InnerAngleDeg >> lc.OuterAngleDeg;
            int castShadows = 1;
            if (ss >> castShadows)
                lc.CastShadows = castShadows != 0;
            int distanceSoftening = 1;
            if (ss >> distanceSoftening)
                lc.DistanceSoftening = distanceSoftening != 0;
            lc.Enabled = enabled != 0;
            if (type == static_cast<int>(LightType::Spot))
                lc.Type = LightType::Spot;
            else if (type == static_cast<int>(LightType::Directional))
                lc.Type = LightType::Directional;
            else if (type == static_cast<int>(LightType::Area))
                lc.Type = LightType::Area;
            else
                lc.Type = LightType::Point;
        }

        if (!audioLine.empty())
        {
            std::stringstream ss(audioLine.substr(6));
            int enabled = 1, loop = 0, playOnStart = 0;
            entity.AddAudio();
            AudioComponent& ac = entity.GetAudioComponent();
            ss >> enabled >> loop >> playOnStart
               >> ac.Volume >> ac.Pitch >> ac.MinDistance >> ac.MaxDistance
               >> std::quoted(ac.ClipPath);
            ac.Enabled     = enabled     != 0;
            ac.Loop        = loop        != 0;
            ac.PlayOnStart = playOnStart != 0;
        }

        return true;
    }

    // Leitor recursivo para formato hierárquico v3
    Entity* ReadHierarchyNode(std::istream& file, Scene& scene, uint64_t parentID,
                             const std::string& prefabPath, Entity* existingRoot = nullptr)
    {
        std::string line;
        // Procura pelo início do nó
        while (std::getline(file, line))
        {
            if (line.rfind("@node_begin", 0) == 0) break;
        }
        if (file.eof()) return nullptr;

        Entity* current = existingRoot;
        if (!current)
        {
            current = &scene.CreateEntity();
        }

        current->PrefabPath = prefabPath;
        if (parentID != 0)
        {
            scene.SetParent(current->GetID(), parentID);
        }

        std::string nextHeader;
        if (!ReadEntityFieldsFromStream(file, *current, nextHeader))
        {
            return current;
        }

        int childCount = 0;
        if (nextHeader.rfind("@children_count", 0) == 0)
        {
            std::stringstream ss(nextHeader.substr(15));
            ss >> childCount;
        }
        else
        {
            while (std::getline(file, line))
            {
                if (line.rfind("@children_count", 0) == 0)
                {
                    std::stringstream ss(line.substr(15));
                    ss >> childCount;
                    break;
                }
                if (line.rfind("@node_end", 0) == 0) break;
            }
        }

        for (int i = 0; i < childCount; ++i)
        {
            ReadHierarchyNode(file, scene, current->GetID(), prefabPath, nullptr);
        }

        // Aguarda @node_end correspondente
        while (line.rfind("@node_end", 0) != 0 && std::getline(file, line)) {}

        return current;
    }

    // Leitor para formato v2 simples legado (entidade única)
    bool ReadLegacyEntityData(Entity& entity, const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            std::cerr << "[Prefab] Nao foi possivel abrir: " << path << std::endl;
            return false;
        }

        std::string dummy;
        return ReadEntityFieldsFromStream(file, entity, dummy);
    }

    void CollectPrefabDescendants(Scene& scene, uint64_t rootID, const std::string& prefabPath, std::vector<uint64_t>& outIDs)
    {
        Entity* current = scene.FindEntityByID(rootID);
        if (!current) return;

        for (uint64_t cid : current->GetChildrenIDs())
        {
            Entity* child = scene.FindEntityByID(cid);
            if (child && child->PrefabPath == prefabPath)
            {
                outIDs.push_back(cid);
                CollectPrefabDescendants(scene, cid, prefabPath, outIDs);
            }
        }
    }

    void DeleteEntityByID(Scene& scene, uint64_t id)
    {
        auto& entities = scene.GetEntities();
        for (size_t i = 0; i < entities.size(); ++i)
        {
            if (entities[i].GetID() == id)
            {
                scene.DeleteEntity(static_cast<int>(i));
                break;
            }
        }
    }

    bool NearlyEqual(float a, float b, float epsilon = 0.0001f)
    {
        return std::abs(a - b) <= epsilon;
    }

    bool TransformEquals(const Transform& a, const Transform& b)
    {
        return NearlyEqual(a.Position.X, b.Position.X) &&
               NearlyEqual(a.Position.Y, b.Position.Y) &&
               NearlyEqual(a.Position.Z, b.Position.Z) &&
               NearlyEqual(a.Rotation.x, b.Rotation.x) &&
               NearlyEqual(a.Rotation.y, b.Rotation.y) &&
               NearlyEqual(a.Rotation.z, b.Rotation.z) &&
               NearlyEqual(a.Rotation.w, b.Rotation.w) &&
               NearlyEqual(a.Scale.X, b.Scale.X) &&
               NearlyEqual(a.Scale.Y, b.Scale.Y) &&
               NearlyEqual(a.Scale.Z, b.Scale.Z);
    }

    bool MaterialEquals(const Material& a, const Material& b)
    {
        return a.Type == b.Type &&
               std::equal(std::begin(a.Color), std::end(a.Color), std::begin(b.Color),
                          [](float x, float y) { return NearlyEqual(x, y); }) &&
               a.PBRParametersEnabled == b.PBRParametersEnabled &&
               NearlyEqual(a.Roughness, b.Roughness) &&
               NearlyEqual(a.Metallic, b.Metallic) &&
               NearlyEqual(a.Specular, b.Specular) &&
               NearlyEqual(a.EmissionStrength, b.EmissionStrength) &&
               NearlyEqual(a.Alpha, b.Alpha) &&
               NearlyEqual(a.NormalStrength, b.NormalStrength) &&
               a.NormalMapPath == b.NormalMapPath &&
               a.EmissionTexturePath == b.EmissionTexturePath &&
               a.TexturePath == b.TexturePath &&
               a.TextureName == b.TextureName &&
               a.TextureGUID == b.TextureGUID &&
               a.ScaleMode == b.ScaleMode &&
               NearlyEqual(a.UVWorldScale, b.UVWorldScale) &&
               NearlyEqual(a.TileSize, b.TileSize) &&
               a.GUID == b.GUID;
    }

    bool LightEquals(const LightComponent& a, const LightComponent& b)
    {
        return a.Enabled == b.Enabled && a.CastShadows == b.CastShadows && a.Type == b.Type &&
               std::equal(std::begin(a.Color), std::end(a.Color), std::begin(b.Color),
                          [](float x, float y) { return NearlyEqual(x, y); }) &&
               NearlyEqual(a.Intensity, b.Intensity) && a.UseTemperature == b.UseTemperature &&
               NearlyEqual(a.TemperatureKelvin, b.TemperatureKelvin) &&
               NearlyEqual(a.Range, b.Range) && NearlyEqual(a.SourceRadius, b.SourceRadius) &&
               NearlyEqual(a.ShadowSoftness, b.ShadowSoftness) &&
               NearlyEqual(a.ShadowStrength, b.ShadowStrength) &&
               a.DistanceSoftening == b.DistanceSoftening &&
               NearlyEqual(a.ContactShadowStrength, b.ContactShadowStrength) &&
               a.IndirectContribution == b.IndirectContribution &&
               NearlyEqual(a.InnerAngleDeg, b.InnerAngleDeg) &&
               NearlyEqual(a.OuterAngleDeg, b.OuterAngleDeg) &&
               std::equal(std::begin(a.DirectionLocal), std::end(a.DirectionLocal),
                          std::begin(b.DirectionLocal),
                          [](float x, float y) { return NearlyEqual(x, y); });
    }

    struct PrefabOverrides
    {
        bool Transform = false;
        ::Transform TransformValue{};
        bool Mesh = false;
        std::string MeshPath;
        std::string PrimitiveType;
        bool Color = false;
        Vector3 ColorValue{};
        bool Material = false;
        ::Material MaterialValue{};
        bool Physics = false;
        bool PhysicsEnabled = true;
        bool UseGravity = true;
        bool IsStatic = false;
        bool ColliderEnabled = true;
        bool IsTrigger = false;
        Entity::ColliderShape Collider = Entity::ColliderShape::Box;
        float Mass = 1.0f;
        float Restitution = 0.15f;
        float Friction = 0.65f;
        float PickingRadius = 1.0f;
        Vector3 AngularVelocity{};
        bool ContinuousCollision = true;
        bool FreezeRotation = false;
        bool Metadata = false;
        bool Active = true;
        float Opacity = 1.0f;
        bool CastShadows = true;
        bool RaycastTarget = true;
        int Layer = 0;
        std::string Tag;
        bool Light = false;
        bool HasLight = false;
        LightComponent LightValue{};
        bool Audio = false;
        bool HasAudio = false;
        AudioComponent AudioValue{};
    };

    std::unordered_map<uint64_t, std::shared_ptr<Entity>> s_PrefabBaselines;

    std::shared_ptr<Entity> ClonePrefabBaseline(const Entity& source)
    {
        auto result = std::make_shared<Entity>();
        result->GetTransform() = const_cast<Entity&>(source).GetTransform();
        result->GetMeshComponent() = const_cast<Entity&>(source).GetMeshComponent();
        result->GetColorComponent() = const_cast<Entity&>(source).GetColorComponent();
        result->GetMaterialComponent() = const_cast<Entity&>(source).GetMaterialComponent();
        result->PhysicsEnabled = source.PhysicsEnabled;
        result->UseGravity = source.UseGravity;
        result->IsStatic = source.IsStatic;
        result->ColliderEnabled = source.ColliderEnabled;
        result->IsTrigger = source.IsTrigger;
        result->Collider = source.Collider;
        result->Mass = source.Mass;
        result->Restitution = source.Restitution;
        result->Friction = source.Friction;
        result->PickingRadius = source.PickingRadius;
        result->AngularVelocity = source.AngularVelocity;
        result->ContinuousCollision = source.ContinuousCollision;
        result->FreezeRotation = source.FreezeRotation;
        result->Active = source.Active;
        result->Opacity = source.Opacity;
        result->CastShadows = source.CastShadows;
        result->RaycastTarget = source.RaycastTarget;
        result->Layer = source.Layer;
        result->Tag = source.Tag;
        if (source.HasLight())
        {
            result->AddLight(const_cast<Entity&>(source).GetLightComponent().Type);
            result->GetLightComponent() = const_cast<Entity&>(source).GetLightComponent();
        }
        if (source.HasAudio())
        {
            result->AddAudio();
            result->GetAudioComponent() = const_cast<Entity&>(source).GetAudioComponent();
        }
        return result;
    }

    void StorePrefabBaseline(const Entity& entity)
    {
        s_PrefabBaselines[entity.GetID()] = ClonePrefabBaseline(entity);
    }

    void StorePrefabBaselineFor(uint64_t entityID, const Entity& source)
    {
        s_PrefabBaselines[entityID] = ClonePrefabBaseline(source);
    }

    PrefabOverrides CaptureTransformOverride(const Entity& instance, bool keepTransform)
    {
        PrefabOverrides result;
        if (keepTransform)
        {
            result.Transform = true;
            result.TransformValue = const_cast<Entity&>(instance).GetTransform();
        }
        return result;
    }

    bool PhysicsEquals(const Entity& a, const Entity& b)
    {
        const Vector3 av = a.AngularVelocity;
        const Vector3 bv = b.AngularVelocity;
        return a.PhysicsEnabled == b.PhysicsEnabled && a.UseGravity == b.UseGravity &&
               a.IsStatic == b.IsStatic && a.ColliderEnabled == b.ColliderEnabled &&
               a.IsTrigger == b.IsTrigger &&
               a.Collider == b.Collider && NearlyEqual(a.Mass, b.Mass) &&
               NearlyEqual(a.Restitution, b.Restitution) && NearlyEqual(a.Friction, b.Friction) &&
               NearlyEqual(a.PickingRadius, b.PickingRadius) &&
               NearlyEqual(av.X, bv.X) && NearlyEqual(av.Y, bv.Y) && NearlyEqual(av.Z, bv.Z) &&
               a.ContinuousCollision == b.ContinuousCollision &&
               a.FreezeRotation == b.FreezeRotation;
    }

    PrefabOverrides CaptureOverrides(const Entity& instance, const Entity& source,
                                     bool keepTransform)
    {
        PrefabOverrides result;
        if (keepTransform && !TransformEquals(const_cast<Entity&>(instance).GetTransform(),
                                              const_cast<Entity&>(source).GetTransform()))
        {
            result.Transform = true;
            result.TransformValue = const_cast<Entity&>(instance).GetTransform();
        }
        const MeshComponent& instanceMesh = const_cast<Entity&>(instance).GetMeshComponent();
        const MeshComponent& sourceMesh = const_cast<Entity&>(source).GetMeshComponent();
        if (instanceMesh.MeshPath != sourceMesh.MeshPath ||
            instanceMesh.PrimitiveType != sourceMesh.PrimitiveType)
        {
            result.Mesh = true;
            result.MeshPath = instanceMesh.MeshPath;
            result.PrimitiveType = instanceMesh.PrimitiveType;
        }
        const ColorComponent& instanceColor = const_cast<Entity&>(instance).GetColorComponent();
        const ColorComponent& sourceColor = const_cast<Entity&>(source).GetColorComponent();
        if (!NearlyEqual(instanceColor.Color.X, sourceColor.Color.X) ||
            !NearlyEqual(instanceColor.Color.Y, sourceColor.Color.Y) ||
            !NearlyEqual(instanceColor.Color.Z, sourceColor.Color.Z))
        {
            result.Color = true;
            result.ColorValue = instanceColor.Color;
        }
        const Material& instanceMaterial = const_cast<Entity&>(instance).GetMaterialComponent().MaterialData;
        const Material& sourceMaterial = const_cast<Entity&>(source).GetMaterialComponent().MaterialData;
        if (!MaterialEquals(instanceMaterial, sourceMaterial))
        {
            result.Material = true;
            result.MaterialValue = instanceMaterial;
        }
        if (!PhysicsEquals(instance, source))
        {
            result.Physics = true;
            result.PhysicsEnabled = instance.PhysicsEnabled;
            result.UseGravity = instance.UseGravity;
            result.IsStatic = instance.IsStatic;
            result.ColliderEnabled = instance.ColliderEnabled;
            result.IsTrigger = instance.IsTrigger;
            result.Collider = instance.Collider;
            result.Mass = instance.Mass;
            result.Restitution = instance.Restitution;
            result.Friction = instance.Friction;
            result.PickingRadius = instance.PickingRadius;
            result.AngularVelocity = instance.AngularVelocity;
            result.ContinuousCollision = instance.ContinuousCollision;
            result.FreezeRotation = instance.FreezeRotation;
        }
        if (instance.Active != source.Active || !NearlyEqual(instance.Opacity, source.Opacity) ||
            instance.CastShadows != source.CastShadows ||
            instance.RaycastTarget != source.RaycastTarget || instance.Layer != source.Layer ||
            instance.Tag != source.Tag)
        {
            result.Metadata = true;
            result.Active = instance.Active;
            result.Opacity = instance.Opacity;
            result.CastShadows = instance.CastShadows;
            result.RaycastTarget = instance.RaycastTarget;
            result.Layer = instance.Layer;
            result.Tag = instance.Tag;
        }
        if (instance.HasLight() != source.HasLight() ||
            (instance.HasLight() && source.HasLight() &&
             !LightEquals(const_cast<Entity&>(instance).GetLightComponent(),
                          const_cast<Entity&>(source).GetLightComponent())))
        {
            result.Light = true;
            result.HasLight = instance.HasLight();
            if (result.HasLight) result.LightValue = const_cast<Entity&>(instance).GetLightComponent();
        }
        if (instance.HasAudio() != source.HasAudio() ||
            (instance.HasAudio() && source.HasAudio() &&
             (const_cast<Entity&>(instance).GetAudioComponent().SourceID != const_cast<Entity&>(source).GetAudioComponent().SourceID ||
              const_cast<Entity&>(instance).GetAudioComponent().ClipID != const_cast<Entity&>(source).GetAudioComponent().ClipID ||
              const_cast<Entity&>(instance).GetAudioComponent().ClipPath != const_cast<Entity&>(source).GetAudioComponent().ClipPath ||
              !NearlyEqual(const_cast<Entity&>(instance).GetAudioComponent().Volume, const_cast<Entity&>(source).GetAudioComponent().Volume) ||
              !NearlyEqual(const_cast<Entity&>(instance).GetAudioComponent().Pitch, const_cast<Entity&>(source).GetAudioComponent().Pitch) ||
              const_cast<Entity&>(instance).GetAudioComponent().Loop != const_cast<Entity&>(source).GetAudioComponent().Loop)))
        {
            result.Audio = true;
            result.HasAudio = instance.HasAudio();
            if (result.HasAudio) result.AudioValue = const_cast<Entity&>(instance).GetAudioComponent();
        }
        return result;
    }

    void ApplyOverrides(Entity& instance, const PrefabOverrides& overrides)
    {
        if (overrides.Transform) instance.GetTransform() = overrides.TransformValue;
        if (overrides.Mesh)
        {
            MeshComponent& mesh = instance.GetMeshComponent();
            mesh.MeshPath = overrides.MeshPath;
            mesh.PrimitiveType = overrides.PrimitiveType;
            if (!mesh.MeshPath.empty() && ModelImporter::IsSupportedModel(mesh.MeshPath))
                mesh.MeshData = MeshCache::Load(mesh.MeshPath);
            else
                mesh.MeshData = MeshCache::CreateCubeInstance();
        }
        if (overrides.Color)
        {
            instance.GetColorComponent().Color = overrides.ColorValue;
            Material& material = instance.GetMaterialComponent().MaterialData;
            material.Color[0] = overrides.ColorValue.X;
            material.Color[1] = overrides.ColorValue.Y;
            material.Color[2] = overrides.ColorValue.Z;
        }
        if (overrides.Material) instance.GetMaterialComponent().MaterialData = overrides.MaterialValue;
        if (overrides.Physics)
        {
            instance.PhysicsEnabled = overrides.PhysicsEnabled;
            instance.UseGravity = overrides.UseGravity;
            instance.IsStatic = overrides.IsStatic;
            instance.ColliderEnabled = overrides.ColliderEnabled;
            instance.IsTrigger = overrides.IsTrigger;
            instance.Collider = overrides.Collider;
            instance.Mass = overrides.Mass;
            instance.Restitution = overrides.Restitution;
            instance.Friction = overrides.Friction;
            instance.PickingRadius = overrides.PickingRadius;
            instance.AngularVelocity = overrides.AngularVelocity;
            instance.ContinuousCollision = overrides.ContinuousCollision;
            instance.FreezeRotation = overrides.FreezeRotation;
        }
        if (overrides.Metadata)
        {
            instance.Active = overrides.Active;
            instance.Opacity = overrides.Opacity;
            instance.CastShadows = overrides.CastShadows;
            instance.RaycastTarget = overrides.RaycastTarget;
            instance.Layer = overrides.Layer;
            instance.Tag = overrides.Tag;
        }
        if (overrides.Light)
        {
            if (overrides.HasLight)
            {
                if (!instance.HasLight()) instance.AddLight();
                instance.GetLightComponent() = overrides.LightValue;
            }
            else if (instance.HasLight()) instance.RemoveLight();
        }
        if (overrides.Audio)
        {
            if (overrides.HasAudio)
            {
                if (!instance.HasAudio()) instance.AddAudio();
                instance.GetAudioComponent() = overrides.AudioValue;
            }
            else if (instance.HasAudio()) instance.RemoveAudio();
        }
    }

} // namespace

// ---------------------------------------------------------------------------
// API pública
// ---------------------------------------------------------------------------

bool Prefab::HasHierarchy(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) return false;
    std::string header;
    std::getline(file, header);
    return header.rfind(k_HierarchyHeader, 0) == 0;
}

bool Prefab::Save(const Entity& entity, const std::string& path, const Scene* scene)
{
    std::ofstream file(path);
    if (!file.is_open())
    {
        std::cerr << "[Prefab] Nao foi possivel salvar em: " << path << std::endl;
        return false;
    }

    // Se temos acesso à cena e a entidade possui filhos, salva no formato hierárquico v3
    if (scene != nullptr && !entity.GetChildrenIDs().empty())
    {
        file << k_HierarchyHeader << "\n";
        SaveNodeRecursive(file, *scene, entity, false);
    }
    else
    {
        // Se temos cena e não tem filhos, também salvamos no formato hierárquico v3
        // para padronização completa de nós e transforms locais
        file << k_HierarchyHeader << "\n";
        file << "@node_begin\n";
        WriteEntityData(file, entity, false);
        file << "@children_count 0\n";
        file << "@node_end\n";
    }

    file.close();
    std::cout << "[Prefab] Salvo com sucesso: " << path << std::endl;
    return true;
}

bool Prefab::Load(Entity& entity, const std::string& path)
{
    if (HasHierarchy(path))
    {
        std::ifstream file(path);
        if (!file.is_open()) return false;
        std::string dummyHeader;
        std::getline(file, dummyHeader); // Pula @prefab_hierarchy_v3

        std::string line;
        while (std::getline(file, line))
        {
            if (line.rfind("@node_begin", 0) == 0) break;
        }

        std::string nextHeader;
        return ReadEntityFieldsFromStream(file, entity, nextHeader);
    }
    return ReadLegacyEntityData(entity, path);
}

Entity& Prefab::Instantiate(Scene& scene, const std::string& path)
{
    if (HasHierarchy(path))
    {
        std::ifstream file(path);
        if (file.is_open())
        {
            std::string header;
            std::getline(file, header);
            Entity* root = ReadHierarchyNode(file, scene, 0, path, nullptr);
            if (root)
            {
                StorePrefabBaseline(*root);
                scene.UpdateTransforms();
                std::cout << "[Prefab] Instanciado hierarquicamente: " << path << std::endl;
                return *root;
            }
        }
    }

    // Fallback para legado
    Entity& entity = scene.CreateEntity();
    ReadLegacyEntityData(entity, path);
    entity.PrefabPath = path;
    StorePrefabBaseline(entity);
    scene.UpdateTransforms();
    std::cout << "[Prefab] Instanciado: " << path << std::endl;
    return entity;
}

bool Prefab::ApplyToFile(const Entity& entity, const Scene* scene)
{
    if (entity.PrefabPath.empty())
    {
        std::cerr << "[Prefab] ApplyToFile: entidade nao tem PrefabPath definido.\n";
        return false;
    }
    return Save(entity, entity.PrefabPath, scene);
}

bool Prefab::RevertInstance(Entity& entity, Scene* scene)
{
    if (entity.PrefabPath.empty())
    {
        std::cerr << "[Prefab] RevertInstance: entidade nao tem PrefabPath definido.\n";
        return false;
    }

    const std::string prefabPath = entity.PrefabPath;
    const Transform savedTransform = const_cast<Entity&>(entity).GetTransform();
    const uint64_t rootID = entity.GetID();

    if (scene != nullptr && HasHierarchy(prefabPath))
    {
        const uint64_t parentID = entity.GetParentID();
        // Remove todos os filhos anteriores vinculados a essa instância do prefab
        std::vector<uint64_t> descendants;
        CollectPrefabDescendants(*scene, rootID, prefabPath, descendants);
        for (uint64_t did : descendants)
        {
            scene->RemoveParent(did);
            DeleteEntityByID(*scene, did);
        }

        std::ifstream file(prefabPath);
        if (file.is_open())
        {
            std::string header;
            std::getline(file, header);
            Entity* root = scene->FindEntityByID(rootID);
            if (!root) return false;
            ReadHierarchyNode(file, *scene, parentID, prefabPath, root);
            root = scene->FindEntityByID(rootID);
            if (!root) return false;
            root->PrefabPath = prefabPath;
            root->GetTransform() = savedTransform;

            Entity prefabSource;
            if (Load(prefabSource, prefabPath))
                StorePrefabBaselineFor(rootID, prefabSource);
        }
    }
    else
    {
        ReadLegacyEntityData(entity, prefabPath);
        entity.PrefabPath = prefabPath;
        entity.GetTransform() = savedTransform;
        StorePrefabBaseline(entity);
    }

    if (scene)
    {
        scene->UpdateTransforms();
    }

    return true;
}

void Prefab::ApplyToAllInstances(Scene& scene,
                                const std::string& prefabPath,
                                bool keepTransform)
{
    Entity prefabSource;
    if (!Prefab::Load(prefabSource, prefabPath))
    {
        std::cerr << "[Prefab] ApplyToAllInstances: nao foi possivel carregar a fonte: "
                  << prefabPath << "\n";
        return;
    }

    if (!HasHierarchy(prefabPath))
    {
        // Se for simples, aplica diretamente nas instâncias existentes
        int updated = 0;
        for (Entity& entity : scene.GetEntities())
        {
            if (entity.PrefabPath != prefabPath) continue;

            const auto baseline = s_PrefabBaselines.find(entity.GetID());
            const PrefabOverrides overrides = baseline != s_PrefabBaselines.end()
                ? CaptureOverrides(entity, *baseline->second, keepTransform)
                : CaptureTransformOverride(entity, keepTransform);

            ReadLegacyEntityData(entity, prefabPath);
            entity.PrefabPath = prefabPath;
            ApplyOverrides(entity, overrides);
            StorePrefabBaselineFor(entity.GetID(), prefabSource);

            ++updated;
        }
        scene.UpdateTransforms();
        std::cout << "[Prefab] ApplyToAllInstances: " << updated << " instancia(s) atualizada(s).\n";
        return;
    }

    // Hierárquico: identifica todas as raízes das instâncias deste prefab
    std::vector<uint64_t> rootIDs;
    for (const Entity& entity : scene.GetEntities())
    {
        if (entity.PrefabPath == prefabPath)
        {
            // É raiz da instância se não tem pai OU se o pai não pertence a esse mesmo prefab
            Entity* parent = scene.FindParent(entity);
            if (!parent || parent->PrefabPath != prefabPath)
            {
                rootIDs.push_back(entity.GetID());
            }
        }
    }

    int count = 0;
    for (uint64_t rid : rootIDs)
    {
        Entity* root = scene.FindEntityByID(rid);
        if (!root) continue;

        const auto baseline = s_PrefabBaselines.find(root->GetID());
        const PrefabOverrides overrides = baseline != s_PrefabBaselines.end()
            ? CaptureOverrides(*root, *baseline->second, keepTransform)
            : CaptureTransformOverride(*root, keepTransform);
        const uint64_t parentID = root->GetParentID();

        // Remove filhos antigos vinculados a esta instância
        std::vector<uint64_t> descendants;
        CollectPrefabDescendants(scene, rid, prefabPath, descendants);
        for (uint64_t did : descendants)
        {
            scene.RemoveParent(did);
            DeleteEntityByID(scene, did);
        }

        // DeleteEntity usa std::vector::erase; qualquer ponteiro/referência
        // anterior pode ter sido invalidado mesmo quando a raiz sobreviveu.
        root = scene.FindEntityByID(rid);
        if (!root) continue;

        // Recarrega do arquivo recriando toda a nova hierarquia de nós
        std::ifstream file(prefabPath);
        if (file.is_open())
        {
            std::string header;
            std::getline(file, header);
            ReadHierarchyNode(file, scene, parentID, prefabPath, root);
        }

        root = scene.FindEntityByID(rid);
        if (!root) continue;
        root->PrefabPath = prefabPath;
        ApplyOverrides(*root, overrides);
        StorePrefabBaselineFor(root->GetID(), prefabSource);

        ++count;
    }

    scene.UpdateTransforms();
    std::cout << "[Prefab] ApplyToAllInstances (Hierarquico): " << count
              << " hierarquia(s) de instancias atualizada(s) com sucesso de " << prefabPath << std::endl;
}

void Prefab::UnlinkHierarchy(Scene& scene, Entity& entity)
{
    s_PrefabBaselines.erase(entity.GetID());
    entity.PrefabPath.clear();
    for (uint64_t cid : entity.GetChildrenIDs())
    {
        Entity* child = scene.FindEntityByID(cid);
        if (child)
        {
            UnlinkHierarchy(scene, *child);
        }
    }
}

int Prefab::RunRegressionTests()
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "marae_prefab_m006_regression.prefab";
    std::error_code error;
    std::filesystem::remove(path, error);

    Scene scene;
    Entity& source = scene.CreateEntity();
    source.SetName("PrefabSource");
    source.GetColorComponent().Color = Vector3(0.8f, 0.1f, 0.1f);
    source.GetMaterialComponent().MaterialData.Type = MaterialType::Plastic;
    source.Mass = 1.0f;
    source.AddLight(LightType::Point);
    source.GetLightComponent().Intensity = 12.0f;
    source.AddAudio();
    source.GetAudioComponent().Volume = 0.7f;
    if (!Save(source, path.string(), &scene))
    {
        std::cerr << "[PrefabTests] falha ao salvar prefab inicial\n";
        return 1;
    }

    const uint64_t firstID = Instantiate(scene, path.string()).GetID();
    const uint64_t secondID = Instantiate(scene, path.string()).GetID();
    Entity* first = scene.FindEntityByID(firstID);
    Entity* second = scene.FindEntityByID(secondID);
    if (!first || !second)
    {
        std::cerr << "[PrefabTests] falha ao instanciar duas raizes\n";
        std::filesystem::remove(path, error);
        return 2;
    }

    first->GetTransform().Position = Vector3(10.0f, 2.0f, -3.0f);
    first->GetColorComponent().Color = Vector3(0.1f, 0.2f, 0.9f);
    first->GetMaterialComponent().MaterialData.Type = MaterialType::Neon;
    first->Mass = 4.0f;
    first->Tag = "override";
    first->GetLightComponent().Intensity = 22.0f;
    first->GetAudioComponent().Volume = 0.25f;
    first->GetAudioComponent().Loop = true;
    first->IsTrigger = true;
    second->GetTransform().Position = Vector3(-5.0f, 1.0f, 0.0f);

    Entity changedSource;
    if (!Load(changedSource, path.string()))
    {
        std::cerr << "[PrefabTests] falha ao carregar fonte para alteracao\n";
        std::filesystem::remove(path, error);
        return 3;
    }
    changedSource.GetColorComponent().Color = Vector3(0.1f, 0.8f, 0.2f);
    changedSource.GetMaterialComponent().MaterialData.Type = MaterialType::Metal;
    changedSource.Mass = 2.0f;
    changedSource.GetLightComponent().Type = LightType::Spot;
    changedSource.GetLightComponent().Intensity = 33.0f;
    changedSource.GetAudioComponent().Volume = 0.4f;
    if (!Save(changedSource, path.string()))
    {
        std::cerr << "[PrefabTests] falha ao salvar fonte alterada\n";
        std::filesystem::remove(path, error);
        return 4;
    }

    ApplyToAllInstances(scene, path.string(), true);
    first = scene.FindEntityByID(firstID);
    second = scene.FindEntityByID(secondID);
    const bool firstOverridesSurvived =
        first && NearlyEqual(first->GetTransform().Position.X, 10.0f) &&
        NearlyEqual(first->GetTransform().Position.Y, 2.0f) &&
        first->GetMaterialComponent().MaterialData.Type == MaterialType::Neon &&
        NearlyEqual(first->GetColorComponent().Color.Z, 0.9f) &&
        NearlyEqual(first->Mass, 4.0f) && first->Tag == "override" &&
        first->HasLight() && NearlyEqual(first->GetLightComponent().Intensity, 22.0f) &&
        first->HasAudio() && NearlyEqual(first->GetAudioComponent().Volume, 0.25f) &&
        first->GetAudioComponent().Loop && first->IsTrigger;
    const bool secondUpdatedAndMovedTransformSurvived =
        second && NearlyEqual(second->GetTransform().Position.X, -5.0f) &&
        second->GetMaterialComponent().MaterialData.Type == MaterialType::Metal &&
        NearlyEqual(second->GetColorComponent().Color.Y, 0.8f) &&
        NearlyEqual(second->Mass, 2.0f) && second->HasLight() &&
        second->GetLightComponent().Type == LightType::Spot &&
        NearlyEqual(second->GetLightComponent().Intensity, 33.0f) &&
        second->HasAudio() && NearlyEqual(second->GetAudioComponent().Volume, 0.4f) &&
        !second->IsTrigger;
    if (!firstOverridesSurvived || !secondUpdatedAndMovedTransformSurvived)
    {
        std::cerr << "[PrefabTests] ApplyToAllInstances perdeu override ou nao propagou fonte\n";
        std::filesystem::remove(path, error);
        return 5;
    }

    if (!RevertInstance(*first, &scene) ||
        first->GetMaterialComponent().MaterialData.Type != MaterialType::Metal ||
        !NearlyEqual(first->GetTransform().Position.X, 10.0f) ||
        !NearlyEqual(first->GetLightComponent().Intensity, 33.0f) ||
        !NearlyEqual(first->GetAudioComponent().Volume, 0.4f) || first->IsTrigger)
    {
        std::cerr << "[PrefabTests] RevertInstance nao restaurou fonte preservando transform\n";
        std::filesystem::remove(path, error);
        return 6;
    }
    UnlinkHierarchy(scene, *first);
    const bool unlinked = first->PrefabPath.empty();
    std::filesystem::remove(path, error);
    if (!unlinked)
    {
        std::cerr << "[PrefabTests] UnlinkHierarchy nao removeu vinculo\n";
        return 7;
    }

    std::cout << "[PrefabTests] M-006 passou: fonte propagada, overrides preservados e revert/unlink OK.\n";
    return 0;
}
