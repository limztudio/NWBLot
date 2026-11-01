// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skin_region_geometry.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SIMDVectorDoubleResult> LoadUiSkinSliceExtents(const UiSkinRegion& region)noexcept{
    Double4 sliceExtents{};
    if(!TryLoadUiSkinSliceExtents(region, sliceExtents))
        return MakeUnexpected(Failure{});
    return SIMDVectorDoubleResult{ LoadDouble(sliceExtents) };
}

Expected<SIMDVectorSingleResult> LoadUiSkinMinimumSize(const UiSkinRegion& region, const SIMDVectorDouble sliceExtents, const SIMDVectorDouble density)noexcept{
    Double4 sliceExtentsData{};
    Double4 densityData{};
    Float4 minimumSize{};
    StoreDouble(sliceExtents, sliceExtentsData);
    StoreDouble(density, densityData);
    if(!TryLoadUiSkinMinimumSize(region, sliceExtentsData, densityData, minimumSize))
        return MakeUnexpected(Failure{});
    return SIMDVectorSingleResult{ LoadFloat(minimumSize) };
}

Expected<SIMDVectorSingleResult> LoadUiSkinMinimumSize(const UiSkinRegion& region, const SIMDVectorDouble density)noexcept{
    Double4 densityData{};
    Float4 minimumSize{};
    StoreDouble(density, densityData);
    if(!TryLoadUiSkinMinimumSize(region, densityData, minimumSize))
        return MakeUnexpected(Failure{});
    return SIMDVectorSingleResult{ LoadFloat(minimumSize) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

