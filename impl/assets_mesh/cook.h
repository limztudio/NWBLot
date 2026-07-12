
#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include <global/core/alloc/scratch.h>
#include <global/core/alloc/thread.h>
#include <global/core/metascript/parser.h>


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
    Core::Assets::AssetVector<MeshletPositionStreamRef> meshletPositionStreamRefs;
    Core::Assets::AssetVector<MeshletAttributeStreamRef> meshletAttributeStreamRefs;
    Core::Assets::AssetVector<u8> meshletPositionRefDeltas;
    Core::Assets::AssetVector<u8> meshletAttributeRefDeltas;
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
        , meshletPositionStreamRefs(arena)
        , meshletAttributeStreamRefs(arena)
        , meshletPositionRefDeltas(arena)
        , meshletAttributeRefDeltas(arena)
        , meshletLocalVertexRefs(arena)
        , meshletPrimitiveIndices(arena)
    {}
};

[[nodiscard]] bool ParseMeshCookMetadata(
    const Path& assetRoot,
    AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    MeshCookEntry& outEntry,
    Core::Alloc::ThreadPool& threadPool,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] bool ParseMeshCookMetadata(
    Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    MeshCookEntry& outEntry,
    Core::Alloc::ThreadPool& threadPool,
    Core::Alloc::ScratchArena& scratchArena
);

[[nodiscard]] bool BuildMeshAsset(MeshCookEntry& meshEntry, Mesh& outMesh);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

