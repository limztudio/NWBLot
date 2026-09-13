// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_cap_builder.h"
#include "deform_wall_builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = Core::Alloc::ScratchArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CsgDeformCapBuilder::CollectBoundaryEdges(
    ScratchArena& scratchArena,
    const CsgDeformTriangleVector<ScratchArena>& triangles,
    Vector<CsgDeformCutLoopEdge, ScratchArena>& outEdges
){
    outEdges.clear();
    if(triangles.empty())
        return;
    HashMap<u64, u32, CsgDeformEdgeSplitKeyHash, EqualTo<u64>, ScratchArena> edgeUses(0, CsgDeformEdgeSplitKeyHash(), EqualTo<u64>(), scratchArena);
    edgeUses.reserve(triangles.size() * s_EdgesPerTriangle + s_ReserveSlack);
    for(const CsgDeformTriangle& triangle : triangles){
        const u64 edges[s_TriangleCornerCount] = {
            CsgDeformEdgeKey(triangle.indices[0u], triangle.indices[1u]),
            CsgDeformEdgeKey(triangle.indices[1u], triangle.indices[2u]),
            CsgDeformEdgeKey(triangle.indices[2u], triangle.indices[0u]),
        };
        for(u32 corner = 0u; corner < s_TriangleCornerCount; ++corner){
            const auto found = edgeUses.find(edges[corner]);
            if(found == edgeUses.end())
                edgeUses.emplace(edges[corner], 1u);
            else
                found.value() = found.value() + 1u;
        }
    }
    outEdges.reserve(triangles.size());
    for(const CsgDeformTriangle& triangle : triangles){
        const u32 corners[s_TriangleCornerCount][2u] = {
            { triangle.indices[0u], triangle.indices[1u] },
            { triangle.indices[1u], triangle.indices[2u] },
            { triangle.indices[2u], triangle.indices[0u] },
        };
        for(u32 corner = 0u; corner < s_TriangleCornerCount; ++corner){
            const auto found = edgeUses.find(CsgDeformEdgeKey(corners[corner][0u], corners[corner][1u]));
            if(found != edgeUses.end() && found.value() == 1u)
                outEdges.push_back(CsgDeformCutLoopEdge{ corners[corner][0u], corners[corner][1u] });
        }
    }
    // Deterministic boundary order: traversal input matches emitted triangle order.
}

bool CsgDeformCapBuilder::OrderBoundaryLoop(
    ScratchArena& scratchArena,
    const Vector<CsgDeformCutLoopEdge, ScratchArena>& edges,
    Vector<u32, ScratchArena>& outLoop
){
    outLoop.clear();
    if(edges.empty())
        return true;
    // Boundary edges keep source-triangle winding, so consecutive edges meet head-to-tail or tail-to-tail. Walk them undirected for one closed ring.
    Vector<CsgDeformCutLoopEdge, ScratchArena> remaining(scratchArena);
    remaining = edges;
    u32 start = remaining.front().first;
    u32 cursor = start;
    for(const CsgDeformCutLoopEdge& edge : remaining){
        if(edge.first < start)
            start = edge.first;
        if(edge.second < start)
            start = edge.second;
    }
    cursor = start;
    outLoop.reserve(edges.size());
    outLoop.push_back(cursor);
    {
        const auto seed = ::FindIf(
            remaining.begin(),
            remaining.end(),
            [cursor](const CsgDeformCutLoopEdge& edge){ return edge.first == cursor || edge.second == cursor; }
        );
        if(seed == remaining.end())
            return false;
        cursor = seed->first == cursor ? seed->second : seed->first;
        outLoop.push_back(cursor);
        remaining.erase(seed);
    }
    while(!remaining.empty()){
        bool advanced = false;
        for(auto it = remaining.begin(); it != remaining.end(); ++it){
            if(it->first == cursor || it->second == cursor){
                const u32 next = it->first == cursor ? it->second : it->first;
                remaining.erase(it);
                cursor = next;
                if(cursor == start)
                    break;
                outLoop.push_back(cursor);
                advanced = true;
                break;
            }
        }
        if(cursor == start || !advanced)
            break;
    }
    if(cursor != start || !remaining.empty())
        return false;
    HashSet<u32, Hasher<u32>, EqualTo<u32>, ScratchArena> seenLoopVertices(0u, Hasher<u32>(), EqualTo<u32>(), scratchArena);
    seenLoopVertices.reserve(outLoop.size() + s_ReserveSlack);
    for(const u32 loopVertex : outLoop){
        if(!seenLoopVertices.insert(loopVertex).second)
            return false;
    }
    return true;
}

