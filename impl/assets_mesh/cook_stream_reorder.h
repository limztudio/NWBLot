// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"
#include "cook_metadata.h"
#include "binary_payload_io.h"

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


namespace MeshCookStreamReorder{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StreamVectorT>
void PrepareMeshStreamReorder(const StreamVectorT& source, StreamVectorT& outStream, ScratchVector<u32>& outRemap);
template<typename StreamVectorT>
[[nodiscard]] b8 RemapMeshStreamRef(const Name& virtualPath, TStringView metaKind, TStringView streamName, const StreamVectorT& source, Span<u32> remap, StreamVectorT& reordered, u32& index);
[[nodiscard]] b8 RemapMeshletAttributeRefs(TStringView metaKind, MeshCookEntry& entry, MeshCookCommonStreamReorder& reorder);
[[nodiscard]] b8 RemapMeshletPositionRefs(TStringView metaKind, MeshCookEntry& entry, MeshCookCommonStreamReorder& reorder);
[[nodiscard]] b8 ReorderMeshStreamsByMeshletTraversal(MeshCookEntry& entry, Core::Alloc::ScratchArena& scratchArena);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

inline b8 MeshCookStreamReorder::RemapMeshletAttributeRefs(const TStringView metaKind, MeshCookEntry& entry, MeshCookCommonStreamReorder& reorder){
    for(const MeshletDesc& meshlet : entry.meshlets){
        for(u32 localAttributeIndex = 0u; localAttributeIndex < MeshletAttributeCount(meshlet); ++localAttributeIndex){
            MeshletAttributeStreamRef& ref = entry.meshletAttributeStreamRefs[meshlet.attributeRefOffset + localAttributeIndex];
            if(!RemapMeshStreamRef(entry.virtualPath, metaKind, MeshAssetBinaryPayload::s_NormalStreamNameView, entry.normals, reorder.normalRemap, reorder.normals, ref.normal))
                return false;
            if(!RemapMeshStreamRef(entry.virtualPath, metaKind, MeshAssetBinaryPayload::s_TangentStreamNameView, entry.tangents, reorder.tangentRemap, reorder.tangents, ref.tangent))
                return false;
            if(!RemapMeshStreamRef(entry.virtualPath, metaKind, MeshAssetBinaryPayload::s_Uv0StreamNameView, entry.uv0, reorder.uv0Remap, reorder.uv0, ref.uv0))
                return false;
            if(!RemapMeshStreamRef(entry.virtualPath, metaKind, MeshAssetBinaryPayload::s_ColorStreamNameView, entry.colors, reorder.colorRemap, reorder.colors, ref.color))
                return false;
        }
    }

    return true;
}

inline b8 MeshCookStreamReorder::RemapMeshletPositionRefs(const TStringView metaKind, MeshCookEntry& entry, MeshCookCommonStreamReorder& reorder){
    for(const MeshletDesc& meshlet : entry.meshlets){
        for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(meshlet); ++localPositionIndex){
            MeshletPositionStreamRef& ref = entry.meshletPositionStreamRefs[meshlet.positionRefOffset + localPositionIndex];
            if(!RemapMeshStreamRef(entry.virtualPath, metaKind, MeshAssetBinaryPayload::s_PositionStreamNameView, entry.positions, reorder.positionRemap, reorder.positions, ref.position))
                return false;
            if(ref.skin != s_MeshMissingStreamIndex){
                NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': static meshlet position reference cannot contain skin"), metaKind, ConvertString(entry.virtualPath.resolvedText()));
                return false;
            }
        }
    }

    return true;
}

inline b8 MeshCookStreamReorder::ReorderMeshStreamsByMeshletTraversal(MeshCookEntry& entry, Core::Alloc::ScratchArena& scratchArena){
    MeshCookCommonStreamReorder reorder(entry.positions.get_allocator().arena(), scratchArena);
    PrepareMeshStreamReorder(entry.positions, reorder.positions, reorder.positionRemap);
    PrepareMeshStreamReorder(entry.normals, reorder.normals, reorder.normalRemap);
    PrepareMeshStreamReorder(entry.tangents, reorder.tangents, reorder.tangentRemap);
    PrepareMeshStreamReorder(entry.uv0, reorder.uv0, reorder.uv0Remap);
    PrepareMeshStreamReorder(entry.colors, reorder.colors, reorder.colorRemap);

    if(!RemapMeshletPositionRefs(s_MeshMetaKind, entry, reorder))
        return false;

    if(!RemapMeshletAttributeRefs(s_MeshMetaKind, entry, reorder))
        return false;

    entry.positions = Move(reorder.positions);
    entry.normals = Move(reorder.normals);
    entry.tangents = Move(reorder.tangents);
    entry.uv0 = Move(reorder.uv0);
    entry.colors = Move(reorder.colors);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

