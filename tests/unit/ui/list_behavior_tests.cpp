// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/list.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_behavior_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RangeSource final : public IListDataSource{
public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ return generation; }
    [[nodiscard]] virtual u64 revision()const override{ return revisionValue; }
    [[nodiscard]] virtual u64 rowCount()const override{ return count; }

    [[nodiscard]] virtual u64 key(const u64 index)const override{
        ++keyCalls;
        if(index >= count)
            return 0u;
        return reversed ? count - index : index + 1u;
    }

    [[nodiscard]] virtual bool indexOf(const u64 key, u64& index)const override{
        ++lookupCalls;
        if(changeRevisionOnLookup){
            changeRevisionOnLookup = false;
            ++revisionValue;
        }
        if(key == 0u || key > count)
            return false;
        index = badLookup ? count : wrongLookup ? 0u : reversed ? count - key : key - 1u;
        return true;
    }

    [[nodiscard]] virtual bool findEnabled(const u64 start, const bool reverse, u64& index)const override{
        ++findCalls;
        if(start >= count)
            return false;
        if(badFind){
            index = count;
            return true;
        }
        index = start;
        if(index >= disabledBegin && index < disabledEnd){
            if(reverse){
                if(disabledBegin == 0u)
                    return false;
                index = disabledBegin - 1u;
            }
            else{
                if(disabledEnd >= count)
                    return false;
                index = disabledEnd;
            }
        }
        return true;
    }

    [[nodiscard]] virtual StringView text(u64)const override{ return "row"; }

    [[nodiscard]] virtual bool enabled(const u64 index)const override{
        ++enabledCalls;
        return index < count && (index < disabledBegin || index >= disabledEnd);
    }

    void clearCalls(){
        keyCalls = 0u;
        lookupCalls = 0u;
        findCalls = 0u;
        enabledCalls = 0u;
    }


public:
    u64 generation = 41u;
    mutable u64 revisionValue = 1u;
    u64 count = 10u;
    u64 disabledBegin = Limit<u64>::s_Max;
    u64 disabledEnd = Limit<u64>::s_Max;
    bool reversed = false;
    bool badLookup = false;
    bool wrongLookup = false;
    bool badFind = false;
    mutable bool changeRevisionOnLookup = false;
    mutable u64 keyCalls = 0u;
    mutable u64 lookupCalls = 0u;
    mutable u64 findCalls = 0u;
    mutable u64 enabledCalls = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static ControlAction Action(const ListState& state, const RangeSource& source,
    const ControlActionKind::Enum kind){
    ControlAction action;
    action.control = { state.inputGeneration(), source.instanceGeneration(), source.revision() };
    action.kind = kind;
    return action;
}

static void ExpectResult(const ListResult& actual, const ListResult& expected){
    EXPECT_EQ(actual.valid, expected.valid);
    EXPECT_EQ(actual.selectionChanged, expected.selectionChanged);
    EXPECT_EQ(actual.activated, expected.activated);
    EXPECT_EQ(actual.focused, expected.focused);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiListBehaviorTests, InitialApplicationSelectionAndOffsetSurviveFirstSourceBinding){
    RangeSource source;
    ListState state;
    state.select(5u);
    ASSERT_TRUE(state.scrollTo(90.0));
    const u64 inputGeneration = state.inputGeneration();
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    EXPECT_EQ(state.selectedKey(), 5u);
    EXPECT_EQ(state.cursorKey(), 5u);
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 90.0);
    EXPECT_EQ(state.inputGeneration(), inputGeneration);
}

TEST(UiListBehaviorTests, ReorderingRetainsStableSelectionAndCursorKeysAndRevealsTheirNewIndex){
    RangeSource source;
    ListState state;
    state.select(5u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListOptions options;
    options.selectOnNavigate = false;
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(state, source, options, Action(state, source, ControlActionKind::Down), result));
    ASSERT_EQ(state.cursorKey(), 6u);
    source.reversed = true;
    ++source.revisionValue;
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    EXPECT_EQ(state.selectedKey(), 5u);
    EXPECT_EQ(state.cursorKey(), 6u);
    ASSERT_TRUE(ListBehavior::EnsureCursor(state, source, 32.0f, 64.0));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 96.0);
}

