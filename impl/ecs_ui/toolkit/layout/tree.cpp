// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "tree.h"
#include <impl/ecs_ui/toolkit/rect_math.h>

#include <global/simd/convert.h>
#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


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
        m_buildFailed || m_nodes.size() >= m_maxNodes || !IsValidLayoutNodeDesc(description)
        || (m_nodes.empty() ? parent != s_LayoutNoParent : parent >= m_nodes.size())
        || (parent != s_LayoutNoParent && m_nodes[parent].description.direction == LayoutDirection::Leaf)
    ){
        m_buildFailed = true;
        return MakeUnexpected(Failure{});
    }
    const u32 index = static_cast<u32>(m_nodes.size());
    m_nodes.push_back(LayoutTreeNode(description));
    if(parent != s_LayoutNoParent){
        LayoutTreeNode& parentNode = m_nodes[parent];
        if(parentNode.lastChild == s_LayoutNoParent)
            parentNode.firstChild = index;
        else
            m_nodes[parentNode.lastChild].nextSibling = index;
        parentNode.lastChild = index;
    }
    return index;
}

b8 LayoutTree::arrange(const Rect& viewport){
    if(m_buildFailed || m_nodes.empty() || !IsValidUiRect(viewport))
        return false;
    m_work.assign(m_nodes.size(), {});
    if(!measure())
        return false;
    const LayoutNodeDesc& root = m_nodes[0u].description;
    const Point measured = m_work[0u].box.measuredSize;
    const f32 width = root.width.policy == LayoutSizePolicy::Stretch ? viewport.width : measured.x;
    const f32 height = root.height.policy == LayoutSizePolicy::Stretch ? viewport.height : measured.y;
    m_work[0u].box.rectangle = { viewport.x, viewport.y, width, height };
    m_work[0u].inheritedClip = viewport;
    for(u32 index = 0u; index < m_nodes.size(); ++index){
        if(!arrangeNode(index))
            return false;
    }
    m_stagedBoxes.clear();
    for(const LayoutTreeWork& work : m_work)
        m_stagedBoxes.push_back(work.box);
    m_boxes.swap(m_stagedBoxes);
    return true;
}

b8 LayoutTree::measure()noexcept{
    for(usize reverse = m_nodes.size(); reverse > 0u; --reverse){
        const u32 index = static_cast<u32>(reverse - 1u);
        const LayoutTreeNode& node = m_nodes[index];
        const LayoutNodeDesc& description = node.description;
        SIMDVectorDouble extent = VectorDoubleZero();
        u32 childCount = 0u;
        for(u32 child = node.firstChild; child != s_LayoutNoParent; child = m_nodes[child].nextSibling){
            const Point measured = m_work[child].box.measuredSize;
            const SIMDVectorDouble candidate = VectorDoubleSet(measured.x, measured.y, 0.0, 0.0);
            const SIMDVectorDouble maximum = VectorDoubleSelect(candidate, extent, VectorDoubleGreater(extent, candidate));
            if(description.direction == LayoutDirection::Row){
                const SIMDVectorDouble accumulated = VectorDoubleAdd(VectorDoubleSelect(VectorDoubleZero(), extent, s_DoubleSIMDMaskX), VectorDoubleSelect(VectorDoubleZero(), candidate, s_DoubleSIMDMaskX));
                extent = VectorDoubleSelect(maximum, accumulated, s_DoubleSIMDMaskX);
            }
            else if(description.direction == LayoutDirection::Column){
                const SIMDVectorDouble accumulated = VectorDoubleAdd(VectorDoubleSelect(VectorDoubleZero(), extent, s_DoubleSIMDMaskY), VectorDoubleSelect(VectorDoubleZero(), candidate, s_DoubleSIMDMaskY));
                extent = VectorDoubleSelect(maximum, accumulated, s_DoubleSIMDMaskY);
            }
            else
                extent = maximum;
            ++childCount;
        }
        if(childCount > 1u){
            const SIMDVectorDouble spacing = VectorDoubleScale(VectorDoubleReplicate(static_cast<f64>(description.gap)), static_cast<f64>(childCount - 1u));
            if(description.direction == LayoutDirection::Row){
                const SIMDVectorDouble horizontal = VectorDoubleSelect(VectorDoubleZero(), extent, s_DoubleSIMDMaskX);
                const SIMDVectorDouble accumulated = VectorDoubleAdd(horizontal, spacing);
                extent = VectorDoubleSelect(extent, accumulated, s_DoubleSIMDMaskX);
            }
            else if(description.direction == LayoutDirection::Column){
                const SIMDVectorDouble vertical = VectorDoubleSelect(VectorDoubleZero(), extent, s_DoubleSIMDMaskY);
                const SIMDVectorDouble accumulated = VectorDoubleAdd(vertical, spacing);
                extent = VectorDoubleSelect(extent, accumulated, s_DoubleSIMDMaskY);
            }
        }
        const SIMDVectorDouble intrinsic = VectorDoubleSet(description.intrinsicSize.x, description.intrinsicSize.y, 0.0, 0.0);
        const SIMDVectorDouble maximum = VectorDoubleSelect(intrinsic, extent, VectorDoubleGreater(extent, intrinsic));
        const SIMDVectorDouble padded = VectorDoubleAdd(VectorDoubleAdd(maximum, VectorDoubleSet(description.padding.left, description.padding.top, 0.0, 0.0)), VectorDoubleSet(description.padding.right, description.padding.bottom, 0.0, 0.0));
        const f64 width = VectorDoubleGetX(padded);
        const f64 height = VectorDoubleGetY(padded);
        if(width > Limit<f32>::s_Max || height > Limit<f32>::s_Max)
            return false;
        const f32 resolvedWidth = description.width.policy == LayoutSizePolicy::Fixed ? description.width.value : static_cast<f32>(width);
        const f32 resolvedHeight = description.height.policy == LayoutSizePolicy::Fixed ? description.height.value : static_cast<f32>(height);
        m_work[index].box.measuredSize = { resolvedWidth, resolvedHeight };
    }
    return true;
}

