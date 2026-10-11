// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deform_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns finiteness, topology, and option checks so preview and commit share one classifier before any cutter, wall, or cap work runs.
class CsgDeformValidator final : NoCopy{
public:
    [[nodiscard]] static bool FiniteFloat(const f32 value)noexcept;
    [[nodiscard]] static bool FiniteVertexVec(const SIMDVectorSingle position, const SIMDVectorSingle normal, const SIMDVectorSingle tangent, const SIMDVectorSingle uv0, const SIMDVectorSingle color)noexcept;
    [[nodiscard]] static bool FiniteVertex(const CsgDeformVertex& vertex)noexcept;
    [[nodiscard]] static SIMDVectorSingle SaturateVec(const SIMDVectorSingle value)noexcept;
    [[nodiscard]] static SIMDVectorSingle AbsDivideVec(const SIMDVectorSingle numerator, const SIMDVectorSingle denominator)noexcept;
    [[nodiscard]] static f32 ShapeEpsilon(const CsgDeformBuildOptions& options)noexcept;
    [[nodiscard]] static bool ValidOptions(const CsgDeformBuildOptions& options)noexcept;
    [[nodiscard]] static bool ValidTopology(
        NotNull<const CsgDeformTriangle*> triangles,
        const usize triangleCount,
        const usize vertexCount
    )noexcept;
    [[nodiscard]] static Expected<void, CsgDeformViabilityReason::Enum> FiniteInput(
        NotNull<const CsgDeformVertex*> vertices,
        const usize vertexCount
    )noexcept;


public:
    CsgDeformValidator() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

