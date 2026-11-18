// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "hdr10_shader_host.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::HdrShaderHost{

[[nodiscard]] inline float3 operator+(float3 a, float3 b)noexcept{ return HdrAdd(a, b); }
[[nodiscard]] inline float3 operator*(float3 a, float3 b)noexcept{ return HdrMultiply(a, b); }
[[nodiscard]] inline float3 operator/(float3 a, float3 b)noexcept{ return HdrDivide(a, b); }
[[nodiscard]] inline double dot(float3 a, float3 b)noexcept{ return HdrDot(a, b); }
[[nodiscard]] inline float3 max(float3 a, float3 b)noexcept{ return HdrMax(a, b); }
[[nodiscard]] inline float saturate(float a)noexcept{ return EcsGraphicsCases::HdrSaturateScalar(a); }
[[nodiscard]] inline float3 saturate(float3 a)noexcept{ return HdrSaturate(a); }
[[nodiscard]] inline float3 pow(float3 a, double exponent)noexcept{ return HdrPow(a, exponent); }

#include <impl/assets/graphics/common/hdr10.slangi>

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

