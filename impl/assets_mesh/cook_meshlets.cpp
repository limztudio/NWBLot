// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_meshlets.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MeshCookMeshlets::ClearMeshletFrontier(Vector<u32, Core::Alloc::ScratchArena>& frontier, const Span<u8> frontierFlags)noexcept{
    for(const u32 triangleIndex : frontier)
        frontierFlags[triangleIndex] = 0u;
    frontier.clear();
}

void MeshCookMeshlets::RemoveMeshletFrontierCandidate(Vector<u32, Core::Alloc::ScratchArena>& frontier, const Span<u8> frontierFlags, const usize frontierOffset)noexcept{
    const u32 triangleIndex = frontier[frontierOffset];
    frontierFlags[triangleIndex] = 0u;
    frontier[frontierOffset] = frontier.back();
    frontier.pop_back();
}

void MeshCookMeshlets::AddMeshletTriangleNeighborsToFrontier(const MeshletTrianglePrecompute& trianglePrecompute, const u32 triangleIndex, Vector<u32, Core::Alloc::ScratchArena>& frontier, const Span<u8> frontierFlags){
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    for(const u32 positionIndex : triangle.positions){
        const u32 triangleOffsetBegin = trianglePrecompute.positionTriangleOffsets[positionIndex];
        const u32 triangleOffsetEnd = trianglePrecompute.positionTriangleOffsets[positionIndex + 1u];
        for(u32 triangleOffset = triangleOffsetBegin; triangleOffset < triangleOffsetEnd; ++triangleOffset){
            const u32 neighborTriangleIndex = trianglePrecompute.positionTriangleIndices[triangleOffset];
            if(neighborTriangleIndex == triangleIndex || trianglePrecompute.visitedTriangles[neighborTriangleIndex] != 0u || frontierFlags[neighborTriangleIndex] != 0u)
                continue;

            frontierFlags[neighborTriangleIndex] = 1u;
            frontier.push_back(neighborTriangleIndex);
        }
    }
}

b8 MeshCookMeshlets::AddMeshletTriangleToBuilder(const Path& nwbFilePath, const TStringView metaKind, const MeshletTriangleData& triangle, Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs, MeshletDesc& meshlet, Core::Assets::AssetVector<u8>& primitiveIndices){
    for(const u32 vertexRefIndex : triangle.vertexRefs){
        const auto found = MeshletCandidateSearch::FindMeshletLocalVertex(localSourceVertexRefs, vertexRefIndex);
        u8 localVertex = found ? *found : 0u;
        if(!found){
            if(localSourceVertexRefs.size() >= s_MeshMaxMeshletVertices){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': triangle cannot fit within one meshlet"), metaKind, PathToString<tchar>(nwbFilePath));
                return false;
            }
            localVertex = static_cast<u8>(localSourceVertexRefs.size());
            localSourceVertexRefs.push_back(vertexRefIndex);
        }
        primitiveIndices.push_back(localVertex);
    }
    meshlet.counts = PackMeshletCounts(0u, MeshletPrimitiveCount(meshlet) + 1u, 0u, 0u);
    return true;
}

b8 MeshCookMeshlets::AddVisitedMeshletTriangle(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const u32 triangleIndex,
    MeshletTrianglePrecompute& trianglePrecompute,
    Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs,
    Vector<u32, Core::Alloc::ScratchArena>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Vector<u32, Core::Alloc::ScratchArena>& frontier,
    const Span<u8> frontierFlags
){
    const MeshletTriangleData& triangle = trianglePrecompute.triangles[triangleIndex];
    if(!AddMeshletTriangleToBuilder(nwbFilePath, metaKind, triangle, localSourceVertexRefs, meshlet, primitiveIndices))
        return false;

    localTriangleIndices.push_back(triangleIndex);
    const MeshletTriangleCalculation& triangleCalculation = trianglePrecompute.triangleCalculations[triangleIndex];
    const MeshletTriangleVectors triangleVectors = MeshletTriangleGeometry::MakeMeshletTriangleVectors(LoadFloat(triangleCalculation.positions[0u]), LoadFloat(triangleCalculation.positions[1u]), LoadFloat(triangleCalculation.positions[2u]), LoadFloat(triangleCalculation.centroid), LoadFloat(triangleCalculation.areaNormal));
    const auto triangleAreaNormalAt = [&](const u32 otherTriangleIndex)noexcept{
        return LoadFloat(trianglePrecompute.triangleCalculations[otherTriangleIndex].areaNormal);
    };
    MeshletCandidateSearch::AddMeshletTriangleToScoreState(localTriangleIndices, triangleVectors, triangleAreaNormalAt, scoreState);
    trianglePrecompute.visitedTriangles[triangleIndex] = 1u;
    AddMeshletTriangleNeighborsToFrontier(trianglePrecompute, triangleIndex, frontier, frontierFlags);
    return true;
}


b8 MeshCookMeshlets::GrowMeshletFromFrontier(
    const Path& nwbFilePath,
    const TStringView metaKind,
    const usize seedSearchOffset,
    Core::CpuTaskScheduler& cpuScheduler,
    MeshletTrianglePrecompute& trianglePrecompute,
    Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs,
    Vector<u32, Core::Alloc::ScratchArena>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena>& parallelCandidates,
    Vector<u32, Core::Alloc::ScratchArena>& frontier,
    const Span<u8> frontierFlags
){
    while(MeshletPrimitiveCount(meshlet) < s_MeshMaxMeshletTriangles){
        if(frontier.empty()){
            const auto disconnectedCandidate = MeshletCandidateSearch::FindBestDisconnectedMeshletCandidate(trianglePrecompute, seedSearchOffset + 1u, localTriangleIndices, scoreState, meshlet, localSourceVertexRefs, cpuScheduler, parallelCandidates);
            if(!disconnectedCandidate)
                break;

            if(!AddVisitedMeshletTriangle(nwbFilePath, metaKind, disconnectedCandidate->triangleIndex, trianglePrecompute, localSourceVertexRefs, localTriangleIndices, meshlet, primitiveIndices, scoreState, frontier, frontierFlags))
                return false;
            continue;
        }

        const auto bestCandidate = MeshletCandidateSearch::FindBestMeshletFrontierCandidate(trianglePrecompute, frontier, localTriangleIndices, scoreState, meshlet, localSourceVertexRefs);
        if(!bestCandidate)
            break;

        RemoveMeshletFrontierCandidate(frontier, frontierFlags, bestCandidate->frontierOffset);
        if(!AddVisitedMeshletTriangle(nwbFilePath, metaKind, bestCandidate->triangleIndex, trianglePrecompute, localSourceVertexRefs, localTriangleIndices, meshlet, primitiveIndices, scoreState, frontier, frontierFlags))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

