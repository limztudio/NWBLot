// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/ui/name_cases.adhd.h>

#include <impl/ecs_ui/toolkit/input/bindings.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_input_bindings_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiInputBindingsTests : public testing::Test{
public:
    UiInputBindingsTests()
        : m_arena(Name(NWB::Tests::UiCases::s_UiCaseTestsUiInputBindings))
        , m_bindings(m_arena)
    {}


protected:
    Core::Alloc::GlobalArena m_arena;
    InputBindings m_bindings;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiInputBindingsTests, OverlappingIgnoredModifierChordsRejectAtomicallyRegardlessOfCommand){
    const InputKeyBinding accepted{ Core::Key::W, 0, 0, InputCommand::Left };
    ASSERT_TRUE(m_bindings.set(Span(&accepted, 1u)));
    const InputKeyBinding* previous = m_bindings.bindings().data();
    const Array<InputKeyBinding, 2> overlap{
        InputKeyBinding{ Core::Key::W, Core::InputModifier::Control, Core::InputModifier::Shift, InputCommand::Right },
        InputKeyBinding{ Core::Key::W, Core::InputModifier::Shift, Core::InputModifier::Control, InputCommand::Up },
    };
    EXPECT_FALSE(m_bindings.set(Span(overlap.data(), 2u)));
    EXPECT_EQ(m_bindings.bindings().data(), previous);
    EXPECT_EQ(m_bindings.resolve(Core::Key::W, 0).command, InputCommand::Left);
    EXPECT_EQ(m_bindings.resolve(Core::Key::W, Core::InputModifier::Control | Core::InputModifier::Shift).command, InputCommand::None);
    const Array<InputKeyBinding, 2> duplicate{ accepted, accepted };
    EXPECT_FALSE(m_bindings.set(Span(duplicate.data(), 2u)));
    EXPECT_EQ(m_bindings.bindings().data(), previous);
}

TEST_F(UiInputBindingsTests, MalformedKeysEnumsAndModifiersPreserveAcceptedProfile){
    const InputKeyBinding accepted{ Core::Key::W, 0, 0, InputCommand::Left };
    ASSERT_TRUE(m_bindings.set(Span(&accepted, 1u)));
    const InputKeyBinding* previous = m_bindings.bindings().data();
    const Array<i32, 5> invalidKeys{ Core::Key::Unknown, Core::Key::Space - 1, Core::Key::Menu + 1, Core::Key::Apostrophe + 1, Core::Key::GraveAccent + 1 };
    for(const i32 key : invalidKeys){
        InputKeyBinding invalid = accepted;
        invalid.key = key;
        EXPECT_FALSE(m_bindings.set(Span(&invalid, 1u)));
        EXPECT_EQ(m_bindings.bindings().data(), previous);
        EXPECT_EQ(m_bindings.resolve(key, 0).command, InputCommand::None);
    }
    InputKeyBinding invalid = accepted;
    invalid.command = InputCommand::None;
    EXPECT_FALSE(m_bindings.set(Span(&invalid, 1u)));
    invalid.command = static_cast<InputCommand::Enum>(255u);
    EXPECT_FALSE(m_bindings.set(Span(&invalid, 1u)));
    invalid = accepted;
    invalid.selection = static_cast<InputSelectionPolicy::Enum>(255u);
    EXPECT_FALSE(m_bindings.set(Span(&invalid, 1u)));
    invalid = accepted;
    invalid.modifiers = Core::InputModifier::CapsLock;
    EXPECT_FALSE(m_bindings.set(Span(&invalid, 1u)));
    invalid = accepted;
    invalid.ignoredModifiers = Core::InputModifier::NumLock;
    EXPECT_FALSE(m_bindings.set(Span(&invalid, 1u)));
    invalid.modifiers = invalid.ignoredModifiers = Core::InputModifier::Shift;
    EXPECT_FALSE(m_bindings.set(Span(&invalid, 1u)));
    EXPECT_EQ(m_bindings.resolve(Core::Key::W, 0x0040).command, InputCommand::None);
    EXPECT_EQ(m_bindings.bindings().data(), previous);
    EXPECT_EQ(m_bindings.resolve(Core::Key::W, 0).command, InputCommand::Left);
}

