// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>

#include <global/simd/vector.h>
#include <global/simd/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshletTrianglePrecompute{
    Core::Assets::AssetVector<MeshletTriangleData> triangles;
    Core::Assets::AssetVector<MeshletTriangleCalculation> triangleCalculations;
    Core::Assets::AssetVector<u32> positionTriangleOffsets;
    Core::Assets::AssetVector<u32> positionTriangleIndices;
    Core::Assets::AssetVector<u8> visitedTriangles;


public:
    explicit MeshletTrianglePrecompute(Core::Assets::AssetArena& arena)noexcept
        : triangles(arena)
        , triangleCalculations(arena)
        , positionTriangleOffsets(arena)
        , positionTriangleIndices(arena)
        , visitedTriangles(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-triangle cook scratch: SIMD triangle records, position adjacency, and the visited-triangle scan.
namespace MeshletTriangleGeometry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<MeshletTrianglePrecompute> PrecomputeMeshletTriangleData(const Path& nwbFilePath, const TStringView metaKind, const Span<const u32> indices, const MeshCookEntry& entry, Core::Alloc::ScratchArena& scratchArena){
    MeshletTrianglePrecompute data(entry.positions.get_allocator().arena());

    const usize triangleCount = indices.size() / s_MeshletTriangleIndexCount;
    if(triangleCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet triangle count exceeds u32 limits"), metaKind, PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    data.triangles.resize(triangleCount);
    data.triangleCalculations.resize(triangleCount);
    BuildMeshletPositionTriangleOffsets(indices, entry.vertexRefs, entry.positions.size(), data.positionTriangleOffsets);
    data.positionTriangleIndices.resize(indices.size());
    data.visitedTriangles.resize(triangleCount, 0u);

    Vector<u32, Core::Alloc::ScratchArena> positionTriangleCursor(data.positionTriangleOffsets.begin(), data.positionTriangleOffsets.end(), scratchArena);

    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        MeshletTriangleData& triangle = data.triangles[triangleIndex];
        MeshletTriangleCalculation& calculation = data.triangleCalculations[triangleIndex];
        ComputeMeshletTriangleDataAndCalculation(indices, entry.vertexRefs, entry.positions, triangleIndex, triangle, calculation);

        const u32 triangleIndexU32 = static_cast<u32>(triangleIndex);
        for(const u32 positionIndex : triangle.positions){
            const u32 adjacencyOffset = positionTriangleCursor[positionIndex]++;
            data.positionTriangleIndices[adjacencyOffset] = triangleIndexU32;
        }
    }

    return data;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

