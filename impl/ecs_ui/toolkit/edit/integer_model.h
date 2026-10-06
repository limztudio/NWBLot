// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "model.h"
#include "numeric_parse.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns a typed value and a bounded draft; user bounds never constrain authoritative setValue().
class IntegerEditModel final : NoCopy{
private:
    [[nodiscard]] NumericEditResult commit(const IntegerBounds& bounds, bool canonical);
    [[nodiscard]] bool canonicalize(i64 value, bool* textChanged = nullptr);
    void advanceRevision()noexcept;


public:
    explicit IntegerEditModel(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] i64 value()const noexcept{ return m_value; }
    [[nodiscard]] const EditModel& draft()const noexcept{ return m_draft; }
    [[nodiscard]] NumericParseStatus::Enum status(const IntegerBounds& bounds = {})const;
    [[nodiscard]] bool dirty()const;
    [[nodiscard]] u64 revision()const noexcept{ return m_revision; }
    // Neither the returned reference nor a view into it may outlive the synchronous edit operation.
    [[nodiscard]] EditModel& lendDraft()noexcept{ return m_draft; }
    // External replacements fence pending native/clipboard intentions even when the supplied value is unchanged.
    [[nodiscard]] bool setValue(i64 value);
    [[nodiscard]] bool setDraft(AStringView text);
    // Submit retains complete draft bytes, caret and history except when clamping rewrites the value.
    [[nodiscard]] NumericEditResult submit(const IntegerBounds& bounds = {});
    [[nodiscard]] NumericEditResult blur(const IntegerBounds& bounds = {});
    [[nodiscard]] NumericEditResult cancel();
    [[nodiscard]] NumericEditResult abandon();


private:
    EditModel m_draft;
    AString<Core::Alloc::GlobalArena> m_acceptedDraft;
    i64 m_value = 0;
    u64 m_revision = 1u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

