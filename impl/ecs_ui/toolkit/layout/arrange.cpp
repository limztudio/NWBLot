// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/numeric_simd.h>

#include "tree.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LayoutTree::arrange(const Rect& viewport){
    if(m_buildFailed || m_nodes.empty() || !IsValidRectangle(viewport))
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
    for(const Work& work : m_work)
        m_stagedBoxes.push_back(work.box);
    m_boxes.swap(m_stagedBoxes);
    return true;
}

bool LayoutTree::arrangeNode(const u32 index)noexcept{
    const Node& node = m_nodes[index];
    const LayoutNodeDesc& description = node.description;
    Work& work = m_work[index];
    LayoutBox& box = work.box;
    if(!IsValidRectangle(box.rectangle))
        return false;
    box.content = Inset(box.rectangle, description.padding);
    box.clip = Intersect(box.rectangle, work.inheritedClip);
    box.hit = box.clip;
    if(!IsValidRectangle(box.content) || !IsValidRectangle(box.clip))
        return false;
    const Rect childClip = description.clipChildren ? Intersect(work.inheritedClip, box.content) : work.inheritedClip;
    const bool row = description.direction == LayoutDirection::Row;
    const bool column = description.direction == LayoutDirection::Column;
    SIMDVectorDouble consumedAndWeight{};
    u32 childCount = 0u;
    for(u32 child = node.firstChild; child != s_LayoutNoParent; child = m_nodes[child].nextSibling){
        const LayoutNodeDesc& childDescription = m_nodes[child].description;
        if(row || column){
            const LayoutSize& main = row ? childDescription.width : childDescription.height;
            if(main.policy == LayoutSizePolicy::Stretch)
                consumedAndWeight.y = NumericSimd::Add(consumedAndWeight.y, static_cast<f64>(main.value));
            else
                consumedAndWeight.x = NumericSimd::Add(consumedAndWeight.x, static_cast<f64>(row ? m_work[child].box.measuredSize.x : m_work[child].box.measuredSize.y));
        }
        ++childCount;
    }
    const f64 spacing = childCount > 1u ? NumericSimd::Multiply(static_cast<f64>(description.gap), static_cast<f64>(childCount - 1u)) : 0.0;
    const f64 mainSize = row ? box.content.width : box.content.height;
    const f64 available = NumericSimd::Max(0.0, NumericSimd::Subtract(NumericSimd::Subtract(mainSize, consumedAndWeight.x), spacing));
    f64 cursor = row ? box.content.x : box.content.y;
    for(u32 child = node.firstChild; child != s_LayoutNoParent; child = m_nodes[child].nextSibling){
        const LayoutNodeDesc& childDescription = m_nodes[child].description;
        const Point measured = m_work[child].box.measuredSize;
        f32 width = measured.x;
        f32 height = measured.y;
        if(childDescription.width.policy == LayoutSizePolicy::Stretch){
            width = row ? static_cast<f32>(NumericSimd::Multiply(available, NumericSimd::Divide(static_cast<f64>(childDescription.width.value), consumedAndWeight.y))) : box.content.width;
        }
        if(childDescription.height.policy == LayoutSizePolicy::Stretch){
            height = column ? static_cast<f32>(NumericSimd::Multiply(available, NumericSimd::Divide(static_cast<f64>(childDescription.height.value), consumedAndWeight.y))) : box.content.height;
        }
        const f32 x = row ? static_cast<f32>(cursor) : box.content.x;
        const f32 y = column ? static_cast<f32>(cursor) : box.content.y;
        const Rect rectangle{ x, y, width, height };
        if(!IsValidRectangle(rectangle))
            return false;
        m_work[child].box.rectangle = rectangle;
        m_work[child].inheritedClip = childClip;
        if(row)
            cursor = NumericSimd::Add(cursor, NumericSimd::Add(static_cast<f64>(width), static_cast<f64>(description.gap)));
        else if(column)
            cursor = NumericSimd::Add(cursor, NumericSimd::Add(static_cast<f64>(height), static_cast<f64>(description.gap)));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

