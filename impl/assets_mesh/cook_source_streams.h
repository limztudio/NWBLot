// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"
#include "cook_metadata.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/metascript/parser.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Mesh cook source stream parsing and validation.


struct SourceMeshStreams{
    ScratchVector<Float3U> positions;
    ScratchVector<Float3U> normals;
    ScratchVector<Float4U> tangents;
    ScratchVector<Float2U> uv0;
    ScratchVector<Float4U> colors;
    ScratchVector<MeshVertexRef> vertexRefs;
    Core::Assets::AssetVector<u32> indices;

    SourceMeshStreams(Core::Assets::AssetArena& assetArena, Core::Alloc::ScratchArena& scratchArena)
        : positions(scratchArena)
        , normals(scratchArena)
        , tangents(scratchArena)
        , uv0(scratchArena)
        , colors(scratchArena)
        , vertexRefs(scratchArena)
        , indices(assetArena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshCookSourceStreams final : NoCopy{
public:
    static bool ParseSourceVertexRefs(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const NotNull<const tchar*> metaKind,
    const bool includeSkin,
    ScratchVector<MeshVertexRef>& outVertexRefs,
    Core::Alloc::ScratchArena& scratchArena
    );
    static bool ValidateSourceStreamIndex(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const AStringView streamName,
    const u32 index,
    const usize streamCount
    );
    static bool ValidateSourceIndexStream(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const Core::Assets::AssetVector<u32>& indices,
    const usize vertexRefCount
    );
    static bool ValidateSourceVertexRefs(
    const Path& nwbFilePath,
    const NotNull<const tchar*> metaKind,
    const bool includeSkin,
    const SourceMeshStreams& streams,
    const usize skinCount
    );
    template<typename CookEntryT>
    static void CopySourceStreams(SourceMeshStreams& streams, CookEntryT& outEntry);
    static bool ParseCommonSourceMeshStreams(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    const NotNull<const tchar*> metaKind,
    const bool includeSkin,
    SourceMeshStreams& streams,
    const usize skinCount,
    Core::Alloc::ScratchArena& scratchArena
    );


public:
    MeshCookSourceStreams() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CookEntryT>
void MeshCookSourceStreams::CopySourceStreams(SourceMeshStreams& streams, CookEntryT& outEntry){
    outEntry.positions.clear();
    outEntry.normals.clear();
    outEntry.tangents.clear();
    outEntry.uv0.clear();
    outEntry.colors.clear();
    outEntry.vertexRefs.clear();

    outEntry.positions.reserve(streams.positions.size());
    outEntry.normals.reserve(streams.normals.size());
    outEntry.tangents.reserve(streams.tangents.size());
    outEntry.uv0.reserve(streams.uv0.size());
    outEntry.colors.reserve(streams.colors.size());
    outEntry.vertexRefs.reserve(streams.vertexRefs.size());

    outEntry.positions.insert(outEntry.positions.end(), streams.positions.begin(), streams.positions.end());
    for(const Float3U& normal : streams.normals)
        outEntry.normals.push_back(MakeHalf4U(normal.x, normal.y, normal.z, 0.0f));
    for(const Float4U& tangent : streams.tangents)
        outEntry.tangents.push_back(MakeHalf4U(tangent.x, tangent.y, tangent.z, tangent.w));
    outEntry.uv0.insert(outEntry.uv0.end(), streams.uv0.begin(), streams.uv0.end());
    for(const Float4U& color : streams.colors)
        outEntry.colors.push_back(MakeHalf4U(color.x, color.y, color.z, color.w));
    outEntry.vertexRefs.insert(outEntry.vertexRefs.end(), streams.vertexRefs.begin(), streams.vertexRefs.end());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

