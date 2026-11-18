// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once
#include <tests/unit/ecs_graphics/surfel_shader_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::SurfelShaderHost{
using EcsGraphicsCases::SurfelCoverageFloat3;
using EcsGraphicsCases::SurfelCoverageFloat4;
using EcsGraphicsCases::SurfelHashInt3;
[[nodiscard]] f32 nwbSurfelGatherCoverage(f32 totalWeight);
[[nodiscard]] b8 nwbSurfelUpsampleNormalCompatible(f32 normalDot);
[[nodiscard]] f32 nwbSurfelUpsampleGeometryWeight(f32 bilinearWeight, f32 distanceSquared, f32 inverseWeight);
[[nodiscard]] f32 nwbSurfelUpsampleCoveredWeight(f32 weight, f32 coverage);
[[nodiscard]] SurfelCoverageFloat4 nwbSurfelUpsampleFinalize(SurfelCoverageFloat3 color, f32 coveredWeight, f32 compatibleWeight);
[[nodiscard]] SurfelHashInt3 nwbSurfelCellCoord(Float3U position, f32 cellSize);
[[nodiscard]] b8 nwbSurfelSameCell(SurfelHashInt3 owner, SurfelHashInt3 visited);
[[nodiscard]] u32 nwbSurfelHashCell(SurfelHashInt3 cell, u32 count);
[[nodiscard]] b8 nwbSurfelSpawnCoversCandidate(b8 sameCell, f32 alignment, f32 distanceSquared, f32 planeDistance, f32 radius, f32 normalBias);

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

