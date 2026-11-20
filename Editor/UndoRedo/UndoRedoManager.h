#pragma once

#include "../../Scene/Transform/Transform.h"
#include "../../Scene/Entity/Entity.h"
#include <vector>
#include <cstdint>

enum class CommandType
    {
        Transform,
        Delete,
        Create
    };

struct EntityTransformSnapshot
{
    uint64_t EntityID = 0;
    Transform Before;
    Transform After;
};

struct TransformCommand
{
    CommandType Type = CommandType::Transform;

    uint64_t EntityID = 0;

    int EntityIndex = -1;

    // Serialized snapshot of entity (name + transform + color lines)
    std::string EntityData;

    Transform Before;
    Transform After;
    // Um gesto em seleção múltipla deve continuar sendo um único passo de Undo/Redo.
    std::vector<EntityTransformSnapshot> BatchTransforms;

    // Operações do modo Modelador usam o mesmo histórico, mas também
    // preservam a geometria CPU para que Undo/Redo não perca a malha.
    bool HasMeshData = false;
    std::vector<Vertex> MeshBefore;
    std::vector<Vertex> MeshAfter;

    TransformCommand() : Type(CommandType::Transform), EntityID(0), EntityIndex(-1) {}
};

class UndoRedoManager
{
public:
    void Push(
        const TransformCommand& command
    );

    bool CanUndo() const;

    bool CanRedo() const;

    TransformCommand Undo();

    TransformCommand Redo();

    

private:
    std::vector<TransformCommand>
        m_UndoStack;

    std::vector<TransformCommand>
        m_RedoStack;
};