TEST(UiListBehaviorTests, RemovedCursorClearsIndependentlyOfSurvivingSelection){
    RangeSource source;
    ListState state;
    state.select(5u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListOptions options;
    options.selectOnNavigate = false;
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(state, source, options, Action(state, source, ControlActionKind::Down), result));
    source.count = 5u;
    ++source.revisionValue;
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    EXPECT_EQ(state.selectedKey(), 5u);
    EXPECT_EQ(state.cursorKey(), 0u);
}

TEST(UiListBehaviorTests, DisabledSelectionClearsIndependentlyOfEnabledCursor){
    RangeSource source;
    ListState state;
    state.select(5u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListOptions options;
    options.selectOnNavigate = false;
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(state, source, options, Action(state, source, ControlActionKind::Down), result));
    source.disabledBegin = 4u;
    source.disabledEnd = 5u;
    ++source.revisionValue;
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    EXPECT_EQ(state.selectedKey(), 0u);
    EXPECT_EQ(state.cursorKey(), 6u);
}

TEST(UiListBehaviorTests, ReplacingAnEstablishedSourceClearsSelectionCursorAndOffset){
    RangeSource source;
    ListState state;
    state.select(5u);
    ASSERT_TRUE(state.scrollTo(90.0));
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    const u64 inputGeneration = state.inputGeneration();
    ++source.generation;
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    EXPECT_EQ(state.selectedKey(), 0u);
    EXPECT_EQ(state.cursorKey(), 0u);
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 0.0);
    EXPECT_EQ(state.inputGeneration(), inputGeneration);
}

TEST(UiListBehaviorTests, InvalidSourceIdentitiesAndMalformedLookupFailWithoutPartialMutation){
    RangeSource source;
    ListState state;
    state.select(5u);
    ASSERT_TRUE(state.scrollTo(90.0));
    source.generation = 0u;
    EXPECT_FALSE(ListBehavior::Reconcile(state, source));
    source.generation = 41u;
    source.revisionValue = 0u;
    EXPECT_FALSE(ListBehavior::Reconcile(state, source));
    source.revisionValue = 1u;
    source.badLookup = true;
    EXPECT_FALSE(ListBehavior::Reconcile(state, source));
    source.badLookup = false;
    source.wrongLookup = true;
    EXPECT_FALSE(ListBehavior::Reconcile(state, source));
    EXPECT_EQ(state.selectedKey(), 5u);
    EXPECT_EQ(state.cursorKey(), 5u);
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 90.0);
    source.wrongLookup = false;
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
}

TEST(UiListBehaviorTests, SourceRevisionChangingDuringLookupFailsAtomically){
    RangeSource source;
    ListState state;
    state.select(5u);
    source.changeRevisionOnLookup = true;
    EXPECT_FALSE(ListBehavior::Reconcile(state, source));
    EXPECT_EQ(state.selectedKey(), 5u);
    EXPECT_EQ(state.cursorKey(), 5u);
    ListResult result;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Down), result));
    EXPECT_FALSE(result.valid);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
}

TEST(UiListBehaviorTests, EmptySourceClearsKeysAndNavigationAndSubmitAreValidNoOps){
    RangeSource source;
    source.count = 0u;
    ListState state;
    state.select(5u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    EXPECT_EQ(state.selectedKey(), 0u);
    EXPECT_EQ(state.cursorKey(), 0u);
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Down), result));
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Submit), result));
    EXPECT_TRUE(result.valid);
    EXPECT_FALSE(result.selectionChanged);
    EXPECT_FALSE(result.activated);
    EXPECT_EQ(source.findCalls, 0u);
    EXPECT_EQ(source.lookupCalls, 0u);
}

