#include "SceneCommands.h"
#include "../Scene/Scene/Scene.h"
#include "../Scene/Entity/Entity.h"
#include "Components/ColorComponent.h"
#include "../../Renderer/MeshFactory/MeshFactory.h"
#include "../../Renderer/Mesh/MeshCache.h"
#include "../Scene/Entity/Entity.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>

static std::string StripCopySuffix(const std::string& name, std::string& outBase)
{
    // Tentar padrao " Copy N" com numeros no final
    std::regex copyNRe(R"(\s+Copy\s+(\d+)$)");
    std::smatch m;
    if (std::regex_search(name, m, copyNRe))
    {
        outBase.assign(name.begin(), m[0].first);
        return outBase;
    }
    // Tentar padrao " Copy" no final
    const std::string suffix = " Copy";
    if (name.size() >= suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
    {
        outBase = name.substr(0, name.size() - suffix.size());
        return outBase;
    }
    outBase = name;
    return name;
}

static int NextCopyIndex(const std::vector<Entity>& entities, const std::string& baseName)
{
    std::regex copyNRe(R"(\s+Copy\s+(\d+)$)");
    const std::string plainSuffix = " Copy";
    int maxIdx = 0;
    for (const auto& e : entities)
    {
        const std::string& ename = e.GetName();
        if (ename.size() <= baseName.size()) continue;
        if (ename.compare(0, baseName.size(), baseName) != 0) continue;

        const char* p = ename.c_str() + baseName.size();
        // Primeiro char apos base deve ser espaco
        if (*p != ' ') continue;
        p++;

        // Segue "Copy" ?
        const std::string cs = "Copy";
        if (strncmp(p, cs.c_str(), cs.size()) != 0) continue;
        p += cs.size();

        if (*p == '\0')
        {
            // "Base Copy" sem numero -> conta como 1
            if (maxIdx < 1) maxIdx = 1;
            continue;
        }
        if (*p == ' ')
        {
            p++;
            const char* numStart = p;
            char* endp = nullptr;
            long v = std::strtol(numStart, &endp, 10);
            if (endp && endp != numStart && *endp == '\0' && v > 0)
            {
                int iv = (int)v;
                if (maxIdx < iv) maxIdx = iv;
            }
        }
    }
    return maxIdx + 1;
}

void SceneCommands::DeleteEntity(
    Scene& scene,
    int index)
{
    scene.DeleteEntity(index);
}

int SceneCommands::DuplicateEntity(
    Scene& scene,
    int index)
{
    EntitySelectionSnapshot snapshot = CaptureSelection(scene, { index });
    const std::vector<int> roots = PasteSelection(scene, snapshot);
    return roots.empty() ? index : roots.front();
}

SceneCommands::EntitySelectionSnapshot SceneCommands::CaptureSelection(
    Scene& scene,
    const std::vector<int>& indices)
{
    EntitySelectionSnapshot snapshot;
    const auto& entities = scene.GetEntities();
    std::unordered_set<uint64_t> requested;
    std::unordered_map<uint64_t, uint64_t> parentByChild;
    for (int index : indices)
        if (index >= 0 && index < static_cast<int>(entities.size()))
            requested.insert(entities[static_cast<size_t>(index)].GetID());

    for (const Entity& candidate : entities)
    {
        if (candidate.GetParentID() != 0)
            parentByChild[candidate.GetID()] = candidate.GetParentID();
        for (uint64_t childID : candidate.GetChildrenIDs())
            if (!parentByChild.contains(childID)) parentByChild[childID] = candidate.GetID();
    }

    // Selecting both a Model and one of its children must only copy that subtree once.
    std::unordered_set<uint64_t> selectedRoots;
    for (int index : indices)
    {
        if (index < 0 || index >= static_cast<int>(entities.size())) continue;
        const uint64_t id = entities[static_cast<size_t>(index)].GetID();
        if (!requested.contains(id) || !selectedRoots.insert(id).second) continue;
        const Entity* entity = scene.FindEntityByID(id);
        std::unordered_set<uint64_t> visited;
        bool coveredBySelectedAncestor = false;
        uint64_t parentID = 0;
        if (entity)
        {
            const auto parent = parentByChild.find(id);
            if (parent != parentByChild.end()) parentID = parent->second;
        }
        for (; parentID != 0 && visited.insert(parentID).second;)
        {
            if (requested.contains(parentID))
            {
                coveredBySelectedAncestor = true;
                break;
            }
            const auto parent = parentByChild.find(parentID);
            parentID = parent == parentByChild.end() ? 0 : parent->second;
        }
        if (!coveredBySelectedAncestor && entity && !entity->IsEditorOnly())
            snapshot.RootIDs.push_back(id);
    }
    if (snapshot.RootIDs.empty()) return snapshot;

    std::unordered_set<uint64_t> captured;
    std::vector<uint64_t> pending(snapshot.RootIDs.rbegin(), snapshot.RootIDs.rend());
    while (!pending.empty())
    {
        const uint64_t id = pending.back();
        pending.pop_back();
        if (!captured.insert(id).second) continue;
        const Entity* entity = scene.FindEntityByID(id);
        if (!entity || entity->IsEditorOnly()) continue;
        snapshot.Entities.push_back(*entity);
        // Push in reverse scene order so the preorder matches the hierarchy order.
        for (auto it = entities.rbegin(); it != entities.rend(); ++it)
        {
            const auto parent = parentByChild.find(it->GetID());
            if (parent != parentByChild.end() && parent->second == id)
                pending.push_back(it->GetID());
        }
    }
    return snapshot;
}

std::vector<int> SceneCommands::PasteSelection(
    Scene& scene,
    const EntitySelectionSnapshot& snapshot,
    std::vector<uint64_t>* createdIDs,
    bool suffixRootNames)
{
    std::vector<int> rootIndices;
    if (snapshot.Entities.empty() || snapshot.RootIDs.empty()) return rootIndices;

    auto& entities = scene.GetEntities();
    std::unordered_set<uint64_t> rootIDs(snapshot.RootIDs.begin(), snapshot.RootIDs.end());
    std::unordered_map<uint64_t, uint64_t> idRemap;
    idRemap.reserve(snapshot.Entities.size());
    std::vector<uint64_t> createdInOrder;
    createdInOrder.reserve(snapshot.Entities.size());

    // Create every destination first, then restore internal links by remapped ID.
    for (const Entity& source : snapshot.Entities)
    {
        Entity& clone = scene.CreateEntity();
        const uint64_t cloneID = clone.GetID();
        clone.CopyDataFrom(source);
        clone.SetParentID(0);
        clone.GetChildrenIDs().clear();
        idRemap[source.GetID()] = cloneID;
        createdInOrder.push_back(cloneID);

        if (rootIDs.contains(source.GetID()) && suffixRootNames)
        {
            std::string baseName;
            StripCopySuffix(source.GetName(), baseName);
            clone.SetName(baseName + " Copy " + std::to_string(NextCopyIndex(entities, baseName)));
        }
        if (clone.HasAudio())
        {
            clone.GetAudioComponent().RuntimeVoiceID = AUDIO_INVALID_VOICE;
            clone.GetAudioComponent().PlayState = AudioPlayState::Stopped;
            clone.GetAudioComponent().SourceID = "audio_" + std::to_string(cloneID);
        }
        if (clone.HasAI()) clone.GetAIComponent().ResetRuntime();
    }

    std::unordered_map<uint64_t, uint64_t> snapshotParentByChild;
    for (const Entity& candidate : snapshot.Entities)
    {
        if (candidate.GetParentID() != 0)
            snapshotParentByChild[candidate.GetID()] = candidate.GetParentID();
        for (uint64_t childID : candidate.GetChildrenIDs())
            if (!snapshotParentByChild.contains(childID)) snapshotParentByChild[childID] = candidate.GetID();
    }
    for (const Entity& source : snapshot.Entities)
    {
        const auto cloneIt = idRemap.find(source.GetID());
        if (cloneIt == idRemap.end()) continue;
        const auto sourceParent = snapshotParentByChild.find(source.GetID());
        uint64_t parentID = sourceParent == snapshotParentByChild.end() ? 0 : sourceParent->second;
        if (const auto internalParent = idRemap.find(parentID); internalParent != idRemap.end())
            parentID = internalParent->second;
        else if (parentID != 0 && !scene.FindEntityByID(parentID))
            parentID = 0;
        if (parentID != 0)
            scene.SetParent(cloneIt->second, parentID, false);
    }

    for (uint64_t rootID : snapshot.RootIDs)
        if (const auto it = idRemap.find(rootID); it != idRemap.end())
        {
            const auto indexIt = std::find_if(entities.begin(), entities.end(),
                [&](const Entity& e) { return e.GetID() == it->second; });
            if (indexIt != entities.end()) rootIndices.push_back(static_cast<int>(indexIt - entities.begin()));
        }

    if (createdIDs) *createdIDs = std::move(createdInOrder);
    scene.UpdateTransforms();
    return rootIndices;
}

Entity& SceneCommands::CreateCube(
    Scene& scene)
{
    Entity& cube = scene.CreateEntity();
    cube.GetMeshComponent().MeshData = MeshCache::CreateCubeInstance();
    return cube;
}
