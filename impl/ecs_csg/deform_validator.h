// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deform_types.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Input validation for deformable CSG rebuilds.
//
// Owns finiteness, topology, and option checks so preview and commit share
// one classifier before any cutter, wall, or cap work runs.
class CsgDeformValidator final : NoCopy{
public:
    CsgDeformValidator() = delete;


public:
    [[nodiscard]] static bool FiniteFloat(const f32 value);
    [[nodiscard]] static bool FiniteVertex(const CsgDeformVertex& vertex);
    [[nodiscard]] static f32 SaturateFloat(const f32 value);
    [[nodiscard]] static f32 ShapeEpsilon(const CsgDeformBuildOptions& options);
    [[nodiscard]] static bool ValidOptions(const CsgDeformBuildOptions& options);
    [[nodiscard]] static bool ValidTopology(
        NotNull<const CsgDeformVertex*> vertices,
        const usize vertexCount,
        NotNull<const CsgDeformTriangle*> triangles,
        const usize triangleCount
    );
    [[nodiscard]] static bool FiniteInput(
        NotNull<const CsgDeformVertex*> vertices,
        const usize vertexCount,
        CsgDeformViabilityReason::Enum& outReason
    );
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

