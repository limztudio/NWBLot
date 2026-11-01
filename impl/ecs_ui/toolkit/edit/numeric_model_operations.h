// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "model.h"
#include "numeric_parse.h"

#include <core/alloc/global.h>

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NumericEditDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Both numeric models share draft and bounds handling; parsing and equality stay type-specific.
template<typename Value, typename Bounds, typename Canonicalize, typename Changed>
[[nodiscard]] NumericEditResult CommitDraft(
    const Expected<Value, NumericParseStatus::Enum>& parsed,
    const Bounds& bounds,
    const b8 canonical,
    const AStringView draft,
    Canonicalize canonicalize,
    Changed changed,
    AString<Core::Alloc::GlobalArena>& acceptedDraft,
    Value& value
){
    const b8 outside = parsed && (*parsed < bounds.minimum || *parsed > bounds.maximum);
    const b8 clamped = outside && bounds.policy == NumericBoundsPolicy::Clamp;
    const b8 rejected = !parsed || (outside && !clamped);
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
[[nodiscard]] NumericEditResult RestoreDraft(Canonicalize canonicalize, const b8 cancelled)noexcept(
    []()constexpr noexcept{
        using Returned = decltype(DeclVal<Canonicalize&>()());
        using Result = Decay_T<Returned>;
        return
            noexcept(DeclVal<Canonicalize&>()())
            && IsNothrowDestructible_V<Canonicalize>
            && (!IsReference_V<Returned> || IsNothrowConvertible_V<Returned, Result>)
            && IsNothrowDestructible_V<Result>
            && noexcept(static_cast<b8>(!DeclVal<const Result&>()))
            && noexcept(DeclVal<b8&>() = *DeclVal<const Result&>())
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

void AdvanceRevision(u64& inOutRevision)noexcept;
[[nodiscard]] Expected<b8, ErrorCode> CanonicalizeDraft(const Expected<AStringView, ErrorCode>& formatted, EditModel& inOutDraft, AString<Core::Alloc::GlobalArena>& outAcceptedDraft);

template<typename Value, typename Formatter>
[[nodiscard]] inline Expected<b8, ErrorCode> FormatCanonicalDraft(const Value value, Formatter formatter, EditModel& inOutDraft, AString<Core::Alloc::GlobalArena>& outAcceptedDraft){
    Array<char, s_NumericEditMaxBytes> buffer{};
    return CanonicalizeDraft(formatter(value, buffer), inOutDraft, outAcceptedDraft);
}

template<typename Value, typename Bounds>
[[nodiscard]] inline NumericParseStatus::Enum DraftStatus(const Expected<Value, NumericParseStatus::Enum>& parsed, const Bounds& bounds)noexcept{
    if(parsed && (*parsed < bounds.minimum || *parsed > bounds.maximum))
        return NumericParseStatus::OutOfRange;
    return parsed ? NumericParseStatus::Complete : parsed.error();
}

[[nodiscard]] b8 SetDraftText(AStringView text, EditModel& inOutDraft, u64& inOutRevision);

template<typename Canonicalize>
[[nodiscard]] inline NumericEditResult RestoreModelDraft(Canonicalize canonicalize, const b8 cancelled, u64& inOutRevision)noexcept(noexcept(RestoreDraft(Move(canonicalize), cancelled))){
    NumericEditResult result = RestoreDraft(Move(canonicalize), cancelled);
    if(result.valid)
        AdvanceRevision(inOutRevision);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

