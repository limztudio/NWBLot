// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "radio_group.h"

#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_radio_group_behavior{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidChoices(const RadioGroupChoices& choices)noexcept{
    if(choices.sourceGeneration == 0u || choices.sourceRevision == 0u || choices.count > s_RadioGroupMaxChoices)
        return false;
    for(u32 index = 0u; index < choices.count; ++index){
        if(choices.rows[index].key == 0u)
            return false;
        for(u32 previous = 0u; previous < index; ++previous){
            if(choices.rows[previous].key == choices.rows[index].key)
                return false;
        }
    }
    return true;
}

[[nodiscard]] static Expected<u32> FindChoice(const RadioGroupChoices& choices, const u64 key)noexcept{
    if(key == 0u)
        return MakeUnexpected(Failure{});
    for(u32 candidate = 0u; candidate < choices.count; ++candidate){
        if(choices.rows[candidate].key == key)
            return candidate;
    }
    return MakeUnexpected(Failure{});
}

[[nodiscard]] static u64 FindEnabled(const RadioGroupChoices& choices, const u32 start, const bool reverse)noexcept{
    if(choices.count == 0u || start >= choices.count)
        return 0u;
    u32 index = start;
    for(u32 visited = 0u; visited < choices.count; ++visited){
        if(choices.rows[index].enabled)
            return choices.rows[index].key;
        if(reverse)
            index = index == 0u ? choices.count - 1u : index - 1u;
        else
            index = index + 1u == choices.count ? 0u : index + 1u;
    }
    return 0u;
}

[[nodiscard]] static bool StateCurrent(
    const RadioGroupState& state,
    const RadioGroupSnapshot& snapshot,
    const bool& reentryObserved,
    const IRadioGroupReconcileGuard* const guard
){
    if(reentryObserved || !state.matches(snapshot) || (guard && !guard->current()))
        return false;
    return !reentryObserved && state.matches(snapshot);
}

[[nodiscard]] static bool SourceMatches(
    const IListDataSource& source,
    const RadioGroupState& state,
    const RadioGroupSnapshot& snapshot,
    const bool& reentryObserved,
    const RadioGroupChoices& choices,
    const IRadioGroupReconcileGuard* const guard
){
    if(!StateCurrent(state, snapshot, reentryObserved, guard))
        return false;
    const u64 generation = source.instanceGeneration();
    if(!StateCurrent(state, snapshot, reentryObserved, guard) || generation != choices.sourceGeneration)
        return false;
    const u64 revision = source.revision();
    if(!StateCurrent(state, snapshot, reentryObserved, guard) || revision != choices.sourceRevision)
        return false;
    const u64 count = source.rowCount();
    return StateCurrent(state, snapshot, reentryObserved, guard) && count == choices.count;
}

[[nodiscard]] static bool ValidOptions(const RadioGroupOptions& options)noexcept{
    return
        options.enabled && IsFinite(options.rowHeight) && options.rowHeight >= s_RadioGroupMinimumRowHeight
        && options.width.policy <= LayoutSizePolicy::Stretch && IsFinite(options.width.value) && options.width.value >= 0.0f
        && (options.width.policy != LayoutSizePolicy::Stretch || options.width.value > 0.0f)
    ;
}

