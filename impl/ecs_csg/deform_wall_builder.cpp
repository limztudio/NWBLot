// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_wall_builder.h"

#include "deform_cutter_field.h"

#include <global/scope_exit.h>
#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u32> CsgDeformWallBuilder::SplitEdgeVertex(const u32 first, const u32 second, const f32 firstDistance, const f32 secondDistance, CsgDeformVertexVector<Core::Alloc::ScratchArena>& vertices, CsgDeformEdgeSplitMap& edgeSplits){
    if(vertices.size() + 1u > s_MaxDeformVertices)
        return MakeUnexpected(Failure{});
    const u64 edge = CsgDeformEdgeKey(first, second);
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
        const Array<f32, s_TriangleCornerCount> distances = {{ scratchDistances[triangle.indices[0u]], scratchDistances[triangle.indices[1u]], scratchDistances[triangle.indices[2u]] }};
        const Array<b8, s_TriangleCornerCount> kept = {{ distances[0u] >= s_KeepDistanceZero, distances[1u] >= s_KeepDistanceZero, distances[2u] >= s_KeepDistanceZero }};
        const u32 keepCount = (kept[0u] ? 1u : 0u) + (kept[1u] ? 1u : 0u) + (kept[2u] ? 1u : 0u);
        if(keepCount == s_TriangleCornerCount){
            scratchKept.push_back(MakeCsgDeformTriangle(triangle.indices[0u], triangle.indices[1u], triangle.indices[2u]));
            continue;
        }
        if(keepCount == 0u)
            continue;
        if(keepCount == 1u){
            u32 keepCorner = 0u;
            for(u32 corner = 0u; corner < s_TriangleCornerCount; ++corner){
                if(kept[corner]){
                    keepCorner = corner;
                    break;
                }
            }
            const u32 keepVertex = triangle.indices[keepCorner];
            const u32 dropA = triangle.indices[(keepCorner + 1u) % s_TriangleCornerCount];
            const u32 dropB = triangle.indices[(keepCorner + 2u) % s_TriangleCornerCount];
            const f32 keepDistance = distances[keepCorner];
            const f32 dropDistanceA = distances[(keepCorner + 1u) % s_TriangleCornerCount];
            const f32 dropDistanceB = distances[(keepCorner + 2u) % s_TriangleCornerCount];
            const auto splitA = CsgDeformWallBuilder::SplitEdgeVertex(keepVertex, dropA, keepDistance, dropDistanceA, inOutVertices, edgeSplits);
            if(!splitA)
                return MakeUnexpected(CsgDeformViabilityReason::NoKeptGeometry);
            const auto splitB = CsgDeformWallBuilder::SplitEdgeVertex(keepVertex, dropB, keepDistance, dropDistanceB, inOutVertices, edgeSplits);
            if(!splitB)
                return MakeUnexpected(CsgDeformViabilityReason::NoKeptGeometry);
            scratchKept.push_back(MakeCsgDeformTriangle(keepVertex, *splitA, *splitB));
            continue;
        }
        u32 dropCorner = 0u;
        for(u32 corner = 0u; corner < s_TriangleCornerCount; ++corner){
            if(!kept[corner]){
                dropCorner = corner;
                break;
            }
        }
        const u32 dropVertex = triangle.indices[dropCorner];
        const u32 keepA = triangle.indices[(dropCorner + 1u) % s_TriangleCornerCount];
        const u32 keepB = triangle.indices[(dropCorner + 2u) % s_TriangleCornerCount];
        const f32 dropDistance = distances[dropCorner];
        const f32 keepDistanceA = distances[(dropCorner + 1u) % s_TriangleCornerCount];
        const f32 keepDistanceB = distances[(dropCorner + 2u) % s_TriangleCornerCount];
        const auto splitA = CsgDeformWallBuilder::SplitEdgeVertex(keepA, dropVertex, keepDistanceA, dropDistance, inOutVertices, edgeSplits);
        if(!splitA)
            return MakeUnexpected(CsgDeformViabilityReason::NoKeptGeometry);
        const auto splitB = CsgDeformWallBuilder::SplitEdgeVertex(keepB, dropVertex, keepDistanceB, dropDistance, inOutVertices, edgeSplits);
        if(!splitB)
            return MakeUnexpected(CsgDeformViabilityReason::NoKeptGeometry);
        scratchKept.push_back(MakeCsgDeformTriangle(keepA, *splitA, *splitB));
        scratchKept.push_back(MakeCsgDeformTriangle(keepA, *splitB, keepB));
        if(scratchKept.size() > s_MaxDeformTriangles)
            return MakeUnexpected(CsgDeformViabilityReason::NoKeptGeometry);
    }
    inOutTriangles = scratchKept;
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

