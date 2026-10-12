// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "tree.h"

#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LayoutTree::measure()noexcept{
    for(usize reverse = m_nodes.size(); reverse > 0u; --reverse){
        const u32 index = static_cast<u32>(reverse - 1u);
        const Node& node = m_nodes[index];
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
            const f64 spacing = static_cast<f64>(description.gap) * static_cast<f64>(childCount - 1u);
            if(description.direction == LayoutDirection::Row){
                const SIMDVectorDouble horizontal = VectorDoubleSelect(VectorDoubleZero(), extent, s_DoubleSIMDMaskX);
                const SIMDVectorDouble accumulated = VectorDoubleAdd(horizontal, VectorDoubleSet(spacing, 0.0, 0.0, 0.0));
                extent = VectorDoubleSelect(extent, accumulated, s_DoubleSIMDMaskX);
            }
            else if(description.direction == LayoutDirection::Column){
                const SIMDVectorDouble vertical = VectorDoubleSelect(VectorDoubleZero(), extent, s_DoubleSIMDMaskY);
                const SIMDVectorDouble accumulated = VectorDoubleAdd(vertical, VectorDoubleSet(0.0, spacing, 0.0, 0.0));
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

