// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "numeric_parse.h"

#include <core/alloc/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NumericEditDetail{


// Both numeric models share draft acceptance and bounds handling; parsing, formatting, and value equality remain specific to the integer and floating-point types.
template<typename Value, typename Bounds, typename Canonicalize, typename Changed>
[[nodiscard]] NumericEditResult CommitDraft(
    Value& value,
    AString<Core::Alloc::GlobalArena>& acceptedDraft,
    const Expected<Value, NumericParseStatus::Enum>& parsed,
    const Bounds& bounds,
    const bool canonical,
    const AStringView draft,
    Canonicalize canonicalize,
    Changed changed
){
    const bool outside = parsed && (*parsed < bounds.minimum || *parsed > bounds.maximum);
    const bool clamped = outside && bounds.policy == NumericBoundsPolicy::Clamp;
    const bool rejected = !parsed || (outside && !clamped);
    NumericEditResult result;
    if(rejected){
        if(canonical){
            const auto restored = canonicalize(value);
            if(!restored)
                return result;
            result.restored = *restored;
        }
        result.valid = true;
        result.rejected = true;
        return result;
    }
    Value candidate = *parsed;
    if(clamped)
        candidate = candidate < bounds.minimum ? bounds.minimum : bounds.maximum;
    if(canonical || clamped){
        if(!canonicalize(candidate))
            return result;
    }
    else
        acceptedDraft.assign(draft.data(), draft.size());
    result.valid = true;
    result.committed = true;
    result.valueChanged = changed(candidate, value);
    result.clamped = clamped;
    value = candidate;
    return result;
}


template<typename Canonicalize>
[[nodiscard]] NumericEditResult RestoreDraft(Canonicalize canonicalize, const bool cancelled)noexcept(
    []()constexpr noexcept{
        using Returned = decltype(DeclVal<Canonicalize&>()());
        using Result = Decay_T<Returned>;
        return
            noexcept(DeclVal<Canonicalize&>()())
            && IsNothrowDestructible_V<Canonicalize>
            && (!IsReference_V<Returned> || IsNothrowConvertible_V<Returned, Result>)
            && IsNothrowDestructible_V<Result>
            && noexcept(static_cast<bool>(!DeclVal<const Result&>()))
            && noexcept(DeclVal<bool&>() = *DeclVal<const Result&>())
        ;
    }()
){
    NumericEditResult result;
    const auto restored = canonicalize();
    if(!restored)
        return result;
    result.restored = *restored;
    result.valid = true;
    result.cancelled = cancelled;
    return result;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