[[nodiscard]] static u64 NavigationTarget(
    const RadioGroupChoices& choices, const u64 cursor, const ControlActionKind::Enum kind
)noexcept{
    if(choices.count == 0u)
        return 0u;
    if(kind == ControlActionKind::Home)
        return FindEnabled(choices, 0u, false);
    if(kind == ControlActionKind::End)
        return FindEnabled(choices, choices.count - 1u, true);
    const bool reverse = kind == ControlActionKind::Up || kind == ControlActionKind::Left;
    const auto current = FindChoice(choices, cursor);
    if(!current)
        return FindEnabled(choices, reverse ? choices.count - 1u : 0u, reverse);
    const u32 start = reverse ? (*current == 0u ? choices.count - 1u : *current - 1u)
        : (*current + 1u == choices.count ? 0u : *current + 1u);
    return FindEnabled(choices, start, reverse);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<RadioGroupChoices> RadioGroupBehavior::Reconcile(
    RadioGroupState& state,
    const IListDataSource& source,
    RadioGroupResult& result,
    const IRadioGroupReconcileGuard* const guard
){
    using namespace __hidden_ui_radio_group_behavior;
    if(state.m_reconciling){
        state.m_reentryObserved = true;
        return MakeUnexpected(Failure{});
    }
    const RadioGroupSnapshot snapshot = state.snapshot();
    state.m_reconciling = true;
    state.m_reentryObserved = false;
    ScopeExit finish([&state]()noexcept{ state.m_reconciling = false; state.m_reentryObserved = false; });

    RadioGroupChoices candidate;
    if(!StateCurrent(state, snapshot, state.m_reentryObserved, guard))
        return MakeUnexpected(Failure{});
    candidate.sourceGeneration = source.instanceGeneration();
    if(!StateCurrent(state, snapshot, state.m_reentryObserved, guard) || candidate.sourceGeneration == 0u)
        return MakeUnexpected(Failure{});
    candidate.sourceRevision = source.revision();
    if(!StateCurrent(state, snapshot, state.m_reentryObserved, guard) || candidate.sourceRevision == 0u)
        return MakeUnexpected(Failure{});
    const u64 count = source.rowCount();
    if(!StateCurrent(state, snapshot, state.m_reentryObserved, guard) || count > s_RadioGroupMaxChoices)
        return MakeUnexpected(Failure{});
    candidate.count = static_cast<u32>(count);
    if(!SourceMatches(source, state, snapshot, state.m_reentryObserved, candidate, guard))
        return MakeUnexpected(Failure{});
    for(u32 index = 0u; index < candidate.count; ++index){
        if(!StateCurrent(state, snapshot, state.m_reentryObserved, guard))
            return MakeUnexpected(Failure{});
        const u64 key = source.key(index);
        if(key == 0u || !SourceMatches(source, state, snapshot, state.m_reentryObserved, candidate, guard))
            return MakeUnexpected(Failure{});
        for(u32 previous = 0u; previous < index; ++previous){
            if(candidate.rows[previous].key == key)
                return MakeUnexpected(Failure{});
        }
        const auto resolved = source.indexOf(key);
        if(!SourceMatches(source, state, snapshot, state.m_reentryObserved, candidate, guard) || !resolved || *resolved != index)
            return MakeUnexpected(Failure{});
        const bool enabled = source.enabled(index);
        if(!SourceMatches(source, state, snapshot, state.m_reentryObserved, candidate, guard))
            return MakeUnexpected(Failure{});
        candidate.rows[index] = { key, enabled };
    }
    const bool replacement = snapshot.sourceGeneration != 0u && snapshot.sourceGeneration != candidate.sourceGeneration;
    u64 selected = replacement ? 0u : snapshot.selectedKey;
    u64 cursor = replacement ? 0u : snapshot.cursorKey;
    if(!FindChoice(candidate, selected))
        selected = 0u;
    const auto cursorIndex = FindChoice(candidate, cursor);
    if(!cursorIndex)
        cursor = FindEnabled(candidate, 0u, false);
    else if(!candidate.rows[*cursorIndex].enabled)
        cursor = FindEnabled(candidate, *cursorIndex + 1u == candidate.count ? 0u : *cursorIndex + 1u, false);
    // A second observation catches one-shot metadata mutation from the preceding final count callback.
    if(
        !SourceMatches(source, state, snapshot, state.m_reentryObserved, candidate, guard)
        || !SourceMatches(source, state, snapshot, state.m_reentryObserved, candidate, guard)
    )
        return MakeUnexpected(Failure{});
    RadioGroupResult candidateResult = result;
    candidateResult.valid = true;
    candidateResult.selectionChanged |= snapshot.selectedKey != selected;
    state.advanceRevision();
    state.m_selected = selected;
    state.m_cursor = cursor;
    state.m_sourceGeneration = candidate.sourceGeneration;
    state.m_sourceRevision = candidate.sourceRevision;
    result = candidateResult;
    return candidate;
}

bool RadioGroupBehavior::Apply(
    RadioGroupState& state,
    const RadioGroupChoices& choices,
    const RadioGroupOptions& options,
    const ControlAction& action,
    RadioGroupResult& result
){
    using namespace __hidden_ui_radio_group_behavior;
    if(state.m_reconciling){
        state.m_reentryObserved = true;
        return false;
    }
    const ControlToken token{ state.m_inputGeneration, state.m_sourceGeneration, state.m_sourceRevision };
    if(
        !ValidOptions(options) || !ValidChoices(choices) || !token.valid() || token != action.control
        || choices.sourceGeneration != token.contentGeneration || choices.sourceRevision != token.contentRevision
    )
        return false;
    u64 candidate = 0u;
    bool activate = false;
    switch(action.kind){
    case ControlActionKind::Up:
    case ControlActionKind::Down:
    case ControlActionKind::Left:
    case ControlActionKind::Right:
    case ControlActionKind::Home:
    case ControlActionKind::End:
        candidate = NavigationTarget(choices, state.m_cursor, action.kind);
        break;
    case ControlActionKind::Submit:
    case ControlActionKind::Activate:{
        candidate = action.kind == ControlActionKind::Activate ? action.value : state.m_cursor;
        const auto index = FindChoice(choices, candidate);
        if(!index || !choices.rows[*index].enabled){
            if(action.kind == ControlActionKind::Activate)
                return false;
            candidate = 0u;
        }
        activate = candidate != 0u;
        break;
    }
    case ControlActionKind::Wheel:
    case ControlActionKind::PageUp:
    case ControlActionKind::PageDown:
        break;
    default:
        return false;
    }
    RadioGroupResult candidateResult = result;
    candidateResult.valid = true;
    if(candidate != 0u){
        candidateResult.selectionChanged |= state.m_selected != candidate;
        candidateResult.activated |= activate;
    }
    state.advanceRevision();
    if(candidate != 0u){
        state.m_selected = candidate;
        state.m_cursor = candidate;
    }
    result = candidateResult;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

