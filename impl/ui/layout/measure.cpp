// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "tree.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LayoutTree::measure(){
    for(usize reverse = m_nodes.size(); reverse > 0u; --reverse){
        const u32 index = static_cast<u32>(reverse - 1u);
        const Node& node = m_nodes[index];
        const LayoutNodeDesc& description = node.description;
        f64 width = 0.0;
        f64 height = 0.0;
        u32 childCount = 0u;
        for(u32 child = node.firstChild; child != s_LayoutNoParent; child = m_nodes[child].nextSibling){
            const Point measured = m_work[child].box.measuredSize;
            if(description.direction == LayoutDirection::Row){
                width += measured.x;
                height = Max(height, static_cast<f64>(measured.y));
            }
            else if(description.direction == LayoutDirection::Column){
                width = Max(width, static_cast<f64>(measured.x));
                height += measured.y;
            }
            else{
                width = Max(width, static_cast<f64>(measured.x));
                height = Max(height, static_cast<f64>(measured.y));
            }
            ++childCount;
        }
        if(childCount > 1u){
            const f64 spacing = static_cast<f64>(description.gap) * (childCount - 1u);
            if(description.direction == LayoutDirection::Row)
                width += spacing;
            else if(description.direction == LayoutDirection::Column)
                height += spacing;
        }
        width = Max(width, static_cast<f64>(description.intrinsicSize.x)) + description.padding.left + description.padding.right;
        height = Max(height, static_cast<f64>(description.intrinsicSize.y)) + description.padding.top + description.padding.bottom;
        if(width > Limit<f32>::s_Max || height > Limit<f32>::s_Max)
            return false;
        const f32 resolvedWidth = description.width.policy == LayoutSizePolicy::Fixed ? description.width.value : static_cast<f32>(width);
        const f32 resolvedHeight = description.height.policy == LayoutSizePolicy::Fixed ? description.height.value : static_cast<f32>(height);
        m_work[index].box.measuredSize = { resolvedWidth, resolvedHeight };
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

