#pragma once
#include "Ray.h"
#include <stdio.h>
#include "AABB.h"
#include <glm/glm.hpp>

class Scene;
class Camera;

class Picking
{
public:
    struct Hit
    {
        int EntityIndex = -1;
        int TriangleIndex = -1;
        float Distance = 0.0f;
        glm::vec3 WorldPosition{ 0.0f };
        glm::vec3 LocalPosition{ 0.0f };
        glm::vec3 LocalNormal{ 0.0f, 1.0f, 0.0f };
    };

    static Hit PickEntityDetailed(
        Scene& scene,
        Camera& camera,
        float mouseX,
        float mouseY,
        int viewportWidth,
        int viewportHeight
    );

    static int PickEntity(
        Scene& scene,
        Camera& camera,
        float mouseX,
        float mouseY,
        int viewportWidth,
        int viewportHeight
    );

    static Ray ScreenPointToRay(
        Camera& camera,
        float mouseX,
        float mouseY,
        int viewportWidth,
        int viewportHeight        
        );
    
    static void PrintRayDirection(const Ray& ray)
    {
    }

    static bool RayIntersectsAABB(
            const Ray & ray,
            const AABB & box,
            float* hitDistance = nullptr
        );
};