TEST_F(UiInputBindingsTests, WorldKeyCodesRejectSparseNeighborsWithoutReplacingTheAcceptedProfile){
    const Array<InputKeyBinding, 2> accepted{
        InputKeyBinding{ Core::Key::World1, 0, 0, InputCommand::Left },
        InputKeyBinding{ Core::Key::World2, 0, 0, InputCommand::Right },
    };
    ASSERT_TRUE(m_bindings.set(Span(accepted.data(), 2u)));
    const InputKeyBinding* previous = m_bindings.bindings().data();
    const Array<i32, 3> invalidKeys{ Core::Key::GraveAccent + 1, Core::Key::World1 - 1, Core::Key::World2 + 1 };
    for(const i32 key : invalidKeys){
        InputKeyBinding invalid = accepted[0u];
        invalid.key = key;
        EXPECT_FALSE(m_bindings.set(Span(&invalid, 1u)));
        EXPECT_EQ(m_bindings.bindings().data(), previous);
        EXPECT_EQ(m_bindings.resolve(key, 0).command, InputCommand::None);
        EXPECT_EQ(m_bindings.resolve(161, 0).command, InputCommand::Left);
        EXPECT_EQ(m_bindings.resolve(162, 0).command, InputCommand::Right);
    }
}

TEST_F(UiInputBindingsTests, ExactCapacitySucceedsAndOverflowPreservesTheProfile){
    Array<InputKeyBinding, s_InputMaxBindings + 1u> custom{};
    for(usize index = 0u; index < s_InputMaxBindings; ++index){
        custom[index].key = Core::Key::F1 + static_cast<i32>(index / 16u);
        custom[index].modifiers = static_cast<i32>(index % 16u);
        custom[index].command = InputCommand::Right;
    }
    ASSERT_TRUE(m_bindings.set(Span(custom.data(), s_InputMaxBindings)));
    const InputKeyBinding* previous = m_bindings.bindings().data();
    EXPECT_FALSE(m_bindings.set(custom));
    EXPECT_EQ(m_bindings.bindings().data(), previous);
    EXPECT_EQ(m_bindings.resolve(Core::Key::F16, s_InputBindingModifierMask).command, InputCommand::Right);
    EXPECT_EQ(m_bindings.resolve(Core::Key::F17, 0).command, InputCommand::None);
}

TEST_F(UiInputBindingsTests, EmptyAndAliasedProfileReplacementCanRecoverToDefaults){
    const Array<InputKeyBinding, 2> custom{
        InputKeyBinding{ Core::Key::W, 0, 0, InputCommand::Up },
        InputKeyBinding{ Core::Key::F, 0, 0, InputCommand::Activate },
    };
    ASSERT_TRUE(m_bindings.set(Span(custom.data(), 2u)));
    ASSERT_TRUE(m_bindings.set(Span(m_bindings.bindings().data() + 1u, 1u)));
    EXPECT_EQ(m_bindings.resolve(Core::Key::W, 0).command, InputCommand::None);
    EXPECT_EQ(m_bindings.resolve(Core::Key::F, 0).command, InputCommand::Activate);
    ASSERT_TRUE(m_bindings.set({}));
    EXPECT_EQ(m_bindings.resolve(Core::Key::F, 0).command, InputCommand::None);
    EXPECT_EQ(m_bindings.resolve(Core::Key::Tab, 0).command, InputCommand::None);
    m_bindings.restoreDefaults();
    EXPECT_EQ(m_bindings.resolve(Core::Key::F, 0).command, InputCommand::None);
    EXPECT_EQ(m_bindings.resolve(Core::Key::Tab, Core::InputModifier::Shift).command, InputCommand::FocusPrevious);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

