// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "glyph_shader_host.h"

#include <global/simd/vector.h>
#include <tests/unit/ui/glyph_shader_case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::UiShaderHost{


using float4 = ::__hidden_ui_glyph_sdf_tests::ShaderFloat4;
using uint = u32;

[[nodiscard]] inline float4 operator*(const float4 value, const f32 scale)noexcept{
    const SIMDVectorSingle scaled = VectorMultiply(VectorSet(value.x, value.y, value.z, value.w), VectorReplicate(scale));
    return { VectorGetX(scaled), VectorGetY(scaled), VectorGetZ(scaled), VectorGetW(scaled) };
}
[[nodiscard]] inline f32 saturate(const f32 value)noexcept{
    const SIMDVectorSingle valueVec = VectorReplicate(value);
    const SIMDVectorSingle clampedLow = VectorSelect(valueVec, VectorZero(), VectorLessOrEqual(valueVec, VectorZero()));
    const SIMDVectorSingle one = VectorSplatOne();
    return VectorGetX(VectorSelect(clampedLow, one, VectorGreater(clampedLow, one)));
}
[[nodiscard]] inline f32 max(const f32 first, const f32 second)noexcept{
    const SIMDVectorSingle firstVec = VectorReplicate(first);
    const SIMDVectorSingle secondVec = VectorReplicate(second);
    return VectorGetX(VectorSelect(secondVec, firstVec, VectorGreater(firstVec, secondVec)));
}

#include <impl/assets/graphics/ui/glyph_sdf.slangi>
#include <impl/assets/graphics/ui/glyph_coverage.slangi>


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

