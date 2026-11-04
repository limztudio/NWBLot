// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_wall_builder.h"

#include "deform_cutter_field.h"

#include <global/scope_exit.h>
#include <global/simd/vector.h>

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u32> CsgDeformWallBuilder::SplitEdgeVertex(const u32 first, const u32 second, const f32 firstDistance, const f32 secondDistance, CsgDeformVertexVector<Core::Alloc::ScratchArena>& vertices, CsgDeformEdgeSplitMap& edgeSplits){
    if(vertices.size() + 1u > s_MaxDeformVertices)
        return MakeUnexpected(Failure{});
    const u64 edge = PackUnorderedU32Pair(first, second);
    const auto found = edgeSplits.find(edge);
    if(found != edgeSplits.end())
        return found.value();
    // A zero secondDistance selects the second endpoint.
    const Expected<CsgDeformVertex> mixed = TryBuildInterpolatedCsgDeformVertex(vertices[first], vertices[second], firstDistance, secondDistance);
    if(!mixed)
        return MakeUnexpected(Failure{});
    const u32 created = static_cast<u32>(vertices.size());
    vertices.push_back(*mixed);
    ScopeExit discardVertex([&]()noexcept{ vertices.pop_back(); });
    if(!edgeSplits.emplace(edge, created).second){
        NWB_ASSERT(false);
        return MakeUnexpected(Failure{});
    }
    discardVertex.release();
    return created;
}

// Keep side is distance >= 0; caller snaps |distance| <= boundaryEpsilon to zero first.
Expected<void, CsgDeformViabilityReason::Enum> CsgDeformWallBuilder::ClipShell(
    Core::Alloc::ScratchArena& scratchArena,
    const CsgDeformShape& shape,
    const f32 boundaryEpsilon,
    const CsgShapeRegistry* shapeRegistry,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& scratchKept,
    Vector<f32, Core::Alloc::ScratchArena>& scratchDistances,
    CsgDeformVertexVector<Core::Alloc::ScratchArena>& inOutVertices,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& inOutTriangles
){
    const auto shapeDistances = CsgDeformCutterField::ShapeDistances(shape, inOutVertices, boundaryEpsilon, shapeRegistry, scratchDistances);
    if(!shapeDistances)
        return MakeUnexpected(shapeDistances.error());

    CsgDeformEdgeSplitMap edgeSplits(0, CsgDeformEdgeSplitKeyHash(), EqualTo<u64>(), scratchArena);
    edgeSplits.reserve(inOutTriangles.size() * s_EdgesPerTriangle + s_ReserveSlack);
    scratchKept.clear();
    scratchKept.reserve(inOutTriangles.size() * s_KeptReserveMultiplier + s_ReserveSlack);

    const usize triangleCount = inOutTriangles.size();
    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        const CsgDeformTriangle triangle = inOutTriangles[triangleIndex];
        NWB_ASSERT(triangle.indices[0u] < inOutVertices.size());
        NWB_ASSERT(triangle.indices[1u] < inOutVertices.size());
        NWB_ASSERT(triangle.indices[2u] < inOutVertices.size());
        const CsgDeformTriangleClipPlan plan = ClassifyCsgDeformTriangleClip(triangle, scratchDistances);
        if(plan.keepCount == s_TriangleCornerCount){
            scratchKept.push_back(MakeCsgDeformTriangle(triangle.indices[0u], triangle.indices[1u], triangle.indices[2u]));
            continue;
        }
        if(plan.keepCount == 0u)
            continue;
        const auto splitA = CsgDeformWallBuilder::SplitEdgeVertex(plan.firstEdgeStart, plan.firstEdgeEnd, plan.firstEdgeStartDistance, plan.firstEdgeEndDistance, inOutVertices, edgeSplits);
        if(!splitA)
            return MakeUnexpected(CsgDeformViabilityReason::NoKeptGeometry);
        const auto splitB = CsgDeformWallBuilder::SplitEdgeVertex(plan.secondEdgeStart, plan.secondEdgeEnd, plan.secondEdgeStartDistance, plan.secondEdgeEndDistance, inOutVertices, edgeSplits);
        if(!splitB)
            return MakeUnexpected(CsgDeformViabilityReason::NoKeptGeometry);
        scratchKept.push_back(MakeCsgDeformTriangle(plan.primaryCorner, *splitA, *splitB));
        if(plan.keepCount == 2u)
            scratchKept.push_back(MakeCsgDeformTriangle(plan.primaryCorner, *splitB, plan.secondKeepVertex));
        if(scratchKept.size() > s_MaxDeformTriangles)
            return MakeUnexpected(CsgDeformViabilityReason::NoKeptGeometry);
    }
    inOutTriangles = scratchKept;
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

