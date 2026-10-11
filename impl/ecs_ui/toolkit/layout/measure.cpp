// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/numeric_simd.h>

#include "tree.h"

#include <global/math/vector_double.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LayoutTree::measure()noexcept{
    for(usize reverse = m_nodes.size(); reverse > 0u; --reverse){
        const u32 index = static_cast<u32>(reverse - 1u);
        const Node& node = m_nodes[index];
        const LayoutNodeDesc& description = node.description;
        SIMDVectorDouble extent{};
        u32 childCount = 0u;
        for(u32 child = node.firstChild; child != s_LayoutNoParent; child = m_nodes[child].nextSibling){
            const Point measured = m_work[child].box.measuredSize;
            const SIMDVectorDouble candidate{ measured.x, measured.y };
            if(description.direction == LayoutDirection::Row)
                extent = SIMDVectorDouble{ NumericSimd::Add(extent.x, candidate.x), NumericSimd::Max(extent.y, candidate.y) };
            else if(description.direction == LayoutDirection::Column)
                extent = SIMDVectorDouble{ NumericSimd::Max(extent.x, candidate.x), NumericSimd::Add(extent.y, candidate.y) };
            else
                extent = (extent > candidate) ? extent : candidate;
            ++childCount;
        }
        if(childCount > 1u){
            const f64 spacing = NumericSimd::Multiply(static_cast<f64>(description.gap), static_cast<f64>(childCount - 1u));
            if(description.direction == LayoutDirection::Row)
                extent.x = NumericSimd::Add(extent.x, spacing);
            else if(description.direction == LayoutDirection::Column)
                extent.y = NumericSimd::Add(extent.y, spacing);
        }
        const SIMDVectorDouble measured = extent;
        const SIMDVectorDouble intrinsic = { description.intrinsicSize.x, description.intrinsicSize.y };
        const SIMDVectorDouble maximum = (measured > intrinsic) ? measured : intrinsic;
        const SIMDVectorDouble padded = maximum + SIMDVectorDouble{ description.padding.left, description.padding.top }
            + SIMDVectorDouble{ description.padding.right, description.padding.bottom };
        const f64 width = padded.x;
        const f64 height = padded.y;
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

