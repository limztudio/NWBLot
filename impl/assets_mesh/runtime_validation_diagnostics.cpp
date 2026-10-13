// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_validation_diagnostics.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MeshPayloadValidationDiagnostics::LogMeshPayloadValidation(const TStringView contextText, const TStringView meshPathText, const TStringView detailText){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' {}"), contextText, meshPathText, detailText);
}


void MeshPayloadValidationDiagnostics::LogMeshPayloadIndexedValidation(const TStringView contextText, const TStringView meshPathText, const TStringView itemText, const usize itemIndex, const TStringView detailText){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' {} {} {}"), contextText, meshPathText, itemText, itemIndex, detailText);
}


void MeshPayloadValidationDiagnostics::LogMeshletPayloadValidation(const TStringView contextText, const TStringView meshPathText, const usize meshletIndex, const TStringView detailText){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} {}"), contextText, meshPathText, meshletIndex, detailText);
}


void MeshPayloadValidationDiagnostics::LogMeshletAttributePayloadValidation(const TStringView contextText, const TStringView meshPathText, const usize meshletIndex, const usize attributeIndex, const TStringView detailText){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} has attribute ref {} {}"), contextText, meshPathText, meshletIndex, attributeIndex, detailText);
}


void MeshPayloadValidationDiagnostics::LogMeshletPrimitivePayloadValidation(const TStringView contextText, const TStringView meshPathText, const usize meshletIndex, const usize primitiveIndex, const TStringView detailText){
    NWB_LOGGER_ERROR(NWB_TEXT("{} failed: mesh '{}' meshlet {} primitive {} {}"), contextText, meshPathText, meshletIndex, primitiveIndex, detailText);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

