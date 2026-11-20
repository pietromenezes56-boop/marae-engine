#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "../../Engine/Animation/Animation.h"

struct AnimationComponent
{
	struct State
	{
		std::string Name;
		std::shared_ptr<AnimationClip> Clip;
	};
	struct Transition
	{
		std::string From;
		std::string To;
		std::string BoolParameter;
		bool ExpectedValue = true;
		float ExitTimeNormalized = 0.0f;
		float BlendDuration = 0.15f;
	};
	bool StateMachineEnabled = false;
	std::string InitialState;
	std::string CurrentState;
	std::vector<State> States;
	std::vector<Transition> Transitions;
	std::unordered_map<std::string, bool> BoolParameters;
	std::shared_ptr<AnimationClip> Clip;
	std::shared_ptr<SkeletalAnimationAsset> ImportedAsset;
	std::vector<glm::mat4> SkinMatrices;
	// Cache de runtime para evitar recalcular a mesma pose quando a animação
	// está parada/pausada. Não é serializado como dado do projeto.
	const SkeletalAnimationAsset* CachedSkinAsset = nullptr;
	const AnimationClip* CachedSkinClip = nullptr;
	float CachedSkinSampleTime = -1.0f;
	std::string ImportedClipName;
	float Time = 0.0f;
	float Speed = 1.0f;
	float Weight = 1.0f; // for blending
	bool Playing = false;
	bool Enabled = true;
	// Clips começam automaticamente no Play por padrão; o usuário ainda pode
	// desligar esta opção no Inspector para animações acionadas por script.
	bool PlayOnStart = true;

	// New smoothing / easing / blend options
	enum class EasingType
	{
		Linear = 0,
		SmoothStep,
		EaseIn,
		EaseOut,
		EaseInOut
	};

	bool UseSmoothing = false;    // enable exponential smoothing toward target pose
	float Smoothness = 8.0f;     // higher = snappier (used in exp smoothing)
	EasingType Easing = EasingType::Linear; // easing applied when blending
	float BlendIn = 0.0f;        // seconds to blend in
	float BlendOut = 0.0f;       // seconds to blend out
	float RuntimeBlend = 1.0f;   // current runtime blend (managed by system)
	// Source pose held constant while a state transition cross-fades.
	bool TransitionBlending = false;
	float TransitionBlendElapsed = 0.0f;
	float TransitionBlendDuration = 0.0f;
	glm::vec3 TransitionSourcePosition{ 0.0f };
	glm::quat TransitionSourceRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
	glm::vec3 TransitionSourceScale{ 1.0f };

	void Play() { Playing = true; }
	void Pause() { Playing = false; }
	void Stop() { Playing = false; Time = 0.0f; }

	void SetClip(const std::shared_ptr<AnimationClip>& clip)
	{
		Clip = clip;
		Time = 0.0f;
	}
};
