#pragma once

#include "../../Scene/Entity/Entity.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MaraeUI
{
    using ResolvedUIMap = std::unordered_map<uint64_t, UIComponent::ResolvedRect>;

    inline std::unordered_map<uint64_t, size_t> GetUIHierarchyDepths(
        const std::vector<Entity>& entities)
    {
        std::unordered_map<uint64_t, const Entity*> byId;
        byId.reserve(entities.size());
        for (const Entity& entity : entities)
            byId.emplace(entity.GetID(), &entity);

        std::unordered_map<uint64_t, size_t> depths;
        depths.reserve(entities.size());
        for (const Entity& entity : entities)
        {
            if (!entity.HasUI())
                continue;
            size_t depth = 0;
            uint64_t parentId = entity.GetParentID();
            std::unordered_set<uint64_t> visited{ entity.GetID() };
            while (parentId != 0 && visited.insert(parentId).second)
            {
                const auto parent = byId.find(parentId);
                if (parent == byId.end())
                    break;
                ++depth;
                parentId = parent->second->GetParentID();
            }
            depths.emplace(entity.GetID(), depth);
        }
        return depths;
    }

    template<typename EntityPointer>
    inline bool IsUIHierarchyEnabled(
        const Entity& entity,
        const std::unordered_map<uint64_t, EntityPointer>& entitiesById)
    {
        const Entity* current = &entity;
        std::unordered_set<uint64_t> visited;
        while (current && visited.insert(current->GetID()).second)
        {
            if (current->HasUI() && !current->GetUIComponent().Enabled)
                return false;
            const uint64_t parentId = current->GetParentID();
            if (parentId == 0)
                return true;
            const auto parent = entitiesById.find(parentId);
            if (parent == entitiesById.end())
                return true;
            current = parent->second;
        }
        return current == nullptr;
    }

    // Resolves parent-relative rectangles first, then applies optional
    // automatic layout to the parent's direct UI children.
    inline ResolvedUIMap ResolveLayouts(std::vector<Entity>& entities,
                                       float canvasWidth, float canvasHeight)
    {
        ResolvedUIMap rects;
        std::unordered_map<uint64_t, Entity*> byId;
        std::vector<Entity*> uiEntities;
        uiEntities.reserve(entities.size());

        for (Entity& entity : entities)
        {
            if (!entity.HasUI())
                continue;
            byId.emplace(entity.GetID(), &entity);
            uiEntities.push_back(&entity);
            const UIComponent& ui = entity.GetUIComponent();
            rects.emplace(entity.GetID(), ui.Type == UIElementType::ScreenGui
                ? UIComponent::ResolvedRect{ 0.0f, 0.0f, canvasWidth, canvasHeight }
                : ui.ResolveRect(canvasWidth, canvasHeight));
        }

        const auto depthOf = [&byId](const Entity* entity)
        {
            size_t depth = 0;
            uint64_t parentId = entity->GetParentID();
            while (parentId != 0 && depth <= byId.size())
            {
                const auto parent = byId.find(parentId);
                if (parent == byId.end()) break;
                parentId = parent->second->GetParentID();
                ++depth;
            }
            return depth;
        };
        std::stable_sort(uiEntities.begin(), uiEntities.end(), [&](const Entity* a, const Entity* b)
        {
            return depthOf(a) < depthOf(b);
        });

        for (Entity* child : uiEntities)
        {
            const auto parentIt = byId.find(child->GetParentID());
            if (child->GetParentID() == 0 || parentIt == byId.end())
                continue;
            const auto parentRectIt = rects.find(parentIt->second->GetID());
            if (parentRectIt == rects.end())
                continue;

            const auto parentRect = parentRectIt->second;
            auto childRect = child->GetUIComponent().ResolveRect(
                parentRect.Width, parentRect.Height);
            childRect.X += parentRect.X;
            childRect.Y += parentRect.Y;
            rects[child->GetID()] = childRect;
        }

        for (Entity* parent : uiEntities)
        {
            UIComponent& container = parent->GetUIComponent();
            const auto parentRectIt = rects.find(parent->GetID());
            if (parentRectIt == rects.end()) continue;
            const auto parentRect = parentRectIt->second;
            std::vector<Entity*> children;
            for (Entity* candidate : uiEntities)
                if (candidate->GetUIComponent().Enabled &&
                    candidate->GetParentID() == parent->GetID())
                    children.push_back(candidate);
            if (children.empty()) continue;

            const float padding = std::clamp(container.LayoutPadding, 0.0f, 4096.0f);
            const float spacing = std::clamp(container.LayoutSpacing, 0.0f, 4096.0f);
            const float scroll = container.ScrollViewEnabled
                ? std::clamp(container.ScrollOffsetY, 0.0f, 1000000.0f) : 0.0f;
            const int columns = std::clamp(container.LayoutColumns, 1, 256);
            const float gridCellWidth = std::max(0.0f,
                (parentRect.Width - 2.0f * padding - spacing * static_cast<float>(columns - 1)) /
                static_cast<float>(columns));
            size_t childIndex = 0;
            float cursorX = parentRect.X + padding;
            float cursorY = parentRect.Y + padding - scroll;

            for (Entity* child : children)
            {
                auto childRectIt = rects.find(child->GetID());
                if (childRectIt == rects.end()) continue;
                auto rect = childRectIt->second;
                switch (container.LayoutMode)
                {
                case UILayoutMode::Horizontal:
                    rect.X = cursorX;
                    rect.Y = cursorY;
                    if (container.StretchLayoutChildren)
                        rect.Height = std::max(0.0f, parentRect.Height - 2.0f * padding);
                    cursorX += rect.Width + spacing;
                    break;
                case UILayoutMode::Vertical:
                    rect.X = parentRect.X + padding;
                    rect.Y = cursorY;
                    if (container.StretchLayoutChildren)
                        rect.Width = std::max(0.0f, parentRect.Width - 2.0f * padding);
                    cursorY += rect.Height + spacing;
                    break;
                case UILayoutMode::Grid:
                {
                    const int column = static_cast<int>(childIndex % static_cast<size_t>(columns));
                    const size_t row = childIndex / static_cast<size_t>(columns);
                    rect.X = parentRect.X + padding + static_cast<float>(column) * (gridCellWidth + spacing);
                    rect.Y = parentRect.Y + padding + static_cast<float>(row) * (rect.Height + spacing) - scroll;
                    if (container.StretchLayoutChildren)
                        rect.Width = gridCellWidth;
                    break;
                }
                case UILayoutMode::None:
                    if (container.ScrollViewEnabled)
                        rect.Y -= scroll;
                    break;
                }
                childRectIt->second = rect;
                ++childIndex;
            }
        }
        return rects;
    }

    inline float GetMaxScrollOffset(const Entity& parent,
                                   const std::vector<Entity>& entities,
                                   float canvasWidth, float canvasHeight,
                                   const ResolvedUIMap* resolvedRects = nullptr)
    {
        if (!parent.HasUI() || !parent.GetUIComponent().ScrollViewEnabled)
            return 0.0f;
        const UIComponent& container = parent.GetUIComponent();
        auto parentRect = container.ResolveRect(canvasWidth, canvasHeight);
        if (resolvedRects)
        {
            const auto it = resolvedRects->find(parent.GetID());
            if (it != resolvedRects->end()) parentRect = it->second;
        }
        const float padding = std::clamp(container.LayoutPadding, 0.0f, 4096.0f);
        const float spacing = std::clamp(container.LayoutSpacing, 0.0f, 4096.0f);
        std::vector<float> childHeights;
        float freeContentBottom = 0.0f;
        for (const Entity& child : entities)
        {
            if (!child.HasUI() || !child.GetUIComponent().Enabled || child.GetParentID() != parent.GetID())
                continue;
            auto rect = child.GetUIComponent().ResolveRect(canvasWidth, canvasHeight);
            if (resolvedRects)
            {
                const auto it = resolvedRects->find(child.GetID());
                if (it != resolvedRects->end()) rect = it->second;
            }
            childHeights.push_back(rect.Height);
            freeContentBottom = std::max(freeContentBottom, rect.Y + rect.Height - parentRect.Y);
        }
        if (childHeights.empty()) return 0.0f;

        float contentHeight = freeContentBottom + padding;
        if (container.LayoutMode == UILayoutMode::Vertical)
        {
            contentHeight = 2.0f * padding + spacing * static_cast<float>(childHeights.size() - 1);
            for (float height : childHeights) contentHeight += height;
        }
        else if (container.LayoutMode == UILayoutMode::Grid)
        {
            const size_t columns = static_cast<size_t>(std::clamp(container.LayoutColumns, 1, 256));
            contentHeight = 2.0f * padding;
            for (size_t first = 0; first < childHeights.size(); first += columns)
            {
                float rowHeight = 0.0f;
                for (size_t i = first; i < std::min(childHeights.size(), first + columns); ++i)
                    rowHeight = std::max(rowHeight, childHeights[i]);
                contentHeight += rowHeight;
                if (first + columns < childHeights.size()) contentHeight += spacing;
            }
        }
        return std::clamp(contentHeight - parentRect.Height, 0.0f, 1000000.0f);
    }
}
