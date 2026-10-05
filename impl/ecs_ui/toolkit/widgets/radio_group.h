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
    f32 rowHeight = s_RadioGroupMinimumRowHeight;
    bool enabled = true;
};

struct RadioGroupResult{
    bool valid = false;
    bool selectionChanged = false;
    bool activated = false;
    bool focused = false;
};

struct RadioGroupChoice{
    u64 key = 0u;
    bool enabled = false;
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

    [[nodiscard]] bool operator==(const RadioGroupSnapshot&)const = default;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Application state is borrowed through its enclosing Builder scope; accepted public intents retire copied input.
class RadioGroupState final : NoCopy{
    friend class Builder;
    friend class RadioGroupBehavior;


public:
    RadioGroupState();


public:
    RadioGroupState(RadioGroupState&&) = delete;
    RadioGroupState& operator=(RadioGroupState&&) = delete;


public:
    [[nodiscard]] u64 instanceGeneration()const{ return m_instanceGeneration; }
    [[nodiscard]] u64 inputGeneration()const{ return m_inputGeneration; }
    [[nodiscard]] u64 revision()const{ return m_revision; }
    [[nodiscard]] u64 selectedKey()const{ return m_selected; }
    [[nodiscard]] u64 cursorKey()const{ return m_cursor; }
    [[nodiscard]] const RadioGroupPlacement& placement()const{ return m_placement; }
    [[nodiscard]] RadioGroupSnapshot snapshot()const;
    [[nodiscard]] bool matches(const RadioGroupSnapshot& snapshot)const;
    void select(u64 key);
    void reset();


private:
    void advanceRevision();


private:
    const u64 m_instanceGeneration;
    u64 m_inputGeneration;
    u64 m_revision = 1u;
    u64 m_selected = 0u;
    u64 m_cursor = 0u;
    u64 m_sourceGeneration = 0u;
    u64 m_sourceRevision = 0u;
    RadioGroupPlacement m_placement;
    bool m_reconciling = false;
    bool m_reentryObserved = false;
};

// An optional guard is borrowed only during reconciliation to stop source calls when its declaration context fails.
interface IRadioGroupReconcileGuard{
public:
    virtual ~IRadioGroupReconcileGuard() = default;


public:
    [[nodiscard]] virtual bool current()const = 0;
};

class RadioGroupBehavior final{
public:
    // Copy the complete bounded source before atomically committing reconciliation; callbacks cannot reenter this state.
    [[nodiscard]] static bool reconcile(
        RadioGroupState& state,
        const IListDataSource& source,
        RadioGroupChoices& choices,
        RadioGroupResult& result,
        const IRadioGroupReconcileGuard* guard = nullptr
    );
    // Navigation consumes copied choices and never calls the source; accepted internal intents preserve the input token.
    [[nodiscard]] static bool apply(
        RadioGroupState& state,
        const RadioGroupChoices& choices,
        const RadioGroupOptions& options,
        const ControlAction& action,
        RadioGroupResult& result
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

