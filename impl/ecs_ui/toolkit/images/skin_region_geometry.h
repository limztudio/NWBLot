// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/global.h>

#include <impl/assets_ui_skin/asset.h>

#include <global/simd/convert.h>
#include <global/simd/result.h>
#include <global/simd/vector.h>
#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<SIMDVectorDoubleResult> LoadUiSkinSliceExtents(const UiSkinRegion& region)noexcept;
[[nodiscard]] Expected<SIMDVectorSingleResult> LoadUiSkinMinimumSize(const UiSkinRegion& region, SIMDVectorDouble sliceExtents, SIMDVectorDouble density)noexcept;
[[nodiscard]] Expected<SIMDVectorSingleResult> LoadUiSkinMinimumSize(const UiSkinRegion& region, SIMDVectorDouble density)noexcept;

[[nodiscard]] NWB_INLINE f32 ResolveUiSkinSquareExtent(const f32 minimumExtent, const UiSkinRegion& region)noexcept{
    return Max(minimumExtent, Max(region.minimumWidth, region.minimumHeight));
}

[[nodiscard]] f32 ResolveUiSkinSquareExtent(f32 minimumExtent, const UiSkinRegion& region, f32 density)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

