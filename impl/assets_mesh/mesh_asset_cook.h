// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_mesh_asset.h"
#include "mesh_asset.h"

#include <core/alloc/scratch.h>
#include <core/mesh/mesh_class.h>
#include <core/metascript/parser.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshVertexRef{
    u32 position = s_MeshMissingStreamIndex;
    u32 normal = s_MeshMissingStreamIndex;
    u32 tangent = s_MeshMissingStreamIndex;
    u32 uv0 = s_MeshMissingStreamIndex;
    u32 color = s_MeshMissingStreamIndex;
    u32 skin = s_MeshMissingStreamIndex;
};

struct MeshCookEntry{
    Name virtualPath = NAME_NONE;
    Core::Assets::AssetVector<Float3U> positions;
    Core::Assets::AssetVector<Half4U> normals;
    Core::Assets::AssetVector<Half4U> tangents;
    Core::Assets::AssetVector<Float2U> uv0;
    Core::Assets::AssetVector<Half4U> colors;
    Core::Assets::AssetVector<MeshVertexRef> vertexRefs;
    Core::Assets::AssetVector<MeshletDesc> meshlets;
    Core::Assets::AssetVector<MeshletBounds> meshletBounds;
    Core::Assets::AssetVector<MeshletDeformedPositionRef> meshletPositionRefs;
    Core::Assets::AssetVector<MeshletShadingAttributeRef> meshletAttributeRefs;
    Core::Assets::AssetVector<MeshletLocalVertexRef> meshletLocalVertexRefs;
    Core::Assets::AssetVector<u8> meshletPrimitiveIndices;

    explicit MeshCookEntry(Core::Assets::AssetArena& arena)
        : positions(arena)
        , normals(arena)
        , tangents(arena)
        , uv0(arena)
        , colors(arena)
        , vertexRefs(arena)
        , meshlets(arena)
        , meshletBounds(arena)
        , meshletPositionRefs(arena)
        , meshletAttributeRefs(arena)
        , meshletLocalVertexRefs(arena)
        , meshletPrimitiveIndices(arena)
    {}
};

struct SkinnedMeshCookEntry{
    Name virtualPath = NAME_NONE;
    u32 meshClass = Core::Mesh::MeshClass::Skinned;
    Core::Assets::AssetVector<Float3U> positions;
    Core::Assets::AssetVector<Half4U> normals;
    Core::Assets::AssetVector<Half4U> tangents;
    Core::Assets::AssetVector<Float2U> uv0;
    Core::Assets::AssetVector<Half4U> colors;
    Core::Assets::AssetVector<SkinInfluence4> skin;
    u32 skeletonJointCount = 0u;
    Core::Assets::AssetVector<SkinnedMeshJointMatrix> inverseBindMatrices;
    Core::Assets::AssetVector<MeshVertexRef> vertexRefs;
    Core::Assets::AssetVector<MeshletDesc> meshlets;
    Core::Assets::AssetVector<MeshletBounds> meshletBounds;
    Core::Assets::AssetVector<MeshletDeformedPositionRef> meshletPositionRefs;
    Core::Assets::AssetVector<MeshletShadingAttributeRef> meshletAttributeRefs;
    Core::Assets::AssetVector<MeshletLocalVertexRef> meshletLocalVertexRefs;
    Core::Assets::AssetVector<u8> meshletPrimitiveIndices;

    explicit SkinnedMeshCookEntry(Core::Assets::AssetArena& arena)
        : positions(arena)
        , normals(arena)
        , tangents(arena)
        , uv0(arena)
        , colors(arena)
        , skin(arena)
        , inverseBindMatrices(arena)
        , vertexRefs(arena)
        , meshlets(arena)
        , meshletBounds(arena)
        , meshletPositionRefs(arena)
        , meshletAttributeRefs(arena)
        , meshletLocalVertexRefs(arena)
        , meshletPrimitiveIndices(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ParseMeshCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    MeshCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] bool ParseSkinnedMeshCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    SkinnedMeshCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
);

[[nodiscard]] bool BuildMeshAsset(MeshCookEntry& meshEntry, Mesh& outMesh);
[[nodiscard]] bool BuildSkinnedMeshAsset(SkinnedMeshCookEntry& meshEntry, SkinnedMesh& outMesh);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

