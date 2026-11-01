// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "model.h"
#include "numeric_model_operations.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns a typed value and a bounded draft; user bounds never constrain authoritative setValue().
class IntegerEditModel final : NoCopy{
public:
    explicit IntegerEditModel(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] i64 value()const noexcept{ return m_value; }

    [[nodiscard]] const EditModel& draft()const noexcept{ return m_draft; }
    [[nodiscard]] NumericParseStatus::Enum status(const IntegerBounds& bounds = {})const noexcept;
    [[nodiscard]] b8 dirty()const noexcept{ return m_draft.text() != m_acceptedDraft; }

    [[nodiscard]] u64 revision()const noexcept{ return m_revision; }
    // Neither the returned reference nor a view into it may outlive the synchronous edit operation.
    [[nodiscard]] EditModel& lendDraft()noexcept{ return m_draft; }
    // External replacements fence pending native/clipboard intentions even when the supplied value is unchanged.
    [[nodiscard]] b8 setValue(i64 value);
    [[nodiscard]] b8 setDraft(const AStringView text){ return NumericEditDetail::SetDraftText(text, m_draft, m_revision); }
    // Submit retains complete draft bytes, caret and history except when clamping rewrites the value.
    [[nodiscard]] NumericEditResult submit(const IntegerBounds& bounds = {}){ return commit(bounds, false); }
    [[nodiscard]] NumericEditResult blur(const IntegerBounds& bounds = {}){ return commit(bounds, true); }
    [[nodiscard]] NumericEditResult cancel(){ return NumericEditDetail::RestoreModelDraft([this](){ return canonicalize(m_value); }, true, m_revision); }
    [[nodiscard]] NumericEditResult abandon(){ return NumericEditDetail::RestoreModelDraft([this](){ return canonicalize(m_value); }, false, m_revision); }


private:
    [[nodiscard]] NumericEditResult commit(const IntegerBounds& bounds, b8 canonical);
    [[nodiscard]] Expected<b8, ErrorCode> canonicalize(i64 value);


private:
    EditModel m_draft;
    AString<Core::Alloc::GlobalArena> m_acceptedDraft;
    i64 m_value = 0;
    u64 m_revision = 1u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

