// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"
#include "cook_metadata.h"
#include "meshlet_payload_packing.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Mesh cook meshlet-traversal stream reordering.


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

    MeshCookCommonStreamReorder(Core::Assets::AssetArena& assetArena, Core::Alloc::ScratchArena& scratchArena)
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
    static void PrepareMeshStreamReorder(
    const StreamVectorT& source,
    StreamVectorT& outStream,
    ScratchVector<u32>& outRemap
    );
    template<typename StreamVectorT>
    [[nodiscard]] static bool RemapMeshStreamRef(
    const Name& virtualPath,
    const NotNull<const tchar*> metaKind,
    const NotNull<const tchar*> streamName,
    const StreamVectorT& source,
    ScratchVector<u32>& remap,
    StreamVectorT& reordered,
    u32& index
    );
    template<typename CookEntryT>
    static void PrepareCommonMeshStreamReorder(
    const CookEntryT& entry,
    MeshCookCommonStreamReorder& reorder
    );
    template<typename CookEntryT>
    [[nodiscard]] static bool RemapMeshletAttributeRefs(
    CookEntryT& entry,
    const NotNull<const tchar*> metaKind,
    MeshCookCommonStreamReorder& reorder
    );
    template<typename CookEntryT, typename SkinRemapperT>
    [[nodiscard]] static bool RemapMeshletPositionRefs(
    CookEntryT& entry,
    const NotNull<const tchar*> metaKind,
    MeshCookCommonStreamReorder& reorder,
    SkinRemapperT remapSkin
    );
    template<typename CookEntryT>
    static void CommitCommonMeshStreamReorder(CookEntryT& entry, MeshCookCommonStreamReorder& reorder);
    [[nodiscard]] static bool ReorderMeshStreamsByMeshletTraversal(
    MeshCookEntry& entry,
    Core::Alloc::ScratchArena& scratchArena
    );


public:
    MeshCookStreamReorder() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StreamVectorT>
void MeshCookStreamReorder::PrepareMeshStreamReorder(
    const StreamVectorT& source,
    StreamVectorT& outStream,
    ScratchVector<u32>& outRemap
){
    outStream.clear();
    outStream.reserve(source.size());
    outRemap.clear();
    outRemap.resize(source.size(), s_MeshMissingStreamIndex);
}


template<typename StreamVectorT>
bool MeshCookStreamReorder::RemapMeshStreamRef(
    const Name& virtualPath,
    const NotNull<const tchar*> metaKind,
    const NotNull<const tchar*> streamName,
    const StreamVectorT& source,
    ScratchVector<u32>& remap,
    StreamVectorT& reordered,
    u32& index
){
    if(index == s_MeshMissingStreamIndex){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet {} stream reference is missing")
            , metaKind.get()
            , StringConvert(virtualPath.c_str())
            , streamName.get()
        );
        return false;
    }

    const u32 sourceIndex = index;
    if(static_cast<usize>(sourceIndex) >= source.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet {} stream reference is out of range")
            , metaKind.get()
            , StringConvert(virtualPath.c_str())
            , streamName.get()
        );
        return false;
    }

    u32 mappedIndex = remap[sourceIndex];
    if(mappedIndex == s_MeshMissingStreamIndex){
        if(reordered.size() >= static_cast<usize>(s_MeshMissingStreamIndex)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': reordered {} stream exceeds u32 index limits")
                , metaKind.get()
                , StringConvert(virtualPath.c_str())
                , streamName.get()
            );
            return false;
        }
        mappedIndex = static_cast<u32>(reordered.size());
        remap[sourceIndex] = mappedIndex;
        reordered.push_back(source[sourceIndex]);
    }

    index = mappedIndex;
    return true;
}


template<typename CookEntryT>
void MeshCookStreamReorder::PrepareCommonMeshStreamReorder(
    const CookEntryT& entry,
    MeshCookCommonStreamReorder& reorder
){
    PrepareMeshStreamReorder(entry.positions, reorder.positions, reorder.positionRemap);
    PrepareMeshStreamReorder(entry.normals, reorder.normals, reorder.normalRemap);
    PrepareMeshStreamReorder(entry.tangents, reorder.tangents, reorder.tangentRemap);
    PrepareMeshStreamReorder(entry.uv0, reorder.uv0, reorder.uv0Remap);
    PrepareMeshStreamReorder(entry.colors, reorder.colors, reorder.colorRemap);
}


template<typename CookEntryT>
bool MeshCookStreamReorder::RemapMeshletAttributeRefs(
    CookEntryT& entry,
    const NotNull<const tchar*> metaKind,
    MeshCookCommonStreamReorder& reorder
){
    for(const MeshletDesc& meshlet : entry.meshlets){
        for(u32 localAttributeIndex = 0u; localAttributeIndex < MeshletAttributeCount(meshlet); ++localAttributeIndex){
            MeshletAttributeStreamRef& ref = entry.meshletAttributeStreamRefs[meshlet.attributeRefOffset + localAttributeIndex];
            if(!RemapMeshStreamRef(
                entry.virtualPath,
                metaKind,
                MakeNotNull(NWB_TEXT("normal")),
                entry.normals,
                reorder.normalRemap,
                reorder.normals,
                ref.normal
            ))
                return false;
            if(!RemapMeshStreamRef(
                entry.virtualPath,
                metaKind,
                MakeNotNull(NWB_TEXT("tangent")),
                entry.tangents,
                reorder.tangentRemap,
                reorder.tangents,
                ref.tangent
            ))
                return false;
            if(!RemapMeshStreamRef(
                entry.virtualPath,
                metaKind,
                MakeNotNull(NWB_TEXT("uv0")),
                entry.uv0,
                reorder.uv0Remap,
                reorder.uv0,
                ref.uv0
            ))
                return false;
            if(!RemapMeshStreamRef(
                entry.virtualPath,
                metaKind,
                MakeNotNull(NWB_TEXT("color")),
                entry.colors,
                reorder.colorRemap,
                reorder.colors,
                ref.color
            ))
                return false;
        }
    }

    return true;
}


template<typename CookEntryT, typename SkinRemapperT>
bool MeshCookStreamReorder::RemapMeshletPositionRefs(
    CookEntryT& entry,
    const NotNull<const tchar*> metaKind,
    MeshCookCommonStreamReorder& reorder,
    SkinRemapperT remapSkin
){
    for(const MeshletDesc& meshlet : entry.meshlets){
        for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(meshlet); ++localPositionIndex){
            MeshletPositionStreamRef& ref = entry.meshletPositionStreamRefs[meshlet.positionRefOffset + localPositionIndex];
            if(!RemapMeshStreamRef(
                entry.virtualPath,
                metaKind,
                MakeNotNull(NWB_TEXT("position")),
                entry.positions,
                reorder.positionRemap,
                reorder.positions,
                ref.position
            ))
                return false;
            if(!remapSkin(ref))
                return false;
        }
    }

    return true;
}


template<typename CookEntryT>
void MeshCookStreamReorder::CommitCommonMeshStreamReorder(CookEntryT& entry, MeshCookCommonStreamReorder& reorder){
    entry.positions = Move(reorder.positions);
    entry.normals = Move(reorder.normals);
    entry.tangents = Move(reorder.tangents);
    entry.uv0 = Move(reorder.uv0);
    entry.colors = Move(reorder.colors);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

