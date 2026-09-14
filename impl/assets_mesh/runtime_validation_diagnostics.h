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
    [[nodiscard]] static bool FailMeshPayloadValidation(
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText,
    const TStringView detailText
    );
    [[nodiscard]] static bool FailMeshPayloadIndexedValidation(
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText,
    const TStringView itemText,
    const usize itemIndex,
    const TStringView detailText
    );
    [[nodiscard]] static bool FailMeshletPayloadValidation(
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const TStringView detailText
    );
    [[nodiscard]] static bool FailMeshletAttributePayloadValidation(
    const NotNull<const tchar*> contextText,
    const TStringView meshPathText,
    const usize meshletIndex,
    const usize attributeIndex,
    const TStringView detailText
    );
    [[nodiscard]] static bool FailMeshletPrimitivePayloadValidation(
    const NotNull<const tchar*> contextText,
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

