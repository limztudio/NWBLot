// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deform_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Seam-safe, attribute-aware wall handling for deformable CSG rebuilds.


// Owns zero-crossing splits with the shared edge cache (never welds source verts) and interpolated attributes, emitting the kept split triangles.
class CsgDeformWallBuilder final : NoCopy{
public:
    // SIMD-domain cores: inputs and outputs stay on vector lanes, never touch storage.
    [[nodiscard]] static SIMDVector MixAttributeVec(SIMDVector firstVec, SIMDVector secondVec, SIMDVector blendVec, SIMDVector otherVec);
    [[nodiscard]] static SIMDVector NormalizeDirectionVec(SIMDVector direction);
    [[nodiscard]] static SIMDVector KeepWVec(SIMDVector normalizedVec, SIMDVector sourceVec);
    [[nodiscard]] static SIMDVector TangentHandednessVec(SIMDVector normalizedTangent, SIMDVector tangentVec);
    // Beginner boundaries: the only places that Load/Store deform storage; math stays on the cores above.
    [[nodiscard]] static CsgDeformVertex MixVertices(const CsgDeformVertex& first, const CsgDeformVertex& second, const f32 firstWeight);
    [[nodiscard]] static bool NormalizeDeformVertex(CsgDeformVertex& vertex);
    [[nodiscard]] static bool SplitEdgeVertex(
        CsgDeformVertexVector<Core::Alloc::ScratchArena>& vertices,
        CsgDeformEdgeSplitMap& edgeSplits,
        const u32 first,
        const u32 second,
        const f32 firstDistance,
        const f32 secondDistance,
        u32& outVertex
    );
    static void EmitTriangle(
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& triangles,
        const u32 first,
        const u32 second,
        const u32 third
    );
    // Keep side is distance >= 0; caller snaps |distance| <= epsilon to zero first.
    [[nodiscard]] static bool ClipShell(
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

