// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_validation_diagnostics.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool MeshPayloadValidationDiagnostics::failMeshPayloadValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(GLB_TEXT("{} failed: mesh '{}' {}")
        , contextText
        , meshPathText
        , detailText
    );
    return false;
}


[[nodiscard]] bool MeshPayloadValidationDiagnostics::failMeshPayloadIndexedValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const TStringView itemText,
    const usize itemIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(GLB_TEXT("{} failed: mesh '{}' {} {} {}")
        , contextText
        , meshPathText
        , itemText
        , itemIndex
        , detailText
    );
    return false;
}


[[nodiscard]] bool MeshPayloadValidationDiagnostics::failMeshletPayloadValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(GLB_TEXT("{} failed: mesh '{}' meshlet {} {}")
        , contextText
        , meshPathText
        , meshletIndex
        , detailText
    );
    return false;
}


[[nodiscard]] bool MeshPayloadValidationDiagnostics::failMeshletAttributePayloadValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const usize attributeIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(GLB_TEXT("{} failed: mesh '{}' meshlet {} has attribute ref {} {}")
        , contextText
        , meshPathText
        , meshletIndex
        , attributeIndex
        , detailText
    );
    return false;
}


[[nodiscard]] bool MeshPayloadValidationDiagnostics::failMeshletPrimitivePayloadValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const usize primitiveIndex,
    const TStringView detailText
){
    NWB_LOGGER_ERROR(GLB_TEXT("{} failed: mesh '{}' meshlet {} primitive {} {}")
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