SIMDVector CsgDeformCapBuilder::AccumulateFanAreaVec(SIMDVector inAreaVec, SIMDVector originVec, SIMDVector firstVec, SIMDVector secondVec){
    return VectorAdd(inAreaVec, Vector3Cross(VectorSubtract(firstVec, originVec), VectorSubtract(secondVec, originVec)));
}

SIMDVector CsgDeformCapBuilder::ScaleCenterVec(SIMDVector sumVec, SIMDVector loopSizeVec){
    return VectorDivide(sumVec, loopSizeVec);
}

SIMDVector CsgDeformCapBuilder::CapCenterNormalVec(SIMDVector loopNormalVec){
    return loopNormalVec;
}

bool CsgDeformCapBuilder::CapNormal(
    const CsgDeformVertexVector<ScratchArena>& vertices,
    const Vector<u32, ScratchArena>& loop,
    Float4& outNormal
){
    outNormal = s_UpAxis;
    if(loop.size() < s_MinLoopVertices)
        return false;
    // Beginner boundary: load each loop position once, accumulate the fan area on the SIMD core, store the normal once.
    const SIMDVector originVec = LoadFloat(vertices[loop[0u]].position);
    SIMDVector areaVec = VectorZero();
    for(usize vertexIndex = 1u; vertexIndex + 1u < loop.size(); ++vertexIndex){
        const SIMDVector firstVec = LoadFloat(vertices[loop[vertexIndex]].position);
        const SIMDVector secondVec = LoadFloat(vertices[loop[vertexIndex + 1u]].position);
        areaVec = CsgDeformCapBuilder::AccumulateFanAreaVec(areaVec, originVec, firstVec, secondVec);
    }
    const f32 areaLengthSq = VectorGetX(Vector3LengthSq(areaVec));
    if(!(areaLengthSq > s_LoopAreaEpsilonSq))
        return false;
    const SIMDVector normalized = Vector3Normalize(areaVec);
    const SIMDVector packed = VectorSet(VectorGetX(normalized), VectorGetY(normalized), VectorGetZ(normalized), s_UpAxis.w);
    StoreFloat(packed, outNormal);
    return true;
}

bool CsgDeformCapBuilder::FillCapLoop(
    const Float4& loopNormal,
    CsgDeformVertexVector<ScratchArena>& inOutVertices,
    CsgDeformTriangleVector<ScratchArena>& inOutTriangles,
    const Vector<u32, ScratchArena>& loop,
    u32& outCapTriangles
){
    outCapTriangles = 0u;
    if(loop.size() < s_MinLoopVertices)
        return false;
    if(inOutVertices.size() + 1u > s_MaxDeformVertices)
        return false;
    // SIMD center accumulation keeps position/uv/color sums on vector lanes.
    SIMDVector centerPositionVec = VectorZero();
    SIMDVector centerUvVec = VectorZero();
    SIMDVector centerColorVec = VectorZero();
    for(const u32 vertexIndex : loop){
        NWB_ASSERT(vertexIndex < inOutVertices.size());
        const CsgDeformVertex& vertex = inOutVertices[vertexIndex];
        centerPositionVec = VectorAdd(centerPositionVec, LoadFloat(vertex.position));
        centerUvVec = VectorAdd(centerUvVec, LoadFloat(vertex.uv0));
        centerColorVec = VectorAdd(centerColorVec, LoadFloat(vertex.color));
    }
    // Beginner boundary: loopNormal crosses into SIMD once; center lanes average on SIMD cores and store once.
    const SIMDVector loopNormalVec = LoadFloat(loopNormal);
    const SIMDVector loopSizeVec = VectorReplicate(static_cast<f32>(loop.size()));
    const SIMDVector centerNormalVec = CsgDeformCapBuilder::CapCenterNormalVec(loopNormalVec);
    CsgDeformVertex center;
    StoreFloat(CsgDeformCapBuilder::ScaleCenterVec(centerPositionVec, loopSizeVec), center.position);
    StoreFloat(centerNormalVec, center.normal);
    center.tangent = s_FallbackTangent;
    StoreFloat(CsgDeformCapBuilder::ScaleCenterVec(centerUvVec, loopSizeVec), center.uv0);
    StoreFloat(CsgDeformCapBuilder::ScaleCenterVec(centerColorVec, loopSizeVec), center.color);
    if(!CsgDeformWallBuilder::NormalizeDeformVertex(center))
        return false;
    const u32 centerIndex = static_cast<u32>(inOutVertices.size());
    inOutVertices.push_back(center);

    // One orientation rule for every loop: reverse kept winding so caps face the cut.
    for(usize vertexIndex = 0u; vertexIndex < loop.size(); ++vertexIndex){
        if(inOutTriangles.size() + 1u > s_MaxDeformTriangles)
            return false;
        const u32 first = loop[vertexIndex];
        const u32 second = loop[(vertexIndex + 1u) % loop.size()];
        CsgDeformWallBuilder::EmitTriangle(inOutTriangles, centerIndex, second, first);
        ++outCapTriangles;
    }
    return true;
}

