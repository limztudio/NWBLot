// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <global/name.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsAvboit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_OccupancyPixelShaderName("engine/graphics/avboit/occupancy_ps");
inline constexpr Name s_DepthWarpComputeShaderName("engine/graphics/avboit/depth_warp_cs");
inline constexpr Name s_ExtinctionPixelShaderName("engine/graphics/avboit/extinction_ps");
inline constexpr Name s_IntegrateComputeShaderName("engine/graphics/avboit/integrate_cs");
// Fallback fixed accumulate PS, used by a transparent material that declares explicit `shaders` instead of a
// `surface` (the per-material accumulate PS generation covers `surface`-authored transparent materials).
inline constexpr Name s_AccumulatePixelShaderName("engine/graphics/avboit/accumulate_ps");
// Shared identity prefix for the cook-generated per-material AVBOIT accumulate PS. The cook generates one such
// PS per `surface`-authored transparent material (cook.cpp EmitMaterialAvboitAccumulatePixelShaders) under the
// name "<prefix><material virtual path>"; the renderer derives the SAME name from the material to bind it for
// the transparent draw (material_pipeline.cpp, AvboitAccumulate pass). Keep both sites in sync via this prefix.
inline constexpr AStringView s_AccumulatePixelShaderGeneratedPrefix("generated/avboit_accumulate_ps/");
// The occupancy/extinction twins of s_AccumulatePixelShaderGeneratedPrefix. The cook generates one per
// `surface`-authored transparent material (EmitMaterialAvboitOccupancyPixelShaders /
// EmitMaterialAvboitExtinctionPixelShaders), so all three AVBOIT passes read the SAME shader-decided
// surface.alpha; the renderer derives these names to bind them for the transparent draw's occupancy/extinction
// passes (material_pipeline.cpp, AvboitOccupancy / AvboitExtinction). Keep each in sync via its prefix.
inline constexpr AStringView s_OccupancyPixelShaderGeneratedPrefix("generated/avboit_occupancy_ps/");
inline constexpr AStringView s_ExtinctionPixelShaderGeneratedPrefix("generated/avboit_extinction_ps/");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

