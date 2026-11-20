#pragma once

#include <vector>
#include <string>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

// Simple animation primitives for transform animation (position/rotation/scale)
struct KeyframeVec3
{
	float Time = 0.0f;
	glm::vec3 Value = glm::vec3(0.0f);
};

struct KeyframeQuat
{
	float Time = 0.0f;
	glm::quat Value = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
};

struct AnimationNode
{
	std::string Name;
	int Parent = -1;
	glm::vec3 BindTranslation{ 0.0f };
	glm::quat BindRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
	glm::vec3 BindScale{ 1.0f };
};

class AnimationCurveVec3
{
public:
	std::vector<KeyframeVec3> Keys;

	// Evaluate with linear interpolation, clamps to ends
	glm::vec3 Evaluate(float time) const;
};

class AnimationCurveQuat
{
public:
	std::vector<KeyframeQuat> Keys;

	// Evaluate with spherical linear interpolation
	glm::quat Evaluate(float time) const;
};

struct AnimationNodeTrack
{
	int NodeIndex = -1;
	AnimationCurveVec3 Translation;
	AnimationCurveQuat Rotation;
	AnimationCurveVec3 Scale;
};

class AnimationClip
{
public:
	AnimationClip();

	// Cria um clip inicial neutro, sem movimento pré-programado. O auto-keying
	// da timeline adiciona as poses conforme o usuário transforma o objeto.
	static std::shared_ptr<AnimationClip> CreateDefault(
		const std::string& name,
		float duration,
		const glm::vec3& position,
		const glm::quat& rotation,
		const glm::vec3& scale);

	// Identificador estável do asset de animação. É salvo na cena e pode ser
	// consultado/validado pelos scripts C#.
	std::string ID;
	std::string Name;
	float Duration = 0.0f;
	bool Loop = true;

	AnimationCurveVec3 Position;
	AnimationCurveQuat Rotation;
	AnimationCurveVec3 Scale;
	std::vector<AnimationNodeTrack> NodeTracks;

	// Evaluate local transform at time (seconds)
	glm::mat4 EvaluateLocalTransform(float time) const;
};

struct SkeletalAnimationAsset
{
	std::vector<AnimationNode> Nodes;
	std::vector<int> JointNodeIndices;
	std::vector<glm::mat4> InverseBindMatrices;
	int MeshNodeIndex = -1;
	std::vector<std::shared_ptr<AnimationClip>> Clips;
};
