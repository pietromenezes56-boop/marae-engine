#pragma once

#include "../../Math/Vector3.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Runtime AI data belongs to a game entity; it is separate from the editor's
// AI assistant, which only generates C# source code.
struct AIComponent
{
    enum class Mode : int { BehaviorTree = 0, StateMachine = 1 };
    enum class NodeType : int
    {
        Root = 0, Sequence, Selector, Inverter,
        HasVisibleTarget, TargetWithinRange,
        ChaseTarget, FleeTarget, Wait, Stop
    };
    enum class Task : int { Idle = 0, ChaseTarget, FleeTarget, Wait };
    enum class TransitionCondition : int
    {
        TargetVisible = 0, TargetLost, TargetWithinRange, TargetOutsideRange
    };

    struct BehaviorNode
    {
        int Id = 1;
        int ParentId = 0;
        NodeType Type = NodeType::Root;
        float Range = 3.0f;
        float Duration = 1.0f;
        float Speed = 3.0f;
    };
    struct State
    {
        std::string Name = "Idle";
        Task Action = Task::Idle;
        float Speed = 3.0f;
        float Range = 3.0f;
        float WaitDuration = 1.0f;
    };
    struct Transition
    {
        std::string From;
        std::string To;
        TransitionCondition Condition = TransitionCondition::TargetVisible;
        float Range = 8.0f;
    };

    bool Enabled = true;
    Mode CurrentMode = Mode::BehaviorTree;

    // Sight perception. Forward is local +Z; targets must be active, tagged
    // accordingly, inside the cone and within SightRadius.
    bool SightEnabled = true;
    float SightRadius = 18.0f;
    float FieldOfViewDegrees = 120.0f;
    float TargetMemorySeconds = 2.0f;
    std::string TargetTag = "Player";
    uint64_t VisibleTargetID = 0;
    uint64_t RememberedTargetID = 0;
    float RememberedTargetTime = 0.0f;

    // Behavior tree, stored as a bounded flat node list with parent IDs.
    int RootNodeId = 1;
    std::vector<BehaviorNode> Nodes = { { 1, 0, NodeType::Root } };
    std::unordered_map<int, float> RuntimeWaitElapsed;

    // Optional finite state machine.
    std::string InitialState = "Idle";
    std::string CurrentState = "Idle";
    std::vector<State> States = { { "Idle", Task::Idle } };
    std::vector<Transition> Transitions;
    bool RuntimeStarted = false;

    void ResetRuntime()
    {
        VisibleTargetID = 0;
        RememberedTargetID = 0;
        RememberedTargetTime = 0.0f;
        RuntimeWaitElapsed.clear();
        CurrentState = InitialState;
        RuntimeStarted = false;
    }
};

