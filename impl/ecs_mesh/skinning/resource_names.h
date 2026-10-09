// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinningResourceNamesDetail{
inline constexpr AStringView s_RuntimePrefix = ":runtime_";
inline constexpr AStringView s_RevisionSeparator = "_revision_";
inline constexpr AStringView s_OwnerSeparator = "_";
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Name DeriveRuntimeResourceName(
    const Name& sourceName,
    const u64 ownerId,
    const u32 editRevision,
    const AStringView label
){
    if(!sourceName || label.empty())
        return s_NameNone;

    char ownerBuffer[TextDetail::s_DecimalTextBufferBytes] = {};
    char revisionBuffer[TextDetail::s_DecimalTextBufferBytes] = {};
    const AStringView ownerText = FormatDecimal(static_cast<usize>(ownerId), ownerBuffer);
    const AStringView revisionText = FormatDecimal(static_cast<usize>(editRevision), revisionBuffer);
    if(ownerText.empty() || revisionText.empty())
        return s_NameNone;

    auto derivedHash = BeginDerivedNameHash(sourceName);
    if(!derivedHash)
        return s_NameNone;
    if(
        !UpdateDerivedNameHashText(*derivedHash, SkinningResourceNamesDetail::s_RuntimePrefix)
        || !UpdateDerivedNameHashText(*derivedHash, ownerText)
        || !UpdateDerivedNameHashText(*derivedHash, SkinningResourceNamesDetail::s_RevisionSeparator)
        || !UpdateDerivedNameHashText(*derivedHash, revisionText)
        || !UpdateDerivedNameHashText(*derivedHash, SkinningResourceNamesDetail::s_OwnerSeparator)
        || !UpdateDerivedNameHashText(*derivedHash, label)
    )
        return s_NameNone;

    return FinishDerivedNameHash(*derivedHash);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

