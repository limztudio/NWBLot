// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Mesh payload validation failure diagnostics.


class MeshPayloadValidationDiagnostics final : NoCopy{
public:
    [[nodiscard]] static bool failMeshPayloadValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const TStringView detailText
    );
    [[nodiscard]] static bool failMeshPayloadIndexedValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const TStringView itemText,
    const usize itemIndex,
    const TStringView detailText
    );
    [[nodiscard]] static bool failMeshletPayloadValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const TStringView detailText
    );
    [[nodiscard]] static bool failMeshletAttributePayloadValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const usize attributeIndex,
    const TStringView detailText
    );
    [[nodiscard]] static bool failMeshletPrimitivePayloadValidation(
    const TStringView contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const usize primitiveIndex,
    const TStringView detailText
    );


public:
    MeshPayloadValidationDiagnostics() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

