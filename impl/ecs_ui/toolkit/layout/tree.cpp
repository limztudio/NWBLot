// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "tree.h"
#include "../rect_math.h"

#include <global/math/vector_double.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LayoutTree::IsValidDescription(const LayoutNodeDesc& description)noexcept{
    const auto validSize = [](const LayoutSize& size)noexcept{
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

bool LayoutTree::IsValidRectangle(const Rect& rectangle)noexcept{
    return
        IsFinite(rectangle.x) && IsFinite(rectangle.y) && IsFinite(rectangle.width) && IsFinite(rectangle.height)
        && rectangle.width >= 0.0f && rectangle.height >= 0.0f
        && IsFinite(rectangle.x + rectangle.width) && IsFinite(rectangle.y + rectangle.height)
    ;
}

Rect LayoutTree::Intersect(const Rect& lhs, const Rect& rhs)noexcept{
    const SIMDVector intersection = IntersectRectValue(
        VectorSet(lhs.x, lhs.y, lhs.width, lhs.height), VectorSet(rhs.x, rhs.y, rhs.width, rhs.height)
    );
    return { VectorGetX(intersection), VectorGetY(intersection), VectorGetZ(intersection), VectorGetW(intersection) };
}

Rect LayoutTree::Inset(const Rect& rectangle, const Insets& padding)noexcept{
    const SIMDVector paddingStart = VectorSet(padding.left, padding.top, padding.left, padding.top);
    const SIMDVector rectangleSize = VectorSet(rectangle.width, rectangle.height, rectangle.width, rectangle.height);
    const SIMDVector firstInset = VectorSelect(rectangleSize, paddingStart, VectorLess(paddingStart, rectangleSize));
    const f32 left = VectorGetX(firstInset);
    const f32 top = VectorGetY(firstInset);
    const SIMDVectorDouble widthHeightValue = ((SIMDVectorDouble{ static_cast<f64>(rectangle.width), static_cast<f64>(rectangle.height) } - SIMDVectorDouble{ padding.left, padding.top }) - SIMDVectorDouble{ padding.right, padding.bottom });
    const f64 width = widthHeightValue.x;
    const f64 height = widthHeightValue.y;
    const SIMDVector origin = VectorAdd(VectorSet(rectangle.x, rectangle.y, 0.0f, 0.0f), VectorSet(left, top, 0.0f, 0.0f));
    return { VectorGetX(origin), VectorGetY(origin), static_cast<f32>(Max(0.0, width)), static_cast<f32>(Max(0.0, height)) };
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

void LayoutTree::reset()noexcept{
    m_nodes.clear();
    m_work.clear();
    m_stagedBoxes.clear();
    m_buildFailed = false;
}

Expected<u32> LayoutTree::addNode(const u32 parent, const LayoutNodeDesc& description){
    if(
        m_buildFailed || m_nodes.size() >= m_maxNodes || !IsValidDescription(description)
        || (m_nodes.empty() ? parent != s_LayoutNoParent : parent >= m_nodes.size())
        || (parent != s_LayoutNoParent && m_nodes[parent].description.direction == LayoutDirection::Leaf)
    ){
        m_buildFailed = true;
        return MakeUnexpected(Failure{});
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
    return index;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

