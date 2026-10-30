// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"
#include "cook_metadata.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SourceMeshStreams{
    ScratchVector<Float3U> positions;
    ScratchVector<Float3U> normals;
    ScratchVector<Float4U> tangents;
    ScratchVector<Float2U> uv0;
    ScratchVector<Float4U> colors;
    ScratchVector<MeshVertexRef> vertexRefs;
    Core::Assets::AssetVector<u32> indices;


public:
    SourceMeshStreams(Core::Assets::AssetArena& assetArena, Core::Alloc::ScratchArena& scratchArena)noexcept
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
    static Expected<ScratchVector<MeshVertexRef>> ParseSourceVertexRefs(const Path& nwbFilePath, const Core::Metascript::Value& asset, const TStringView metaKind, Core::Alloc::ScratchArena& scratchArena);
    static b8 ValidateSourceStreamIndex(const Path& nwbFilePath, const TStringView metaKind, const AStringView streamName, const u32 index, const usize streamCount);
    static b8 ValidateSourceIndexStream(const Path& nwbFilePath, const TStringView metaKind, Span<const u32> indices, const usize vertexRefCount);
    static b8 ValidateSourceVertexRefs(const Path& nwbFilePath, const TStringView metaKind, const SourceMeshStreams& streams);
    static void CopySourceStreams(const SourceMeshStreams& streams, MeshCookEntry& outEntry);
    static Expected<SourceMeshStreams> ParseCommonSourceMeshStreams(const DiscoveredNwbFile& discoveredFile, const Core::Metascript::Value& asset, const TStringView metaKind, Core::Assets::AssetArena& assetArena, Core::Alloc::ScratchArena& scratchArena);


public:
    MeshCookSourceStreams() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

