// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_meshlets.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshCookMeshlets::AddVisitedMeshletTriangle(
    const Path& nwbFilePath,
    const TStringView metaKind,
    MeshletTrianglePrecompute& trianglePrecompute,
    const u32 triangleIndex,
    Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs,
    Vector<u32, Core::Alloc::ScratchArena>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Vector<u32, Core::Alloc::ScratchArena>& frontier,
    Vector<u8, Core::Alloc::ScratchArena>& frontierFlags
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


bool MeshCookMeshlets::GrowMeshletFromFrontier(
    const Path& nwbFilePath,
    const TStringView metaKind,
    MeshletTrianglePrecompute& trianglePrecompute,
    const usize seedSearchOffset,
    Vector<u32, Core::Alloc::ScratchArena>& localSourceVertexRefs,
    Vector<u32, Core::Alloc::ScratchArena>& localTriangleIndices,
    MeshletDesc& meshlet,
    Core::Assets::AssetVector<u8>& primitiveIndices,
    MeshletScoreState& scoreState,
    Core::CpuTaskScheduler& cpuScheduler,
    Vector<MeshletCandidateSearchResult, Core::Alloc::ScratchArena>& parallelCandidates,
    Vector<u32, Core::Alloc::ScratchArena>& frontier,
    Vector<u8, Core::Alloc::ScratchArena>& frontierFlags
){
    while(MeshletPrimitiveCount(meshlet) < s_MeshMaxMeshletTriangles){
        if(frontier.empty()){
            const auto disconnectedCandidate = MeshletCandidateSearch::FindBestDisconnectedMeshletCandidate(trianglePrecompute, seedSearchOffset + 1u, localTriangleIndices, scoreState, meshlet, localSourceVertexRefs, cpuScheduler, parallelCandidates);
            if(!disconnectedCandidate)
                break;

            if(!AddVisitedMeshletTriangle(nwbFilePath, metaKind, trianglePrecompute, disconnectedCandidate->triangleIndex, localSourceVertexRefs, localTriangleIndices, meshlet, primitiveIndices, scoreState, frontier, frontierFlags))
                return false;
            continue;
        }

        const auto bestCandidate = MeshletCandidateSearch::FindBestMeshletFrontierCandidate(trianglePrecompute, frontier, localTriangleIndices, scoreState, meshlet, localSourceVertexRefs);
        if(!bestCandidate)
            break;

        RemoveMeshletFrontierCandidate(frontier, frontierFlags, bestCandidate->frontierOffset);
        if(!AddVisitedMeshletTriangle(nwbFilePath, metaKind, trianglePrecompute, bestCandidate->triangleIndex, localSourceVertexRefs, localTriangleIndices, meshlet, primitiveIndices, scoreState, frontier, frontierFlags))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

