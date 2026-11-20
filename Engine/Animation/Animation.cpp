#include "Animation.h"
#include "../Assets/GUID/GUID.h"
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

AnimationClip::AnimationClip()
	: ID("anim_" + GUID::Generate())
{
}

std::shared_ptr<AnimationClip> AnimationClip::CreateDefault(
	const std::string& name,
	float duration,
	const glm::vec3& position,
	const glm::quat& rotation,
	const glm::vec3& scale)
{
	auto clip = std::make_shared<AnimationClip>();
	clip->Name = name;
	clip->Duration = std::max(0.1f, duration);
	clip->Loop = true;

	glm::quat baseRotation = rotation;
	if (glm::length(baseRotation) < 0.0001f)
		baseRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	else
		baseRotation = glm::normalize(baseRotation);

	// Duas poses iguais deixam o clip pronto para receber auto-keyframes sem
	// introduzir uma animação nativa inesperada.
	clip->Position.Keys.push_back({0.0f, position});
	clip->Position.Keys.push_back({clip->Duration, position});
	clip->Rotation.Keys.push_back({0.0f, baseRotation});
	clip->Rotation.Keys.push_back({clip->Duration, baseRotation});
	clip->Scale.Keys.push_back({0.0f, scale});
	clip->Scale.Keys.push_back({clip->Duration, scale});

	return clip;
}

glm::vec3 AnimationCurveVec3::Evaluate(float time) const
{
	if (Keys.empty()) return glm::vec3(0.0f);
	if (time <= Keys.front().Time) return Keys.front().Value;
	if (time >= Keys.back().Time) return Keys.back().Value;

	// find interval
	for (size_t i = 0; i + 1 < Keys.size(); ++i)
	{
		const KeyframeVec3& a = Keys[i];
		const KeyframeVec3& b = Keys[i + 1];
		if (time >= a.Time && time <= b.Time)
		{
			float span = b.Time - a.Time;
			float t = (span > 0.0f) ? ((time - a.Time) / span) : 0.0f;
			return glm::mix(a.Value, b.Value, t);
		}
	}

	return Keys.back().Value;
}

glm::quat AnimationCurveQuat::Evaluate(float time) const
{
	if (Keys.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	if (time <= Keys.front().Time) return Keys.front().Value;
	if (time >= Keys.back().Time) return Keys.back().Value;

	for (size_t i = 0; i + 1 < Keys.size(); ++i)
	{
		const KeyframeQuat& a = Keys[i];
		const KeyframeQuat& b = Keys[i + 1];
		if (time >= a.Time && time <= b.Time)
		{
			float span = b.Time - a.Time;
			float t = (span > 0.0f) ? ((time - a.Time) / span) : 0.0f;
			return glm::slerp(a.Value, b.Value, t);
		}
	}

	return Keys.back().Value;
}

glm::mat4 AnimationClip::EvaluateLocalTransform(float time) const
{
	if (Duration > 0.0f)
	{
		if (Loop)
		{
			time = fmod(time, Duration);
			if (time < 0.0f) time += Duration;
		}
		else
		{
			if (time < 0.0f) time = 0.0f;
			if (time > Duration) time = Duration;
		}
	}

	glm::vec3 pos = Position.Evaluate(time);
	glm::quat rot = Rotation.Evaluate(time);
	glm::vec3 scl = Scale.Evaluate(time);

	glm::mat4 m(1.0f);
	m = glm::translate(m, pos);
	m *= glm::toMat4(rot);
	m = glm::scale(m, scl);
	return m;
}