TEST(UiListBehaviorTests, HundredThousandRowsSkipALargeDisabledRangeWithBoundedSourceCalls){
    RangeSource source;
    source.count = 100000u;
    source.disabledBegin = 49999u;
    source.disabledEnd = 99999u;
    ListState state;
    state.select(49999u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    source.clearCalls();
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Down), result));
    EXPECT_EQ(state.selectedKey(), 100000u);
    EXPECT_EQ(state.cursorKey(), 100000u);
    EXPECT_EQ(source.findCalls, 1u);
    EXPECT_LE(source.lookupCalls, 2u);
    EXPECT_LE(source.keyCalls, 3u);
    EXPECT_LE(source.enabledCalls, 2u);
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Up), result));
    EXPECT_EQ(state.cursorKey(), 49999u);
    EXPECT_TRUE(result.selectionChanged);
    EXPECT_FALSE(result.activated);
}

TEST(UiListBehaviorTests, NoCursorStartsAtTheEnabledBoundaryInTheNavigationDirection){
    RangeSource source;
    ListState forward;
    ListState reverse;
    ASSERT_TRUE(ListBehavior::Reconcile(forward, source));
    ASSERT_TRUE(ListBehavior::Reconcile(reverse, source));
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(forward, source, {}, Action(forward, source, ControlActionKind::Down), result));
    ASSERT_TRUE(ListBehavior::Apply(reverse, source, {}, Action(reverse, source, ControlActionKind::Up), result));
    EXPECT_EQ(forward.cursorKey(), 1u);
    EXPECT_EQ(reverse.cursorKey(), 10u);
}

TEST(UiListBehaviorTests, HomeEndAndBoundaryNavigationNeverWrap){
    RangeSource source;
    ListState state;
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Home), result));
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Up), result));
    EXPECT_EQ(state.cursorKey(), 1u);
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::End), result));
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Down), result));
    EXPECT_EQ(state.cursorKey(), 10u);
    EXPECT_FALSE(result.activated);
}

TEST(UiListBehaviorTests, PageNavigationUsesAcceptedRowsAndSaturatesWithoutIntegerOverflow){
    RangeSource source;
    ListState state;
    state.select(3u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListResult result;
    ControlAction action = Action(state, source, ControlActionKind::PageDown);
    action.pageRows = 4u;
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_EQ(state.cursorKey(), 7u);
    action.kind = ControlActionKind::PageUp;
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_EQ(state.cursorKey(), 3u);
    action.kind = ControlActionKind::PageDown;
    action.pageRows = Limit<u64>::s_Max;
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_EQ(state.cursorKey(), 10u);
    action.kind = ControlActionKind::PageUp;
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_EQ(state.cursorKey(), 1u);
}

TEST(UiListBehaviorTests, PageNavigationSkipsDisabledRowsBeyondTheRequestedBoundary){
    RangeSource source;
    source.disabledBegin = 4u;
    source.disabledEnd = 7u;
    ListState state;
    state.select(2u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListResult result;
    ControlAction action = Action(state, source, ControlActionKind::PageDown);
    action.pageRows = 3u;
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_EQ(state.cursorKey(), 8u);
    action.kind = ControlActionKind::PageUp;
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_EQ(state.cursorKey(), 4u);
}

TEST(UiListBehaviorTests, CursorCanNavigateWithoutChangingSelectionUntilSubmit){
    RangeSource source;
    ListState state;
    state.select(3u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListOptions options;
    options.selectOnNavigate = false;
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(state, source, options, Action(state, source, ControlActionKind::Down), result));
    EXPECT_EQ(state.selectedKey(), 3u);
    EXPECT_EQ(state.cursorKey(), 4u);
    EXPECT_FALSE(result.selectionChanged);
    EXPECT_FALSE(result.activated);
    ASSERT_TRUE(ListBehavior::Apply(state, source, options, Action(state, source, ControlActionKind::Submit), result));
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_TRUE(result.selectionChanged);
    EXPECT_TRUE(result.activated);
}

TEST(UiListBehaviorTests, ActivateCommitsAnEnabledStableKeyEvenWhenNavigationSelectionIsDisabled){
    RangeSource source;
    ListState state;
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListOptions options;
    options.selectOnNavigate = false;
    ListResult result;
    ControlAction action = Action(state, source, ControlActionKind::Activate);
    action.value = 9u;
    ASSERT_TRUE(ListBehavior::Apply(state, source, options, action, result));
    EXPECT_EQ(state.selectedKey(), 9u);
    EXPECT_EQ(state.cursorKey(), 9u);
    EXPECT_TRUE(result.selectionChanged);
    EXPECT_TRUE(result.activated);
}

TEST(UiListBehaviorTests, AllDisabledNavigationAndUnselectedSubmitAreValidNoOps){
    RangeSource source;
    source.disabledBegin = 0u;
    source.disabledEnd = source.count;
    ListState state;
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Home), result));
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Submit), result));
    EXPECT_EQ(state.cursorKey(), 0u);
    EXPECT_EQ(state.selectedKey(), 0u);
    EXPECT_FALSE(result.activated);
}

