// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "glyph_shader_host.h"

#include <global/simplemath.h>
#include <tests/unit/ui/glyph_shader_case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::UiShaderHost{


using float4 = ::__hidden_ui_glyph_sdf_tests::ShaderFloat4;
using uint = u32;

[[nodiscard]] inline float4 operator*(const float4 value, const f32 scale)noexcept{
    return { value.x * scale, value.y * scale, value.z * scale, value.w * scale };
}
[[nodiscard]] inline f32 saturate(const f32 value)noexcept{ return value > 0.0f ? Min(1.0f, value) : 0.0f; }
[[nodiscard]] inline f32 max(const f32 first, const f32 second)noexcept{ return Max(first, second); }

#include <impl/assets/graphics/ui/glyph_sdf.slangi>
#include <impl/assets/graphics/ui/glyph_coverage.slangi>


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

