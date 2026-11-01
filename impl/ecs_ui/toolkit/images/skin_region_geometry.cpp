// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skin_region_geometry.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SIMDVectorDoubleResult> LoadUiSkinSliceExtents(const UiSkinRegion& region)noexcept{
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
    return SIMDVectorDoubleResult{ VectorDoubleSet(static_cast<f64>(horizontal), static_cast<f64>(vertical), 0.0, 0.0) };
}

Expected<SIMDVectorSingleResult> LoadUiSkinMinimumSize(const UiSkinRegion& region, const SIMDVectorDouble sliceExtents, const SIMDVectorDouble density)noexcept{
    if(!IsFinite(region.minimumWidth) || region.minimumWidth < 0.0f || !IsFinite(region.minimumHeight) || region.minimumHeight < 0.0f)
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble minimumSize = VectorDoubleSet(static_cast<f64>(region.minimumWidth), static_cast<f64>(region.minimumHeight), 0.0, 0.0);
    const SIMDVectorDouble scaledSlices = VectorDoubleSelect(VectorDoubleZero(), VectorDoubleDivide(sliceExtents, density), s_DoubleSIMDMaskXY);
    const SIMDVectorDouble widthHeightValue = VectorDoubleSelect(scaledSlices, minimumSize, VectorDoubleGreater(minimumSize, scaledSlices));
    const f64 width = VectorDoubleGetX(widthHeightValue);
    const f64 height = VectorDoubleGetY(widthHeightValue);
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    return SIMDVectorSingleResult{ ConvertVectorDoubleToSingle(widthHeightValue) };
}

Expected<SIMDVectorSingleResult> LoadUiSkinMinimumSize(const UiSkinRegion& region, const SIMDVectorDouble density)noexcept{
    const auto sliceExtents = LoadUiSkinSliceExtents(region);
    if(!sliceExtents)
        return MakeUnexpected(sliceExtents.error());
    return LoadUiSkinMinimumSize(region, sliceExtents->value, density);
}

f32 ResolveUiSkinSquareExtent(const f32 minimumExtent, const UiSkinRegion& region, const f32 density)noexcept{
    const f32 sourceExtent = static_cast<f32>(Max(region.rectangle.width, region.rectangle.height)) / density;
    return Max(ResolveUiSkinSquareExtent(minimumExtent, region), sourceExtent);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

