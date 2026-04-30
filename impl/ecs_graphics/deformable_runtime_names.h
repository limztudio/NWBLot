// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline AString BuildRuntimeResourceSuffix(const u64 ownerId, const u32 editRevision, const AStringView label){
    if(label.empty())
        return AString();

    char ownerBuffer[32] = {};
    char revisionBuffer[32] = {};
    const AStringView ownerText = FormatDecimal(static_cast<usize>(ownerId), ownerBuffer);
    const AStringView revisionText = FormatDecimal(static_cast<usize>(editRevision), revisionBuffer);
    if(ownerText.empty() || revisionText.empty())
        return AString();

    AString suffix;
    suffix.reserve(9u + ownerText.size() + 10u + revisionText.size() + 1u + label.size());
    suffix += ":runtime_";
    suffix.append(ownerText.data(), ownerText.size());
    suffix += "_revision_";
    suffix.append(revisionText.data(), revisionText.size());
    suffix += '_';
    suffix.append(label.data(), label.size());
    return suffix;
}

[[nodiscard]] inline Name DeriveRuntimeResourceName(
    const Name& sourceName,
    const u64 ownerId,
    const u32 editRevision,
    const AStringView label
){
    if(!sourceName || label.empty())
        return NAME_NONE;

    char ownerBuffer[32] = {};
    char revisionBuffer[32] = {};
    const AStringView ownerText = FormatDecimal(static_cast<usize>(ownerId), ownerBuffer);
    const AStringView revisionText = FormatDecimal(static_cast<usize>(editRevision), revisionBuffer);
    if(ownerText.empty() || revisionText.empty())
        return NAME_NONE;

    NameHash derivedHash = {};
    if(
        !BeginDerivedNameHash(sourceName, derivedHash)
        || !UpdateDerivedNameHashText(derivedHash, AStringView(":runtime_"))
        || !UpdateDerivedNameHashText(derivedHash, ownerText)
        || !UpdateDerivedNameHashText(derivedHash, AStringView("_revision_"))
        || !UpdateDerivedNameHashText(derivedHash, revisionText)
        || !UpdateDerivedNameHashText(derivedHash, AStringView("_"))
        || !UpdateDerivedNameHashText(derivedHash, label)
    )
        return NAME_NONE;

    return FinishDerivedNameHash(derivedHash);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

