// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_cap_builder.h"

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CsgDeformCapBuilder::CollectBoundaryEdges(Core::Alloc::ScratchArena& scratchArena, const Span<const CsgDeformTriangle> triangles, Vector<CsgDeformCutLoopEdge, Core::Alloc::ScratchArena>& outEdges){
    outEdges.clear();
    if(triangles.empty())
        return;
    HashMap<u64, u32, Core::Alloc::ScratchArena, CsgDeformEdgeSplitKeyHash, EqualTo<u64>> edgeUses(0, CsgDeformEdgeSplitKeyHash(), EqualTo<u64>(), scratchArena);
    edgeUses.reserve(triangles.size() * s_EdgesPerTriangle + s_ReserveSlack);
    for(const CsgDeformTriangle& triangle : triangles){
        const Array<u64, s_TriangleCornerCount> edges = {{
            PackUnorderedU32Pair(triangle.indices[0u], triangle.indices[1u]),
            PackUnorderedU32Pair(triangle.indices[1u], triangle.indices[2u]),
            PackUnorderedU32Pair(triangle.indices[2u], triangle.indices[0u]),
        }};
        for(u32 corner = 0u; corner < s_TriangleCornerCount; ++corner){
            const auto edgeUse = edgeUses.try_emplace(edges[corner], 0u);
            ++edgeUse.first.value();
        }
    }
    outEdges.reserve(triangles.size());
    for(const CsgDeformTriangle& triangle : triangles){
        const Array<Array<u32, 2u>, s_TriangleCornerCount> corners = {{
            {{ triangle.indices[0u], triangle.indices[1u] }},
            {{ triangle.indices[1u], triangle.indices[2u] }},
            {{ triangle.indices[2u], triangle.indices[0u] }},
        }};
        for(u32 corner = 0u; corner < s_TriangleCornerCount; ++corner){
            const auto found = edgeUses.find(PackUnorderedU32Pair(corners[corner][0u], corners[corner][1u]));
            if(found != edgeUses.end() && found.value() == 1u)
                outEdges.push_back(MakeCsgDeformCutLoopEdge(corners[corner][0u], corners[corner][1u]));
        }
    }
    // Deterministic boundary order: traversal input matches emitted triangle order.
}

b8 CsgDeformCapBuilder::OrderBoundaryLoop(Core::Alloc::ScratchArena& scratchArena, const Span<const CsgDeformCutLoopEdge> edges, Vector<u32, Core::Alloc::ScratchArena>& outLoop){
    outLoop.clear();
    if(edges.empty())
        return true;
    // Boundary edges keep source-triangle winding, so consecutive edges meet head-to-tail or tail-to-tail. Walk them undirected for one closed ring.
    Vector<CsgDeformCutLoopEdge, Core::Alloc::ScratchArena> remaining(scratchArena);
    remaining.assign(edges.begin(), edges.end());
    const u32 start = FindInitialCsgDeformBoundaryLoopVertex(remaining);
    u32 cursor = start;
    outLoop.reserve(edges.size());
    outLoop.push_back(cursor);
    {
        const auto seedIndex = TryFindIncidentCsgDeformLoopEdge(remaining, cursor);
        if(!seedIndex)
            return false;
        cursor = OtherCsgDeformLoopEdgeVertex(remaining[*seedIndex], cursor);
        outLoop.push_back(cursor);
        remaining.erase(remaining.begin() + static_cast<isize>(*seedIndex));
    }
    while(!remaining.empty()){
        const auto nextIndex = TryFindIncidentCsgDeformLoopEdge(remaining, cursor);
        if(!nextIndex)
            break;
        const u32 next = OtherCsgDeformLoopEdgeVertex(remaining[*nextIndex], cursor);
        remaining.erase(remaining.begin() + static_cast<isize>(*nextIndex));
        cursor = next;
        if(cursor == start)
            break;
        outLoop.push_back(cursor);
    }
    if(cursor != start || !remaining.empty())
        return false;
    HashSet<u32, Core::Alloc::ScratchArena, Hasher<u32>, EqualTo<u32>> seenLoopVertices(0u, Hasher<u32>(), EqualTo<u32>(), scratchArena);
    seenLoopVertices.reserve(outLoop.size() + s_ReserveSlack);
    for(const u32 loopVertex : outLoop){
        if(!seenLoopVertices.insert(loopVertex).second)
            return false;
    }
    return true;
}

Expected<u32> CsgDeformCapBuilder::FillCutCaps(
    Core::Alloc::ScratchArena& scratchArena,
    Vector<CsgDeformCutLoopEdge, Core::Alloc::ScratchArena>& scratchEdges,
    CsgDeformVertexVector<Core::Alloc::ScratchArena>& inOutVertices,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& inOutTriangles
){
    u32 capTriangles = 0u;
    CsgDeformCapBuilder::CollectBoundaryEdges(scratchArena, inOutTriangles, scratchEdges);
    if(scratchEdges.empty())
        return capTriangles;
    // Peel one closed loop at a time from the boundary set; open chains stay cap-free while a closed but degenerate loop fails viability.
    Vector<CsgDeformCutLoopEdge, Core::Alloc::ScratchArena> remaining(scratchArena);
    remaining = scratchEdges;
    Vector<CsgDeformCutLoopEdge, Core::Alloc::ScratchArena> loopEdges(scratchArena);
    Vector<u32, Core::Alloc::ScratchArena> loop(scratchArena);
    HashSet<u32, Core::Alloc::ScratchArena, Hasher<u32>, EqualTo<u32>> loopMembers(0u, Hasher<u32>(), EqualTo<u32>(), scratchArena);
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
        for(b8 grown = true; grown;){
            grown = false;
            for(usize edgeIndex = 0u; edgeIndex < remaining.size();){
                const CsgDeformCutLoopEdge& candidate = remaining[edgeIndex];
                const b8 adjacent = loopMembers.find(candidate.first) != loopMembers.end() || loopMembers.find(candidate.second) != loopMembers.end();
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
        // Open chains carry no closable loop; they stay viable cap-free. Only a closed degenerate loop fails.
        if(!CsgDeformCapBuilder::OrderBoundaryLoop(scratchArena, loopEdges, loop))
            continue;
        const Expected<CsgDeformVertex> center = TryResolveCsgDeformCapLoopCenter(inOutVertices, loop, inOutTriangles.size());
        if(!center)
            return MakeUnexpected(Failure{});
        const u32 centerIndex = static_cast<u32>(inOutVertices.size());
        inOutVertices.push_back(*center);
        for(usize vertexIndex = 0u; vertexIndex < loop.size(); ++vertexIndex){
            inOutTriangles.push_back(MakeCsgDeformCapLoopTriangle(centerIndex, loop, vertexIndex));
            ++capTriangles;
        }
    }
    return capTriangles;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

