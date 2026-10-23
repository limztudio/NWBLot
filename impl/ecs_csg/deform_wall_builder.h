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
    [[nodiscard]] static SIMDVectorSingle MixAttributeVec(SIMDVectorSingle firstVec, SIMDVectorSingle secondVec, SIMDVectorSingle blendVec, SIMDVectorSingle otherVec)noexcept;
    [[nodiscard]] static SIMDVectorSingle KeepWVec(SIMDVectorSingle normalizedVec, SIMDVectorSingle sourceVec)noexcept;
    [[nodiscard]] static SIMDVectorSingle TangentHandednessVec(SIMDVectorSingle normalizedTangent, SIMDVectorSingle tangentVec)noexcept;
    [[nodiscard]] static SIMDVectorSingle UpAxisVec()noexcept;
    static void NormalizeDeformVertex(SIMDVectorSingle& normal, SIMDVectorSingle& tangent)noexcept;
    [[nodiscard]] static Expected<u32> SplitEdgeVertex(const u32 first, const u32 second, const f32 firstDistance, const f32 secondDistance, CsgDeformVertexVector<Core::Alloc::ScratchArena>& vertices, CsgDeformEdgeSplitMap& edgeSplits);
    static void EmitTriangle(CsgDeformTriangleVector<Core::Alloc::ScratchArena>& triangles, const u32 first, const u32 second, const u32 third);
    // Keep side is distance >= 0; caller snaps |distance| <= epsilon to zero first.
    [[nodiscard]] static Expected<void, CsgDeformViabilityReason::Enum> ClipShell(
        Core::Alloc::ScratchArena& scratchArena,
        const CsgDeformShape& shape,
        const f32 epsilon,
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& scratchKept,
        Vector<f32, Core::Alloc::ScratchArena>& scratchDistances,
        CsgDeformVertexVector<Core::Alloc::ScratchArena>& inOutVertices,
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& inOutTriangles
    );


public:
    CsgDeformWallBuilder() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

