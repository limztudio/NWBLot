// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "tree.h"
#include "../rect_math.h"

#include <global/simd/vector.h>
#include <global/simplemath.h>
#include <global/simd/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LayoutTree::IsValidDescription(const LayoutNodeDesc& description)noexcept{
    const auto validSize = [](const LayoutSize& size)noexcept{
        return size.policy <= LayoutSizePolicy::Stretch && IsFinite(size.value) && size.value >= 0.0f && (size.policy != LayoutSizePolicy::Stretch || size.value > 0.0f);
    };
    const Insets& padding = description.padding;
    return
        description.direction <= LayoutDirection::Overlay && validSize(description.width) && validSize(description.height)
        && IsFinite(padding.left) && padding.left >= 0.0f && IsFinite(padding.top) && padding.top >= 0.0f
        && IsFinite(padding.right) && padding.right >= 0.0f && IsFinite(padding.bottom) && padding.bottom >= 0.0f
        && VectorIsFinite(VectorAdd(VectorSet(padding.left, padding.top, 0.0f, 0.0f), VectorSet(padding.right, padding.bottom, 0.0f, 0.0f)), VectorComponentMask::s_XY)
        && IsFinite(description.gap) && description.gap >= 0.0f
        && IsFinite(description.intrinsicSize.x) && description.intrinsicSize.x >= 0.0f
        && IsFinite(description.intrinsicSize.y) && description.intrinsicSize.y >= 0.0f
    ;
}

bool LayoutTree::IsValidRectangle(const Rect& rectangle)noexcept{
    return
        IsFinite(rectangle.x) && IsFinite(rectangle.y) && IsFinite(rectangle.width) && IsFinite(rectangle.height) && rectangle.width >= 0.0f && rectangle.height >= 0.0f
        && VectorIsFinite(VectorAdd(VectorSet(rectangle.x, rectangle.y, 0.0f, 0.0f), VectorSet(rectangle.width, rectangle.height, 0.0f, 0.0f)), VectorComponentMask::s_XY)
    ;
}

SIMDVectorSingle LayoutTree::Inset(const SIMDVectorSingle rectangle, const SIMDVectorSingle padding)noexcept{
    const SIMDVectorSingle paddingStart = VectorSwizzle<0, 1, 0, 1>(padding);
    const SIMDVectorSingle rectangleSize = VectorSwizzle<2, 3, 2, 3>(rectangle);
    const SIMDVectorSingle firstInset = VectorSelect(rectangleSize, paddingStart, VectorLess(paddingStart, rectangleSize));
    const SIMDVectorDouble rectangleDouble = ConvertVectorSingleToDouble(rectangle);
    const SIMDVectorDouble paddingDouble = ConvertVectorSingleToDouble(padding);
    const SIMDVectorDouble rectangleExtent = VectorDoubleSelect(VectorDoubleZero(), VectorDoubleSwizzle<2, 3, 2, 3>(rectangleDouble), s_DoubleSIMDMaskXY);
    const SIMDVectorDouble paddingFirst = VectorDoubleSelect(VectorDoubleZero(), paddingDouble, s_DoubleSIMDMaskXY);
    const SIMDVectorDouble paddingLast = VectorDoubleSelect(VectorDoubleZero(), VectorDoubleSwizzle<2, 3, 2, 3>(paddingDouble), s_DoubleSIMDMaskXY);
    const SIMDVectorDouble extent = VectorDoubleSubtract(VectorDoubleSubtract(rectangleExtent, paddingFirst), paddingLast);
    const SIMDVectorDouble size = VectorDoubleSelect(extent, VectorDoubleZero(), VectorDoubleGreater(VectorDoubleZero(), extent));
    const SIMDVectorSingle origin = VectorAdd(VectorSelect(VectorZero(), rectangle, s_SIMDMaskXY), VectorSelect(VectorZero(), firstInset, s_SIMDMaskXY));
    const SIMDVectorSingle sizeSingle = ConvertVectorDoubleToSingle(size);
    return VectorSelect(VectorSwizzle<0, 1, 0, 1>(sizeSingle), origin, s_SIMDMaskXY);
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

