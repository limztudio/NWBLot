// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../../global.h"

#include <core/alloc/general.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SkinningResourceNamesDetail{
inline constexpr AStringView s_RuntimePrefix = ":runtime_";
inline constexpr AStringView s_RevisionSeparator = "_revision_";
inline constexpr AStringView s_OwnerSeparator = "_";
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline AString<Core::Alloc::GlobalArena> BuildRuntimeResourceSuffix(Core::Alloc::GlobalArena& arena, const u64 ownerId, const u32 editRevision, const AStringView label){
    AString<Core::Alloc::GlobalArena> suffix{arena};
    if(label.empty())
        return suffix;

    char ownerBuffer[TextDetail::s_DecimalTextBufferBytes] = {};
    char revisionBuffer[TextDetail::s_DecimalTextBufferBytes] = {};
    const AStringView ownerText = FormatDecimal(static_cast<usize>(ownerId), ownerBuffer);
    const AStringView revisionText = FormatDecimal(static_cast<usize>(editRevision), revisionBuffer);
    if(ownerText.empty() || revisionText.empty())
        return suffix;

    suffix.reserve(
        SkinningResourceNamesDetail::s_RuntimePrefix.size()
        + ownerText.size()
        + SkinningResourceNamesDetail::s_RevisionSeparator.size()
        + revisionText.size()
        + SkinningResourceNamesDetail::s_OwnerSeparator.size()
        + label.size()
    );
    suffix += SkinningResourceNamesDetail::s_RuntimePrefix;
    suffix.append(ownerText.data(), ownerText.size());
    suffix += SkinningResourceNamesDetail::s_RevisionSeparator;
    suffix.append(revisionText.data(), revisionText.size());
    suffix += SkinningResourceNamesDetail::s_OwnerSeparator;
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

    char ownerBuffer[TextDetail::s_DecimalTextBufferBytes] = {};
    char revisionBuffer[TextDetail::s_DecimalTextBufferBytes] = {};
    const AStringView ownerText = FormatDecimal(static_cast<usize>(ownerId), ownerBuffer);
    const AStringView revisionText = FormatDecimal(static_cast<usize>(editRevision), revisionBuffer);
    if(ownerText.empty() || revisionText.empty())
        return NAME_NONE;

    NameHash derivedHash = {};
    if(
        !BeginDerivedNameHash(sourceName, derivedHash)
        || !UpdateDerivedNameHashText(derivedHash, SkinningResourceNamesDetail::s_RuntimePrefix)
        || !UpdateDerivedNameHashText(derivedHash, ownerText)
        || !UpdateDerivedNameHashText(derivedHash, SkinningResourceNamesDetail::s_RevisionSeparator)
        || !UpdateDerivedNameHashText(derivedHash, revisionText)
        || !UpdateDerivedNameHashText(derivedHash, SkinningResourceNamesDetail::s_OwnerSeparator)
        || !UpdateDerivedNameHashText(derivedHash, label)
    )
        return NAME_NONE;

    return FinishDerivedNameHash(derivedHash);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

