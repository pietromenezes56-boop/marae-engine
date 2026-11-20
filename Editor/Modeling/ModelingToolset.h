#pragma once

#include "MeshTopology.h"
#include "ModelingSelection.h"
#include <glm/glm.hpp>

namespace ModelingToolset
{
    enum class BooleanMode
    {
        Union,
        Difference,
        Intersection
    };

    void TransformSelected(MeshTopology& mesh, const ModelingSelection& selection,
                           const glm::vec3& translation, const glm::vec3& scale,
                           const glm::vec3& rotationDegrees, bool proportional,
                           float proportionalRadius);
    void MergeSelected(MeshTopology& mesh, const ModelingSelection& selection);
    void DissolveSelected(MeshTopology& mesh, const ModelingSelection& selection);
    void FillSelected(MeshTopology& mesh, const ModelingSelection& selection);
    std::vector<int> ExtrudeSelectedFaces(MeshTopology& mesh, const ModelingSelection& selection, float distance);
    void InsetSelectedFaces(MeshTopology& mesh, const ModelingSelection& selection, float amount);
    void BevelSelected(MeshTopology& mesh, const ModelingSelection& selection, float amount);
    void LoopCut(MeshTopology& mesh, const ModelingSelection& selection);
    void Knife(MeshTopology& mesh, const ModelingSelection& selection);
    void Slide(MeshTopology& mesh, const ModelingSelection& selection, float amount);
    void DuplicateSelected(MeshTopology& mesh, const ModelingSelection& selection,
                           const glm::vec3& offset);
    void Mirror(MeshTopology& mesh, int axis, float plane = 0.0f);
    void Array(MeshTopology& mesh, int count, const glm::vec3& offset);
    void Solidify(MeshTopology& mesh, float thickness);
    void Decimate(MeshTopology& mesh, float ratio);
    void BooleanCombine(MeshTopology& target, const MeshTopology& operand, BooleanMode mode);
    void BridgeSelectedEdges(MeshTopology& mesh, const ModelingSelection& selection);
    void SetFlatShading(MeshTopology& mesh);
    void SetSmoothShading(MeshTopology& mesh);
    void FlattenSelected(MeshTopology& mesh, const ModelingSelection& selection, int axis = -1);
    void SpherizeSelected(MeshTopology& mesh, const ModelingSelection& selection, float factor = 1.0f);
    void PokeSelectedFaces(MeshTopology& mesh, const ModelingSelection& selection, float offset = 0.0f);
    void WeldByDistance(MeshTopology& mesh, float distance = 0.01f);
    void TwistSelected(MeshTopology& mesh, const ModelingSelection& selection, int axis = 1, float angleDegrees = 45.0f);
}
