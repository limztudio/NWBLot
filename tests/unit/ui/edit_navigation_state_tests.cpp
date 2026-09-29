// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ui/edit/vertical_navigation.h>

#include <global/bit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_navigation_state_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiEditNavigationStateTests, IndependentOwnersCannotReviveEachOthersCopiedIntent){
    EditNavigationState first;
    EditNavigationState second;
    EXPECT_NE(first.instanceGeneration(), 0u);
    EXPECT_NE(first.instanceGeneration(), second.instanceGeneration());
    EXPECT_FALSE(first.hasPreferredX());
    EXPECT_TRUE(first.matches(first.snapshot()));
    EXPECT_FALSE(second.matches(first.snapshot()));
    ASSERT_TRUE(first.setPreferredX(40.0f));
    ASSERT_TRUE(second.setPreferredX(40.0f));
    EXPECT_FALSE(second.matches(first.snapshot()));
}

TEST(UiEditNavigationStateTests, IdenticalPreferredColumnRetiresAnEarlierBorrow){
    EditNavigationState state;
    ASSERT_TRUE(state.setPreferredX(40.0f));
    const auto accepted = state.snapshot();
    ASSERT_TRUE(state.setPreferredX(40.0f));
    EXPECT_GT(state.generation(), accepted.generation);
    EXPECT_FALSE(state.matches(accepted));
    EXPECT_TRUE(state.hasPreferredX());
    EXPECT_FLOAT_EQ(state.preferredX(), 40.0f);
}

TEST(UiEditNavigationStateTests, ColumnAwayAndBackCannotReviveAnEarlierBorrow){
    EditNavigationState state;
    ASSERT_TRUE(state.setPreferredX(20.0f));
    const auto accepted = state.snapshot();
    ASSERT_TRUE(state.setPreferredX(30.0f));
    ASSERT_TRUE(state.setPreferredX(20.0f));
    EXPECT_FALSE(state.matches(accepted));
    EXPECT_TRUE(state.matches(state.snapshot()));
}

TEST(UiEditNavigationStateTests, ResetRetiresBothValidAndAlreadyEmptyIntent){
    EditNavigationState state;
    ASSERT_TRUE(state.setPreferredX(60.0f));
    const auto preferred = state.snapshot();
    state.reset();
    EXPECT_FALSE(state.matches(preferred));
    EXPECT_FALSE(state.hasPreferredX());
    EXPECT_FLOAT_EQ(state.preferredX(), 0.0f);
    const auto empty = state.snapshot();
    state.reset();
    EXPECT_FALSE(state.matches(empty));
    EXPECT_GT(state.generation(), empty.generation);
}

TEST(UiEditNavigationStateTests, NonfiniteColumnsRejectWithoutRetiringAcceptedIntent){
    EditNavigationState state;
    ASSERT_TRUE(state.setPreferredX(25.0f));
    const auto accepted = state.snapshot();
    const f32 invalid[]{ BitCast<f32>(0x7f800000u), BitCast<f32>(0xff800000u), BitCast<f32>(0x7fc00000u) };
    for(const f32 value : invalid){
        EXPECT_FALSE(state.setPreferredX(value));
        EXPECT_TRUE(state.matches(accepted));
    }
}

TEST(UiEditNavigationStateTests, SnapshotValueChangesAreRejectedEvenAtTheSameGeneration){
    EditNavigationState state;
    ASSERT_TRUE(state.setPreferredX(25.0f));
    auto copied = state.snapshot();
    copied.preferredX = 26.0f;
    EXPECT_FALSE(state.matches(copied));
    copied = state.snapshot();
    copied.valid = false;
    EXPECT_FALSE(state.matches(copied));
}

TEST(UiEditNavigationStateTests, FiniteExtremeColumnsRemainFiniteAuthoritativeIntent){
    EditNavigationState state;
    ASSERT_TRUE(state.setPreferredX(Limit<f32>::s_Max));
    EXPECT_EQ(state.preferredX(), Limit<f32>::s_Max);
    ASSERT_TRUE(state.setPreferredX(-Limit<f32>::s_Max));
    EXPECT_EQ(state.preferredX(), -Limit<f32>::s_Max);
    EXPECT_TRUE(state.matches(state.snapshot()));
}

TEST(UiEditNavigationStateTests, VerticalKeysKeepDirectionAcrossShiftAndRepeat){
    struct KeyCase{
        EditKey::Enum key;
        EditNavigationDirection::Enum direction;
    };
    const KeyCase cases[]{
        { EditKey::Up, EditNavigationDirection::Up }, { EditKey::Down, EditNavigationDirection::Down },
        { EditKey::PageUp, EditNavigationDirection::PageUp }, { EditKey::PageDown, EditNavigationDirection::PageDown }
    };
    for(const auto& entry : cases){
        for(const bool extend : { false, true }){
            for(const bool repeat : { false, true }){
                EditNavigationDirection::Enum output = EditNavigationDirection::Up;
                ASSERT_TRUE(TranslateEditNavigation({ entry.key, false, extend, false, repeat }, output));
                EXPECT_EQ(output, entry.direction);
            }
        }
    }
}

TEST(UiEditNavigationStateTests, OsModifiersAndUnrelatedKeysLeaveDirectionOutputUntouched){
    const EditKeyStroke keys[]{
        { EditKey::Up, true }, { EditKey::Down, false, false, true },
        { EditKey::PageUp, true, true }, { EditKey::PageDown, false, true, true },
        { EditKey::None }, { EditKey::Enter }, { EditKey::Left }, { static_cast<EditKey::Enum>(255u) }
    };
    for(const auto& key : keys){
        EditNavigationDirection::Enum output = EditNavigationDirection::PageDown;
        EXPECT_FALSE(TranslateEditNavigation(key, output));
        EXPECT_EQ(output, EditNavigationDirection::PageDown);
    }
}

TEST(UiEditNavigationStateTests, VerticalKeysAreSeparateFromGeometryFreeCommands){
    for(const EditKey::Enum key : { EditKey::Up, EditKey::Down, EditKey::PageUp, EditKey::PageDown }){
        EXPECT_EQ(TranslateEditCommand({ key }).command, EditCommand::None);
        EXPECT_EQ(TranslateEditCommand({ key }, EditTextMode::Multiline).command, EditCommand::None);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

