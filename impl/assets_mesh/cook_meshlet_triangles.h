// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"

#include <core/alloc/scratch.h>

#include <global/simd/vector.h>
#include <global/simd/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshletTriangleVectors{
    Array<SIMDVectorSingle, s_MeshletTriangleIndexCount> positions = {};
    SIMDVectorSingle centroid = {};
    SIMDVectorSingle areaNormal = {};
};

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
class MeshletTriangleGeometry final : NoCopy{
public:
    [[nodiscard]] static Expected<MeshletTrianglePrecompute> PrecomputeMeshletTriangleData(const Path& nwbFilePath, const TStringView metaKind, Span<const u32> indices, const MeshCookEntry& entry, Core::Alloc::ScratchArena& scratchArena);


public:
    MeshletTriangleGeometry() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

