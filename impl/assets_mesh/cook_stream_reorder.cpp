// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_stream_reorder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshCookStreamReorder::reorderMeshStreamsByMeshletTraversal(
    MeshCookEntry& entry,
    Core::Alloc::ScratchArena& scratchArena
){
    MeshCookCommonStreamReorder reorder(entry.positions.get_allocator().arena(), scratchArena);
    prepareCommonMeshStreamReorder(entry, reorder);

    if(!remapMeshletPositionRefs(
        entry,
        s_MeshMetaKind,
        reorder,
        [&](const MeshletPositionStreamRef& ref){
            if(ref.skin == s_MeshMissingStreamIndex)
                return true;

            NWB_LOGGER_ERROR(GLB_TEXT("{} meta '{}': static meshlet position reference cannot contain skin")
                , s_MeshMetaKind
                , StringConvert(entry.virtualPath.resolvedText())
            );
            return false;
        }
    ))
        return false;

    if(!remapMeshletAttributeRefs(entry, s_MeshMetaKind, reorder))
        return false;

    commitCommonMeshStreamReorder(entry, reorder);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

