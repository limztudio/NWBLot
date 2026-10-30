// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_stream_reorder.h"
#include "binary_payload_io.h"
#include "meshlet_payload_packing.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MeshCookStreamReorder::PrepareCommonMeshStreamReorder(const MeshCookEntry& entry, MeshCookCommonStreamReorder& reorder){
    PrepareMeshStreamReorder(entry.positions, reorder.positions, reorder.positionRemap);
    PrepareMeshStreamReorder(entry.normals, reorder.normals, reorder.normalRemap);
    PrepareMeshStreamReorder(entry.tangents, reorder.tangents, reorder.tangentRemap);
    PrepareMeshStreamReorder(entry.uv0, reorder.uv0, reorder.uv0Remap);
    PrepareMeshStreamReorder(entry.colors, reorder.colors, reorder.colorRemap);
}


b8 MeshCookStreamReorder::RemapMeshletAttributeRefs(const TStringView metaKind, MeshCookEntry& entry, MeshCookCommonStreamReorder& reorder){
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


b8 MeshCookStreamReorder::RemapMeshletPositionRefs(const TStringView metaKind, MeshCookEntry& entry, MeshCookCommonStreamReorder& reorder){
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


void MeshCookStreamReorder::CommitCommonMeshStreamReorder(MeshCookEntry& entry, MeshCookCommonStreamReorder& reorder)noexcept{
    entry.positions = Move(reorder.positions);
    entry.normals = Move(reorder.normals);
    entry.tangents = Move(reorder.tangents);
    entry.uv0 = Move(reorder.uv0);
    entry.colors = Move(reorder.colors);
}


b8 MeshCookStreamReorder::ReorderMeshStreamsByMeshletTraversal(MeshCookEntry& entry, Core::Alloc::ScratchArena& scratchArena){
    MeshCookCommonStreamReorder reorder(entry.positions.get_allocator().arena(), scratchArena);
    PrepareCommonMeshStreamReorder(entry, reorder);

    if(!RemapMeshletPositionRefs(s_MeshMetaKind, entry, reorder))
        return false;

    if(!RemapMeshletAttributeRefs(s_MeshMetaKind, entry, reorder))
        return false;

    CommitCommonMeshStreamReorder(entry, reorder);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

