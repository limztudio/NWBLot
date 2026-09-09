// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool FailMeshPayloadValidation(
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' {}")
        , contextText.get()
        , meshPathText
        , detailText
    );
    return false;
}

[[nodiscard]] static bool FailMeshPayloadIndexedValidation(
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText,
    const TStringView itemText,
    const usize itemIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' {} {} {}")
        , contextText.get()
        , meshPathText
        , itemText
        , itemIndex
        , detailText
    );
    return false;
}

[[nodiscard]] static bool FailMeshletPayloadValidation(
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} {}")
        , contextText.get()
        , meshPathText
        , meshletIndex
        , detailText
    );
    return false;
}

[[nodiscard]] static bool FailMeshletAttributePayloadValidation(
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const usize attributeIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} has attribute ref {} {}")
        , contextText.get()
        , meshPathText
        , meshletIndex
        , attributeIndex
        , detailText
    );
    return false;
}

[[nodiscard]] static bool FailMeshletPrimitivePayloadValidation(
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const usize primitiveIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} primitive {} {}")
        , contextText.get()
        , meshPathText
        , meshletIndex
        , primitiveIndex
        , detailText
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

