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
namespace CsgDeformWallBuilder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<u32> SplitEdgeVertex(u32 first, u32 second, f32 firstDistance, f32 secondDistance, CsgDeformVertexVector<Core::Alloc::ScratchArena>& vertices, CsgDeformEdgeSplitMap& edgeSplits);
// Keep side is distance >= 0; caller snaps |distance| <= boundaryEpsilon to zero first.
[[nodiscard]] Expected<void, CsgDeformViabilityReason::Enum> ClipShell(
    Core::Alloc::ScratchArena& scratchArena,
    const CsgDeformShape& shape,
    f32 boundaryEpsilon,
    const CsgShapeRegistry* shapeRegistry,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& scratchKept,
    Vector<f32, Core::Alloc::ScratchArena>& scratchDistances,
    CsgDeformVertexVector<Core::Alloc::ScratchArena>& inOutVertices,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& inOutTriangles
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

