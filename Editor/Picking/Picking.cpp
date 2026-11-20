#include "Picking.h"
#include <glad/glad.h>
#include "../../Scene/Entity/Entity.h"
#include "../../Scene/Transform/Transform.h"
#include <cmath>
#include <limits>
#include <algorithm>
#include "../../Scene/Scene/Scene.h"
#include "../../Scene/Camera/Camera.h"
#include "../../Renderer/CameraRenderer/CameraRenderer.h"
#include "Ray.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace
{
    bool RayTriangle(
        const glm::vec3& origin,
        const glm::vec3& direction,
        const Vertex& va,
        const Vertex& vb,
        const Vertex& vc,
        float& distance,
        glm::vec3& localNormal)
    {
        const glm::vec3 a(va.X, va.Y, va.Z);
        const glm::vec3 b(vb.X, vb.Y, vb.Z);
        const glm::vec3 c(vc.X, vc.Y, vc.Z);
        const glm::vec3 edge1 = b - a;
        const glm::vec3 edge2 = c - a;
        const glm::vec3 p = glm::cross(direction, edge2);
        const float determinant = glm::dot(edge1, p);
        if (std::abs(determinant) < 0.0000001f)
            return false;

        const float inverseDeterminant = 1.0f / determinant;
        const glm::vec3 toOrigin = origin - a;
        const float u = glm::dot(toOrigin, p) * inverseDeterminant;
        if (u < 0.0f || u > 1.0f)
            return false;

        const glm::vec3 q = glm::cross(toOrigin, edge1);
        const float v = glm::dot(direction, q) * inverseDeterminant;
        if (v < 0.0f || u + v > 1.0f)
            return false;

        const float t = glm::dot(edge2, q) * inverseDeterminant;
        if (t < 0.0f)
            return false;

        distance = t;
        const glm::vec3 geometricNormal = glm::cross(edge1, edge2);
        localNormal = glm::length(geometricNormal) > 0.000001f
            ? glm::normalize(geometricNormal)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        return true;
    }

    glm::mat4 BuildModel(const Transform& transform)
    {
        glm::mat4 model = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(transform.Position.X, transform.Position.Y, transform.Position.Z));
        model *= glm::mat4_cast(transform.Rotation);
        return glm::scale(model, glm::vec3(
            transform.Scale.X, transform.Scale.Y, transform.Scale.Z));
    }
}

