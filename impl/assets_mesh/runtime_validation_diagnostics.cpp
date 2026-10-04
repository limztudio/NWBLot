// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_validation_diagnostics.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool MeshPayloadValidationDiagnostics::FailMeshPayloadValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(GLOBAL_TEXT("{} failed: mesh '{}' {}")
        , contextText
        , meshPathText
        , detailText
    );
    return false;
}


[[nodiscard]] bool MeshPayloadValidationDiagnostics::FailMeshPayloadIndexedValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const TStringView itemText,
    const usize itemIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(GLOBAL_TEXT("{} failed: mesh '{}' {} {} {}")
        , contextText
        , meshPathText
        , itemText
        , itemIndex
        , detailText
    );
    return false;
}


[[nodiscard]] bool MeshPayloadValidationDiagnostics::FailMeshletPayloadValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(GLOBAL_TEXT("{} failed: mesh '{}' meshlet {} {}")
        , contextText
        , meshPathText
        , meshletIndex
        , detailText
    );
    return false;
}


[[nodiscard]] bool MeshPayloadValidationDiagnostics::FailMeshletAttributePayloadValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const usize attributeIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(GLOBAL_TEXT("{} failed: mesh '{}' meshlet {} has attribute ref {} {}")
        , contextText
        , meshPathText
        , meshletIndex
        , attributeIndex
        , detailText
    );
    return false;
}


[[nodiscard]] bool MeshPayloadValidationDiagnostics::FailMeshletPrimitivePayloadValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const usize primitiveIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(GLOBAL_TEXT("{} failed: mesh '{}' meshlet {} primitive {} {}")
        , contextText
        , meshPathText
        , meshletIndex
        , primitiveIndex
        , detailText
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

