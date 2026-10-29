// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <impl/ecs_csg/deform_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns finiteness, topology, and option checks so preview and commit share one classifier before any cutter, wall, or cap work runs.
class CsgDeformValidator final : NoCopy{
public:
    [[nodiscard]] static b8 FiniteFloat(const f32 value)noexcept;
    [[nodiscard]] static b8 FiniteVertexVec(const SIMDVectorSingle position, const SIMDVectorSingle normal, const SIMDVectorSingle tangent, const SIMDVectorSingle uv0, const SIMDVectorSingle color)noexcept;
    [[nodiscard]] static SIMDVectorSingle ShapeEpsilon(const SIMDVectorSingle distanceEpsilon)noexcept;
    [[nodiscard]] static b8 ValidOptions(const CsgDeformBuildOptions& options)noexcept;
    [[nodiscard]] static b8 ValidTopology(NotNull<const CsgDeformTriangle*> triangles, const usize triangleCount, const usize vertexCount)noexcept;
    [[nodiscard]] static Expected<void, CsgDeformViabilityReason::Enum> FiniteInput(NotNull<const CsgDeformVertex*> vertices, const usize vertexCount)noexcept;


public:
    CsgDeformValidator() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

