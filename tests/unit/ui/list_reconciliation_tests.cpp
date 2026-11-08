// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "list_behavior_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_reconciliation_tests{


using namespace UiListBehaviorTestSupport;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiListBehaviorTests, ReorderingRetainsStableSelectionAndCursorKeys){
    RangeSource source;
    ListState state;
    state.select(5u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListOptions options;
    options.selectOnNavigate = false;
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(source, options, Action(state, source, ControlActionKind::Down), state, result));
    ASSERT_EQ(state.cursorKey(), 6u);
    source.reversed = true;
    ++source.revisionValue;
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    EXPECT_EQ(state.selectedKey(), 5u);
    EXPECT_EQ(state.cursorKey(), 6u);
}

TEST(UiListBehaviorTests, RemovedCursorClearsIndependentlyOfSurvivingSelection){
    RangeSource source;
    ListState state;
    state.select(5u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ListOptions options;
    options.selectOnNavigate = false;
    ListResult result;
    ASSERT_TRUE(ListBehavior::Apply(source, options, Action(state, source, ControlActionKind::Down), state, result));
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
    ASSERT_TRUE(ListBehavior::Apply(source, options, Action(state, source, ControlActionKind::Down), state, result));
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
    EXPECT_FALSE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Down), state, result));
    EXPECT_FALSE(result.valid);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
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
    EXPECT_FALSE(ListBehavior::Apply(source, {}, action, state, result));
    action = valid;
    ++action.control.contentGeneration;
    EXPECT_FALSE(ListBehavior::Apply(source, {}, action, state, result));
    action = valid;
    ++action.control.contentRevision;
    EXPECT_FALSE(ListBehavior::Apply(source, {}, action, state, result));
    ++source.revisionValue;
    EXPECT_FALSE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Down), state, result));
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    EXPECT_FALSE(ListBehavior::Apply(source, {}, valid, state, result));
    ASSERT_TRUE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Down), state, result));
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
    EXPECT_FALSE(ListBehavior::Apply(source, {}, action, state, result));
    EXPECT_EQ(state.cursorKey(), 2u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ASSERT_TRUE(ListBehavior::Apply(source, {}, Action(state, source, ControlActionKind::Down), state, result));
    EXPECT_EQ(state.cursorKey(), 3u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


