// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "list.h"
#include "radio_group_layout.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RadioGroupOptions{
    LayoutSize width = { LayoutSizePolicy::Stretch, 1.0f };
    b8 enabled = true;
};

struct RadioGroupResult{
    b8 valid = false;
    b8 selectionChanged = false;
    b8 activated = false;
    b8 focused = false;
};

struct RadioGroupChoice{
    u64 key = 0u;
    b8 enabled = false;
};

struct RadioGroupChoices{
    Array<RadioGroupChoice, s_RadioGroupMaxChoices> rows{};
    u64 sourceGeneration = 0u;
    u64 sourceRevision = 0u;
    u32 count = 0u;
};

struct RadioGroupSnapshot{
    u64 instanceGeneration = 0u;
    u64 inputGeneration = 0u;
    u64 revision = 0u;
    u64 selectedKey = 0u;
    u64 cursorKey = 0u;
    u64 sourceGeneration = 0u;
    u64 sourceRevision = 0u;


public:
    [[nodiscard]] b8 operator==(const RadioGroupSnapshot&)const noexcept = default;
};


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
    [[nodiscard]] RadioGroupSnapshot snapshot()const noexcept;
    [[nodiscard]] b8 matches(const RadioGroupSnapshot& value)const noexcept;
    void select(u64 key)noexcept;
    void reset()noexcept;


private:
    void advanceRevision()noexcept;


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
    [[nodiscard]] static Expected<RadioGroupChoices> Reconcile(RadioGroupState& state, RadioGroupResult& result, const IListDataSource& source, const IRadioGroupReconcileGuard* guard = nullptr);
    // Navigation consumes copied choices and never calls the source; accepted internal intents preserve the input token.
    [[nodiscard]] static b8 Apply(RadioGroupState& state, RadioGroupResult& result, const RadioGroupChoices& choices, const RadioGroupOptions& options, const ControlAction& action)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