TEST(UiListBehaviorTests, MissingDisabledOrMalformedActivationDoesNotModifyStateOrResult){
    RangeSource source;
    source.disabledBegin = 3u;
    source.disabledEnd = 4u;
    ListState state;
    state.select(2u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    const ListResult previous{ true, true, false, true };
    ListResult result = previous;
    ControlAction action = Action(state, source, ControlActionKind::Activate);
    action.value = 99u;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    action.value = 4u;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    action.value = 5u;
    source.badLookup = true;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_EQ(state.selectedKey(), 2u);
    EXPECT_EQ(state.cursorKey(), 2u);
    ExpectResult(result, previous);
}

TEST(UiListBehaviorTests, MalformedEnabledSearchFailsAtomically){
    RangeSource source;
    ListState state;
    state.select(2u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    source.badFind = true;
    ListResult result;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Down), result));
    EXPECT_EQ(state.cursorKey(), 2u);
    EXPECT_EQ(state.selectedKey(), 2u);
    EXPECT_FALSE(result.valid);
}

TEST(UiListBehaviorTests, InputSourceAndRevisionTokensRejectStaleActions){
    RangeSource source;
    ListState state;
    state.select(2u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListResult result;
    const ControlAction valid = Action(state, source, ControlActionKind::Down);
    ControlAction action = valid;
    ++action.control.instanceGeneration;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    action = valid;
    ++action.control.contentGeneration;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    action = valid;
    ++action.control.contentRevision;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    ++source.revisionValue;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Down), result));
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, valid, result));
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Down), result));
    EXPECT_EQ(state.selectedKey(), 3u);
}

TEST(UiListBehaviorTests, ApplicationChangesFenceEvenAnUnchangedSelectionValue){
    RangeSource source;
    ListState state;
    state.select(2u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    const ControlAction action = Action(state, source, ControlActionKind::Down);
    state.select(2u);
    ListResult result;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_EQ(state.cursorKey(), 2u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Down), result));
    EXPECT_EQ(state.cursorKey(), 3u);
}

TEST(UiListBehaviorTests, RevisionChangeRequestsEnsureVisibleExactlyUntilItSucceeds){
    RangeSource source;
    ListState state;
    state.select(5u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ASSERT_TRUE(ListBehavior::EnsureCursor(state, source, 32.0f, 64.0));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 96.0);
    ASSERT_TRUE(state.scrollTo(0.0));
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ASSERT_TRUE(ListBehavior::EnsureCursor(state, source, 32.0f, 64.0));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 0.0);
    ++source.revisionValue;
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ASSERT_TRUE(ListBehavior::EnsureCursor(state, source, 32.0f, 64.0));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 96.0);
}

TEST(UiListBehaviorTests, EnsureVisibleFailureKeepsItsRequestForTheNextValidAttempt){
    RangeSource source;
    ListState state;
    state.select(5u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    EXPECT_FALSE(ListBehavior::EnsureCursor(state, source, 0.0f, 64.0));
    EXPECT_FALSE(ListBehavior::EnsureCursor(state, source, 32.0f, Limit<f64>::s_QuietNaN));
    source.badLookup = true;
    EXPECT_FALSE(ListBehavior::EnsureCursor(state, source, 32.0f, 64.0));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 0.0);
    source.badLookup = false;
    ASSERT_TRUE(ListBehavior::EnsureCursor(state, source, 32.0f, 64.0));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 96.0);
}

