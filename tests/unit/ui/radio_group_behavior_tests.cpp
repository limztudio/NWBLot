// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/radio_group.h>

#include <global/simplemath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_radio_group_behavior_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;

namespace CallbackPhase{
    enum Enum : u8{ None, Generation, Revision, Count, Key, Lookup, Enabled };
};

namespace CallbackMutation{
    enum Enum : u8{
        SelectIdentical, SelectAba, Reset, Reconcile, Apply, SourceRevision, SourceGeneration, SourceCount, GuardFailure
    };
};

class ReconcileGate final : public IRadioGroupReconcileGuard{
public:
    [[nodiscard]] virtual bool current()const override{
        if(mutateState && state){
            mutateState = false;
            state->select(state->selectedKey());
        }
        return allowed;
    }


public:
    bool allowed = true;
    RadioGroupState* state = nullptr;
    mutable bool mutateState = false;
};

class ChoiceSource final : public IListDataSource{
public:
    ChoiceSource(){
        for(u32 index = 0u; index < s_RadioGroupMaxChoices; ++index)
            rows[index] = { index + 1u, true };
    }


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{
        callback(CallbackPhase::Generation);
        return generation;
    }
    [[nodiscard]] virtual u64 revision()const override{
        callback(CallbackPhase::Revision);
        return revisionValue;
    }
    [[nodiscard]] virtual u64 rowCount()const override{
        ++countCalls;
        callback(CallbackPhase::Count);
        return count;
    }
    [[nodiscard]] virtual u64 key(const u64 index)const override{
        callback(CallbackPhase::Key);
        ++keyCalls;
        return index < s_RadioGroupMaxChoices ? rows[static_cast<usize>(index)].key : 0u;
    }
    [[nodiscard]] virtual bool indexOf(const u64 key, u64& index)const override{
        callback(CallbackPhase::Lookup);
        for(u32 candidate = 0u; candidate < Min<u64>(count, s_RadioGroupMaxChoices); ++candidate){
            if(rows[candidate].key == key){
                index = badLookup ? count : candidate;
                return !missingLookup;
            }
        }
        return false;
    }
    [[nodiscard]] virtual bool findEnabled(const u64 start, const bool reverse, u64& index)const override{
        ++findCalls;
        if(start >= count || count > s_RadioGroupMaxChoices)
            return false;
        index = start;
        for(u64 visited = 0u; visited < count; ++visited){
            if(rows[static_cast<usize>(index)].enabled)
                return true;
            if(reverse){
                if(index == 0u)
                    return false;
                --index;
            }
            else{
                ++index;
                if(index == count)
                    return false;
            }
        }
        return false;
    }
    [[nodiscard]] virtual StringView text(u64)const override{ return "choice"; }
    [[nodiscard]] virtual bool enabled(const u64 index)const override{
        callback(CallbackPhase::Enabled);
        return index < s_RadioGroupMaxChoices && rows[static_cast<usize>(index)].enabled;
    }


private:
    void callback(const CallbackPhase::Enum current)const{
        ++callbackCalls;
        if(!armed || phase != current || (triggerCount != 0u && countCalls != triggerCount))
            return;
        armed = false;
        callbacksAtMutation = callbackCalls;
        if(mutation == CallbackMutation::GuardFailure){
            if(gate)
                gate->allowed = false;
            return;
        }
        if(mutation == CallbackMutation::SourceRevision){
            ++revisionValue;
            return;
        }
        if(mutation == CallbackMutation::SourceGeneration){
            ++generation;
            return;
        }
        if(mutation == CallbackMutation::SourceCount){
            ++count;
            return;
        }
        if(!state)
            return;
        switch(mutation){
        case CallbackMutation::SelectIdentical:
            state->select(state->selectedKey());
            break;
        case CallbackMutation::SelectAba:{
            const u64 original = state->selectedKey();
            state->select(original == 1u ? 2u : 1u);
            state->select(original);
            break;
        }
        case CallbackMutation::Reset:
            state->reset();
            break;
        case CallbackMutation::Reconcile:{
            RadioGroupChoices nestedChoices;
            RadioGroupResult nestedResult;
            nestedSucceeded = RadioGroupBehavior::reconcile(*state, *this, nestedChoices, nestedResult);
            break;
        }
        case CallbackMutation::Apply:{
            ControlAction action;
            const RadioGroupSnapshot snapshot = state->snapshot();
            action.control = { snapshot.inputGeneration, snapshot.sourceGeneration, snapshot.sourceRevision };
            action.kind = ControlActionKind::Down;
            RadioGroupResult nestedResult;
            nestedSucceeded = RadioGroupBehavior::apply(*state, borrowedChoices, {}, action, nestedResult);
            break;
        }
        default:
            break;
        }
    }


public:
    Array<RadioGroupChoice, s_RadioGroupMaxChoices> rows{};
    mutable u64 generation = 41u;
    mutable u64 revisionValue = 7u;
    mutable u64 count = 4u;
    bool badLookup = false;
    bool missingLookup = false;
    RadioGroupState* state = nullptr;
    ReconcileGate* gate = nullptr;
    RadioGroupChoices borrowedChoices;
    CallbackPhase::Enum phase = CallbackPhase::None;
    CallbackMutation::Enum mutation = CallbackMutation::SelectIdentical;
    mutable bool armed = false;
    mutable bool nestedSucceeded = true;
    mutable u32 keyCalls = 0u;
    mutable u32 findCalls = 0u;
    mutable u32 countCalls = 0u;
    mutable u32 callbackCalls = 0u;
    mutable u32 callbacksAtMutation = 0u;
    u32 triggerCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static ControlAction Action(const RadioGroupState& state, const ControlActionKind::Enum kind, const u64 key = 0u){
    const RadioGroupSnapshot snapshot = state.snapshot();
    ControlAction action;
    action.control = { snapshot.inputGeneration, snapshot.sourceGeneration, snapshot.sourceRevision };
    action.kind = kind;
    action.value = key;
    return action;
}

static void ExpectResult(const RadioGroupResult& actual, const RadioGroupResult& expected){
    EXPECT_EQ(actual.valid, expected.valid);
    EXPECT_EQ(actual.selectionChanged, expected.selectionChanged);
    EXPECT_EQ(actual.activated, expected.activated);
    EXPECT_EQ(actual.focused, expected.focused);
}

static void ExpectChoices(const RadioGroupChoices& actual, const RadioGroupChoices& expected){
    EXPECT_EQ(actual.sourceGeneration, expected.sourceGeneration);
    EXPECT_EQ(actual.sourceRevision, expected.sourceRevision);
    EXPECT_EQ(actual.count, expected.count);
    for(u32 index = 0u; index < s_RadioGroupMaxChoices; ++index){
        EXPECT_EQ(actual.rows[index].key, expected.rows[index].key);
        EXPECT_EQ(actual.rows[index].enabled, expected.rows[index].enabled);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiRadioGroupBehaviorTests, DisabledCheckedChoiceIsPreservedWhileCursorMovesForward){
    ChoiceSource source;
    source.rows[1u].enabled = false;
    source.rows[2u].enabled = false;
    RadioGroupState state;
    state.select(2u);
    RadioGroupChoices choices;
    RadioGroupResult result;
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    EXPECT_EQ(state.selectedKey(), 2u);
    EXPECT_EQ(state.cursorKey(), 4u);
    EXPECT_FALSE(result.selectionChanged);
    EXPECT_FALSE(choices.rows[1u].enabled);
}

TEST(UiRadioGroupBehaviorTests, RevisionReorderPreservesStableKeys){
    ChoiceSource source;
    RadioGroupState state;
    state.select(3u);
    RadioGroupChoices choices;
    RadioGroupResult result;
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    Swap(source.rows[0u], source.rows[2u]);
    ++source.revisionValue;
    result = {};
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    EXPECT_EQ(state.selectedKey(), 3u);
    EXPECT_EQ(state.cursorKey(), 3u);
    EXPECT_FALSE(result.selectionChanged);
    EXPECT_EQ(choices.rows[0u].key, 3u);
}

TEST(UiRadioGroupBehaviorTests, RemovedSelectionAndSourceReplacementClearKnownChoices){
    ChoiceSource source;
    RadioGroupState state;
    state.select(4u);
    RadioGroupChoices choices;
    RadioGroupResult result;
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    source.count = 3u;
    ++source.revisionValue;
    result = {};
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    EXPECT_EQ(state.selectedKey(), 0u);
    EXPECT_EQ(state.cursorKey(), 1u);
    EXPECT_TRUE(result.selectionChanged);
    state.select(2u);
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    ++source.generation;
    result = {};
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    EXPECT_EQ(state.selectedKey(), 0u);
    EXPECT_EQ(state.cursorKey(), 1u);
    EXPECT_TRUE(result.selectionChanged);
}

TEST(UiRadioGroupBehaviorTests, ResetAllowsExplicitChoiceOnANewFirstBinding){
    ChoiceSource source;
    RadioGroupState state;
    RadioGroupChoices choices;
    RadioGroupResult result;
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    state.reset();
    state.select(3u);
    ++source.generation;
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    EXPECT_EQ(state.selectedKey(), 3u);
}

TEST(UiRadioGroupBehaviorTests, EmptyAndAllDisabledSourcesAreValidWithoutAutomaticSelection){
    ChoiceSource source;
    RadioGroupState state;
    RadioGroupChoices choices;
    RadioGroupResult result;
    source.count = 0u;
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(state.selectedKey(), 0u);
    EXPECT_EQ(state.cursorKey(), 0u);
    source.count = 4u;
    for(u32 index = 0u; index < 4u; ++index)
        source.rows[index].enabled = false;
    ++source.revisionValue;
    state.select(2u);
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    EXPECT_EQ(state.selectedKey(), 2u);
    EXPECT_EQ(state.cursorKey(), 0u);
    result = {};
    ASSERT_TRUE(RadioGroupBehavior::apply(state, choices, {}, Action(state, ControlActionKind::Submit), result));
    EXPECT_FALSE(result.activated);
    EXPECT_FALSE(result.selectionChanged);
}

TEST(UiRadioGroupBehaviorTests, AllFourArrowsWrapAndSkipDisabledChoices){
    ChoiceSource source;
    source.rows[1u].enabled = false;
    source.rows[2u].enabled = false;
    RadioGroupState state;
    state.select(1u);
    RadioGroupChoices choices;
    RadioGroupResult result;
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    const u64 input = state.inputGeneration();
    const ControlActionKind::Enum kinds[]{ ControlActionKind::Up, ControlActionKind::Right, ControlActionKind::Left,
        ControlActionKind::Down };
    const u64 expected[]{ 4u, 1u, 4u, 1u };
    for(u32 index = 0u; index < 4u; ++index){
        result = {};
        ASSERT_TRUE(RadioGroupBehavior::apply(state, choices, {}, Action(state, kinds[index]), result));
        EXPECT_EQ(state.selectedKey(), expected[index]);
        EXPECT_EQ(state.cursorKey(), expected[index]);
        EXPECT_EQ(state.inputGeneration(), input);
        EXPECT_TRUE(result.selectionChanged);
        EXPECT_FALSE(result.activated);
    }
}

TEST(UiRadioGroupBehaviorTests, HomeEndChooseEnabledEndpoints){
    ChoiceSource source;
    source.rows[0u].enabled = false;
    source.rows[3u].enabled = false;
    RadioGroupState state;
    RadioGroupChoices choices;
    RadioGroupResult result;
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    ASSERT_TRUE(RadioGroupBehavior::apply(state, choices, {}, Action(state, ControlActionKind::End), result));
    EXPECT_EQ(state.selectedKey(), 3u);
    ASSERT_TRUE(RadioGroupBehavior::apply(state, choices, {}, Action(state, ControlActionKind::Home), result));
    EXPECT_EQ(state.selectedKey(), 2u);
    EXPECT_FALSE(result.activated);
}

TEST(UiRadioGroupBehaviorTests, SubmitAndPointerActivateReportActivationEvenForAnIdenticalChoice){
    ChoiceSource source;
    RadioGroupState state;
    state.select(2u);
    RadioGroupChoices choices;
    RadioGroupResult result;
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    const u64 input = state.inputGeneration();
    const u64 revision = state.revision();
    result = {};
    ASSERT_TRUE(RadioGroupBehavior::apply(state, choices, {}, Action(state, ControlActionKind::Submit), result));
    EXPECT_TRUE(result.activated);
    EXPECT_FALSE(result.selectionChanged);
    EXPECT_EQ(state.revision(), revision + 1u);
    result = {};
    ASSERT_TRUE(RadioGroupBehavior::apply(state, choices, {}, Action(state, ControlActionKind::Activate, 2u), result));
    EXPECT_TRUE(result.activated);
    EXPECT_FALSE(result.selectionChanged);
    EXPECT_EQ(state.inputGeneration(), input);
    EXPECT_EQ(state.revision(), revision + 2u);
}

TEST(UiRadioGroupBehaviorTests, IdenticalNavigationAndIgnoredPageIntentAdvanceRevisionWithoutRetiringInput){
    ChoiceSource source;
    source.count = 1u;
    RadioGroupState state;
    state.select(1u);
    RadioGroupChoices choices;
    RadioGroupResult result;
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    const RadioGroupSnapshot before = state.snapshot();
    ASSERT_TRUE(RadioGroupBehavior::apply(state, choices, {}, Action(state, ControlActionKind::Down), result));
    ASSERT_TRUE(RadioGroupBehavior::apply(state, choices, {}, Action(state, ControlActionKind::PageUp), result));
    EXPECT_EQ(state.selectedKey(), 1u);
    EXPECT_EQ(state.inputGeneration(), before.inputGeneration);
    EXPECT_EQ(state.revision(), before.revision + 2u);
    EXPECT_FALSE(result.selectionChanged);
    EXPECT_FALSE(result.activated);
}

TEST(UiRadioGroupBehaviorTests, RejectedActionsPreserveStateAndResult){
    ChoiceSource source;
    source.rows[1u].enabled = false;
    RadioGroupState state;
    state.select(1u);
    RadioGroupChoices choices;
    RadioGroupResult result;
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    const RadioGroupSnapshot before = state.snapshot();
    const RadioGroupResult expected{ true, true, true, true };
    result = expected;
    EXPECT_FALSE(RadioGroupBehavior::apply(state, choices, {}, Action(state, ControlActionKind::Activate, 2u), result));
    EXPECT_TRUE(state.matches(before));
    ExpectResult(result, expected);
    RadioGroupOptions disabled;
    disabled.enabled = false;
    EXPECT_FALSE(RadioGroupBehavior::apply(state, choices, disabled, Action(state, ControlActionKind::Down), result));
    EXPECT_TRUE(state.matches(before));
    ExpectResult(result, expected);
    RadioGroupOptions tooSmall;
    tooSmall.rowHeight = 31.0f;
    EXPECT_FALSE(RadioGroupBehavior::apply(state, choices, tooSmall, Action(state, ControlActionKind::Down), result));
    EXPECT_TRUE(state.matches(before));
    ExpectResult(result, expected);
    ControlAction stale = Action(state, ControlActionKind::Down);
    ++stale.control.contentRevision;
    EXPECT_FALSE(RadioGroupBehavior::apply(state, choices, {}, stale, result));
    EXPECT_TRUE(state.matches(before));
    ExpectResult(result, expected);
}

TEST(UiRadioGroupBehaviorTests, IdenticalPublicSelectionRejectsCopiedOlderInput){
    ChoiceSource source;
    RadioGroupState state;
    state.select(2u);
    RadioGroupChoices choices;
    RadioGroupResult result;
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    const ControlAction action = Action(state, ControlActionKind::Down);
    state.select(2u);
    const RadioGroupSnapshot before = state.snapshot();
    result = {};
    EXPECT_FALSE(RadioGroupBehavior::apply(state, choices, {}, action, result));
    EXPECT_TRUE(state.matches(before));
    EXPECT_FALSE(result.valid);
}

TEST(UiRadioGroupBehaviorTests, FullBoundIsAcceptedAndOversizedSourcePreservesEveryOutput){
    ChoiceSource source;
    source.count = s_RadioGroupMaxChoices;
    RadioGroupState state;
    RadioGroupChoices choices;
    RadioGroupResult result;
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    EXPECT_EQ(choices.count, s_RadioGroupMaxChoices);
    const RadioGroupSnapshot before = state.snapshot();
    const RadioGroupChoices expected = choices;
    const RadioGroupResult expectedResult = result;
    source.count = s_RadioGroupMaxChoices + 1u;
    source.keyCalls = 0u;
    EXPECT_FALSE(RadioGroupBehavior::reconcile(state, source, choices, result));
    EXPECT_TRUE(state.matches(before));
    ExpectChoices(choices, expected);
    ExpectResult(result, expectedResult);
    EXPECT_EQ(source.keyCalls, 0u);
}

TEST(UiRadioGroupBehaviorTests, MalformedKeysAndLookupsRejectAtomically){
    const u32 modes[]{ 0u, 1u, 2u, 3u, 4u, 5u };
    for(const u32 mode : modes){
        ChoiceSource source;
        RadioGroupState state;
        state.select(3u);
        RadioGroupChoices choices;
        choices.sourceGeneration = 99u;
        choices.sourceRevision = 89u;
        choices.count = 1u;
        choices.rows[0u] = { 78u, true };
        RadioGroupResult result{ true, true, true, true };
        const RadioGroupSnapshot before = state.snapshot();
        const RadioGroupChoices expected = choices;
        const RadioGroupResult expectedResult = result;
        switch(mode){
        case 0u: source.rows[0u].key = 0u; break;
        case 1u: source.rows[1u].key = source.rows[0u].key; break;
        case 2u: source.badLookup = true; break;
        case 3u: source.missingLookup = true; break;
        case 4u: source.generation = 0u; break;
        case 5u: source.revisionValue = 0u; break;
        }
        EXPECT_FALSE(RadioGroupBehavior::reconcile(state, source, choices, result));
        EXPECT_TRUE(state.matches(before));
        ExpectChoices(choices, expected);
        ExpectResult(result, expectedResult);
    }
}

TEST(UiRadioGroupBehaviorTests, EverySourceCallbackRejectsStateSelectionAbaBeforeUsingItsResult){
    const CallbackPhase::Enum phases[]{ CallbackPhase::Generation, CallbackPhase::Revision, CallbackPhase::Count,
        CallbackPhase::Key, CallbackPhase::Lookup, CallbackPhase::Enabled };
    for(const CallbackPhase::Enum phase : phases){
        ChoiceSource source;
        RadioGroupState state;
        state.select(2u);
        RadioGroupChoices choices;
        RadioGroupResult result;
        ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
        const RadioGroupSnapshot before = state.snapshot();
        const RadioGroupChoices expected = choices;
        const RadioGroupResult expectedResult = result;
        source.state = &state;
        source.phase = phase;
        source.mutation = CallbackMutation::SelectAba;
        source.armed = true;
        EXPECT_FALSE(RadioGroupBehavior::reconcile(state, source, choices, result));
        EXPECT_EQ(state.selectedKey(), before.selectedKey);
        EXPECT_EQ(state.cursorKey(), before.cursorKey);
        EXPECT_EQ(state.revision(), before.revision + 2u);
        EXPECT_NE(state.inputGeneration(), before.inputGeneration);
        ExpectChoices(choices, expected);
        ExpectResult(result, expectedResult);
    }
}

TEST(UiRadioGroupBehaviorTests, CallbackIdenticalSelectionAndResetRemainAuthoritativeAfterRejection){
    const CallbackMutation::Enum mutations[]{ CallbackMutation::SelectIdentical, CallbackMutation::Reset };
    for(const CallbackMutation::Enum mutation : mutations){
        ChoiceSource source;
        RadioGroupState state;
        state.select(2u);
        RadioGroupChoices choices;
        RadioGroupResult result;
        ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
        const RadioGroupSnapshot before = state.snapshot();
        const RadioGroupChoices expected = choices;
        source.state = &state;
        source.phase = CallbackPhase::Lookup;
        source.mutation = mutation;
        source.armed = true;
        EXPECT_FALSE(RadioGroupBehavior::reconcile(state, source, choices, result));
        EXPECT_EQ(state.revision(), before.revision + 1u);
        EXPECT_NE(state.inputGeneration(), before.inputGeneration);
        EXPECT_EQ(state.selectedKey(), mutation == CallbackMutation::Reset ? 0u : 2u);
        ExpectChoices(choices, expected);
    }
}

TEST(UiRadioGroupBehaviorTests, ReentrantBehaviorRejectsTheOuterReconciliationAndLeavesStateUsable){
    const CallbackMutation::Enum mutations[]{ CallbackMutation::Reconcile, CallbackMutation::Apply };
    for(const CallbackMutation::Enum mutation : mutations){
        ChoiceSource source;
        RadioGroupState state;
        RadioGroupChoices choices;
        RadioGroupResult result;
        ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
        const RadioGroupSnapshot before = state.snapshot();
        const RadioGroupChoices expected = choices;
        const RadioGroupResult expectedResult = result;
        source.state = &state;
        source.borrowedChoices = choices;
        source.phase = CallbackPhase::Enabled;
        source.mutation = mutation;
        source.armed = true;
        EXPECT_FALSE(RadioGroupBehavior::reconcile(state, source, choices, result));
        EXPECT_FALSE(source.nestedSucceeded);
        EXPECT_TRUE(state.matches(before));
        ExpectChoices(choices, expected);
        ExpectResult(result, expectedResult);
        ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    }
}

TEST(UiRadioGroupBehaviorTests, SourceMutationDuringChoiceCallbacksRejectsWithoutCommittingState){
    const CallbackMutation::Enum mutations[]{ CallbackMutation::SourceRevision, CallbackMutation::SourceGeneration,
        CallbackMutation::SourceCount };
    for(const CallbackMutation::Enum mutation : mutations){
        ChoiceSource source;
        RadioGroupState state;
        RadioGroupChoices choices;
        RadioGroupResult result;
        ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
        const RadioGroupSnapshot before = state.snapshot();
        const RadioGroupChoices expected = choices;
        const RadioGroupResult expectedResult = result;
        source.phase = CallbackPhase::Enabled;
        source.mutation = mutation;
        source.armed = true;
        EXPECT_FALSE(RadioGroupBehavior::reconcile(state, source, choices, result));
        EXPECT_TRUE(state.matches(before));
        ExpectChoices(choices, expected);
        ExpectResult(result, expectedResult);
    }
}

TEST(UiRadioGroupBehaviorTests, FinalOneShotMetadataMutationIsRejectedBeforeCommit){
    const CallbackMutation::Enum mutations[]{ CallbackMutation::SourceRevision, CallbackMutation::SourceGeneration };
    for(const CallbackMutation::Enum mutation : mutations){
        ChoiceSource source;
        RadioGroupState state;
        RadioGroupChoices choices;
        RadioGroupResult result;
        ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
        const RadioGroupSnapshot before = state.snapshot();
        const RadioGroupChoices expected = choices;
        const RadioGroupResult expectedResult = result;
        source.countCalls = 0u;
        source.phase = CallbackPhase::Count;
        source.mutation = mutation;
        source.triggerCount = static_cast<u32>(source.count) * 3u + 3u;
        source.armed = true;
        EXPECT_FALSE(RadioGroupBehavior::reconcile(state, source, choices, result));
        EXPECT_FALSE(source.armed);
        EXPECT_TRUE(state.matches(before));
        ExpectChoices(choices, expected);
        ExpectResult(result, expectedResult);
    }
}

TEST(UiRadioGroupBehaviorTests, FailedBorrowedGuardStopsAllFurtherSourceCallbacks){
    ChoiceSource source;
    RadioGroupState state;
    RadioGroupChoices choices;
    RadioGroupResult result;
    ASSERT_TRUE(RadioGroupBehavior::reconcile(state, source, choices, result));
    const RadioGroupSnapshot before = state.snapshot();
    const RadioGroupChoices expected = choices;
    const RadioGroupResult expectedResult = result;
    ReconcileGate gate;
    source.gate = &gate;
    source.phase = CallbackPhase::Enabled;
    source.mutation = CallbackMutation::GuardFailure;
    source.armed = true;
    EXPECT_FALSE(RadioGroupBehavior::reconcile(state, source, choices, result, &gate));
    EXPECT_EQ(source.callbackCalls, source.callbacksAtMutation);
    EXPECT_TRUE(state.matches(before));
    ExpectChoices(choices, expected);
    ExpectResult(result, expectedResult);
}

TEST(UiRadioGroupBehaviorTests, BorrowedGuardMutationIsRejectedBeforeTheFirstSourceCall){
    ChoiceSource source;
    RadioGroupState state;
    state.select(2u);
    const RadioGroupSnapshot before = state.snapshot();
    RadioGroupChoices choices;
    RadioGroupResult result;
    ReconcileGate gate;
    gate.state = &state;
    gate.mutateState = true;
    EXPECT_FALSE(RadioGroupBehavior::reconcile(state, source, choices, result, &gate));
    EXPECT_EQ(source.callbackCalls, 0u);
    EXPECT_EQ(state.selectedKey(), 2u);
    EXPECT_EQ(state.revision(), before.revision + 1u);
    EXPECT_FALSE(state.matches(before));
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(choices.count, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