Picking::Hit Picking::PickEntityDetailed(
    Scene& scene,
    Camera& camera,
    float mouseX,
    float mouseY,
    int viewportWidth,
    int viewportHeight)
{
    scene.UpdateTransforms();

    const auto& entities = scene.GetEntities();
    const Ray ray = ScreenPointToRay(
        camera, mouseX, mouseY, viewportWidth, viewportHeight);

    Hit best;
    best.Distance = std::numeric_limits<float>::max();

    for (size_t i = 0; i < entities.size(); ++i)
    {
        const Entity& entity = entities[i];
        if (entity.SelectionLocked || entity.IsEditorOnly() || !entity.Active ||
            entity.Opacity <= 0.001f || !entity.RaycastTarget ||
            entity.IsGlobalLight() || entity.GetName() == "Global Light" ||
            entity.IsGlobalScript() || entity.GetName() == "Global Script")
            continue;

        const Transform& transform = const_cast<Entity&>(entity).GetTransform();
        const glm::mat4 model = (entity.GetParentID() != 0) ? transform.WorldMatrix : BuildModel(transform);

        // Câmeras não possuem mesh de cena: o viewport desenha um ícone 2D
        // orientado no mundo. Ainda assim elas precisam participar do picking
        // para continuarem selecionáveis como qualquer outro componente.
        if (entity.HasCamera())
        {
            const glm::vec3 center = glm::vec3(model[3]);
            const float scaleX = glm::length(glm::vec3(model[0]));
            const float scaleY = glm::length(glm::vec3(model[1]));
            const float scaleZ = glm::length(glm::vec3(model[2]));
            const float scale = std::max({ scaleX, scaleY, scaleZ, 0.001f });
            const float radius = std::max(0.28f, entity.PickingRadius * scale * 1.35f);
            const glm::vec3 offset = ray.Origin - center;
            const float b = glm::dot(offset, ray.Direction);
            const float c = glm::dot(offset, offset) - radius * radius;
            const float discriminant = b * b - c;
            if (discriminant >= 0.0f)
            {
                const float root = std::sqrt(discriminant);
                float distance = -b - root;
                if (distance < 0.0f)
                    distance = -b + root;
                if (distance >= 0.0f && distance < best.Distance)
                {
                    best.EntityIndex = static_cast<int>(i);
                    best.TriangleIndex = -1;
                    best.Distance = distance;
                    best.WorldPosition = ray.Origin + ray.Direction * distance;
                    best.LocalPosition = glm::vec3(
                        glm::inverse(model) * glm::vec4(best.WorldPosition, 1.0f));
                    best.LocalNormal = glm::vec3(0.0f, 0.0f, 1.0f);
                }
            }
            continue;
        }

        const auto& mesh = const_cast<Entity&>(entity).GetMeshComponent().MeshData;
        if (!mesh || !mesh->IsValid() || mesh->GetVertices().size() < 3 ||
            mesh->GetPrimitiveType() != GL_TRIANGLES)
            continue;

        const glm::mat4 inverseModel = glm::inverse(model);
        const glm::vec3 localOrigin = glm::vec3(
            inverseModel * glm::vec4(ray.Origin, 1.0f));
        const glm::vec3 localDirection = glm::vec3(
            inverseModel * glm::vec4(ray.Direction, 0.0f));
        if (glm::length(localDirection) < 0.000001f)
            continue;

        const glm::vec3 normalizedLocalDirection = glm::normalize(localDirection);
        const auto& vertices = mesh->GetVertices();
        for (size_t vertexIndex = 0; vertexIndex + 2 < vertices.size(); vertexIndex += 3)
        {
            float localDistance = 0.0f;
            glm::vec3 localNormal(0.0f, 1.0f, 0.0f);
            if (!RayTriangle(
                    localOrigin, normalizedLocalDirection,
                    vertices[vertexIndex], vertices[vertexIndex + 1],
                    vertices[vertexIndex + 2], localDistance, localNormal))
                continue;

            const glm::vec3 localPosition = localOrigin +
                normalizedLocalDirection * localDistance;
            const glm::vec3 worldPosition = glm::vec3(
                model * glm::vec4(localPosition, 1.0f));
            const float worldDistance = glm::length(worldPosition - ray.Origin);
            if (worldDistance >= best.Distance)
                continue;

            best.EntityIndex = static_cast<int>(i);
            best.TriangleIndex = static_cast<int>(vertexIndex / 3);
            best.Distance = worldDistance;
            best.WorldPosition = worldPosition;
            best.LocalPosition = localPosition;
            best.LocalNormal = localNormal;
        }
    }

    if (best.EntityIndex < 0)
        best.Distance = 0.0f;
    return best;
}


