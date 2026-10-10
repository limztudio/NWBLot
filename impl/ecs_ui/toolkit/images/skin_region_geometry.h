// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <impl/assets_ui_skin/asset.h>

#include <global/math/vector_double.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE Expected<SIMDVectorDouble> LoadUiSkinSliceExtents(const UiSkinRegion& region)noexcept{
    if(region.rectangle.width == 0u || region.rectangle.height == 0u)
        return MakeUnexpected(Failure{});
    const u64 horizontal = static_cast<u64>(region.sliceInsets.left) + region.sliceInsets.right;
    const u64 vertical = static_cast<u64>(region.sliceInsets.top) + region.sliceInsets.bottom;
    if(region.drawMode == UiSkinDrawMode::Sprite){
        if(horizontal != 0u || vertical != 0u)
            return MakeUnexpected(Failure{});
    }
    else if(region.drawMode == UiSkinDrawMode::NineSlice){
        if(horizontal > region.rectangle.width || vertical > region.rectangle.height)
            return MakeUnexpected(Failure{});
    }
    else
        return MakeUnexpected(Failure{});
    return SIMDVectorDouble{ static_cast<f64>(horizontal), static_cast<f64>(vertical) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

