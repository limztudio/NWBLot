// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../paint.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_LayoutNoParent = Limit<u32>::s_Max;
inline constexpr u32 s_LayoutMaxNodes = 4096u;

namespace LayoutDirection{
    enum Enum : u8{ Leaf, Row, Column, Overlay };
};

namespace LayoutSizePolicy{
    enum Enum : u8{ Fixed, Content, Stretch };
};

struct LayoutSize{
    LayoutSizePolicy::Enum policy = LayoutSizePolicy::Content;
    // Fixed logical size, or a positive stretch weight. Content ignores this finite, nonnegative value.
    f32 value = 0.0f;
};

struct LayoutNodeDesc{
    LayoutDirection::Enum direction = LayoutDirection::Leaf;
    LayoutSize width;
    LayoutSize height;
    Insets padding;
    f32 gap = 0.0f;
    Point intrinsicSize;
    bool clipChildren = true;
};

struct LayoutBox{
    Rect rectangle;
    Rect content;
    Rect clip;
    Rect hit;
    Point measuredSize;
};


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
    [[nodiscard]] static bool IsValidDescription(const LayoutNodeDesc& description);
    [[nodiscard]] static bool IsValidRectangle(const Rect& rectangle);
    [[nodiscard]] static Rect Intersect(const Rect& lhs, const Rect& rhs);
    [[nodiscard]] static Rect Inset(const Rect& rectangle, const Insets& padding);


public:
    // Capacity is clamped to s_LayoutMaxNodes; zero capacity admits no nodes.
    explicit LayoutTree(Core::Alloc::GlobalArena& arena, u32 maxNodes = s_LayoutMaxNodes);


public:
    // Start a new build while retaining the last successful arranged output.
    void reset();
    // A failed admission poisons this build until reset(), and leaves outIndex unchanged.
    [[nodiscard]] bool addNode(u32 parent, const LayoutNodeDesc& description, u32& outIndex);
    // Root sizing follows its policies against the viewport. All geometry stays in logical units.
    [[nodiscard]] bool arrange(const Rect& viewport);
    [[nodiscard]] u32 nodeCount()const{ return static_cast<u32>(m_nodes.size()); }
    [[nodiscard]] const LayoutBox* box(u32 index)const{ return index < m_boxes.size() ? &m_boxes[index] : nullptr; }
    [[nodiscard]] const PaintVector<LayoutBox>& boxes()const{ return m_boxes; }


private:
    [[nodiscard]] bool measure();
    [[nodiscard]] bool arrangeNode(u32 index);


private:
    PaintVector<Node> m_nodes;
    PaintVector<Work> m_work;
    PaintVector<LayoutBox> m_boxes;
    PaintVector<LayoutBox> m_stagedBoxes;
    u32 m_maxNodes = 0u;
    bool m_buildFailed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

