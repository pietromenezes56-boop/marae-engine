#include "AISystem.h"
#include "../../Scene/Entity/Entity.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

namespace
{
    enum class Result { Failure, Running, Success };

    glm::vec3 Position(const Entity& entity)
    {
        const Transform& t = const_cast<Entity&>(entity).GetTransform();
        return { t.Position.X, t.Position.Y, t.Position.Z };
    }

    Entity* Find(Scene& scene, uint64_t id)
    {
        return id ? scene.FindEntityByID(id) : nullptr;
    }

    bool HasVisibleTarget(const AIComponent& ai)
    {
        return ai.VisibleTargetID != 0;
    }

    Entity* PerceivedTarget(Scene& scene, const AIComponent& ai, bool allowMemory)
    {
        Entity* target = Find(scene, ai.VisibleTargetID);
        if (!target && allowMemory)
            target = Find(scene, ai.RememberedTargetID);
        if (target && (!target->Active || target->IsEditorOnly())) return nullptr;
        return target;
    }

    void StopHorizontal(Entity& entity)
    {
        entity.Velocity.X = 0.0f;
        entity.Velocity.Z = 0.0f;
    }

    void Steer(Entity& entity, const glm::vec3& direction, float speed, float dt)
    {
        if (entity.IsStatic || !std::isfinite(speed)) return;
        if (entity.PhysicsEnabled)
        {
            entity.Velocity.X = direction.x * speed;
            entity.Velocity.Z = direction.z * speed;
        }
        else
        {
            Transform& transform = entity.GetTransform();
            transform.Position.X += direction.x * speed * dt;
            transform.Position.Z += direction.z * speed * dt;
            transform.LocalPosition = transform.Position;
        }
    }

    bool MoveToward(Scene& scene, Entity& entity, AIComponent& ai,
                    const glm::vec3& destination, float speed, float stopDistance, float dt)
    {
        const glm::vec3 delta = destination - Position(entity);
        const float distance = glm::length(glm::vec2(delta.x, delta.z));
        if (distance <= std::max(0.01f, stopDistance))
        {
            StopHorizontal(entity);
            return true;
        }
        if (distance <= 0.0001f || speed <= 0.0f)
        {
            StopHorizontal(entity);
            return false;
        }
        const glm::vec3 direction(delta.x / distance, 0.0f, delta.z / distance);
        Steer(entity, direction, std::clamp(speed, 0.0f, 1000.0f), dt);
        (void)scene;
        (void)ai;
        return false;
    }

    void UpdatePerception(Scene& scene, Entity& observer, AIComponent& ai, float dt)
    {
        ai.VisibleTargetID = 0;
        if (ai.SightEnabled && ai.SightRadius > 0.0f)
        {
            const glm::vec3 origin = Position(observer);
            glm::vec3 forward = observer.GetTransform().Rotation * glm::vec3(0.0f, 0.0f, 1.0f);
            forward.y = 0.0f;
            if (glm::length(forward) < 0.0001f) forward = glm::vec3(0.0f, 0.0f, 1.0f);
            forward = glm::normalize(forward);
            const float cosHalfFov = std::cos(glm::radians(
                std::clamp(ai.FieldOfViewDegrees, 1.0f, 360.0f) * 0.5f));
            const float radiusSq = ai.SightRadius * ai.SightRadius;
            float closestSq = radiusSq;
            for (Entity& candidate : scene.GetEntities())
            {
                if (candidate.GetID() == observer.GetID() || !candidate.Active ||
                    candidate.IsEditorOnly() || candidate.IsFolder || candidate.IsModel)
                    continue;
                if (!ai.TargetTag.empty() && ai.TargetTag != "*" && candidate.Tag != ai.TargetTag)
                    continue;
                const glm::vec3 offset = Position(candidate) - origin;
                const glm::vec3 planar(offset.x, 0.0f, offset.z);
                const float distanceSq = glm::dot(planar, planar);
                if (distanceSq <= 0.0001f || distanceSq > closestSq) continue;
                const float facing = glm::dot(forward, glm::normalize(planar));
                if (facing < cosHalfFov) continue;
                closestSq = distanceSq;
                ai.VisibleTargetID = candidate.GetID();
            }
        }

        if (ai.VisibleTargetID)
        {
            ai.RememberedTargetID = ai.VisibleTargetID;
            ai.RememberedTargetTime = std::max(0.0f, ai.TargetMemorySeconds);
        }
        else if (ai.RememberedTargetID)
        {
            ai.RememberedTargetTime -= dt;
            if (ai.RememberedTargetTime <= 0.0f || !Find(scene, ai.RememberedTargetID))
            {
                ai.RememberedTargetID = 0;
                ai.RememberedTargetTime = 0.0f;
            }
        }
    }

