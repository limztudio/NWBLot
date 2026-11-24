// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "surfel_shader_host.h"
#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::SurfelShaderHost{

using float3 = SurfelCoverageFloat3;
using float4 = SurfelCoverageFloat4;
[[nodiscard]] inline float3 operator/(float3 value, f32 divisor)noexcept{ return SurfelCoverageDivide(value, divisor); }
[[nodiscard]] inline f32 exp(f32 value)noexcept{ return VectorGetX(VectorExp(VectorReplicate(value))); }
#include <impl/assets/graphics/gi/surfel/surfel_coverage.slangi>

using uint = u32;
using int3 = SurfelHashInt3;
[[nodiscard]] inline Float3U operator/(Float3U value, f32 divisor)noexcept{ return EcsGraphicsCases::SurfelPositionDivide(value, divisor); }
[[nodiscard]] inline b8 operator==(int3 a, int3 b)noexcept{ return SurfelEqualCells(a, b); }
[[nodiscard]] inline f32 MaxCellSize(const f32 first, const f32 second)noexcept{
    const SIMDVectorSingle firstVec = VectorReplicate(first);
    const SIMDVectorSingle secondVec = VectorReplicate(second);
    return VectorGetX(VectorSelect(secondVec, firstVec, VectorGreater(firstVec, secondVec)));
}
#define float3 Float3U
#define floor EcsGraphicsCases::SurfelFloorPosition
#define max MaxCellSize
#define all(value) (value)
#include <impl/assets/graphics/gi/surfel/surfel_hash.slangi>
#undef all
#undef max
#undef floor
#undef float3

#include <impl/assets/graphics/gi/surfel/surfel_spawn_policy.slangi>

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

