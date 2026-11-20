#include "AnimationSystem.h"
#include "../../Scene/Scene/Scene.h"
#include "../../Scene/Entity/Entity.h"
#include "../../Scene/Components/AnimationComponent.h"
#include "../../Renderer/Shader/Shader.h"
#include <glm/gtx/quaternion.hpp>
#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
    struct SkinningScratch
    {
        std::vector<glm::vec3> translations;
        std::vector<glm::quat> rotations;
        std::vector<glm::vec3> scales;
        std::vector<glm::mat4> local;
        std::vector<glm::mat4> global;
        std::vector<uint8_t> visit;
    };

    void UpdateImportedSkin(AnimationComponent& animation, float time)
    {
        const auto& asset = animation.ImportedAsset;
        if (!asset || asset->Nodes.empty() || asset->JointNodeIndices.empty()) return;
        if ((!animation.Clip || animation.Clip->NodeTracks.empty()) && !asset->Clips.empty())
        {
            auto selected = std::find_if(asset->Clips.begin(), asset->Clips.end(), [&](const auto& clip)
                { return clip && clip->Name == animation.ImportedClipName; });
            animation.Clip = selected != asset->Clips.end() ? *selected : asset->Clips.front();
            animation.ImportedClipName = animation.Clip->Name;
        }

        const auto& nodes = asset->Nodes;
        const float duration = animation.Clip ? animation.Clip->Duration : 0.0f;
        float sampleTime = time;
        if (duration > 0.0f)
        {
            if (animation.Clip->Loop) { sampleTime = std::fmod(sampleTime, duration); if (sampleTime < 0.0f) sampleTime += duration; }
            else sampleTime = std::clamp(sampleTime, 0.0f, duration);
        }
        const size_t jointCount = std::min<size_t>(asset->JointNodeIndices.size(), Shader::MAX_SKIN_JOINTS);
        const float cacheTime = animation.Clip && !animation.Clip->NodeTracks.empty() ? sampleTime : 0.0f;
        if (animation.CachedSkinAsset == asset.get() && animation.CachedSkinClip == animation.Clip.get() &&
            animation.CachedSkinSampleTime == cacheTime && animation.SkinMatrices.size() == jointCount)
            return;

        // UpdateImportedSkin roda serialmente no loop da engine. Um scratch
        // reutilizado por thread elimina seis alocações por personagem/quadro
        // sem manter buffers grandes duplicados em cada componente.
        thread_local SkinningScratch scratch;
        auto& translations = scratch.translations;
        auto& rotations = scratch.rotations;
        auto& scales = scratch.scales;
        auto& local = scratch.local;
        auto& global = scratch.global;
        auto& visit = scratch.visit;
        translations.resize(nodes.size());
        rotations.resize(nodes.size());
        scales.resize(nodes.size());
        local.resize(nodes.size());
        global.resize(nodes.size());
        visit.resize(nodes.size());
        std::fill(visit.begin(), visit.end(), uint8_t{ 0 });
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            translations[i] = nodes[i].BindTranslation;
            rotations[i] = nodes[i].BindRotation;
            scales[i] = nodes[i].BindScale;
        }
        if (animation.Clip)
        {
            for (const AnimationNodeTrack& track : animation.Clip->NodeTracks)
            {
                if (track.NodeIndex < 0 || static_cast<size_t>(track.NodeIndex) >= nodes.size()) continue;
                const size_t i = static_cast<size_t>(track.NodeIndex);
                if (!track.Translation.Keys.empty()) translations[i] = track.Translation.Evaluate(sampleTime);
                if (!track.Rotation.Keys.empty()) rotations[i] = track.Rotation.Evaluate(sampleTime);
                if (!track.Scale.Keys.empty()) scales[i] = track.Scale.Evaluate(sampleTime);
            }
        }
        for (size_t i = 0; i < nodes.size(); ++i)
            local[i] = glm::translate(glm::mat4(1.0f), translations[i]) * glm::mat4_cast(rotations[i]) * glm::scale(glm::mat4(1.0f), scales[i]);

        auto resolveGlobal = [&](auto&& self, size_t i) -> const glm::mat4& {
            if (visit[i] == 2) return global[i];
            if (visit[i] == 1) { global[i] = local[i]; visit[i] = 2; return global[i]; }
            visit[i] = 1;
            const int parent = nodes[i].Parent;
            global[i] = parent >= 0 && static_cast<size_t>(parent) < nodes.size()
                ? self(self, static_cast<size_t>(parent)) * local[i] : local[i];
            visit[i] = 2;
            return global[i];
        };
        for (size_t i = 0; i < nodes.size(); ++i) resolveGlobal(resolveGlobal, i);

        glm::mat4 inverseMesh(1.0f);
        if (asset->MeshNodeIndex >= 0 && static_cast<size_t>(asset->MeshNodeIndex) < global.size())
            inverseMesh = glm::inverse(global[static_cast<size_t>(asset->MeshNodeIndex)]);
        animation.SkinMatrices.resize(jointCount);
        for (size_t i = 0; i < jointCount; ++i)
        {
            const int node = asset->JointNodeIndices[i];
            if (node < 0 || static_cast<size_t>(node) >= global.size()) continue;
            const glm::mat4 inverseBind = i < asset->InverseBindMatrices.size()
                ? asset->InverseBindMatrices[i] : glm::mat4(1.0f);
            animation.SkinMatrices[i] = inverseMesh * global[static_cast<size_t>(node)] * inverseBind;
        }
        animation.CachedSkinAsset = asset.get();
        animation.CachedSkinClip = animation.Clip.get();
        animation.CachedSkinSampleTime = cacheTime;
    }

    bool EnterState(Entity& entity, const std::string& stateName, float blendDuration = 0.0f)
    {
        AnimationComponent& animation = entity.GetAnimationComponent();
        const auto state = std::find_if(animation.States.begin(), animation.States.end(),
            [&](const AnimationComponent::State& candidate) { return candidate.Name == stateName; });
        if (state == animation.States.end() || !state->Clip) return false;
        const Transform& source = entity.GetTransform();
        animation.TransitionSourcePosition = glm::vec3(source.Position.X, source.Position.Y, source.Position.Z);
        animation.TransitionSourceRotation = source.Rotation;
        animation.TransitionSourceScale = glm::vec3(source.Scale.X, source.Scale.Y, source.Scale.Z);
        animation.TransitionBlendElapsed = 0.0f;
        animation.TransitionBlendDuration = std::max(0.0f, blendDuration);
        animation.TransitionBlending = animation.TransitionBlendDuration > 0.0f;
        animation.CurrentState = state->Name;
        animation.SetClip(state->Clip);
        animation.RuntimeBlend = 0.0f;
        animation.Play();
        return true;
    }
}