    std::vector<const AIComponent::BehaviorNode*> ChildrenOf(
        const AIComponent& ai, int parentId)
    {
        std::vector<const AIComponent::BehaviorNode*> children;
        for (const auto& node : ai.Nodes)
            if (node.ParentId == parentId && node.Id != parentId)
                children.push_back(&node);
        return children;
    }

    Result EvaluateNode(Scene& scene, Entity& entity, AIComponent& ai, int nodeId,
                        float dt, int depth, int& budget, std::unordered_set<int>& stack)
    {
        if (depth > 32 || budget-- <= 0 || stack.contains(nodeId)) return Result::Failure;
        const auto found = std::find_if(ai.Nodes.begin(), ai.Nodes.end(),
            [nodeId](const AIComponent::BehaviorNode& n) { return n.Id == nodeId; });
        if (found == ai.Nodes.end()) return Result::Failure;
        stack.insert(nodeId);
        const auto children = ChildrenOf(ai, nodeId);
        Result result = Result::Failure;
        switch (found->Type)
        {
        case AIComponent::NodeType::Root:
            result = children.empty() ? Result::Failure :
                EvaluateNode(scene, entity, ai, children.front()->Id, dt, depth + 1, budget, stack);
            break;
        case AIComponent::NodeType::Sequence:
            result = Result::Success;
            for (const auto* child : children)
            {
                result = EvaluateNode(scene, entity, ai, child->Id, dt, depth + 1, budget, stack);
                if (result != Result::Success) break;
            }
            break;
        case AIComponent::NodeType::Selector:
            result = Result::Failure;
            for (const auto* child : children)
            {
                result = EvaluateNode(scene, entity, ai, child->Id, dt, depth + 1, budget, stack);
                if (result != Result::Failure) break;
            }
            break;
        case AIComponent::NodeType::Inverter:
            if (children.empty()) result = Result::Failure;
            else
            {
                result = EvaluateNode(scene, entity, ai, children.front()->Id, dt, depth + 1, budget, stack);
                if (result == Result::Success) result = Result::Failure;
                else if (result == Result::Failure) result = Result::Success;
            }
            break;
        case AIComponent::NodeType::HasVisibleTarget:
            result = HasVisibleTarget(ai) ? Result::Success : Result::Failure;
            break;
        case AIComponent::NodeType::TargetWithinRange:
        {
            Entity* target = PerceivedTarget(scene, ai, false);
            const float distance = target ? glm::distance(Position(*target), Position(entity)) : INFINITY;
            result = target && distance <= std::max(0.0f, found->Range)
                ? Result::Success : Result::Failure;
            break;
        }
        case AIComponent::NodeType::ChaseTarget:
        {
            Entity* target = PerceivedTarget(scene, ai, true);
            result = target && MoveToward(scene, entity, ai, Position(*target), found->Speed,
                                           found->Range, dt)
                ? Result::Success : (target ? Result::Running : Result::Failure);
            break;
        }
        case AIComponent::NodeType::FleeTarget:
        {
            Entity* target = PerceivedTarget(scene, ai, true);
            if (!target) { result = Result::Failure; break; }
            glm::vec3 away = Position(entity) - Position(*target);
            away.y = 0.0f;
            const float distance = glm::length(away);
            if (distance >= std::max(0.0f, found->Range))
            {
                StopHorizontal(entity);
                result = Result::Success;
            }
            else if (distance < 0.0001f) result = Result::Running;
            else
            {
                Steer(entity, away / distance, found->Speed, dt);
                result = Result::Running;
            }
            break;
        }
        case AIComponent::NodeType::Wait:
        {
            float& elapsed = ai.RuntimeWaitElapsed[found->Id];
            elapsed += dt;
            if (elapsed >= std::max(0.0f, found->Duration))
            {
                ai.RuntimeWaitElapsed.erase(found->Id);
                result = Result::Success;
            }
            else result = Result::Running;
            break;
        }
        case AIComponent::NodeType::Stop:
            StopHorizontal(entity);
            result = Result::Success;
            break;
        }
        stack.erase(nodeId);
        return result;
    }

