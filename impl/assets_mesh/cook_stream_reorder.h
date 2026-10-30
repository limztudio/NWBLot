// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"
#include "cook_metadata.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshCookCommonStreamReorder{
    Core::Assets::AssetVector<Float3U> positions;
    Core::Assets::AssetVector<Half4U> normals;
    Core::Assets::AssetVector<Half4U> tangents;
    Core::Assets::AssetVector<Float2U> uv0;
    Core::Assets::AssetVector<Half4U> colors;
    ScratchVector<u32> positionRemap;
    ScratchVector<u32> normalRemap;
    ScratchVector<u32> tangentRemap;
    ScratchVector<u32> uv0Remap;
    ScratchVector<u32> colorRemap;


public:
    MeshCookCommonStreamReorder(Core::Assets::AssetArena& assetArena, Core::Alloc::ScratchArena& scratchArena)noexcept
        : positions(assetArena)
        , normals(assetArena)
        , tangents(assetArena)
        , uv0(assetArena)
        , colors(assetArena)
        , positionRemap(scratchArena)
        , normalRemap(scratchArena)
        , tangentRemap(scratchArena)
        , uv0Remap(scratchArena)
        , colorRemap(scratchArena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshCookStreamReorder final : NoCopy{
public:
    template<typename StreamVectorT>
    static void PrepareMeshStreamReorder(const StreamVectorT& source, StreamVectorT& outStream, ScratchVector<u32>& outRemap);
    template<typename StreamVectorT>
    [[nodiscard]] static b8 RemapMeshStreamRef(const Name& virtualPath, const TStringView metaKind, const TStringView streamName, const StreamVectorT& source, Span<u32> remap, StreamVectorT& reordered, u32& index);
    static void PrepareCommonMeshStreamReorder(const MeshCookEntry& entry, MeshCookCommonStreamReorder& reorder);
    [[nodiscard]] static b8 RemapMeshletAttributeRefs(const TStringView metaKind, MeshCookEntry& entry, MeshCookCommonStreamReorder& reorder);
    [[nodiscard]] static b8 RemapMeshletPositionRefs(const TStringView metaKind, MeshCookEntry& entry, MeshCookCommonStreamReorder& reorder);
    static void CommitCommonMeshStreamReorder(MeshCookEntry& entry, MeshCookCommonStreamReorder& reorder)noexcept;
    [[nodiscard]] static b8 ReorderMeshStreamsByMeshletTraversal(MeshCookEntry& entry, Core::Alloc::ScratchArena& scratchArena);


public:
    MeshCookStreamReorder() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StreamVectorT>
void MeshCookStreamReorder::PrepareMeshStreamReorder(const StreamVectorT& source, StreamVectorT& outStream, ScratchVector<u32>& outRemap){
    outStream.clear();
    outStream.reserve(source.size());
    outRemap.clear();
    outRemap.resize(source.size(), s_MeshMissingStreamIndex);
}


template<typename StreamVectorT>
b8 MeshCookStreamReorder::RemapMeshStreamRef(const Name& virtualPath, const TStringView metaKind, const TStringView streamName, const StreamVectorT& source, const Span<u32> remap, StreamVectorT& reordered, u32& index){
    if(index == s_MeshMissingStreamIndex){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet {} stream reference is missing"), metaKind, ConvertString(virtualPath.resolvedText()), streamName);
        return false;
    }

    const u32 sourceIndex = index;
    if(static_cast<usize>(sourceIndex) >= source.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet {} stream reference is out of range"), metaKind, ConvertString(virtualPath.resolvedText()), streamName);
        return false;
    }

    u32 mappedIndex = remap[sourceIndex];
    if(mappedIndex == s_MeshMissingStreamIndex){
        if(reordered.size() >= static_cast<usize>(s_MeshMissingStreamIndex)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': reordered {} stream exceeds u32 index limits"), metaKind, ConvertString(virtualPath.resolvedText()), streamName);
            return false;
        }
        mappedIndex = static_cast<u32>(reordered.size());
        remap[sourceIndex] = mappedIndex;
        reordered.push_back(source[sourceIndex]);
    }

    index = mappedIndex;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

