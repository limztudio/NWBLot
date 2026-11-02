// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "list.h"
#include <impl/ecs_ui/toolkit/widgets/radio_group_style.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Application state is borrowed through its enclosing Builder scope; accepted public intents retire copied input.
class RadioGroupState final : NoCopy{
    friend class Builder;
    friend class RadioGroupBehavior;


public:
    RadioGroupState()noexcept;
    RadioGroupState(RadioGroupState&&) = delete;


public:
    RadioGroupState& operator=(RadioGroupState&&) = delete;


public:
    [[nodiscard]] u64 instanceGeneration()const noexcept{ return m_instanceGeneration; }

    [[nodiscard]] u64 inputGeneration()const noexcept{ return m_inputGeneration; }

    [[nodiscard]] u64 revision()const noexcept{ return m_revision; }

    [[nodiscard]] u64 selectedKey()const noexcept{ return m_selected; }

    [[nodiscard]] u64 cursorKey()const noexcept{ return m_cursor; }

    [[nodiscard]] const RadioGroupPlacement& placement()const noexcept{ return m_placement; }

    [[nodiscard]] RadioGroupSnapshot snapshot()const noexcept{ return { m_instanceGeneration, m_inputGeneration, m_revision, m_selected, m_cursor, m_sourceGeneration, m_sourceRevision }; }

    [[nodiscard]] b8 matches(const RadioGroupSnapshot& value)const noexcept{ return snapshot() == value; }
    void select(u64 key)noexcept;
    void reset()noexcept;


private:
    void advanceRevision()noexcept{ AdvanceRevision(m_revision); }


private:
    const u64 m_instanceGeneration;
    u64 m_inputGeneration;
    u64 m_revision = 1u;
    u64 m_selected = 0u;
    u64 m_cursor = 0u;
    u64 m_sourceGeneration = 0u;
    u64 m_sourceRevision = 0u;
    RadioGroupPlacement m_placement;
    b8 m_reconciling = false;
    b8 m_reentryObserved = false;
};

// An optional guard is borrowed only during reconciliation to stop source calls when its declaration context fails.
interface IRadioGroupReconcileGuard{
public:
    virtual ~IRadioGroupReconcileGuard()noexcept = default;


public:
    [[nodiscard]] virtual b8 current()const = 0;
};

class RadioGroupBehavior final{
public:
    // Copy the complete bounded source before atomically committing reconciliation; callbacks cannot reenter this state.
    [[nodiscard]] static Expected<RadioGroupChoices> Reconcile(const IListDataSource& source, const IRadioGroupReconcileGuard* guard, RadioGroupState& state, RadioGroupResult& result);
    // Navigation consumes copied choices and never calls the source; accepted internal intents preserve the input token.
    [[nodiscard]] static b8 Apply(const RadioGroupChoices& choices, const RadioGroupOptions& options, const ControlAction& action, RadioGroupState& state, RadioGroupResult& result)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