TEST(UiListBehaviorTests, WheelUsesAcceptedStepAndMaximumWithoutSelectingOrRevealingCursor){
    RangeSource source;
    ListState state;
    state.select(5u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListOptions options;
    options.rowHeight = 128.0f;
    options.wheelRows = 7.0f;
    ControlAction action = Action(state, source, ControlActionKind::Wheel);
    action.delta = -2.0;
    action.step = 8.0;
    action.maximum = 64.0;
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(state, source, options, action, result));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 16.0);
    EXPECT_EQ(state.selectedKey(), 5u);
    EXPECT_EQ(state.cursorKey(), 5u);
    EXPECT_FALSE(result.selectionChanged);
    EXPECT_FALSE(result.activated);
    ASSERT_TRUE(ListBehavior::EnsureCursor(state, source, 32.0f, 64.0));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 16.0);
}

TEST(UiListBehaviorTests, HugeWheelDeltasClampWithoutMultiplicationOverflow){
    RangeSource source;
    ListState state;
    ASSERT_TRUE(state.scrollTo(500.0));
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ControlAction action = Action(state, source, ControlActionKind::Wheel);
    action.delta = Limit<f64>::s_Max;
    action.step = Limit<f64>::s_Max;
    action.maximum = 1000.0;
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 0.0);
    action.delta = -Limit<f64>::s_Max;
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 1000.0);
    action.delta = 0.0;
    action.maximum = 20.0;
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 20.0);
}

TEST(UiListBehaviorTests, InvalidWheelMetricsFailWithoutChangingOffsetOrResult){
    RangeSource source;
    ListState state;
    ASSERT_TRUE(state.scrollTo(50.0));
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ControlAction valid = Action(state, source, ControlActionKind::Wheel);
    valid.delta = -1.0;
    valid.step = 32.0;
    valid.maximum = 300.0;
    ListResult result;
    ControlAction action = valid;
    action.delta = Limit<f64>::s_QuietNaN;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    action = valid;
    action.step = 0.0;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    action = valid;
    action.maximum = -1.0;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    action = valid;
    action.maximum = Limit<f64>::s_Infinity;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 50.0);
    EXPECT_FALSE(result.valid);
}

TEST(UiListBehaviorTests, InvalidOptionsKindsAndZeroPageSizeRejectWithoutChangingCursor){
    RangeSource source;
    ListState state;
    state.select(2u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    const ControlAction valid = Action(state, source, ControlActionKind::Down);
    ListOptions options;
    options.enabled = false;
    ListResult result;
    EXPECT_FALSE(ListBehavior::Apply(state, source, options, valid, result));
    options = {};
    options.rowHeight = 0.0f;
    EXPECT_FALSE(ListBehavior::Apply(state, source, options, valid, result));
    options = {};
    options.wheelRows = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(ListBehavior::Apply(state, source, options, valid, result));
    ControlAction action = valid;
    action.kind = static_cast<ControlActionKind::Enum>(255u);
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    action.kind = ControlActionKind::PageDown;
    action.pageRows = 0u;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_EQ(state.cursorKey(), 2u);
    EXPECT_EQ(state.selectedKey(), 2u);
}

TEST(UiListBehaviorTests, InternalChangesKeepInputLifetimeAndAccumulateResultFlags){
    RangeSource source;
    ListState state;
    const u64 inputGeneration = state.inputGeneration();
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Home), result));
    EXPECT_TRUE(result.selectionChanged);
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Home), result));
    EXPECT_TRUE(result.selectionChanged);
    EXPECT_FALSE(result.activated);
    ASSERT_TRUE(ListBehavior::Apply(state, source, {}, Action(state, source, ControlActionKind::Submit), result));
    EXPECT_TRUE(result.activated);
    ASSERT_TRUE(ListBehavior::EnsureCursor(state, source, 32.0f, 64.0));
    ++source.generation;
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    EXPECT_EQ(state.inputGeneration(), inputGeneration);
    EXPECT_TRUE(result.selectionChanged);
    EXPECT_TRUE(result.activated);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

