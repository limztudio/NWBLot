// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "list_behavior_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_behavior_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiListBehaviorTestSupport;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiListBehaviorTests, EmptySourceClearsKeysAndNavigationAndSubmitAreValidNoOps){
    RangeSource source;
    source.count = 0u;
    ListState state;
    state.select(5u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    EXPECT_EQ(state.selectedKey(), 0u);
    EXPECT_EQ(state.cursorKey(), 0u);
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Down), state, result));
    ASSERT_TRUE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Submit), state, result));
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
    ASSERT_TRUE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Down), state, result));
    EXPECT_EQ(state.selectedKey(), 100000u);
    EXPECT_EQ(state.cursorKey(), 100000u);
    EXPECT_EQ(source.findCalls, 1u);
    EXPECT_LE(source.lookupCalls, 2u);
    EXPECT_LE(source.keyCalls, 3u);
    EXPECT_LE(source.enabledCalls, 2u);
    ASSERT_TRUE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Up), state, result));
    EXPECT_EQ(state.cursorKey(), 49999u);
    EXPECT_TRUE(result.selectionChanged);
    EXPECT_FALSE(result.activated);
}

TEST(UiListBehaviorTests, HomeEndAndBoundaryNavigationNeverWrap){
    RangeSource source;
    ListState state;
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Home), state, result));
    ASSERT_TRUE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Up), state, result));
    EXPECT_EQ(state.cursorKey(), 1u);
    ASSERT_TRUE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::End), state, result));
    ASSERT_TRUE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Down), state, result));
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
    action.pageRows = Limit<u64>::s_Max;
    ASSERT_TRUE(ListBehavior::Apply(source, {}, action, state, result));
    EXPECT_EQ(state.cursorKey(), 10u);
    action.kind = ControlActionKind::PageUp;
    ASSERT_TRUE(ListBehavior::Apply(source, {}, action, state, result));
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
    ASSERT_TRUE(ListBehavior::Apply(source, {}, action, state, result));
    EXPECT_EQ(state.cursorKey(), 8u);
    action.kind = ControlActionKind::PageUp;
    ASSERT_TRUE(ListBehavior::Apply(source, {}, action, state, result));
    EXPECT_EQ(state.cursorKey(), 4u);
}

TEST(UiListBehaviorTests, AllDisabledNavigationAndUnselectedSubmitAreValidNoOps){
    RangeSource source;
    source.disabledBegin = 0u;
    source.disabledEnd = source.count;
    ListState state;
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Home), state, result));
    ASSERT_TRUE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Submit), state, result));
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
    EXPECT_FALSE(ListBehavior::Apply(source, {}, action, state, result));
    action.value = 4u;
    EXPECT_FALSE(ListBehavior::Apply(source, {}, action, state, result));
    action.value = 5u;
    source.badLookup = true;
    EXPECT_FALSE(ListBehavior::Apply(source, {}, action, state, result));
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
    EXPECT_FALSE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Down), state, result));
    EXPECT_EQ(state.cursorKey(), 2u);
    EXPECT_EQ(state.selectedKey(), 2u);
    EXPECT_FALSE(result.valid);
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
    ASSERT_TRUE(ListBehavior::Apply(source, {}, action, state, result));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 0.0);
    action.delta = -Limit<f64>::s_Max;
    ASSERT_TRUE(ListBehavior::Apply(source, {}, action, state, result));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 1000.0);
    action.delta = 0.0;
    action.maximum = 20.0;
    ASSERT_TRUE(ListBehavior::Apply(source, {}, action, state, result));
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
    EXPECT_FALSE(ListBehavior::Apply(source, {}, action, state, result));
    action = valid;
    action.step = 0.0;
    EXPECT_FALSE(ListBehavior::Apply(source, {}, action, state, result));
    action = valid;
    action.maximum = -1.0;
    EXPECT_FALSE(ListBehavior::Apply(source, {}, action, state, result));
    action = valid;
    action.maximum = Limit<f64>::s_Infinity;
    EXPECT_FALSE(ListBehavior::Apply(source, {}, action, state, result));
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
    EXPECT_FALSE(ListBehavior::Apply(source, options, valid, state, result));
    options = {};
    options.wheelRows = 0.0f;
    EXPECT_FALSE(ListBehavior::Apply(source, options, valid, state, result));
    options = {};
    options.wheelRows = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(ListBehavior::Apply(source, options, valid, state, result));
    ControlAction action = valid;
    action.kind = static_cast<ControlActionKind::Enum>(255u);
    EXPECT_FALSE(ListBehavior::Apply(source, {}, action, state, result));
    action.kind = ControlActionKind::PageDown;
    action.pageRows = 0u;
    EXPECT_FALSE(ListBehavior::Apply(source, {}, action, state, result));
    EXPECT_EQ(state.cursorKey(), 2u);
    EXPECT_EQ(state.selectedKey(), 2u);
}

TEST(UiListBehaviorTests, RepeatedHomeNoopPreservesEarlierSelectionChange){
    RangeSource source;
    ListState state;
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Home), state, result));
    EXPECT_TRUE(result.selectionChanged);
    ASSERT_TRUE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Home), state, result));
    EXPECT_TRUE(result.selectionChanged);
    EXPECT_FALSE(result.activated);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

