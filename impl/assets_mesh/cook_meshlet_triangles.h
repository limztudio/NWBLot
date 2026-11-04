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


[[nodiscard]] inline Expected<MeshletTrianglePrecompute> PrecomputeMeshletTriangleData(const Path& nwbFilePath, const TStringView metaKind, const Span<const u32> indices, const MeshCookEntry& entry, [[maybe_unused]] Core::Alloc::ScratchArena& scratchArena){
    MeshletTrianglePrecompute data(entry.positions.get_allocator().arena());

    const usize triangleCount = indices.size() / s_MeshletTriangleIndexCount;
    if(triangleCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet triangle count exceeds u32 limits"), metaKind, PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    PrecomputeMeshletTriangleStreams(
        indices,
        entry.vertexRefs,
        entry.positions,
        triangleCount,
        data.triangles,
        data.triangleCalculations,
        data.positionTriangleOffsets,
        data.positionTriangleIndices,
        data.visitedTriangles
    );
    return data;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