int Picking::PickEntity(
    Scene& scene,
    Camera& camera,
    float mouseX,
    float mouseY,
    int viewportWidth,
    int viewportHeight
)
{
    return PickEntityDetailed(
        scene, camera, mouseX, mouseY, viewportWidth, viewportHeight).EntityIndex;

#if 0
    const auto& entities =
        scene.GetEntities();

    Ray ray =
        ScreenPointToRay(
            camera,
            mouseX,
            mouseY,
            viewportWidth,
            viewportHeight
        );

    PrintRayDirection(ray);

    int closestEntity = -1;
    float closestDistance = std::numeric_limits<float>::max();

    for (size_t i = 0;
        i < entities.size();
        i++)
    {
        if (entities[i].IsEditorOnly() || !entities[i].Active ||
            entities[i].Opacity <= 0.001f ||
            !entities[i].RaycastTarget)
        {
            continue;
        }

        const Transform& transform =
            const_cast<Entity&>(
                entities[i]
                ).GetTransform();

        // Constrói a matriz de modelo completa (posição + rotação + escala)
        glm::mat4 model = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(transform.Position.X, transform.Position.Y, transform.Position.Z));
        model *= glm::mat4_cast(transform.Rotation);
        model = glm::scale(model, glm::vec3(
            transform.Scale.X, transform.Scale.Y, transform.Scale.Z));

        // 8 cantos do OBB unitário local [-0.5, 0.5]^3. Isso corresponde a
        // um mesh unitário (padrão da MeshFactory), e considera a rotação
        // no cálculo da AABB em world space — antes o teste ignorava rotação,
        // então um cubo girado 45° tinha AABB menor que o objeto real,
        // causando "missed clicks" em toda a borda.
        const glm::vec3 localCorners[8] = {
            {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
            {-0.5f,  0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f},
            {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
            {-0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f,  0.5f}
        };

        glm::vec3 minCorner(std::numeric_limits<float>::max());
        glm::vec3 maxCorner(-std::numeric_limits<float>::max());
        for (const glm::vec3& corner : localCorners)
        {
            const glm::vec3 world = glm::vec3(model * glm::vec4(corner, 1.0f));
            minCorner = glm::min(minCorner, world);
            maxCorner = glm::max(maxCorner, world);
        }

        // Folga extra para casos onde o mesh real não é unitário ou tem
        // deslocamento (ex: pivô fora do centro). Evita falsos negativos
        // em objetos finos ou quase alinhados com o raio.
        const glm::vec3 padding(0.05f);
        AABB box;
        box.Min = minCorner - padding;
        box.Max = maxCorner + padding;

        float hitDistance = 0.0f;
        if (RayIntersectsAABB(ray, box, &hitDistance) &&
            hitDistance < closestDistance)
        {
            closestDistance = hitDistance;
            closestEntity = static_cast<int>(i);
        }
    }

    return closestEntity;
#endif
}
Ray Picking::ScreenPointToRay(
    Camera& camera,
    float mouseX,
    float mouseY,
    int viewportWidth,
    int viewportHeight
)
{
    Ray ray;

    float x =
        (2.0f * mouseX) /
        viewportWidth - 1.0f;

    float y =
        1.0f -
        (2.0f * mouseY) /
        viewportHeight;

    glm::vec4 rayClip(
        x,
        y,
        -1.0f,
        1.0f
    );

    CameraRenderer cameraRenderer;

    const float aspectRatio = viewportHeight > 0
        ? static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight)
        : 16.0f / 9.0f;

    // Os parâmetros precisam bater com Renderer::Render(). A projeção usada
    // para construir o raio deve ser idêntica àquela usada para desenhar a
    // cena, inclusive quando uma câmera ortográfica está ativa.
    glm::mat4 projection;
    const float nearClip = std::max(0.001f, camera.NearClip);
    const float farClip = std::max(nearClip + 0.01f, camera.FarClip);
    if (camera.Orthographic)
    {
        const float halfHeight = std::max(0.01f, camera.OrthographicSize) * 0.5f;
        const float halfWidth = halfHeight * aspectRatio;
        projection = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight,
                                nearClip, farClip);
    }
    else
    {
        projection = cameraRenderer.GetProjectionMatrix(
            std::clamp(camera.FieldOfView, 1.0f, 179.0f),
            aspectRatio,
            nearClip,
            farClip);
    }

    glm::mat4 view =
        cameraRenderer.GetViewMatrix(
            camera
        );

    glm::vec4 rayEye =
        glm::inverse(projection) *
        rayClip;

    rayEye.z = -1.0f;
    rayEye.w = 0.0f;

    glm::vec4 rayWorld =
        glm::inverse(view) *
        rayEye;

    glm::vec3 direction =
        glm::normalize(
            glm::vec3(rayWorld)
        );

    ray.Origin =
        glm::vec3(
            camera.Position.X,
            camera.Position.Y,
            camera.Position.Z
        );

    ray.Direction =
        direction;

    return ray;
}


bool Picking::RayIntersectsAABB(
    const Ray& ray,
    const AABB& box,
    float* hitDistance
)
{
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis)
    {
        const float origin = ray.Origin[axis];
        const float direction = ray.Direction[axis];
        const float minValue = box.Min[axis];
        const float maxValue = box.Max[axis];

        if (std::abs(direction) < 0.000001f)
        {
            if (origin < minValue || origin > maxValue)
                return false;
            continue;
        }

        const float inverseDirection = 1.0f / direction;
        float axisMin = (minValue - origin) * inverseDirection;
        float axisMax = (maxValue - origin) * inverseDirection;
        if (axisMin > axisMax)
            std::swap(axisMin, axisMax);

        tMin = std::max(tMin, axisMin);
        tMax = std::min(tMax, axisMax);
        if (tMin > tMax)
            return false;
    }

    if (tMax < 0.0f)
        return false;

    if (hitDistance)
        *hitDistance = std::max(tMin, 0.0f);
    return true;


}
