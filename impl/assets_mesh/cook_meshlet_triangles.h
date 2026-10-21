// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"
#include "meshlet_payload_packing.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>

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

// CPU cook-scratch record kept for every triangle. It stays in memory, so it uses Float storage; the scoring callers load it into MeshletTriangleVectors.
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

static constexpr f32 s_TriangleVertexCount = 3.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-triangle cook scratch: SIMD triangle records, position adjacency, and the visited-triangle scan.
class MeshletTriangleGeometry final : NoCopy{
public:
    [[nodiscard]] static SIMDVectorSingle MakeMeshletPositionVector(const SIMDVectorSingle position)noexcept;
    [[nodiscard]] static MeshletTriangleVectors MakeMeshletTriangleVectors(const SIMDVectorSingle position0, const SIMDVectorSingle position1, const SIMDVectorSingle position2, const SIMDVectorSingle centroid, const SIMDVectorSingle areaNormal)noexcept;
    [[nodiscard]] static SIMDVectorSingle NormalizeMeshletDirectionOrZero(const SIMDVectorSingle value)noexcept;
    template<typename CookEntryT>
    [[nodiscard]] static Expected<MeshletTrianglePrecompute> PrecomputeMeshletTriangleData(const Path& nwbFilePath, const TStringView metaKind, const Core::Assets::AssetVector<u32>& indices, const CookEntryT& entry, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] static Expected<u32> FindNextUnvisitedMeshletTriangle(const MeshletTrianglePrecompute& trianglePrecompute, const usize searchOffset)noexcept;


public:
    MeshletTriangleGeometry() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CookEntryT>
Expected<MeshletTrianglePrecompute> MeshletTriangleGeometry::PrecomputeMeshletTriangleData(const Path& nwbFilePath, const TStringView metaKind, const Core::Assets::AssetVector<u32>& indices, const CookEntryT& entry, Core::Alloc::ScratchArena& scratchArena){
    MeshletTrianglePrecompute data(entry.positions.get_allocator().arena());

    const usize triangleCount = indices.size() / s_MeshletTriangleIndexCount;
    if(triangleCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet triangle count exceeds u32 limits"), metaKind, PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    data.triangles.resize(triangleCount);
    data.triangleCalculations.resize(triangleCount);
    data.positionTriangleOffsets.resize(entry.positions.size() + 1u, 0u);
    data.positionTriangleIndices.resize(indices.size());
    data.visitedTriangles.resize(triangleCount, 0u);

    for(const u32 vertexRefIndex : indices){
        const MeshVertexRef& vertexRef = entry.vertexRefs[vertexRefIndex];
        ++data.positionTriangleOffsets[vertexRef.position + 1u];
    }

    for(usize positionIndex = 0u; positionIndex < entry.positions.size(); ++positionIndex)
        data.positionTriangleOffsets[positionIndex + 1u] += data.positionTriangleOffsets[positionIndex];

    Vector<u32, Core::Alloc::ScratchArena> positionTriangleCursor(data.positionTriangleOffsets.begin(), data.positionTriangleOffsets.end(), scratchArena);

    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        MeshletTriangleData& triangle = data.triangles[triangleIndex];
        MeshletTriangleCalculation& calculation = data.triangleCalculations[triangleIndex];
        const usize indexOffset = triangleIndex * s_MeshletTriangleIndexCount;
        triangle.vertexRefs[0] = indices[indexOffset + 0u];
        triangle.vertexRefs[1] = indices[indexOffset + 1u];
        triangle.vertexRefs[2] = indices[indexOffset + 2u];
        triangle.positions[0] = entry.vertexRefs[triangle.vertexRefs[0]].position;
        triangle.positions[1] = entry.vertexRefs[triangle.vertexRefs[1]].position;
        triangle.positions[2] = entry.vertexRefs[triangle.vertexRefs[2]].position;

        const SIMDVectorSingle p0 = MakeMeshletPositionVector(LoadFloat(entry.positions[triangle.positions[0u]]));
        const SIMDVectorSingle p1 = MakeMeshletPositionVector(LoadFloat(entry.positions[triangle.positions[1u]]));
        const SIMDVectorSingle p2 = MakeMeshletPositionVector(LoadFloat(entry.positions[triangle.positions[2u]]));
        const SIMDVectorSingle centroid = VectorScale(VectorAdd(VectorAdd(p0, p1), p2), 1.0f / s_TriangleVertexCount);
        const SIMDVectorSingle areaNormal = TriangleTests::AreaNormal(p0, p1, p2);
        StoreFloat(p0, calculation.positions[0u]);
        StoreFloat(p1, calculation.positions[1u]);
        StoreFloat(p2, calculation.positions[2u]);
        StoreFloat(VectorSetW(centroid, 0.0f), calculation.centroid);
        StoreFloat(VectorSetW(areaNormal, 0.0f), calculation.areaNormal);

        const u32 triangleIndexU32 = static_cast<u32>(triangleIndex);
        for(const u32 positionIndex : triangle.positions){
            const u32 adjacencyOffset = positionTriangleCursor[positionIndex]++;
            data.positionTriangleIndices[adjacencyOffset] = triangleIndexU32;
        }
    }

    return data;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

