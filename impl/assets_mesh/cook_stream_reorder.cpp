// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_stream_reorder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshCookStreamReorder::ReorderMeshStreamsByMeshletTraversal(
    MeshCookEntry& entry,
    Core::Alloc::ScratchArena& scratchArena
){
    MeshCookCommonStreamReorder reorder(entry.positions.get_allocator().arena(), scratchArena);
    PrepareCommonMeshStreamReorder(entry, reorder);

    if(!RemapMeshletPositionRefs(
        entry,
        s_MeshMetaKind,
        reorder,
        [&](const MeshletPositionStreamRef& ref){
            if(ref.skin == s_MeshMissingStreamIndex)
                return true;

            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': static meshlet position reference cannot contain skin")
                , s_MeshMetaKind.get()
                , StringConvert(entry.virtualPath.c_str())
            );
            return false;
        }
    ))
        return false;

    if(!RemapMeshletAttributeRefs(entry, s_MeshMetaKind, reorder))
        return false;

    CommitCommonMeshStreamReorder(entry, reorder);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

