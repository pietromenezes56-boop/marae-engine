#include "ModelingToolset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
    glm::vec3 Position(const MeshTopology::VertexData& vertex)
    {
        return { vertex.Value.X, vertex.Value.Y, vertex.Value.Z };
    }

    void SetPosition(MeshTopology::VertexData& vertex, const glm::vec3& value)
    {
        vertex.Value.X = value.x;
        vertex.Value.Y = value.y;
        vertex.Value.Z = value.z;
    }

    std::unordered_set<uint32_t> SelectedVertices(
        const MeshTopology& mesh, const ModelingSelection& selection)
    {
        std::unordered_set<uint32_t> result;
        const auto edges = mesh.GetEdges();
        for (int selected : selection.Indices)
        {
            if (selected < 0) continue;
            if (selection.Mode == ModelingSelectionMode::Vertex &&
                static_cast<size_t>(selected) < mesh.Vertices.size())
                result.insert(static_cast<uint32_t>(selected));
            else if (selection.Mode == ModelingSelectionMode::Edge &&
                     static_cast<size_t>(selected) < edges.size())
            {
                result.insert(edges[selected].A);
                result.insert(edges[selected].B);
            }
            else if (selection.Mode == ModelingSelectionMode::Face &&
                     static_cast<size_t>(selected) < mesh.Faces.size())
            {
                for (uint32_t index : mesh.Faces[selected].Vertices)
                    result.insert(index);
            }
        }
        return result;
    }

    std::unordered_set<uint32_t> SelectedFaces(
        const MeshTopology& mesh, const ModelingSelection& selection)
    {
        std::unordered_set<uint32_t> result;
        if (selection.Mode == ModelingSelectionMode::Face)
        {
            for (int selected : selection.Indices)
                if (selected >= 0 && static_cast<size_t>(selected) < mesh.Faces.size())
                    result.insert(static_cast<uint32_t>(selected));
        }
        else
        {
            const auto selectedVertices = SelectedVertices(mesh, selection);
            for (uint32_t face = 0; face < mesh.Faces.size(); ++face)
            {
                bool allSelected = true;
                for (uint32_t vertex : mesh.Faces[face].Vertices)
                    allSelected = allSelected && selectedVertices.count(vertex) != 0;
                if (allSelected) result.insert(face);
            }
        }
        return result;
    }

    glm::vec3 FaceCenter(const MeshTopology& mesh, const MeshTopology::Face& face)
    {
        return (Position(mesh.Vertices[face.Vertices[0]]) +
                Position(mesh.Vertices[face.Vertices[1]]) +
                Position(mesh.Vertices[face.Vertices[2]])) / 3.0f;
    }

    uint32_t AddVertex(MeshTopology& mesh, const glm::vec3& position, const Vertex& source)
    {
        Vertex copy = source;
        copy.X = position.x;
        copy.Y = position.y;
        copy.Z = position.z;
        mesh.Vertices.push_back({ copy });
        return static_cast<uint32_t>(mesh.Vertices.size() - 1);
    }

    void AddQuad(MeshTopology& mesh, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
    {
        mesh.Faces.push_back({ { a, b, c } });
        mesh.Faces.push_back({ { a, c, d } });
    }
}