b8 LayoutTree::arrangeNode(const u32 index)noexcept{
    const LayoutTreeNode& node = m_nodes[index];
    const LayoutNodeDesc& description = node.description;
    LayoutTreeWork& work = m_work[index];
    LayoutBox& box = work.box;
    if(!IsValidUiRect(box.rectangle))
        return false;
    const SIMDVectorSingle rectangle = LoadRect(box.rectangle);
    const SIMDVectorSingle inheritedClip = LoadRect(work.inheritedClip);
    const SIMDVectorSingle content = ConvertVectorDoubleToSingle(LoadDouble(ComputeInsetUiRectBounds(box.rectangle, description.padding)));
    const SIMDVectorSingle clip = IntersectRectValue(rectangle, inheritedClip);
    box.content = StoreRect(content);
    box.clip = StoreRect(clip);
    box.hit = box.clip;
    if(!IsValidUiRect(box.content) || !IsValidUiRect(box.clip))
        return false;
    const SIMDVectorSingle childClipValue = description.clipChildren ? IntersectRectValue(inheritedClip, content) : inheritedClip;
    const Rect childClip = StoreRect(childClipValue);
    const b8 row = description.direction == LayoutDirection::Row;
    const b8 column = description.direction == LayoutDirection::Column;
    f64 consumed = 0.0;
    f64 weight = 0.0;
    u32 childCount = 0u;
    for(u32 child = node.firstChild; child != s_LayoutNoParent; child = m_nodes[child].nextSibling){
        const LayoutNodeDesc& childDescription = m_nodes[child].description;
        if(row || column){
            const LayoutSize& main = row ? childDescription.width : childDescription.height;
            if(main.policy == LayoutSizePolicy::Stretch)
                weight += static_cast<f64>(main.value);
            else
                consumed += static_cast<f64>(row ? m_work[child].box.measuredSize.x : m_work[child].box.measuredSize.y);
        }
        ++childCount;
    }
    const f64 gap = static_cast<f64>(description.gap);
    const f64 spacing = childCount > 1u ? gap * static_cast<f64>(childCount - 1u) : 0.0;
    const f64 mainSize = static_cast<f64>(row ? box.content.width : box.content.height);
    const f64 available = Max(0.0, (mainSize - consumed) - spacing);
    f64 cursor = static_cast<f64>(row ? box.content.x : box.content.y);
    for(u32 child = node.firstChild; child != s_LayoutNoParent; child = m_nodes[child].nextSibling){
        const LayoutNodeDesc& childDescription = m_nodes[child].description;
        const Point measured = m_work[child].box.measuredSize;
        f32 width = measured.x;
        f32 height = measured.y;
        if(childDescription.width.policy == LayoutSizePolicy::Stretch)
            width = row ? static_cast<f32>(available * (static_cast<f64>(childDescription.width.value) / weight)) : box.content.width;
        if(childDescription.height.policy == LayoutSizePolicy::Stretch)
            height = column ? static_cast<f32>(available * (static_cast<f64>(childDescription.height.value) / weight)) : box.content.height;
        const f32 x = row ? static_cast<f32>(cursor) : box.content.x;
        const f32 y = column ? static_cast<f32>(cursor) : box.content.y;
        const Rect rectangle{ x, y, width, height };
        if(!IsValidUiRect(rectangle))
            return false;
        m_work[child].box.rectangle = rectangle;
        m_work[child].inheritedClip = childClip;
        if(row)
            cursor += static_cast<f64>(width) + gap;
        else if(column)
            cursor += static_cast<f64>(height) + gap;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

