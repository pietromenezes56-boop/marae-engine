#pragma once

#include "../../Scene/Scene/Scene.h"

class Entity;

class AnimationSystem
{
public:
	static void BeginPlay(Scene& scene);
    static void Update(Scene& scene, float deltaTime);

	static void ApplyPoseAtTime(Entity& entity, float time, float deltaTime = 0.0f);
};
