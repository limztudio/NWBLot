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
    [[nodiscard]] static bool finiteFloat(const f32 value);
    [[nodiscard]] static bool finiteVertex(const CsgDeformVertex& vertex);
    [[nodiscard]] static f32 saturateFloat(const f32 value);
    [[nodiscard]] static SIMDVector saturateVec(const SIMDVector value);
    [[nodiscard]] static SIMDVector absDivideVec(const SIMDVector numerator, const SIMDVector denominator);
    [[nodiscard]] static f32 shapeEpsilon(const CsgDeformBuildOptions& options);
    [[nodiscard]] static bool validOptions(const CsgDeformBuildOptions& options);
    [[nodiscard]] static bool validTopology(
        NotNull<const CsgDeformTriangle*> triangles,
        const usize triangleCount,
        const usize vertexCount
    );
    [[nodiscard]] static bool finiteInput(
        NotNull<const CsgDeformVertex*> vertices,
        const usize vertexCount,
        CsgDeformViabilityReason::Enum& outReason
    );


public:
    CsgDeformValidator() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

