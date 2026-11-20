#include "UndoRedoManager.h"

void UndoRedoManager::Push(
    const TransformCommand& command
)
{
    m_UndoStack.push_back(command);

    m_RedoStack.clear();
}

bool UndoRedoManager::CanUndo() const
{
    return !m_UndoStack.empty();
}

bool UndoRedoManager::CanRedo() const
{
    return !m_RedoStack.empty();
}

TransformCommand UndoRedoManager::Undo()
{
    TransformCommand command =
        m_UndoStack.back();

    m_UndoStack.pop_back();

    m_RedoStack.push_back(
        command
    );

    return command;
}

TransformCommand UndoRedoManager::Redo()
{
    TransformCommand command =
        m_RedoStack.back();

    m_RedoStack.pop_back();

    m_UndoStack.push_back(
        command
    );

    return command;
}