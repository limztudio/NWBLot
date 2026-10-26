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


// Both numeric models share draft and bounds handling; parsing and equality stay type-specific.
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

inline void AdvanceRevision(u64& inOutRevision)noexcept{
    if(inOutRevision == Limit<u64>::s_Max)
        TerminateInvariant();
    ++inOutRevision;
}

[[nodiscard]] inline Expected<bool, ErrorCode> CanonicalizeDraft(const Expected<AStringView, ErrorCode>& formatted, EditModel& inOutDraft, AString<Core::Alloc::GlobalArena>& outAcceptedDraft){
    if(!formatted)
        return MakeUnexpected(formatted.error());
    const bool changed = *formatted != inOutDraft.text();
    if(!inOutDraft.setText(*formatted))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    outAcceptedDraft.assign(formatted->data(), formatted->size());
    return changed;
}

template<typename Value, typename Bounds>
[[nodiscard]] inline NumericParseStatus::Enum DraftStatus(const Expected<Value, NumericParseStatus::Enum>& parsed, const Bounds& bounds)noexcept{
    if(parsed && (*parsed < bounds.minimum || *parsed > bounds.maximum))
        return NumericParseStatus::OutOfRange;
    return parsed ? NumericParseStatus::Complete : parsed.error();
}

[[nodiscard]] inline bool DraftDirty(const EditModel& draft, const AStringView acceptedDraft)noexcept{
    return draft.text() != acceptedDraft;
}

[[nodiscard]] inline bool SetDraftText(const AStringView text, EditModel& inOutDraft, u64& inOutRevision){
    if(!inOutDraft.setText(text))
        return false;
    AdvanceRevision(inOutRevision);
    return true;
}

template<typename Canonicalize>
[[nodiscard]] inline NumericEditResult RestoreModelDraft(Canonicalize canonicalize, const bool cancelled, u64& inOutRevision)noexcept(noexcept(RestoreDraft(Move(canonicalize), cancelled))){
    NumericEditResult result = RestoreDraft(Move(canonicalize), cancelled);
    if(result.valid)
        AdvanceRevision(inOutRevision);
    return result;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

