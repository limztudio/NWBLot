// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../paint.h"

#include <impl/ecs_ui/toolkit/layout/types.adhd.h>

#include <global/simd/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Build and arrange on one owning UI thread. The supplied arena outlives the tree and its published boxes.
class LayoutTree final : NoCopy{
private:
    struct Node{
        LayoutNodeDesc description;
        u32 parent = s_LayoutNoParent;
        u32 firstChild = s_LayoutNoParent;
        u32 lastChild = s_LayoutNoParent;
        u32 nextSibling = s_LayoutNoParent;
    };

    struct Work{
        LayoutBox box;
        Rect inheritedClip;
    };


private:
    [[nodiscard]] static b8 IsValidDescription(const LayoutNodeDesc& description)noexcept;
    [[nodiscard]] static b8 IsValidRectangle(const Rect& rectangle)noexcept;
    [[nodiscard]] static SIMDVectorSingle Inset(SIMDVectorSingle rectangle, SIMDVectorSingle padding)noexcept;


public:
    // Capacity is clamped to s_LayoutMaxNodes; zero capacity admits no nodes.
    explicit LayoutTree(Core::Alloc::GlobalArena& arena, u32 maxNodes = s_LayoutMaxNodes);


public:
    // Start a new build while retaining the last successful arranged output.
    void reset()noexcept;
    // A failed admission poisons this build until reset().
    [[nodiscard]] Expected<u32> addNode(u32 parent, const LayoutNodeDesc& description);
    // Root sizing follows its policies against the viewport. All geometry stays in logical units.
    [[nodiscard]] b8 arrange(const Rect& viewport);
    [[nodiscard]] u32 nodeCount()const noexcept{ return static_cast<u32>(m_nodes.size()); }

    [[nodiscard]] const LayoutBox* box(u32 index)const noexcept{ return index < m_boxes.size() ? &m_boxes[index] : nullptr; }

    [[nodiscard]] const PaintVector<LayoutBox>& boxes()const noexcept{ return m_boxes; }


private:
    [[nodiscard]] b8 measure()noexcept;
    [[nodiscard]] b8 arrangeNode(u32 index)noexcept;


private:
    PaintVector<Node> m_nodes;
    PaintVector<Work> m_work;
    PaintVector<LayoutBox> m_boxes;
    PaintVector<LayoutBox> m_stagedBoxes;
    u32 m_maxNodes = 0u;
    b8 m_buildFailed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