static void DecomposeTransform(const glm::mat4& m, glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale)
{
    // translation
    outPos = glm::vec3(m[3]);

    // extract scale from columns
    glm::vec3 col0 = glm::vec3(m[0]);
    glm::vec3 col1 = glm::vec3(m[1]);
    glm::vec3 col2 = glm::vec3(m[2]);
    outScale.x = glm::length(col0);
    outScale.y = glm::length(col1);
    outScale.z = glm::length(col2);

    // remove scale from matrix to get pure rotation
    glm::mat4 rotMat = m;
    if (outScale.x != 0.0f) { rotMat[0] /= outScale.x; }
    if (outScale.y != 0.0f) { rotMat[1] /= outScale.y; }
    if (outScale.z != 0.0f) { rotMat[2] /= outScale.z; }

    outRot = glm::quat_cast(rotMat);
}

static float ApplyEasing(float t, AnimationComponent::EasingType type)
{
    switch (type)
    {
    case AnimationComponent::EasingType::SmoothStep:
        return t * t * (3.0f - 2.0f * t);
    case AnimationComponent::EasingType::EaseIn:
        return t * t;
    case AnimationComponent::EasingType::EaseOut:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case AnimationComponent::EasingType::EaseInOut:
        return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
    case AnimationComponent::EasingType::Linear:
    default:
        return t;
    }
}

