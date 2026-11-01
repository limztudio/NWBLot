// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <impl/ecs_csg/deform_types.adhd.h>
#include <impl/ecs_csg/shape_registry.h>
#include <core/alloc/general.h>
#include <core/alloc/scratch.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
using CsgDeformVertexVector = Vector<CsgDeformVertex, ArenaT>;
template<typename ArenaT>
using CsgDeformTriangleVector = Vector<CsgDeformTriangle, ArenaT>;

struct CsgDeformEdgeSplitKeyHash{
    [[nodiscard]] usize operator()(const u64 key)const noexcept{ return CsgDeformEdgeKeyHash(key); }
};

using CsgDeformEdgeSplitMap = HashMap<u64, u32, Core::Alloc::ScratchArena, CsgDeformEdgeSplitKeyHash, EqualTo<u64>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns zero-crossing splits with the shared edge cache (never welds source verts) and interpolated attributes, emitting the kept split triangles.
class CsgDeformWallBuilder final : NoCopy{
public:
    // SIMD-domain cores: inputs and outputs stay on vector lanes, never touch storage.
    [[nodiscard]] static SIMDVectorSingle MixAttributeVec(const SIMDVectorSingle firstVec, const SIMDVectorSingle secondVec, const SIMDVectorSingle blendVec, const SIMDVectorSingle otherVec)noexcept{
        return VectorAdd(VectorMultiply(firstVec, blendVec), VectorMultiply(secondVec, otherVec));
    }
    [[nodiscard]] static SIMDVectorSingle TangentHandednessVec(SIMDVectorSingle normalizedTangent, SIMDVectorSingle tangentVec)noexcept;
    static void NormalizeDeformVertex(SIMDVectorSingle& normal, SIMDVectorSingle& tangent)noexcept;
    [[nodiscard]] static Expected<u32> SplitEdgeVertex(const u32 first, const u32 second, const f32 firstDistance, const f32 secondDistance, CsgDeformVertexVector<Core::Alloc::ScratchArena>& vertices, CsgDeformEdgeSplitMap& edgeSplits);
    // Keep side is distance >= 0; caller snaps |distance| <= epsilon to zero first.
    [[nodiscard]] static Expected<void, CsgDeformViabilityReason::Enum> ClipShell(
        Core::Alloc::ScratchArena& scratchArena,
        const CsgDeformShape& shape,
        SIMDVectorSingle epsilon,
        const CsgShapeRegistry* shapeRegistry,
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& scratchKept,
        Vector<f32, Core::Alloc::ScratchArena>& scratchDistances,
        CsgDeformVertexVector<Core::Alloc::ScratchArena>& inOutVertices,
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& inOutTriangles
    );
    [[nodiscard]] static Expected<void, CsgDeformViabilityReason::Enum> ClipShell(
        Core::Alloc::ScratchArena& scratchArena,
        const CsgDeformShape& shape,
        const SIMDVectorSingle epsilon,
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& scratchKept,
        Vector<f32, Core::Alloc::ScratchArena>& scratchDistances,
        CsgDeformVertexVector<Core::Alloc::ScratchArena>& inOutVertices,
        CsgDeformTriangleVector<Core::Alloc::ScratchArena>& inOutTriangles
    ){
        return ClipShell(scratchArena, shape, epsilon, nullptr, scratchKept, scratchDistances, inOutVertices, inOutTriangles);
    }


public:
    CsgDeformWallBuilder() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


