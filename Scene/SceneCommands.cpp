#include "SceneCommands.h"
#include "../Scene/Scene/Scene.h"
#include "../Scene/Entity/Entity.h"
#include "Components/ColorComponent.h"
#include "../../Renderer/MeshFactory/MeshFactory.h"
#include "../../Renderer/Mesh/MeshCache.h"
#include "../Scene/Entity/Entity.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <regex>
#include <string>

static std::string StripCopySuffix(const std::string& name, std::string& outBase)
{
    // Tentar padrao " Copy N" com numeros no final
    std::regex copyNRe(R"(\s+Copy\s+(\d+)$)");
    std::smatch m;
    if (std::regex_search(name, m, copyNRe))
    {
        outBase.assign(name.begin(), m[0].first);
        return outBase;
    }
    // Tentar padrao " Copy" no final
    const std::string suffix = " Copy";
    if (name.size() >= suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
    {
        outBase = name.substr(0, name.size() - suffix.size());
        return outBase;
    }
    outBase = name;
    return name;
}

static int NextCopyIndex(const std::vector<Entity>& entities, const std::string& baseName)
{
    std::regex copyNRe(R"(\s+Copy\s+(\d+)$)");
    const std::string plainSuffix = " Copy";
    int maxIdx = 0;
    for (const auto& e : entities)
    {
        const std::string& ename = e.GetName();
        if (ename.size() <= baseName.size()) continue;
        if (ename.compare(0, baseName.size(), baseName) != 0) continue;

        const char* p = ename.c_str() + baseName.size();
        // Primeiro char apos base deve ser espaco
        if (*p != ' ') continue;
        p++;

        // Segue "Copy" ?
        const std::string cs = "Copy";
        if (strncmp(p, cs.c_str(), cs.size()) != 0) continue;
        p += cs.size();

        if (*p == '\0')
        {
            // "Base Copy" sem numero -> conta como 1
            if (maxIdx < 1) maxIdx = 1;
            continue;
        }
        if (*p == ' ')
        {
            p++;
            const char* numStart = p;
            char* endp = nullptr;
            long v = std::strtol(numStart, &endp, 10);
            if (endp && endp != numStart && *endp == '\0' && v > 0)
            {
                int iv = (int)v;
                if (maxIdx < iv) maxIdx = iv;
            }
        }
    }
    return maxIdx + 1;
}

void SceneCommands::DeleteEntity(
    Scene& scene,
    int index)
{
    scene.DeleteEntity(index);
}

int SceneCommands::DuplicateEntity(
    Scene& scene,
    int index)
{
    auto& entities =
        scene.GetEntities();

    if (index < 0 ||
        index >= (int)entities.size())
    {
        return index;
    }

    // Se a entidade é filha de um pai (ex: Model), o transform é local.
    // Atualizar os transforms ANTES de copiar a origem para garantir que
    // WorldMatrix esteja correta na referência da cena.
    const bool hasParent = (entities[static_cast<size_t>(index)].GetParentID() != 0);
    if (hasParent)
        scene.UpdateTransforms();

    // Capturar WorldMatrix antes de qualquer mutação (CreateEntity pode realocar).
    glm::mat4 capturedWorld = entities[static_cast<size_t>(index)].GetTransform().WorldMatrix;

    // CreateEntity pode realocar o vetor. Copie a origem depois de capturar a WorldMatrix.
    Entity source = entities[static_cast<size_t>(index)];

    std::string originalName = source.GetName();

    std::string baseName;
    StripCopySuffix(originalName, baseName);

    int nextIdx = NextCopyIndex(entities, baseName);
    std::string newName = baseName + " Copy " + std::to_string(nextIdx);

    // Converter para coordenadas de mundo usando a WorldMatrix capturada,
    // para que o clone seja criado na posição correta no espaço global.
    Transform transform = source.GetTransform();
    if (hasParent)
    {
        glm::vec3 translation(0.0f), scale(1.0f), skew(0.0f);
        glm::vec4 perspective(0.0f);
        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        if (glm::decompose(capturedWorld, scale, rotation, translation, skew, perspective))
        {
            transform.Position = Vector3(translation.x, translation.y, translation.z);
            transform.Rotation = glm::normalize(rotation);
            transform.Scale    = Vector3(scale.x, scale.y, scale.z);
            const glm::vec3 euler = glm::degrees(glm::eulerAngles(transform.Rotation));
            transform.EulerRotation = Vector3(euler.x, euler.y, euler.z);
        }
    }

    ColorComponent color =
        source.GetColorComponent();

    Entity& clone =
        scene.CreateEntity();

    clone.SetName(newName);

    const auto& sourceMesh = source.GetMeshComponent().MeshData;
    if (sourceMesh && sourceMesh->IsValid())
    {
        clone.GetMeshComponent().MeshData = std::make_shared<Mesh>();
        clone.GetMeshComponent().MeshData->Create(
            sourceMesh->GetVertices(),
            sourceMesh->GetPrimitiveType(),
            "SceneCommands::DuplicateMesh");
    }
    else
    {
        clone.GetMeshComponent().MeshData = MeshCache::CreateCubeInstance();
    }
    clone.GetMeshComponent().MeshPath =
        source.GetMeshComponent().MeshPath;
    // Preserve o tipo do primitivo interno. Sem isso, uma cópia de cilindro,
    // esfera ou cone era salva como cubo na próxima abertura da cena.
    clone.GetMeshComponent().PrimitiveType =
        source.GetMeshComponent().PrimitiveType;

    clone.GetTransform() =
        transform;

    clone.GetColorComponent() =
        color;

    clone.PhysicsEnabled = source.PhysicsEnabled;
    clone.UseGravity = source.UseGravity;
    clone.IsStatic = source.IsStatic;
    clone.ColliderEnabled = source.ColliderEnabled;
    clone.IsTrigger = source.IsTrigger;
    clone.Collider = source.Collider;
    clone.Mass = source.Mass;
    clone.Restitution = source.Restitution;
    clone.Friction = source.Friction;
    clone.PickingRadius = source.PickingRadius;
    clone.Velocity = source.Velocity;
    clone.AngularVelocity = source.AngularVelocity;
    clone.ContinuousCollision = source.ContinuousCollision;
    clone.FreezeRotation = source.FreezeRotation;
    clone.Active = source.Active;
    clone.Opacity = source.Opacity;
    clone.CastShadows = source.CastShadows;
    clone.RaycastTarget = source.RaycastTarget;
    clone.Layer = source.Layer;
    clone.Tag = source.Tag;
    clone.GetScriptComponent() = source.GetScriptComponent();
    clone.GetAnimationComponent() = source.GetAnimationComponent();
    clone.GetMaterialComponent() = source.GetMaterialComponent();
    clone.AudioAcousticGeometryEnabled = source.AudioAcousticGeometryEnabled;
    clone.AudioOccluder = source.AudioOccluder;
    clone.AudioTransmissionEnabled = source.AudioTransmissionEnabled;
    clone.AudioTransmission = source.AudioTransmission;
    clone.AudioAbsorption = source.AudioAbsorption;
    clone.AudioDiffraction = source.AudioDiffraction;

    if (source.HasLight())
    {
        clone.AddLight(source.GetLightComponent().Type);
        clone.GetLightComponent() = source.GetLightComponent();
    }
    if (source.HasAudio())
    {
        clone.AddAudio();
        clone.GetAudioComponent() = source.GetAudioComponent();
        // A duplicated source must not control the original voice or reuse its
        // public ID. Playback state is runtime-only and starts stopped.
        clone.GetAudioComponent().RuntimeVoiceID = AUDIO_INVALID_VOICE;
        clone.GetAudioComponent().PlayState = AudioPlayState::Stopped;
        clone.GetAudioComponent().SourceID = "audio_" + std::to_string(clone.GetID());
    }
    if (source.HasUI())
    {
        clone.AddUI(source.GetUIComponent().Type);
        clone.GetUIComponent() = source.GetUIComponent();
    }
    if (source.HasParticleSystem())
    {
        clone.AddParticleSystem();
        clone.GetParticleSystem() = source.GetParticleSystem();
    }
    if (source.HasAI())
    {
        clone.AddAI();
        clone.GetAIComponent() = source.GetAIComponent();
        clone.GetAIComponent().ResetRuntime();
    }

    // O clone é criado na mesma posição do original

    return (int)scene.GetEntities().size() - 1;
}

Entity& SceneCommands::CreateCube(
    Scene& scene)
{
    Entity& cube = scene.CreateEntity();
    cube.GetMeshComponent().MeshData = MeshCache::CreateCubeInstance();
    return cube;
}
