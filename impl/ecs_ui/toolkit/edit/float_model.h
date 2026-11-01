// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "model.h"
#include "numeric_model_operations.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Committed doubles are finite; signed zero and every finite value survive canonical text roundtrips.
class FloatEditModel final : NoCopy{
public:
    explicit FloatEditModel(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] f64 value()const noexcept{ return m_value; }

    [[nodiscard]] const EditModel& draft()const noexcept{ return m_draft; }
    [[nodiscard]] NumericParseStatus::Enum status(const FloatBounds& bounds = {})const noexcept;
    [[nodiscard]] b8 dirty()const noexcept{ return m_draft.text() != m_acceptedDraft; }

    [[nodiscard]] u64 revision()const noexcept{ return m_revision; }
    // Neither the returned reference nor a view into it may outlive the synchronous edit operation.
    [[nodiscard]] EditModel& lendDraft()noexcept{ return m_draft; }
    // Nonfinite values fail atomically. Successful external replacements always fence pending input.
    [[nodiscard]] b8 setValue(f64 value);
    [[nodiscard]] b8 setDraft(const AStringView text){ return NumericEditDetail::SetDraftText(text, m_draft, m_revision); }
    [[nodiscard]] NumericEditResult submit(const FloatBounds& bounds = {}){ return commit(bounds, false); }
    [[nodiscard]] NumericEditResult blur(const FloatBounds& bounds = {}){ return commit(bounds, true); }
    [[nodiscard]] NumericEditResult cancel(){ return NumericEditDetail::RestoreModelDraft([this](){ return canonicalize(m_value); }, true, m_revision); }
    [[nodiscard]] NumericEditResult abandon(){ return NumericEditDetail::RestoreModelDraft([this](){ return canonicalize(m_value); }, false, m_revision); }


private:
    [[nodiscard]] NumericEditResult commit(const FloatBounds& bounds, b8 canonical);
    [[nodiscard]] Expected<b8, ErrorCode> canonicalize(f64 value);


private:
    EditModel m_draft;
    AString<Core::Alloc::GlobalArena> m_acceptedDraft;
    f64 m_value = 0.0;
    u64 m_revision = 1u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

