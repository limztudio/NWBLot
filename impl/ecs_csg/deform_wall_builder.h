// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deform_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns zero-crossing splits with the shared edge cache (never welds source verts) and interpolated attributes, emitting the kept split triangles.
class CsgDeformWallBuilder final : NoCopy{
public:
    // SIMD-domain cores: inputs and outputs stay on vector lanes, never touch storage.
    [[nodiscard]] static SIMDVector mixAttributeVec(SIMDVector firstVec, SIMDVector secondVec, SIMDVector blendVec, SIMDVector otherVec);
    [[nodiscard]] static SIMDVector normalizeDirectionVec(SIMDVector direction);
    [[nodiscard]] static SIMDVector keepWVec(SIMDVector normalizedVec, SIMDVector sourceVec);
    [[nodiscard]] static SIMDVector tangentHandednessVec(SIMDVector normalizedTangent, SIMDVector tangentVec);
    [[nodiscard]] static SIMDVector upAxisVec();
    // Storage conversion stays at these boundaries; the cores use SIMD lanes.
    [[nodiscard]] static CsgDeformVertex mixVertices(const CsgDeformVertex& first, const CsgDeformVertex& second, const f32 firstWeight);
    [[nodiscard]] static bool normalizeDeformVertex(CsgDeformVertex& vertex);
    [[nodiscard]] static bool splitEdgeVertex(
        CsgDeformVertexVector<Core::Alloc::ScratchArena>& vertices,
        CsgDeformEdgeSplitMap& edgeSplits,
        const u32 first,
        const u32 second,
        const f32 firstDistance,
        const f32 secondDistance,
        u32& outVertex
    );
    static void emitTriangle(
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& triangles,
        const u32 first,
        const u32 second,
        const u32 third
    );
    // Keep side is distance >= 0; caller snaps |distance| <= epsilon to zero first.
    [[nodiscard]] static bool clipShell(
        Core::Alloc::ScratchArena& scratchArena,
        const CsgDeformShape& shape,
        const f32 epsilon,
        CsgDeformVertexVector<Core::Alloc::ScratchArena>& inOutVertices,
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& inOutTriangles,
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& scratchKept,
        Vector<f32, Core::Alloc::ScratchArena>& scratchDistances,
        CsgDeformViabilityReason::Enum& outReason
    );


public:
    CsgDeformWallBuilder() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