void AnimationSystem::BeginPlay(Scene& scene)
{
    for (Entity& entity : scene.GetEntities())
    {
        AnimationComponent& animation = entity.GetAnimationComponent();
        if (!entity.IsEditorOnly() && animation.Enabled && animation.StateMachineEnabled &&
            !animation.States.empty())
        {
            const std::string initial = animation.InitialState.empty()
                ? animation.States.front().Name : animation.InitialState;
            animation.TransitionBlending = false;
            if (EnterState(entity, initial))
            {
                if (!animation.PlayOnStart) animation.Pause();
                continue;
            }
        }
        if (!entity.IsEditorOnly() && animation.Clip && animation.Enabled && animation.PlayOnStart)
        {
            animation.Time = 0.0f;
            animation.RuntimeBlend = 1.0f;
            animation.Play();
        }
        else
        {
            animation.Pause();
        }
        if (animation.ImportedAsset)
            UpdateImportedSkin(animation, animation.Time);
    }
}

void AnimationSystem::ApplyPoseAtTime(Entity& entity, float time, float deltaTime)
{
    AnimationComponent& ac = entity.GetAnimationComponent();
    if (!ac.Enabled || !ac.Clip) return;
    if (ac.ImportedAsset && !ac.Clip->NodeTracks.empty())
    {
        UpdateImportedSkin(ac, time);
        return;
    }

    // wrap time in loop mode and apply easing to time sampling (so ease in/out affects clip progression)
    float t = time;
    float dur = ac.Clip->Duration;
    if (dur > 0.0f) {
        if (ac.Clip->Loop) {
            t = fmod(t, dur);
            if (t < 0.0f) t += dur;
        } else {
            t = std::clamp(t, 0.0f, dur);
        }
    }

    // apply easing to the normalized time so easing changes the motion curve across the clip
    float sampleT = t;
    if (dur > 0.0f && ac.Easing != AnimationComponent::EasingType::Linear)
    {
        float tn = std::clamp(t / dur, 0.0f, 1.0f);
        tn = ApplyEasing(tn, ac.Easing);
        sampleT = tn * dur;
    }

    glm::mat4 local = ac.Clip->EvaluateLocalTransform(sampleT);
    glm::vec3 pos; glm::quat rot; glm::vec3 scl;
    DecomposeTransform(local, pos, rot, scl);

    Transform& tr = entity.GetTransform();
    // Calculate eased weight (used to blend from current pose towards sampled pose)
    const float easedWeight = ApplyEasing(std::clamp(ac.Weight, 0.0f, 1.0f), ac.Easing);

    glm::vec3 targetPos(pos.x, pos.y, pos.z);
    glm::quat targetRot = rot;
    glm::vec3 targetScl(scl.x, scl.y, scl.z);

    glm::vec3 curPos(tr.Position.X, tr.Position.Y, tr.Position.Z);
    glm::quat curRot = tr.Rotation;
    glm::vec3 curScl(tr.Scale.X, tr.Scale.Y, tr.Scale.Z);

    // compute the intended new pose based on easing/blend
    glm::vec3 newPos = glm::mix(curPos, targetPos, easedWeight);
    glm::quat newRot = glm::slerp(curRot, targetRot, easedWeight);
    glm::vec3 newScl = glm::mix(curScl, targetScl, easedWeight);

    // If smoothing enabled, further smooth from the *current* towards the *intended new* using an
    // exponential factor independent of frame rate. This ensures smoothing still applies even when
    // easedWeight == 1.0 (full blend) because smoothing is separate from blending.
    if (ac.UseSmoothing && deltaTime > 0.0f)
    {
        const float alpha = 1.0f - std::exp(-ac.Smoothness * deltaTime);
        glm::vec3 smoothPos = glm::mix(curPos, newPos, alpha);
        glm::quat smoothRot = glm::slerp(curRot, newRot, alpha);
        glm::vec3 smoothScl = glm::mix(curScl, newScl, alpha);
        tr.Position = Vector3(smoothPos.x, smoothPos.y, smoothPos.z);
        tr.Rotation = smoothRot;
        tr.Scale = Vector3(smoothScl.x, smoothScl.y, smoothScl.z);
    }
    else
    {
        tr.Position = Vector3(newPos.x, newPos.y, newPos.z);
        tr.Rotation = newRot;
        tr.Scale = Vector3(newScl.x, newScl.y, newScl.z);
    }
    glm::vec3 euler = glm::degrees(glm::eulerAngles(tr.Rotation));
    tr.EulerRotation.X = euler.x;
    tr.EulerRotation.Y = euler.y;
    tr.EulerRotation.Z = euler.z;
}

