// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CookEntryT>
[[nodiscard]] static bool EncodeMeshletRefs(
    CookEntryT& entry,
    const bool skinRequired,
    const tchar* metaKind
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
                , metaKind
                , StringConvert(entry.virtualPath.c_str())
                , meshletIndex
                , reason
            );
            return false;
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