namespace ModelingToolset
{
    void TransformSelected(MeshTopology& mesh, const ModelingSelection& selection,
                           const glm::vec3& translation, const glm::vec3& scale,
                           const glm::vec3& rotationDegrees, bool proportional,
                           float proportionalRadius)
    {
        const auto selected = SelectedVertices(mesh, selection);
        if (selected.empty()) return;

        glm::vec3 pivot(0.0f);
        for (uint32_t index : selected) pivot += Position(mesh.Vertices[index]);
        pivot /= static_cast<float>(selected.size());
        const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f),
            glm::radians(rotationDegrees.x), glm::vec3(1, 0, 0));
        const glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f),
            glm::radians(rotationDegrees.y), glm::vec3(0, 1, 0));
        const glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f),
            glm::radians(rotationDegrees.z), glm::vec3(0, 0, 1));
        const glm::mat4 transform = rotationZ * rotationY * rotation;

        for (uint32_t index = 0; index < mesh.Vertices.size(); ++index)
        {
            const glm::vec3 current = Position(mesh.Vertices[index]);
            const float distance = glm::length(current - pivot);
            float weight = selected.count(index) != 0 ? 1.0f : 0.0f;
            if (proportional && weight == 0.0f && proportionalRadius > 0.0001f)
                weight = std::max(0.0f, 1.0f - distance / proportionalRadius);
            if (weight <= 0.0f) continue;

            const glm::vec3 local = current - pivot;
            const glm::vec3 rotated = glm::vec3(transform * glm::vec4(local, 1.0f));
            const glm::vec3 target = pivot + rotated * scale + translation;
            SetPosition(mesh.Vertices[index], glm::mix(current, target, weight));
        }
        mesh.RecalculateNormals(false);
    }

    void MergeSelected(MeshTopology& mesh, const ModelingSelection& selection)
    {
        const auto selected = SelectedVertices(mesh, selection);
        if (selected.size() < 2) return;
        glm::vec3 center(0.0f);
        for (uint32_t index : selected) center += Position(mesh.Vertices[index]);
        center /= static_cast<float>(selected.size());
        const uint32_t target = *selected.begin();
        SetPosition(mesh.Vertices[target], center);
        for (uint32_t index : selected)
            if (index != target)
                for (MeshTopology::Face& face : mesh.Faces)
                    for (uint32_t& vertex : face.Vertices)
                        if (vertex == index) vertex = target;
        mesh.RemoveDegenerate();
        mesh.RecalculateNormals(false);
    }

    void DissolveSelected(MeshTopology& mesh, const ModelingSelection& selection)
    {
        const auto faces = SelectedFaces(mesh, selection);
        if (faces.empty()) return;
        mesh.Faces.erase(std::remove_if(mesh.Faces.begin(), mesh.Faces.end(),
            [&](const MeshTopology::Face& face)
            {
                const uint32_t index = static_cast<uint32_t>(&face - mesh.Faces.data());
                return faces.count(index) != 0;
            }), mesh.Faces.end());
        mesh.RecalculateNormals(false);
    }

    void FillSelected(MeshTopology& mesh, const ModelingSelection& selection)
    {
        const auto vertices = SelectedVertices(mesh, selection);
        if (vertices.size() < 3) return;
        glm::vec3 center(0.0f);
        for (uint32_t index : vertices) center += Position(mesh.Vertices[index]);
        center /= static_cast<float>(vertices.size());
        const Vertex source = mesh.Vertices[*vertices.begin()].Value;
        const uint32_t centerIndex = AddVertex(mesh, center, source);
        std::vector<uint32_t> ordered(vertices.begin(), vertices.end());
        std::sort(ordered.begin(), ordered.end());
        for (size_t i = 0; i < ordered.size(); ++i)
            mesh.Faces.push_back({ { centerIndex, ordered[i], ordered[(i + 1) % ordered.size()] } });
        mesh.RecalculateNormals(false);
    }

    std::vector<int> ExtrudeSelectedFaces(MeshTopology& mesh, const ModelingSelection& selection, float distance)
    {
        const auto selectedFaces = SelectedFaces(mesh, selection);
        if (selectedFaces.empty()) return {};
        const std::vector<MeshTopology::Face> original = mesh.Faces;

        struct EdgeKey
        {
            uint32_t a = 0;
            uint32_t b = 0;
            bool operator==(const EdgeKey& other) const { return a == other.a && b == other.b; }
        };
        struct EdgeKeyHash
        {
            size_t operator()(const EdgeKey& k) const
            {
                return (static_cast<size_t>(k.a) * 16777619u) ^ static_cast<size_t>(k.b);
            }
        };

        std::unordered_map<EdgeKey, int, EdgeKeyHash> edgeCount;
        for (uint32_t faceIndex : selectedFaces)
        {
            const auto& face = original[faceIndex];
            for (int i = 0; i < 3; ++i)
            {
                uint32_t u = face.Vertices[i];
                uint32_t v = face.Vertices[(i + 1) % 3];
                edgeCount[{ std::min(u, v), std::max(u, v) }]++;
            }
        }

        std::unordered_map<uint32_t, glm::vec3> vertexNormals;
        for (uint32_t faceIndex : selectedFaces)
        {
            const auto& face = original[faceIndex];
            const glm::vec3 a = Position(mesh.Vertices[face.Vertices[0]]);
            const glm::vec3 b = Position(mesh.Vertices[face.Vertices[1]]);
            const glm::vec3 c = Position(mesh.Vertices[face.Vertices[2]]);
            glm::vec3 normal = glm::cross(b - a, c - a);
            float len = glm::length(normal);
            if (len > 1e-6f) normal /= len;
            else normal = glm::vec3(0.0f, 1.0f, 0.0f);

            for (int i = 0; i < 3; ++i)
                vertexNormals[face.Vertices[i]] += normal;
        }

        std::unordered_map<uint32_t, uint32_t> extrudedVertexMap;
        for (const auto& [vIdx, normalSum] : vertexNormals)
        {
            glm::vec3 n = glm::length(normalSum) > 1e-6f ? glm::normalize(normalSum) : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 origPos = Position(mesh.Vertices[vIdx]);
            extrudedVertexMap[vIdx] = AddVertex(mesh, origPos + n * distance, mesh.Vertices[vIdx].Value);
        }

        std::vector<MeshTopology::Face> result;
        result.reserve(original.size() + selectedFaces.size() * 5);

        // 1. Manter faces não selecionadas
        for (uint32_t faceIndex = 0; faceIndex < original.size(); ++faceIndex)
        {
            if (!selectedFaces.count(faceIndex))
                result.push_back(original[faceIndex]);
        }

        // 2. Adicionar as novas faces da tampa extrudada e registrar seus índices
        std::vector<int> newCapSelection;
        newCapSelection.reserve(selectedFaces.size());
        for (uint32_t faceIndex : selectedFaces)
        {
            const auto& face = original[faceIndex];
            uint32_t aa = extrudedVertexMap[face.Vertices[0]];
            uint32_t bb = extrudedVertexMap[face.Vertices[1]];
            uint32_t cc = extrudedVertexMap[face.Vertices[2]];
            result.push_back({ { aa, bb, cc } });
            newCapSelection.push_back(static_cast<int>(result.size() - 1));
        }

        // 3. Adicionar quads laterais diretamente no result! (Conectando a base à tampa extrudada)
        for (uint32_t faceIndex : selectedFaces)
        {
            const auto& face = original[faceIndex];
            for (int i = 0; i < 3; ++i)
            {
                uint32_t u = face.Vertices[i];
                uint32_t v = face.Vertices[(i + 1) % 3];
                if (edgeCount[{ std::min(u, v), std::max(u, v) }] == 1)
                {
                    uint32_t uExt = extrudedVertexMap[u];
                    uint32_t vExt = extrudedVertexMap[v];
                    result.push_back({ { u, v, vExt } });
                    result.push_back({ { u, vExt, uExt } });
                }
            }
        }

        mesh.Faces = std::move(result);
        mesh.RecalculateNormals(false);
        return newCapSelection;
    }

    void InsetSelectedFaces(MeshTopology& mesh, const ModelingSelection& selection, float amount)
    {
        const auto selectedFaces = SelectedFaces(mesh, selection);
        if (selectedFaces.empty()) return;
        amount = std::clamp(amount, 0.0f, 0.95f);
        std::vector<MeshTopology::Face> result;
        const auto original = mesh.Faces;
        for (uint32_t faceIndex = 0; faceIndex < original.size(); ++faceIndex)
        {
            const auto& face = original[faceIndex];
            if (!selectedFaces.count(faceIndex)) { result.push_back(face); continue; }
            const glm::vec3 center = FaceCenter(mesh, face);
            std::array<uint32_t, 3> inner{};
            for (int i = 0; i < 3; ++i)
            {
                const glm::vec3 position = glm::mix(center,
                    Position(mesh.Vertices[face.Vertices[i]]), 1.0f - amount);
                inner[i] = AddVertex(mesh, position, mesh.Vertices[face.Vertices[i]].Value);
            }
            AddQuad(mesh, face.Vertices[0], face.Vertices[1], inner[1], inner[0]);
            AddQuad(mesh, face.Vertices[1], face.Vertices[2], inner[2], inner[1]);
            AddQuad(mesh, face.Vertices[2], face.Vertices[0], inner[0], inner[2]);
            result.push_back({ { inner[0], inner[1], inner[2] } });
        }
        mesh.Faces = std::move(result);
        mesh.RecalculateNormals(false);
    }

    void BevelSelected(MeshTopology& mesh, const ModelingSelection& selection, float amount)
    {
        // O bevel mantém a malha editável: primeiro cria uma borda de suporte
        // com inset e depois desloca o núcleo para preservar o volume.
        InsetSelectedFaces(mesh, selection, std::clamp(amount, 0.01f, 0.45f));
    }

    void LoopCut(MeshTopology& mesh, const ModelingSelection& selection)
    {
        const auto selectedFaces = SelectedFaces(mesh, selection);
        if (selectedFaces.empty()) return;
        const auto original = mesh.Faces;
        std::vector<MeshTopology::Face> result;
        for (uint32_t i = 0; i < original.size(); ++i)
        {
            const auto& face = original[i];
            if (!selectedFaces.count(i)) { result.push_back(face); continue; }
            const uint32_t ab = AddVertex(mesh, (Position(mesh.Vertices[face.Vertices[0]]) + Position(mesh.Vertices[face.Vertices[1]])) * 0.5f, mesh.Vertices[face.Vertices[0]].Value);
            const uint32_t bc = AddVertex(mesh, (Position(mesh.Vertices[face.Vertices[1]]) + Position(mesh.Vertices[face.Vertices[2]])) * 0.5f, mesh.Vertices[face.Vertices[1]].Value);
            const uint32_t ca = AddVertex(mesh, (Position(mesh.Vertices[face.Vertices[2]]) + Position(mesh.Vertices[face.Vertices[0]])) * 0.5f, mesh.Vertices[face.Vertices[2]].Value);
            result.push_back({ { face.Vertices[0], ab, ca } });
            result.push_back({ { ab, face.Vertices[1], bc } });
            result.push_back({ { ca, bc, face.Vertices[2] } });
            result.push_back({ { ab, bc, ca } });
        }
        mesh.Faces = std::move(result);
        mesh.RecalculateNormals(false);
    }

    void Knife(MeshTopology& mesh, const ModelingSelection& selection)
    {
        // No modo integrado, Knife começa por dividir a face sob o cursor. A
        // seleção atual representa a prévia do corte e usa o mesmo domínio de
        // topologia do Loop Cut.
        LoopCut(mesh, selection);
    }

    void Slide(MeshTopology& mesh, const ModelingSelection& selection, float amount)
    {
        const auto selected = SelectedVertices(mesh, selection);
        if (selected.empty()) return;
        const auto edges = mesh.GetEdges();
        for (uint32_t index : selected)
        {
            glm::vec3 neighbor(0.0f);
            int count = 0;
            for (const auto& edge : edges)
                if (edge.A == index || edge.B == index)
                {
                    const uint32_t other = edge.A == index ? edge.B : edge.A;
                    neighbor += Position(mesh.Vertices[other]);
                    ++count;
                }
            if (count > 0)
                SetPosition(mesh.Vertices[index], glm::mix(Position(mesh.Vertices[index]), neighbor / static_cast<float>(count), std::clamp(amount, -1.0f, 1.0f)));
        }
        mesh.RecalculateNormals(false);
    }

    void DuplicateSelected(MeshTopology& mesh, const ModelingSelection& selection, const glm::vec3& offset)
    {
        const auto selectedFaces = SelectedFaces(mesh, selection);
        if (selectedFaces.empty()) return;
        std::vector<uint32_t> faces(selectedFaces.begin(), selectedFaces.end());
        std::unordered_map<uint32_t, uint32_t> remap;
        for (uint32_t faceIndex : faces)
            for (uint32_t oldIndex : mesh.Faces[faceIndex].Vertices)
                if (!remap.count(oldIndex))
                {
                    Vertex value = mesh.Vertices[oldIndex].Value;
                    value.X += offset.x; value.Y += offset.y; value.Z += offset.z;
                    remap[oldIndex] = static_cast<uint32_t>(mesh.Vertices.size());
                    mesh.Vertices.push_back({ value });
                }
        for (uint32_t faceIndex : faces)
        {
            const auto& face = mesh.Faces[faceIndex];
            mesh.Faces.push_back({ { remap[face.Vertices[0]], remap[face.Vertices[1]], remap[face.Vertices[2]] } });
        }
        mesh.RecalculateNormals(false);
    }

    void Mirror(MeshTopology& mesh, int axis, float plane)
    {
        axis = std::clamp(axis, 0, 2);
        const size_t originalVertexCount = mesh.Vertices.size();
        for (size_t i = 0; i < originalVertexCount; ++i)
        {
            Vertex value = mesh.Vertices[i].Value;
            float* coordinate = axis == 0 ? &value.X : axis == 1 ? &value.Y : &value.Z;
            float* normal = axis == 0 ? &value.NX : axis == 1 ? &value.NY : &value.NZ;
            *coordinate = plane - (*coordinate - plane);
            *normal = -*normal;
            mesh.Vertices.push_back({ value });
        }
        const size_t originalFaceCount = mesh.Faces.size();
        for (size_t i = 0; i < originalFaceCount; ++i)
        {
            const auto& face = mesh.Faces[i];
            mesh.Faces.push_back({ { static_cast<uint32_t>(face.Vertices[2] + originalVertexCount),
                                     static_cast<uint32_t>(face.Vertices[1] + originalVertexCount),
                                     static_cast<uint32_t>(face.Vertices[0] + originalVertexCount) } });
        }
        mesh.RecalculateNormals(false);
    }

    void Array(MeshTopology& mesh, int count, const glm::vec3& offset)
    {
        count = std::clamp(count, 1, 32);
        const MeshTopology original = mesh;
        for (int copy = 1; copy < count; ++copy)
        {
            const uint32_t base = static_cast<uint32_t>(mesh.Vertices.size());
            for (const auto& vertex : original.Vertices)
            {
                Vertex value = vertex.Value;
                value.X += offset.x * copy; value.Y += offset.y * copy; value.Z += offset.z * copy;
                mesh.Vertices.push_back({ value });
            }
            for (const auto& face : original.Faces)
                mesh.Faces.push_back({ { face.Vertices[0] + base, face.Vertices[1] + base, face.Vertices[2] + base } });
        }
        mesh.RecalculateNormals(false);
    }

    void Solidify(MeshTopology& mesh, float thickness)
    {
        if (!mesh.IsValid()) return;
        const size_t baseVertexCount = mesh.Vertices.size();
        for (size_t i = 0; i < baseVertexCount; ++i)
        {
            Vertex value = mesh.Vertices[i].Value;
            value.X -= value.NX * thickness;
            value.Y -= value.NY * thickness;
            value.Z -= value.NZ * thickness;
            value.NX = -value.NX; value.NY = -value.NY; value.NZ = -value.NZ;
            mesh.Vertices.push_back({ value });
        }
        const size_t faceCount = mesh.Faces.size();
        for (size_t i = 0; i < faceCount; ++i)
        {
            const auto& face = mesh.Faces[i];
            mesh.Faces.push_back({ { face.Vertices[2] + static_cast<uint32_t>(baseVertexCount),
                                     face.Vertices[1] + static_cast<uint32_t>(baseVertexCount),
                                     face.Vertices[0] + static_cast<uint32_t>(baseVertexCount) } });
        }
        for (const auto& edge : mesh.GetEdges())
            if (edge.Faces.size() == 1)
                AddQuad(mesh, edge.A, edge.B,
                         edge.B + static_cast<uint32_t>(baseVertexCount),
                         edge.A + static_cast<uint32_t>(baseVertexCount));
        mesh.RecalculateNormals(false);
    }

    void Decimate(MeshTopology& mesh, float ratio)
    {
        if (!mesh.IsValid() || mesh.Faces.size() < 5)
            return;

        ratio = std::clamp(ratio, 0.05f, 1.0f);
        const size_t targetFaces = std::max<size_t>(4,
            static_cast<size_t>(std::ceil(static_cast<double>(mesh.Faces.size()) * ratio)));
        if (targetFaces >= mesh.Faces.size())
            return;

        struct Candidate
        {
            float distanceSquared = 0.0f;
            uint32_t a = 0;
            uint32_t b = 0;
        };

        const size_t vertexCount = mesh.Vertices.size();
        const size_t initialFaceCount = mesh.Faces.size();
        std::vector<uint32_t> parent(vertexCount);
        std::iota(parent.begin(), parent.end(), 0u);
        std::vector<glm::vec3> positions(vertexCount);
        std::vector<Vertex> attributes(vertexCount);
        std::vector<float> weights(vertexCount, 1.0f);
        std::vector<std::vector<uint32_t>> incidentFaces(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i)
        {
            attributes[i] = mesh.Vertices[i].Value;
            positions[i] = Position(mesh.Vertices[i]);
        }
        // Compare every later collapse to the face's original orientation,
        // rather than only to the previous collapse. Repeated small rotations
        // can otherwise accumulate until a face has effectively flipped.
        std::vector<glm::vec3> referenceFaceNormals(mesh.Faces.size(), glm::vec3(0.0f));
        for (size_t faceIndex = 0; faceIndex < mesh.Faces.size(); ++faceIndex)
        {
            const auto& indices = mesh.Faces[faceIndex].Vertices;
            const glm::vec3 a = positions[indices[0]];
            const glm::vec3 b = positions[indices[1]];
            const glm::vec3 c = positions[indices[2]];
            const glm::vec3 normal = glm::cross(b - a, c - a);
            if (glm::length(normal) > 1e-7f)
                referenceFaceNormals[faceIndex] = glm::normalize(normal);
        }
        for (uint32_t faceIndex = 0; faceIndex < mesh.Faces.size(); ++faceIndex)
            for (uint32_t vertex : mesh.Faces[faceIndex].Vertices)
                incidentFaces[vertex].push_back(faceIndex);

        auto findRoot = [&](uint32_t index)
        {
            uint32_t root = index;
            while (parent[root] != root)
                root = parent[root];
            while (parent[index] != index)
            {
                const uint32_t next = parent[index];
                parent[index] = root;
                index = next;
            }
            return root;
        };

        std::vector<Candidate> candidates;
        const auto edges = mesh.GetEdges();
        candidates.reserve(edges.size());
        for (const auto& edge : edges)
        {
            const glm::vec3 delta = positions[edge.A] - positions[edge.B];
            candidates.push_back({ glm::dot(delta, delta), edge.A, edge.B });
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
        {
            if (a.distanceSquared != b.distanceSquared)
                return a.distanceSquared < b.distanceSquared;
            if (a.a != b.a) return a.a < b.a;
            return a.b < b.b;
        });

        std::vector<uint32_t> visited(initialFaceCount, 0u);
        uint32_t visitStamp = 0;
        size_t currentFaceCount = initialFaceCount;
        for (const Candidate& candidate : candidates)
        {
            if (currentFaceCount <= targetFaces)
                break;

            uint32_t a = findRoot(candidate.a);
            uint32_t b = findRoot(candidate.b);
            if (a == b)
                continue;

            if (++visitStamp == 0)
            {
                std::fill(visited.begin(), visited.end(), 0u);
                visitStamp = 1;
            }
            std::vector<uint32_t> affectedFaces;
            auto addIncident = [&](uint32_t vertex)
            {
                for (uint32_t faceIndex : incidentFaces[vertex])
                {
                    if (visited[faceIndex] == visitStamp)
                        continue;
                    visited[faceIndex] = visitStamp;
                    affectedFaces.push_back(faceIndex);
                }
            };
            addIncident(a);
            addIncident(b);

            std::unordered_set<uint32_t> neighborsA;
            std::unordered_set<uint32_t> neighborsB;
            std::unordered_set<uint32_t> edgeOpposites;
            size_t edgeFaceCount = 0;
            size_t removedFaces = 0;
            for (uint32_t faceIndex : affectedFaces)
            {
                const auto& indices = mesh.Faces[faceIndex].Vertices;
                std::array<uint32_t, 3> roots = {
                    findRoot(indices[0]), findRoot(indices[1]), findRoot(indices[2]) };
                if (roots[0] == roots[1] || roots[1] == roots[2] || roots[2] == roots[0])
                    continue;
                const bool hasA = std::find(roots.begin(), roots.end(), a) != roots.end();
                const bool hasB = std::find(roots.begin(), roots.end(), b) != roots.end();
                if (hasA && hasB)
                {
                    ++edgeFaceCount;
                    ++removedFaces;
                    for (uint32_t root : roots)
                        if (root != a && root != b)
                            edgeOpposites.insert(root);
                    continue;
                }

                if (hasA)
                    for (uint32_t root : roots)
                        if (root != a) neighborsA.insert(root);
                if (hasB)
                    for (uint32_t root : roots)
                        if (root != b) neighborsB.insert(root);
            }
            if (edgeFaceCount == 0 || edgeFaceCount > 2 || removedFaces == 0 ||
                currentFaceCount - removedFaces < targetFaces)
                continue;

            // Link condition: impede que o colapso crie buracos ou uma junção
            // não-manifold em malhas triangulares comuns.
            std::unordered_set<uint32_t> commonNeighbors;
            for (uint32_t neighbor : neighborsA)
                if (neighborsB.contains(neighbor))
                    commonNeighbors.insert(neighbor);
            if (commonNeighbors.size() != edgeOpposites.size())
                continue;
            bool validLink = true;
            for (uint32_t opposite : edgeOpposites)
                if (!commonNeighbors.contains(opposite))
                {
                    validLink = false;
                    break;
                }
            if (!validLink)
                continue;

            const glm::vec3 mergedPosition = (positions[a] + positions[b]) * 0.5f;
            bool preservesSurface = true;
            for (uint32_t faceIndex : affectedFaces)
            {
                const auto& indices = mesh.Faces[faceIndex].Vertices;
                std::array<uint32_t, 3> roots = {
                    findRoot(indices[0]), findRoot(indices[1]), findRoot(indices[2]) };
                if (roots[0] == roots[1] || roots[1] == roots[2] || roots[2] == roots[0])
                    continue;
                const bool hasA = std::find(roots.begin(), roots.end(), a) != roots.end();
                const bool hasB = std::find(roots.begin(), roots.end(), b) != roots.end();
                if (!hasA && !hasB)
                    continue;
                if (hasA && hasB)
                    continue; // essa face some junto com a aresta colapsada

                glm::vec3 newPoints[3] = {
                    positions[roots[0]], positions[roots[1]], positions[roots[2]] };
                for (size_t corner = 0; corner < roots.size(); ++corner)
                    if (roots[corner] == a || roots[corner] == b)
                        newPoints[corner] = mergedPosition;

                const glm::vec3 newNormal = glm::cross(
                    newPoints[1] - newPoints[0], newPoints[2] - newPoints[0]);
                const float newLength = glm::length(newNormal);
                const glm::vec3 referenceNormal = referenceFaceNormals[faceIndex];
                if (glm::length(referenceNormal) > 0.5f &&
                    (newLength <= 1e-7f ||
                     glm::dot(referenceNormal, newNormal) <= newLength * 0.05f))
                {
                    preservesSurface = false;
                    break;
                }
            }
            if (!preservesSurface)
                continue;

            // Mantém no representante o maior conjunto de faces incidentes,
            // limitando o custo dos colapsos seguintes.
            if (incidentFaces[a].size() < incidentFaces[b].size() ||
                (incidentFaces[a].size() == incidentFaces[b].size() && a > b))
                std::swap(a, b);
            const float mergedWeight = weights[a] + weights[b];
            Vertex mergedAttributes = attributes[a];
            const float weightA = weights[a] / mergedWeight;
            const float weightB = weights[b] / mergedWeight;
            mergedAttributes.U = attributes[a].U * weightA + attributes[b].U * weightB;
            mergedAttributes.V = attributes[a].V * weightA + attributes[b].V * weightB;
            mergedAttributes.NX = attributes[a].NX * weightA + attributes[b].NX * weightB;
            mergedAttributes.NY = attributes[a].NY * weightA + attributes[b].NY * weightB;
            mergedAttributes.NZ = attributes[a].NZ * weightA + attributes[b].NZ * weightB;
            mergedAttributes.X = mergedPosition.x;
            mergedAttributes.Y = mergedPosition.y;
            mergedAttributes.Z = mergedPosition.z;
            attributes[a] = mergedAttributes;
            positions[a] = mergedPosition;
            weights[a] = mergedWeight;
            parent[b] = a;
            incidentFaces[a].insert(incidentFaces[a].end(),
                incidentFaces[b].begin(), incidentFaces[b].end());
            currentFaceCount -= removedFaces;
        }

        MeshTopology reduced;
        reduced.Faces.reserve(currentFaceCount);
        std::vector<uint32_t> compactIndex(vertexCount,
            std::numeric_limits<uint32_t>::max());
        for (const auto& face : mesh.Faces)
        {
            std::array<uint32_t, 3> roots = {
                findRoot(face.Vertices[0]), findRoot(face.Vertices[1]),
                findRoot(face.Vertices[2]) };
            if (roots[0] == roots[1] || roots[1] == roots[2] || roots[2] == roots[0])
                continue;

            std::array<uint32_t, 3> remapped{};
            for (size_t corner = 0; corner < roots.size(); ++corner)
            {
                const uint32_t root = roots[corner];
                if (compactIndex[root] == std::numeric_limits<uint32_t>::max())
                {
                    compactIndex[root] = static_cast<uint32_t>(reduced.Vertices.size());
                    reduced.Vertices.push_back({ attributes[root] });
                }
                remapped[corner] = compactIndex[root];
            }
            reduced.Faces.push_back({ remapped });
        }

        if (reduced.Faces.size() < 4 || !reduced.IsValid())
            return;
        reduced.RemoveDegenerate();
        reduced.RecalculateNormals(false);
        mesh = std::move(reduced);
    }

    void BooleanCombine(MeshTopology& target, const MeshTopology& operand, BooleanMode mode)
    {
        if (!target.IsValid() || !operand.IsValid()) return;

        glm::vec3 operandMin(std::numeric_limits<float>::max());
        glm::vec3 operandMax(std::numeric_limits<float>::lowest());
        for (const auto& vertex : operand.Vertices)
        {
            const glm::vec3 position = Position(vertex);
            operandMin = glm::min(operandMin, position);
            operandMax = glm::max(operandMax, position);
        }
        const auto insideOperand = [&](const MeshTopology::Face& face)
        {
            const glm::vec3 center = FaceCenter(target, face);
            return center.x >= operandMin.x && center.x <= operandMax.x &&
                   center.y >= operandMin.y && center.y <= operandMax.y &&
                   center.z >= operandMin.z && center.z <= operandMax.z;
        };

        if (mode == BooleanMode::Union)
        {
            const uint32_t base = static_cast<uint32_t>(target.Vertices.size());
            target.Vertices.insert(target.Vertices.end(), operand.Vertices.begin(), operand.Vertices.end());
            for (const auto& face : operand.Faces)
                target.Faces.push_back({ { face.Vertices[0] + base,
                                           face.Vertices[1] + base,
                                           face.Vertices[2] + base } });
        }
        else if (mode == BooleanMode::Difference)
        {
            target.Faces.erase(std::remove_if(target.Faces.begin(), target.Faces.end(), insideOperand),
                               target.Faces.end());
        }
        else
        {
            target.Faces.erase(std::remove_if(target.Faces.begin(), target.Faces.end(),
                [&](const MeshTopology::Face& face) { return !insideOperand(face); }),
                target.Faces.end());
        }
        target.RemoveDegenerate();
        target.RecalculateNormals(false);
    }

    void BridgeSelectedEdges(MeshTopology& mesh, const ModelingSelection& selection)
    {
        if (selection.Mode != ModelingSelectionMode::Edge || selection.Indices.size() != 2) return;
        const auto edges = mesh.GetEdges();
        if (selection.Indices[0] < 0 || selection.Indices[1] < 0 ||
            selection.Indices[0] >= static_cast<int>(edges.size()) ||
            selection.Indices[1] >= static_cast<int>(edges.size())) return;
        const auto& first = edges[selection.Indices[0]];
        const auto& second = edges[selection.Indices[1]];
        AddQuad(mesh, first.A, first.B, second.B, second.A);
        mesh.RecalculateNormals(false);
    }

    void SetFlatShading(MeshTopology& mesh) { mesh.RecalculateNormals(false); }
    void SetSmoothShading(MeshTopology& mesh) { mesh.RecalculateNormals(true); }

    void FlattenSelected(MeshTopology& mesh, const ModelingSelection& selection, int axis)
    {
        const auto selected = SelectedVertices(mesh, selection);
        if (selected.empty()) return;
        glm::vec3 center(0.0f);
        for (uint32_t idx : selected)
            center += Position(mesh.Vertices[idx]);
        center /= static_cast<float>(selected.size());

        for (uint32_t idx : selected)
        {
            glm::vec3 pos = Position(mesh.Vertices[idx]);
            if (axis == 0) pos.x = center.x;
            else if (axis == 1) pos.y = center.y;
            else if (axis == 2) pos.z = center.z;
            else
            {
                // Se nenhum eixo especificado, aplana no eixo Y por padrão
                pos.y = center.y;
            }
            SetPosition(mesh.Vertices[idx], pos);
        }
        mesh.RecalculateNormals(false);
    }

    void SpherizeSelected(MeshTopology& mesh, const ModelingSelection& selection, float factor)
    {
        const auto selected = SelectedVertices(mesh, selection);
        if (selected.size() < 3) return;
        glm::vec3 center(0.0f);
        for (uint32_t idx : selected)
            center += Position(mesh.Vertices[idx]);
        center /= static_cast<float>(selected.size());

        float avgRadius = 0.0f;
        for (uint32_t idx : selected)
            avgRadius += glm::length(Position(mesh.Vertices[idx]) - center);
        avgRadius /= static_cast<float>(selected.size());
        if (avgRadius <= 0.0001f) return;

        factor = std::clamp(factor, 0.0f, 1.0f);
        for (uint32_t idx : selected)
        {
            glm::vec3 pos = Position(mesh.Vertices[idx]);
            glm::vec3 dir = pos - center;
            float len = glm::length(dir);
            if (len > 0.0001f)
            {
                glm::vec3 sphereTarget = center + (dir / len) * avgRadius;
                SetPosition(mesh.Vertices[idx], glm::mix(pos, sphereTarget, factor));
            }
        }
        mesh.RecalculateNormals(false);
    }

    void PokeSelectedFaces(MeshTopology& mesh, const ModelingSelection& selection, float offset)
    {
        const auto selectedFaces = SelectedFaces(mesh, selection);
        if (selectedFaces.empty()) return;
        const auto original = mesh.Faces;
        std::vector<MeshTopology::Face> result;
        result.reserve(original.size() + selectedFaces.size() * 3);

        for (uint32_t fIdx = 0; fIdx < original.size(); ++fIdx)
        {
            const auto& face = original[fIdx];
            if (!selectedFaces.count(fIdx))
            {
                result.push_back(face);
                continue;
            }
            const glm::vec3 a = Position(mesh.Vertices[face.Vertices[0]]);
            const glm::vec3 b = Position(mesh.Vertices[face.Vertices[1]]);
            const glm::vec3 c = Position(mesh.Vertices[face.Vertices[2]]);
            glm::vec3 normal = glm::cross(b - a, c - a);
            float len = glm::length(normal);
            normal = (len > 1e-6f) ? (normal / len) : glm::vec3(0, 1, 0);

            glm::vec3 center = (a + b + c) / 3.0f + normal * offset;
            uint32_t centerIdx = AddVertex(mesh, center, mesh.Vertices[face.Vertices[0]].Value);

            result.push_back({ { face.Vertices[0], face.Vertices[1], centerIdx } });
            result.push_back({ { face.Vertices[1], face.Vertices[2], centerIdx } });
            result.push_back({ { face.Vertices[2], face.Vertices[0], centerIdx } });
        }
        mesh.Faces = std::move(result);
        mesh.RecalculateNormals(false);
    }

    void WeldByDistance(MeshTopology& mesh, float distance)
    {
        if (mesh.Vertices.size() < 2 || distance <= 0.00001f) return;
        std::vector<uint32_t> remap(mesh.Vertices.size());
        for (uint32_t i = 0; i < mesh.Vertices.size(); ++i) remap[i] = i;

        for (uint32_t i = 0; i < mesh.Vertices.size(); ++i)
        {
            if (remap[i] != i) continue;
            const glm::vec3 posA = Position(mesh.Vertices[i]);
            for (uint32_t j = i + 1; j < mesh.Vertices.size(); ++j)
            {
                if (remap[j] != j) continue;
                const glm::vec3 posB = Position(mesh.Vertices[j]);
                if (glm::distance(posA, posB) <= distance)
                {
                    remap[j] = i;
                }
            }
        }

        for (auto& face : mesh.Faces)
        {
            for (int k = 0; k < 3; ++k)
                face.Vertices[k] = remap[face.Vertices[k]];
        }
        mesh.RemoveDegenerate();
        mesh.RecalculateNormals(false);
    }

    void TwistSelected(MeshTopology& mesh, const ModelingSelection& selection, int axis, float angleDegrees)
    {
        const auto selected = SelectedVertices(mesh, selection);
        if (selected.empty()) return;

        float minCoord = std::numeric_limits<float>::max();
        float maxCoord = std::numeric_limits<float>::lowest();
        for (uint32_t idx : selected)
        {
            const glm::vec3 pos = Position(mesh.Vertices[idx]);
            float c = (axis == 0) ? pos.x : (axis == 1) ? pos.y : pos.z;
            minCoord = std::min(minCoord, c);
            maxCoord = std::max(maxCoord, c);
        }
        float range = maxCoord - minCoord;
        if (range <= 0.0001f) return;

        for (uint32_t idx : selected)
        {
            glm::vec3 pos = Position(mesh.Vertices[idx]);
            float c = (axis == 0) ? pos.x : (axis == 1) ? pos.y : pos.z;
            float t = (c - minCoord) / range;
            float theta = glm::radians(angleDegrees * t);
            float cosT = std::cos(theta);
            float sinT = std::sin(theta);

            if (axis == 1) // Y axis
            {
                float x = pos.x * cosT - pos.z * sinT;
                float z = pos.x * sinT + pos.z * cosT;
                pos.x = x; pos.z = z;
            }
            else if (axis == 0) // X axis
            {
                float y = pos.y * cosT - pos.z * sinT;
                float z = pos.y * sinT + pos.z * cosT;
                pos.y = y; pos.z = z;
            }
            else // Z axis
            {
                float x = pos.x * cosT - pos.y * sinT;
                float y = pos.x * sinT + pos.y * cosT;
                pos.x = x; pos.y = y;
            }
            SetPosition(mesh.Vertices[idx], pos);
        }
        mesh.RecalculateNormals(false);
    }
}
