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
    const TStringView metaKind
    );



public:
    MeshCookRefEncoding() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CookEntryT>
bool MeshCookRefEncoding::EncodeMeshletRefs(
    CookEntryT& entry,
    const bool skinRequired,
    const TStringView metaKind
){
    return EncodeMeshletRefDeltas(
        entry.meshlets,
        entry.meshletPositionStreamRefs,
        entry.meshletAttributeStreamRefs,
        entry.meshletPositionRefDeltas,
        entry.meshletAttributeRefDeltas,
        skinRequired,
        [&](const usize meshletIndex, const TStringView reason){
            NWB_LOGGER_ERROR(GLB_TEXT("{} meta '{}': meshlet {} {}")
                , metaKind
                , StringConvert(entry.virtualPath.resolvedText())
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

