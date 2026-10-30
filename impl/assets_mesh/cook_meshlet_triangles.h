// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"
#include "meshlet_payload_packing.h"

#include <core/alloc/scratch.h>

#include <global/simd/vector.h>
#include <global/simd/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshletTriangleVectors{
    SIMDVectorSingle positions[s_MeshletTriangleIndexCount] = {};
    SIMDVectorSingle centroid = {};
    SIMDVectorSingle areaNormal = {};
};

struct MeshletTriangleData{
    u32 vertexRefs[s_MeshletTriangleIndexCount] = {};
    u32 positions[s_MeshletTriangleIndexCount] = {};
};

// Cook-scratch record per triangle. Stays in memory, so Float storage; callers load it into vectors.
struct MeshletTriangleCalculation{
    Float4 positions[s_MeshletTriangleIndexCount];
    Float4 centroid;
    Float4 areaNormal;
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
    [[nodiscard]] static SIMDVectorSingle MakeMeshletPositionVector(const SIMDVectorSingle position)noexcept;
    [[nodiscard]] static MeshletTriangleVectors MakeMeshletTriangleVectors(const SIMDVectorSingle position0, const SIMDVectorSingle position1, const SIMDVectorSingle position2, const SIMDVectorSingle centroid, const SIMDVectorSingle areaNormal)noexcept;
    [[nodiscard]] static SIMDVectorSingle NormalizeMeshletDirectionOrZero(const SIMDVectorSingle value)noexcept;
    [[nodiscard]] static Expected<MeshletTrianglePrecompute> PrecomputeMeshletTriangleData(const Path& nwbFilePath, const TStringView metaKind, Span<const u32> indices, const MeshCookEntry& entry, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] static Expected<u32> FindNextUnvisitedMeshletTriangle(const MeshletTrianglePrecompute& trianglePrecompute, const usize searchOffset)noexcept;


public:
    MeshletTriangleGeometry() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

