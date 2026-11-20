#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

enum class ModelingSelectionMode
{
    Vertex = 0,
    Edge = 1,
    Face = 2
};

class ModelingSelection
{
public:
    ModelingSelectionMode Mode = ModelingSelectionMode::Face;
    std::vector<int> Indices;

    void Clear() { Indices.clear(); }
    bool Empty() const { return Indices.empty(); }
    bool Contains(int index) const
    {
        return std::find(Indices.begin(), Indices.end(), index) != Indices.end();
    }
    void Toggle(int index)
    {
        auto found = std::find(Indices.begin(), Indices.end(), index);
        if (found == Indices.end()) Indices.push_back(index);
        else Indices.erase(found);
    }
    void Add(int index)
    {
        if (!Contains(index)) Indices.push_back(index);
    }
    void Normalize()
    {
        std::sort(Indices.begin(), Indices.end());
        Indices.erase(std::unique(Indices.begin(), Indices.end()), Indices.end());
    }
};
