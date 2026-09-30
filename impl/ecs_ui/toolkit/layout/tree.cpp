// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "tree.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LayoutTree::IsValidDescription(const LayoutNodeDesc& description){
    const auto validSize = [](const LayoutSize& size){
        return size.policy <= LayoutSizePolicy::Stretch && IsFinite(size.value) && size.value >= 0.0f
            && (size.policy != LayoutSizePolicy::Stretch || size.value > 0.0f);
    };
    const Insets& padding = description.padding;
    return
        description.direction <= LayoutDirection::Overlay && validSize(description.width) && validSize(description.height)
        && IsFinite(padding.left) && padding.left >= 0.0f && IsFinite(padding.top) && padding.top >= 0.0f
        && IsFinite(padding.right) && padding.right >= 0.0f && IsFinite(padding.bottom) && padding.bottom >= 0.0f
        && IsFinite(padding.left + padding.right) && IsFinite(padding.top + padding.bottom)
        && IsFinite(description.gap) && description.gap >= 0.0f
        && IsFinite(description.intrinsicSize.x) && description.intrinsicSize.x >= 0.0f
        && IsFinite(description.intrinsicSize.y) && description.intrinsicSize.y >= 0.0f
    ;
}

bool LayoutTree::IsValidRectangle(const Rect& rectangle){
    return
        IsFinite(rectangle.x) && IsFinite(rectangle.y) && IsFinite(rectangle.width) && IsFinite(rectangle.height)
        && rectangle.width >= 0.0f && rectangle.height >= 0.0f
        && IsFinite(rectangle.x + rectangle.width) && IsFinite(rectangle.y + rectangle.height)
    ;
}

Rect LayoutTree::Intersect(const Rect& lhs, const Rect& rhs){
    const f32 left = Max(lhs.x, rhs.x);
    const f32 top = Max(lhs.y, rhs.y);
    const f32 right = Min(lhs.x + lhs.width, rhs.x + rhs.width);
    const f32 bottom = Min(lhs.y + lhs.height, rhs.y + rhs.height);
    return { left, top, Max(0.0f, right - left), Max(0.0f, bottom - top) };
}

Rect LayoutTree::Inset(const Rect& rectangle, const Insets& padding){
    const f32 left = Min(padding.left, rectangle.width);
    const f32 top = Min(padding.top, rectangle.height);
    const f64 width = static_cast<f64>(rectangle.width) - padding.left - padding.right;
    const f64 height = static_cast<f64>(rectangle.height) - padding.top - padding.bottom;
    return { rectangle.x + left, rectangle.y + top, static_cast<f32>(Max(0.0, width)), static_cast<f32>(Max(0.0, height)) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


LayoutTree::LayoutTree(Core::Alloc::GlobalArena& arena, const u32 maxNodes)
    : m_nodes(arena)
    , m_work(arena)
    , m_boxes(arena)
    , m_stagedBoxes(arena)
    , m_maxNodes(Min(maxNodes, s_LayoutMaxNodes))
{
    m_nodes.reserve(m_maxNodes);
    m_work.reserve(m_maxNodes);
    m_boxes.reserve(m_maxNodes);
    m_stagedBoxes.reserve(m_maxNodes);
}

void LayoutTree::reset(){
    m_nodes.clear();
    m_work.clear();
    m_stagedBoxes.clear();
    m_buildFailed = false;
}

bool LayoutTree::addNode(const u32 parent, const LayoutNodeDesc& description, u32& outIndex){
    if(
        m_buildFailed || m_nodes.size() >= m_maxNodes || !IsValidDescription(description)
        || (m_nodes.empty() ? parent != s_LayoutNoParent : parent >= m_nodes.size())
        || (parent != s_LayoutNoParent && m_nodes[parent].description.direction == LayoutDirection::Leaf)
    ){
        m_buildFailed = true;
        return false;
    }
    const u32 index = static_cast<u32>(m_nodes.size());
    m_nodes.push_back({ description, parent });
    if(parent != s_LayoutNoParent){
        Node& parentNode = m_nodes[parent];
        if(parentNode.lastChild == s_LayoutNoParent)
            parentNode.firstChild = index;
        else
            m_nodes[parentNode.lastChild].nextSibling = index;
        parentNode.lastChild = index;
    }
    outIndex = index;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

