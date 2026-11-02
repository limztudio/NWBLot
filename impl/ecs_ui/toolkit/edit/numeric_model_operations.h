// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "model.h"

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


template<typename TValue, typename TBounds, typename TDerived>
class NumericEditModelBase : NoCopy{
protected:
    explicit NumericEditModelBase(Core::Alloc::GlobalArena& arena)
        : m_draft(arena, { s_NumericEditMaxBytes, s_NumericEditMaxHistoryRecords, s_NumericEditMaxBytes * s_NumericEditHistoryBytesFactor })
        , m_acceptedDraft(arena)
    {
        m_acceptedDraft.reserve(s_NumericEditMaxBytes);
    }


public:
    [[nodiscard]] TValue value()const noexcept{ return m_value; }

    [[nodiscard]] const EditModel& draft()const noexcept{ return m_draft; }

    [[nodiscard]] b8 dirty()const noexcept{ return m_draft.text() != m_acceptedDraft; }

    [[nodiscard]] u64 revision()const noexcept{ return m_revision; }

    // Neither the returned reference nor a view into it may outlive the synchronous edit operation.
    [[nodiscard]] EditModel& lendDraft()noexcept{ return m_draft; }

    [[nodiscard]] b8 setDraft(const AStringView text){ return NumericEditDetail::SetDraftText(text, m_draft, m_revision); }

    [[nodiscard]] NumericEditResult submit(const TBounds& bounds = {}){ return static_cast<TDerived*>(this)->commit(bounds, false); }

    [[nodiscard]] NumericEditResult blur(const TBounds& bounds = {}){ return static_cast<TDerived*>(this)->commit(bounds, true); }

    [[nodiscard]] NumericEditResult cancel(){
        return NumericEditDetail::RestoreModelDraft([this](){ return static_cast<TDerived*>(this)->canonicalize(m_value); }, true, m_revision);
    }

    [[nodiscard]] NumericEditResult abandon(){
        return NumericEditDetail::RestoreModelDraft([this](){ return static_cast<TDerived*>(this)->canonicalize(m_value); }, false, m_revision);
    }


protected:
    void initializeDefault(const TValue initialValue){
        if(!static_cast<TDerived*>(this)->canonicalize(initialValue))
            TerminateInvariant();
    }

    template<typename TValidateBounds, typename TParseDraft>
    [[nodiscard]] NumericParseStatus::Enum evaluateDraftStatus(const TBounds& bounds, TValidateBounds validateBounds, TParseDraft parseDraft)const noexcept{
        if(!validateBounds(bounds))
            return NumericParseStatus::Invalid;
        return NumericEditDetail::DraftStatus(parseDraft(m_draft.text()), bounds);
    }

    [[nodiscard]] b8 assignAuthoritativeValue(const TValue value){
        if(!static_cast<TDerived*>(this)->canonicalize(value))
            return false;
        m_value = value;
        AdvanceRevision(m_revision);
        return true;
    }

    template<typename TValidateBounds, typename TParseDraft, typename TChanged>
    [[nodiscard]] NumericEditResult commitDraftWith(
        const TBounds& bounds,
        const b8 canonical,
        TValidateBounds validateBounds,
        TParseDraft parseDraft,
        TChanged changed
    ){
        if(!validateBounds(bounds))
            return {};
        NumericEditResult result = NumericEditDetail::CommitDraft(
            parseDraft(m_draft.text()),
            bounds,
            canonical,
            m_draft.text(),
            [this](const TValue candidate){ return static_cast<TDerived*>(this)->canonicalize(candidate); },
            Move(changed),
            m_acceptedDraft,
            m_value
        );
        if(result.valid)
            AdvanceRevision(m_revision);
        return result;
    }

    template<typename TFormatter>
    [[nodiscard]] Expected<b8, ErrorCode> canonicalizeWith(const TValue value, TFormatter formatter){
        return NumericEditDetail::FormatCanonicalDraft(value, Move(formatter), m_draft, m_acceptedDraft);
    }


protected:
    EditModel m_draft;
    AString<Core::Alloc::GlobalArena> m_acceptedDraft;
    TValue m_value = TValue{};
    u64 m_revision = 1u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