bool CsgDeformCapBuilder::FillCutCaps(
    ScratchArena& scratchArena,
    CsgDeformVertexVector<ScratchArena>& inOutVertices,
    CsgDeformTriangleVector<ScratchArena>& inOutTriangles,
    Vector<CsgDeformCutLoopEdge, ScratchArena>& scratchEdges,
    u32& outCapTriangles
){
    outCapTriangles = 0u;
    CsgDeformCapBuilder::CollectBoundaryEdges(scratchArena, inOutTriangles, scratchEdges);
    if(scratchEdges.empty())
        return true;
    // Peel one closed loop at a time from the boundary set; open chains stay cap-free while a closed but degenerate loop fails viability.
    Vector<CsgDeformCutLoopEdge, ScratchArena> remaining(scratchArena);
    remaining = scratchEdges;
    Vector<CsgDeformCutLoopEdge, ScratchArena> loopEdges(scratchArena);
    Vector<u32, ScratchArena> loop(scratchArena);
    HashSet<u32, Hasher<u32>, EqualTo<u32>, ScratchArena> loopMembers(0u, Hasher<u32>(), EqualTo<u32>(), scratchArena);
    loopEdges.reserve(scratchEdges.size());
    loop.reserve(scratchEdges.size());
    loopMembers.reserve(scratchEdges.size() + s_ReserveSlack);
    while(!remaining.empty()){
        loopEdges.clear();
        loopMembers.clear();
        loopEdges.push_back(remaining.back());
        loopMembers.insert(remaining.back().first);
        loopMembers.insert(remaining.back().second);
        remaining.pop_back();
        for(bool grown = true; grown;){
            grown = false;
            for(usize edgeIndex = 0u; edgeIndex < remaining.size();){
                const CsgDeformCutLoopEdge& candidate = remaining[edgeIndex];
                const bool adjacent = loopMembers.find(candidate.first) != loopMembers.end() || loopMembers.find(candidate.second) != loopMembers.end();
                if(adjacent){
                    loopEdges.push_back(candidate);
                    loopMembers.insert(candidate.first);
                    loopMembers.insert(candidate.second);
                    remaining.erase(remaining.begin() + static_cast<typename decltype(remaining)::difference_type>(edgeIndex));
                    grown = true;
                }
                else
                    ++edgeIndex;
            }
        }
        // Open boundary chains (e.g. cuts across an open sheet) carry no closable volume loop; they stay viable cap-free. Only a closed but degenerate loop fails viability.
        if(!CsgDeformCapBuilder::OrderBoundaryLoop(scratchArena, loopEdges, loop))
            continue;
        Float4 loopNormal;
        if(!CsgDeformCapBuilder::CapNormal(inOutVertices, loop, loopNormal))
            return false;
        u32 loopCaps = 0u;
        if(!CsgDeformCapBuilder::FillCapLoop(loopNormal, inOutVertices, inOutTriangles, loop, loopCaps))
            return false;
        outCapTriangles += loopCaps;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

