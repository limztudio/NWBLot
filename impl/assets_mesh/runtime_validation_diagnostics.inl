
[[nodiscard]] static bool FailMeshPayloadValidation(
    const tchar* contextText,
    const TStringView meshPathText,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' {}")
        , contextText
        , meshPathText
        , detailText
    );
    return false;
}

[[nodiscard]] static bool FailMeshPayloadIndexedValidation(
    const tchar* contextText,
    const TStringView meshPathText,
    const TStringView itemText,
    const usize itemIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' {} {} {}")
        , contextText
        , meshPathText
        , itemText
        , itemIndex
        , detailText
    );
    return false;
}

[[nodiscard]] static bool FailMeshletPayloadValidation(
    const tchar* contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} {}")
        , contextText
        , meshPathText
        , meshletIndex
        , detailText
    );
    return false;
}

[[nodiscard]] static bool FailMeshletAttributePayloadValidation(
    const tchar* contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const usize attributeIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} has attribute ref {} {}")
        , contextText
        , meshPathText
        , meshletIndex
        , attributeIndex
        , detailText
    );
    return false;
}

[[nodiscard]] static bool FailMeshletPrimitivePayloadValidation(
    const tchar* contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const usize primitiveIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} primitive {} {}")
        , contextText
        , meshPathText
        , meshletIndex
        , primitiveIndex
        , detailText
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

