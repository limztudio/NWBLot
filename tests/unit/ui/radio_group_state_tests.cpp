// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/radio_group.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_radio_group_state_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;


TEST(UiRadioGroupStateTests, DefaultStatesHaveDistinctNonzeroIdentitiesAndInputLifetimes){
    RadioGroupState first;
    RadioGroupState second;
    EXPECT_NE(first.instanceGeneration(), 0u);
    EXPECT_NE(first.inputGeneration(), 0u);
    EXPECT_NE(first.instanceGeneration(), second.instanceGeneration());
    EXPECT_NE(first.inputGeneration(), second.inputGeneration());
    EXPECT_EQ(first.revision(), 1u);
    EXPECT_EQ(first.selectedKey(), 0u);
    EXPECT_EQ(first.cursorKey(), 0u);
    EXPECT_EQ(first.snapshot().sourceGeneration, 0u);
    EXPECT_EQ(first.snapshot().sourceRevision, 0u);
    EXPECT_EQ(first.placement().count, 0u);
}

TEST(UiRadioGroupStateTests, IdenticalPublicSelectionRetiresInputAndAdvancesRevision){
    RadioGroupState state;
    state.select(41u);
    const RadioGroupSnapshot before = state.snapshot();
    state.select(41u);
    EXPECT_EQ(state.instanceGeneration(), before.instanceGeneration);
    EXPECT_NE(state.inputGeneration(), before.inputGeneration);
    EXPECT_EQ(state.revision(), before.revision + 1u);
    EXPECT_EQ(state.selectedKey(), 41u);
    EXPECT_EQ(state.cursorKey(), 41u);
    EXPECT_FALSE(state.matches(before));
}

TEST(UiRadioGroupStateTests, SelectionAwayAndBackCannotRestoreAnOldSnapshot){
    RadioGroupState state;
    state.select(41u);
    const RadioGroupSnapshot before = state.snapshot();
    state.select(42u);
    state.select(41u);
    EXPECT_EQ(state.selectedKey(), before.selectedKey);
    EXPECT_EQ(state.cursorKey(), before.cursorKey);
    EXPECT_NE(state.inputGeneration(), before.inputGeneration);
    EXPECT_EQ(state.revision(), before.revision + 2u);
    EXPECT_FALSE(state.matches(before));
}

TEST(UiRadioGroupStateTests, ResetClearsBindingAndSelectionEvenWhenAlreadyEmpty){
    RadioGroupState state;
    state.select(41u);
    const u64 identity = state.instanceGeneration();
    state.reset();
    EXPECT_EQ(state.instanceGeneration(), identity);
    EXPECT_EQ(state.selectedKey(), 0u);
    EXPECT_EQ(state.cursorKey(), 0u);
    EXPECT_EQ(state.snapshot().sourceGeneration, 0u);
    EXPECT_EQ(state.snapshot().sourceRevision, 0u);
    const RadioGroupSnapshot before = state.snapshot();
    state.reset();
    EXPECT_NE(state.inputGeneration(), before.inputGeneration);
    EXPECT_EQ(state.revision(), before.revision + 1u);
    EXPECT_FALSE(state.matches(before));
}

TEST(UiRadioGroupStateTests, SnapshotEqualityAndMatchingCoverEveryPublicEpochAndKey){
    RadioGroupState state;
    state.select(41u);
    const RadioGroupSnapshot snapshot = state.snapshot();
    EXPECT_TRUE(state.matches(snapshot));
    EXPECT_EQ(snapshot, state.snapshot());
    RadioGroupSnapshot changed = snapshot;
    ++changed.instanceGeneration;
    EXPECT_FALSE(state.matches(changed));
    changed = snapshot;
    ++changed.inputGeneration;
    EXPECT_FALSE(state.matches(changed));
    changed = snapshot;
    ++changed.revision;
    EXPECT_FALSE(state.matches(changed));
    changed = snapshot;
    ++changed.selectedKey;
    EXPECT_FALSE(state.matches(changed));
    changed = snapshot;
    ++changed.cursorKey;
    EXPECT_FALSE(state.matches(changed));
    changed = snapshot;
    ++changed.sourceGeneration;
    EXPECT_FALSE(state.matches(changed));
    changed = snapshot;
    ++changed.sourceRevision;
    EXPECT_FALSE(state.matches(changed));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