    bool TransitionMatches(const AIComponent::Transition& transition,
                           const AIComponent& ai, Scene& scene, Entity& owner)
    {
        Entity* target = PerceivedTarget(scene, ai, transition.Condition == AIComponent::TransitionCondition::TargetLost ||
                                                   transition.Condition == AIComponent::TransitionCondition::TargetOutsideRange);
        switch (transition.Condition)
        {
        case AIComponent::TransitionCondition::TargetVisible: return ai.VisibleTargetID != 0;
        case AIComponent::TransitionCondition::TargetLost: return ai.VisibleTargetID == 0 && target == nullptr;
        case AIComponent::TransitionCondition::TargetWithinRange:
            return ai.VisibleTargetID && target && glm::distance(Position(*target), Position(owner)) <= transition.Range;
        case AIComponent::TransitionCondition::TargetOutsideRange:
            return target && glm::distance(Position(*target), Position(owner)) >= transition.Range;
        }
        return false;
    }

    void RunStateTask(Scene& scene, Entity& owner, AIComponent& ai,
                      const AIComponent::State& state, float dt)
    {
        Entity* target = PerceivedTarget(scene, ai, true);
        if (state.Action == AIComponent::Task::Idle || state.Action == AIComponent::Task::Wait)
        {
            StopHorizontal(owner);
            return;
        }
        if (!target) { StopHorizontal(owner); return; }
        if (state.Action == AIComponent::Task::ChaseTarget)
            MoveToward(scene, owner, ai, Position(*target), state.Speed, state.Range, dt);
        else if (state.Action == AIComponent::Task::FleeTarget)
        {
            glm::vec3 away = Position(owner) - Position(*target);
            away.y = 0.0f;
            const float distance = glm::length(away);
            if (distance < state.Range && distance > 0.0001f)
                Steer(owner, away / distance, state.Speed, dt);
            else StopHorizontal(owner);
        }
    }
}

void AISystem::Update(Scene& scene, float deltaTime)
{
    const float dt = std::clamp(std::isfinite(deltaTime) ? deltaTime : 0.0f, 0.0f, 0.1f);
    for (Entity& entity : scene.GetEntities())
    {
        if (!entity.Active || entity.IsEditorOnly() || !entity.HasAI()) continue;
        AIComponent& ai = entity.GetAIComponent();
        if (!ai.Enabled) continue;
        UpdatePerception(scene, entity, ai, dt);
        if (ai.CurrentMode == AIComponent::Mode::BehaviorTree)
        {
            int budget = 512;
            std::unordered_set<int> recursionStack;
            EvaluateNode(scene, entity, ai, ai.RootNodeId, dt, 0, budget, recursionStack);
        }
        else
        {
            if (!ai.RuntimeStarted || std::none_of(ai.States.begin(), ai.States.end(),
                [&](const AIComponent::State& state) { return state.Name == ai.CurrentState; }))
            {
                ai.CurrentState = ai.InitialState;
                ai.RuntimeStarted = true;
            }
            for (const auto& transition : ai.Transitions)
            {
                if (transition.From == ai.CurrentState && TransitionMatches(transition, ai, scene, entity))
                {
                    ai.CurrentState = transition.To;
                    break;
                }
            }
            const auto state = std::find_if(ai.States.begin(), ai.States.end(),
                [&](const AIComponent::State& value) { return value.Name == ai.CurrentState; });
            if (state != ai.States.end()) RunStateTask(scene, entity, ai, *state, dt);
            else StopHorizontal(entity);
        }
    }
}

