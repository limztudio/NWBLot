// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


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
    alignas(SIMDVectorSingle) Array<SIMDVectorSingle, s_MeshletTriangleIndexCount> positions = {};
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


public:
    [[nodiscard]] MeshletTriangleVectors loadTriangleVectors(const u32 triangleIndex)const noexcept{
        const MeshletTriangleCalculation& triangleCalculation = triangleCalculations[triangleIndex];
        return MeshletTriangleVectors{
            {
                LoadFloat(triangleCalculation.positions[0u]),
                LoadFloat(triangleCalculation.positions[1u]),
                LoadFloat(triangleCalculation.positions[2u]),
            },
            LoadFloat(triangleCalculation.centroid),
            LoadFloat(triangleCalculation.areaNormal),
        };
    }

    [[nodiscard]] SIMDVectorSingle loadTriangleAreaNormal(const u32 triangleIndex)const noexcept{
        return LoadFloat(triangleCalculations[triangleIndex].areaNormal);
    }

    [[nodiscard]] auto triangleAreaNormalAt()const noexcept{
        return [this](const u32 otherTriangleIndex)noexcept{
            return loadTriangleAreaNormal(otherTriangleIndex);
        };
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-triangle cook scratch: SIMD triangle records, position adjacency, and the visited-triangle scan.
namespace MeshletTriangleGeometry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<MeshletTrianglePrecompute> PrecomputeMeshletTriangleData(const Path& nwbFilePath, TStringView metaKind, Span<const u32> indices, const MeshCookEntry& entry, Core::Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

