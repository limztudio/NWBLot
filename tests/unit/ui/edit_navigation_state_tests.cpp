// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/edit/vertical_navigation.h>
#include <impl/ecs_ui/toolkit/input/bindings.h>

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

TEST(UiEditNavigationStateTests, OsModifiersAndUnrelatedIntentsLeaveDirectionOutputUntouched){
    Core::Alloc::GlobalArena arena(Name("tests/ui/edit/navigation"));
    InputBindings bindings(arena);
    const InputKeyBinding chords[]{
        { Core::Key::Up, Core::InputModifier::Control }, { Core::Key::Down, Core::InputModifier::Alt },
        { Core::Key::PageUp, Core::InputModifier::Control | Core::InputModifier::Shift },
        { Core::Key::PageDown, Core::InputModifier::Shift | Core::InputModifier::Alt },
        { Core::Key::Unknown }, { Core::Key::Enter }, { Core::Key::Left }, { static_cast<i32>(Core::Key::Menu) + 1 }
    };
    for(const auto& chord : chords){
        EditNavigationDirection::Enum output = EditNavigationDirection::PageDown;
        EXPECT_FALSE(TranslateEditNavigation(bindings.resolve(chord.key, chord.modifiers), output));
        EXPECT_EQ(output, EditNavigationDirection::PageDown);
    }
    const InputCommandIntent rejected[]{
        { InputCommand::Up, true, false }, { InputCommand::Down, false, false },
        { InputCommand::PageUp, false, false }, { InputCommand::PageDown, true, false },
        { InputCommand::FocusNext }, { static_cast<InputCommand::Enum>(255u), true }
    };
    for(const InputCommandIntent& intent : rejected){
        EditNavigationDirection::Enum output = EditNavigationDirection::PageDown;
        EXPECT_FALSE(TranslateEditNavigation(intent, output));
        EXPECT_EQ(output, EditNavigationDirection::PageDown);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