void AnimationSystem::Update(Scene& scene, float deltaTime)
{
    auto& entities = scene.GetEntities();
    for (Entity& ent : entities)
    {
        if (!ent.Active || ent.IsEditorOnly()) continue;
        AnimationComponent& ac = ent.GetAnimationComponent();
        if (!ac.Enabled || (!ac.Clip && !ac.ImportedAsset)) continue;
        if (!ac.Clip && ac.ImportedAsset && !ac.ImportedAsset->Clips.empty())
        {
            auto selected = std::find_if(ac.ImportedAsset->Clips.begin(), ac.ImportedAsset->Clips.end(),
                [&](const auto& clip) { return clip && clip->Name == ac.ImportedClipName; });
            ac.Clip = selected != ac.ImportedAsset->Clips.end() ? *selected : ac.ImportedAsset->Clips.front();
        }
        if (!ac.Clip) { UpdateImportedSkin(ac, ac.Time); continue; }

        if (ac.Playing && ac.StateMachineEnabled && !ac.CurrentState.empty())
        {
            for (const AnimationComponent::Transition& transition : ac.Transitions)
            {
                if (transition.From != ac.CurrentState) continue;
                const auto parameter = ac.BoolParameters.find(transition.BoolParameter);
                const bool value = parameter != ac.BoolParameters.end() && parameter->second;
                if (value != transition.ExpectedValue) continue;
                const float normalizedTime = ac.Clip->Duration > 0.0f
                    ? ac.Time / ac.Clip->Duration : 0.0f;
                if (normalizedTime + 1e-5f < transition.ExitTimeNormalized) continue;
                if (EnterState(ent, transition.To, transition.BlendDuration)) break;
            }
        }

        if (ac.Playing)
        {
            ac.Time += deltaTime * ac.Speed;
            // manage runtime blend (simple linear blend-in/out)
            if (ac.BlendIn > 0.0f && ac.Time < ac.BlendIn)
                ac.RuntimeBlend = std::clamp(ac.Time / ac.BlendIn, 0.0f, 1.0f);
            else if (ac.BlendOut > 0.0f && (ac.Clip->Duration - ac.Time) < ac.BlendOut)
                ac.RuntimeBlend = std::clamp((ac.Clip->Duration - ac.Time) / ac.BlendOut, 0.0f, 1.0f);
            else
                ac.RuntimeBlend = 1.0f;

            // multiply weight by runtime blend
            // Clipes glTF controlam ossos da malha; não devem sobrescrever a
            // posição/escala da entidade com curvas de transform vazias.
            if (ac.Clip->NodeTracks.empty())
            {
                const float prevWeight = ac.Weight;
                ac.Weight = ac.Weight * ac.RuntimeBlend;
                ApplyPoseAtTime(ent, ac.Time, deltaTime);
                ac.Weight = prevWeight; // restore
            }

            if (ac.TransitionBlending)
            {
                ac.TransitionBlendElapsed += std::max(0.0f, deltaTime);
                const float alpha = ac.TransitionBlendDuration > 0.0f
                    ? std::clamp(ac.TransitionBlendElapsed / ac.TransitionBlendDuration, 0.0f, 1.0f)
                    : 1.0f;
                Transform& transform = ent.GetTransform();
                const glm::vec3 targetPosition(transform.Position.X, transform.Position.Y, transform.Position.Z);
                const glm::vec3 targetScale(transform.Scale.X, transform.Scale.Y, transform.Scale.Z);
                const glm::vec3 blendedPosition = glm::mix(ac.TransitionSourcePosition, targetPosition, alpha);
                const glm::quat blendedRotation = glm::slerp(ac.TransitionSourceRotation, transform.Rotation, alpha);
                const glm::vec3 blendedScale = glm::mix(ac.TransitionSourceScale, targetScale, alpha);
                transform.Position = Vector3(blendedPosition.x, blendedPosition.y, blendedPosition.z);
                transform.Rotation = blendedRotation;
                transform.Scale = Vector3(blendedScale.x, blendedScale.y, blendedScale.z);
                const glm::vec3 euler = glm::degrees(glm::eulerAngles(transform.Rotation));
                transform.EulerRotation = Vector3(euler.x, euler.y, euler.z);
                if (alpha >= 1.0f) ac.TransitionBlending = false;
            }
        }
        if (ac.ImportedAsset)
            UpdateImportedSkin(ac, ac.Time);
    }
}