int AISystem::RunRegressionTests()
{
    int failures = 0;
    const auto check = [&failures](bool passed, const char* name)
    {
        std::cout << (passed ? "[AITest] PASS: " : "[AITest] FAIL: ") << name << '\n';
        if (!passed) ++failures;
    };

    Scene scene;
    const uint64_t observerId = scene.CreateEntity().GetID();
    Entity* observer = scene.FindEntityByID(observerId);
    observer->SetName("AI Observer"); observer->SetEditorOnly(false); observer->Tag = "Enemy";
    observer->AddAI();
    const uint64_t targetId = scene.CreateEntity().GetID();
    observer = scene.FindEntityByID(observerId);
    Entity* target = scene.FindEntityByID(targetId);
    AIComponent& ai = observer->GetAIComponent();
    ai.TargetTag = "Player"; ai.SightRadius = 10.0f; ai.FieldOfViewDegrees = 90.0f;
    target->SetName("Target"); target->SetEditorOnly(false); target->Tag = "Player";
    target->GetTransform().Position = Vector3(0.0f, 0.0f, 5.0f);
    AISystem::Update(scene, 1.0f / 60.0f);
    check(ai.VisibleTargetID == targetId, "visão identifica alvo compatível no cone e alcance");

    target->GetTransform().Position = Vector3(5.0f, 0.0f, 0.0f);
    AISystem::Update(scene, 1.0f / 60.0f);
    check(ai.VisibleTargetID == 0 && ai.RememberedTargetID == targetId,
          "alvo fora do campo de visão deixa de ficar visível e permanece na memória por prazo limitado");
    for (int i = 0; i < 40; ++i) AISystem::Update(scene, 0.1f);
    check(ai.RememberedTargetID == 0, "memória sensorial expira e limpa alvo perdido");

    target->GetTransform().Position = Vector3(0.0f, 0.0f, 5.0f);
    ai.Nodes = {
        { 1, 0, AIComponent::NodeType::Root },
        { 2, 1, AIComponent::NodeType::Sequence },
        { 3, 2, AIComponent::NodeType::HasVisibleTarget },
        { 4, 2, AIComponent::NodeType::ChaseTarget, 1.0f, 1.0f, 2.5f }
    };
    ai.RootNodeId = 1;
    AISystem::Update(scene, 1.0f / 60.0f);
    check(observer->Velocity.Z > 0.0f && std::abs(observer->Velocity.X) < 0.001f,
          "árvore sequence executa condition e direciona ação de chase");

    ai.CurrentMode = AIComponent::Mode::StateMachine;
    ai.States = { { "Idle", AIComponent::Task::Idle }, { "Chase", AIComponent::Task::ChaseTarget, 4.0f, 0.5f } };
    ai.InitialState = "Idle"; ai.CurrentState = "Idle"; ai.RuntimeStarted = true;
    ai.Transitions = { { "Idle", "Chase", AIComponent::TransitionCondition::TargetVisible, 0.5f } };
    observer->Velocity = Vector3(0.0f, 0.0f, 0.0f);
    AISystem::Update(scene, 1.0f / 60.0f);
    check(ai.CurrentState == "Chase" && observer->Velocity.Z > 0.0f,
          "FSM muda de estado pela condição perceptiva e executa a tarefa do novo estado");

    observer->SetEditorOnly(true);
    const uint64_t oldVisible = ai.VisibleTargetID;
    target->GetTransform().Position = Vector3(0.0f, 0.0f, -5.0f);
    AISystem::Update(scene, 1.0f / 60.0f);
    check(ai.VisibleTargetID == oldVisible, "entidades exclusivas do editor não atualizam IA no runtime");
    std::cout << "[AITest] " << (failures == 0 ? "SUITE PASSOU" : "SUITE FALHOU")
              << " (" << failures << " falha(s))\n";
    return failures;
}

