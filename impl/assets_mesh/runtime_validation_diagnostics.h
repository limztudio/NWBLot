// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshPayloadValidationDiagnostics final : NoCopy{
public:
    static void LogMeshPayloadValidation(const TStringView contextText, const TStringView meshPathText, const TStringView detailText);
    static void LogMeshPayloadIndexedValidation(const TStringView contextText, const TStringView meshPathText, const TStringView itemText, const usize itemIndex, const TStringView detailText);
    static void LogMeshletPayloadValidation(const TStringView contextText, const TStringView meshPathText, const usize meshletIndex, const TStringView detailText);
    static void LogMeshletAttributePayloadValidation(const TStringView contextText, const TStringView meshPathText, const usize meshletIndex, const usize attributeIndex, const TStringView detailText);
    static void LogMeshletPrimitivePayloadValidation(const TStringView contextText, const TStringView meshPathText, const usize meshletIndex, const usize primitiveIndex, const TStringView detailText);


public:
    MeshPayloadValidationDiagnostics() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

