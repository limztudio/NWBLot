// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "model.h"
#include "numeric_parse.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Committed doubles are finite; signed zero and every finite value survive canonical text roundtrips.
class FloatEditModel final : NoCopy{
private:
    [[nodiscard]] NumericEditResult commit(const FloatBounds& bounds, bool canonical);
    [[nodiscard]] Expected<bool> canonicalize(f64 value);
    void advanceRevision()noexcept;


public:
    explicit FloatEditModel(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] f64 value()const noexcept{ return m_value; }
    [[nodiscard]] const EditModel& draft()const noexcept{ return m_draft; }
    [[nodiscard]] NumericParseStatus::Enum status(const FloatBounds& bounds = {})const;
    [[nodiscard]] bool dirty()const;
    [[nodiscard]] u64 revision()const noexcept{ return m_revision; }
    // Neither the returned reference nor a view into it may outlive the synchronous edit operation.
    [[nodiscard]] EditModel& lendDraft()noexcept{ return m_draft; }
    // Nonfinite values fail atomically. Successful external replacements always fence pending input.
    [[nodiscard]] bool setValue(f64 value);
    [[nodiscard]] bool setDraft(AStringView text);
    [[nodiscard]] NumericEditResult submit(const FloatBounds& bounds = {});
    [[nodiscard]] NumericEditResult blur(const FloatBounds& bounds = {});
    [[nodiscard]] NumericEditResult cancel();
    [[nodiscard]] NumericEditResult abandon();


private:
    EditModel m_draft;
    AString<Core::Alloc::GlobalArena> m_acceptedDraft;
    f64 m_value = 0.0;
    u64 m_revision = 1u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

