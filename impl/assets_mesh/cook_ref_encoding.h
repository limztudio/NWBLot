// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"
#include "meshlet_ref_codec.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mesh cook meshlet reference delta encoding.


class MeshCookRefEncoding final : NoCopy{
public:
    template<typename CookEntryT>
    [[nodiscard]] static bool EncodeMeshletRefs(
    CookEntryT& entry,
    const bool skinRequired,
    const NotNull<const tchar*> metaKind
    );



public:
    MeshCookRefEncoding() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CookEntryT>
bool MeshCookRefEncoding::EncodeMeshletRefs(
    CookEntryT& entry,
    const bool skinRequired,
    const NotNull<const tchar*> metaKind
){
    return EncodeMeshletRefDeltas(
        entry.meshlets,
        entry.meshletPositionStreamRefs,
        entry.meshletAttributeStreamRefs,
        entry.meshletPositionRefDeltas,
        entry.meshletAttributeRefDeltas,
        skinRequired,
        [&](const usize meshletIndex, const tchar* reason){
            NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': meshlet {} {}")
                , metaKind.get()
                , StringConvert(entry.virtualPath.c_str())
                , meshletIndex
                , reason
            );
            return false;
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

