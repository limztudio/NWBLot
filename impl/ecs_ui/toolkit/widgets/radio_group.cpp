// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "radio_group.h"

#include <global/simplemath.h>
#include <global/atomic_identity.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_radio_group{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextIdentity{ 1u };

[[nodiscard]] static b8 StateCurrent(const RadioGroupState& state, const RadioGroupSnapshot& snapshot, const b8& reentryObserved, const IRadioGroupReconcileGuard* const guard){
    if(reentryObserved || !state.matches(snapshot) || (guard && !guard->current()))
        return false;
    return !reentryObserved && state.matches(snapshot);
}

[[nodiscard]] static b8 SourceMatches(const IListDataSource& source, const RadioGroupState& state, const RadioGroupSnapshot& snapshot, const b8& reentryObserved, const RadioGroupChoices& choices, const IRadioGroupReconcileGuard* const guard){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RadioGroupState::RadioGroupState()noexcept
    : m_instanceGeneration(NextNonWrappingIdentity(__hidden_ui_radio_group::s_NextIdentity))
    , m_inputGeneration(NextNonWrappingIdentity(__hidden_ui_radio_group::s_NextIdentity))
{}

void RadioGroupState::select(const u64 key)noexcept{
    advanceRevision();
    m_inputGeneration = NextNonWrappingIdentity(__hidden_ui_radio_group::s_NextIdentity);
    m_selected = key;
    m_cursor = key;
}

void RadioGroupState::reset()noexcept{
    advanceRevision();
    m_inputGeneration = NextNonWrappingIdentity(__hidden_ui_radio_group::s_NextIdentity);
    m_selected = 0u;
    m_cursor = 0u;
    m_sourceGeneration = 0u;
    m_sourceRevision = 0u;
    m_placement = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<RadioGroupChoices> RadioGroupBehavior::Reconcile(const IListDataSource& source, const IRadioGroupReconcileGuard* const guard, RadioGroupState& state, RadioGroupResult& result){
    using namespace __hidden_ui_radio_group;
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
        const b8 enabled = source.enabled(index);
        if(!SourceMatches(source, state, snapshot, state.m_reentryObserved, candidate, guard))
            return MakeUnexpected(Failure{});
        candidate.rows[index] = { key, enabled };
    }
    const b8 replacement = snapshot.sourceGeneration != 0u && snapshot.sourceGeneration != candidate.sourceGeneration;
    u64 selected = replacement ? 0u : snapshot.selectedKey;
    u64 cursor = replacement ? 0u : snapshot.cursorKey;
    u32 selectedIndex = 0u;
    if(!TryFindRadioGroupChoice(candidate, selected, selectedIndex))
        selected = 0u;
    u32 cursorIndex = 0u;
    if(!TryFindRadioGroupChoice(candidate, cursor, cursorIndex))
        cursor = FindEnabledRadioGroupChoice(candidate, 0u, false);
    else if(!candidate.rows[cursorIndex].enabled)
        cursor = FindEnabledRadioGroupChoice(candidate, cursorIndex + 1u == candidate.count ? 0u : cursorIndex + 1u, false);
    // A second observation catches one-shot metadata mutation from the preceding final count callback.
    if(!SourceMatches(source, state, snapshot, state.m_reentryObserved, candidate, guard) || !SourceMatches(source, state, snapshot, state.m_reentryObserved, candidate, guard))
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

b8 RadioGroupBehavior::Apply(const RadioGroupChoices& choices, const RadioGroupOptions& options, const ControlAction& action, RadioGroupState& state, RadioGroupResult& result)noexcept{
    if(state.m_reconciling){
        state.m_reentryObserved = true;
        return false;
    }
    const ControlToken token{ state.m_inputGeneration, state.m_sourceGeneration, state.m_sourceRevision };
    if(!IsValidRadioGroupOptions(options) || !IsValidRadioGroupChoices(choices) || !token.valid() || token != action.control || choices.sourceGeneration != token.contentGeneration || choices.sourceRevision != token.contentRevision)
        return false;
    u64 candidate = 0u;
    b8 activate = false;
    switch(action.kind){
    case ControlActionKind::Up:
    case ControlActionKind::Down:
    case ControlActionKind::Left:
    case ControlActionKind::Right:
    case ControlActionKind::Home:
    case ControlActionKind::End:
        candidate = ResolveRadioGroupNavigationTarget(choices, state.m_cursor, action.kind);
        break;
    case ControlActionKind::Submit:
    case ControlActionKind::Activate:{
        candidate = action.kind == ControlActionKind::Activate ? action.value : state.m_cursor;
        u32 index = 0u;
        if(!TryFindRadioGroupChoice(choices, candidate, index) || !choices.rows[index].enabled){
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

