// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        : m_arena(Name("tests/ui/input_bindings"))
        , m_bindings(m_arena)
    {}


protected:
    Core::Alloc::GlobalArena m_arena;
    InputBindings m_bindings;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiInputBindingsTests, ReplacementRemovesDefaultsAndAdmitsArbitraryPhysicalKeysAndSuperChords){
    const InputKeyBinding custom[]{
        { Core::Key::W, 0, Core::InputModifier::Shift, InputCommand::Up, InputSelectionPolicy::Shift },
        { Core::Key::F, Core::InputModifier::Super, 0, InputCommand::FocusNext },
        { Core::Key::F25, Core::InputModifier::Control, 0, InputCommand::ContextMenu, InputSelectionPolicy::None, false },
    };
    ASSERT_TRUE(m_bindings.set(custom, 3u));
    EXPECT_EQ(m_bindings.resolve(Core::Key::Tab, 0).command, InputCommand::None);
    EXPECT_EQ(m_bindings.resolve(Core::Key::Up, 0).command, InputCommand::None);
    EXPECT_EQ(m_bindings.resolve(Core::Key::F, 0).command, InputCommand::None);
    EXPECT_EQ(m_bindings.resolve(Core::Key::F, Core::InputModifier::Super).command, InputCommand::FocusNext);
    EXPECT_EQ(m_bindings.resolve(Core::Key::F, Core::InputModifier::Super | Core::InputModifier::Shift).command, InputCommand::None);
    EXPECT_EQ(m_bindings.resolve(Core::Key::W, Core::InputModifier::CapsLock | Core::InputModifier::NumLock).command, InputCommand::Up);
    EXPECT_EQ(m_bindings.resolve(Core::Key::F25, Core::InputModifier::Control).command, InputCommand::ContextMenu);
    EXPECT_FALSE(m_bindings.resolve(Core::Key::F25, Core::InputModifier::Control).edit);
}

TEST_F(UiInputBindingsTests, OverlappingIgnoredModifierChordsRejectAtomicallyRegardlessOfCommand){
    const InputKeyBinding accepted{ Core::Key::W, 0, 0, InputCommand::Left };
    ASSERT_TRUE(m_bindings.set(&accepted, 1u));
    const InputKeyBinding* previous = m_bindings.bindings().data();
    const InputKeyBinding overlap[]{
        { Core::Key::W, Core::InputModifier::Control, Core::InputModifier::Shift, InputCommand::Right },
        { Core::Key::W, Core::InputModifier::Shift, Core::InputModifier::Control, InputCommand::Up },
    };
    EXPECT_FALSE(m_bindings.set(overlap, 2u));
    EXPECT_EQ(m_bindings.bindings().data(), previous);
    EXPECT_EQ(m_bindings.resolve(Core::Key::W, 0).command, InputCommand::Left);
    EXPECT_EQ(m_bindings.resolve(Core::Key::W, Core::InputModifier::Control | Core::InputModifier::Shift).command, InputCommand::None);
    const InputKeyBinding duplicate[]{ accepted, accepted };
    EXPECT_FALSE(m_bindings.set(duplicate, 2u));
    EXPECT_EQ(m_bindings.bindings().data(), previous);
}

TEST_F(UiInputBindingsTests, MalformedKeysEnumsAndModifiersPreserveAcceptedProfile){
    const InputKeyBinding accepted{ Core::Key::W, 0, 0, InputCommand::Left };
    ASSERT_TRUE(m_bindings.set(&accepted, 1u));
    const InputKeyBinding* previous = m_bindings.bindings().data();
    const i32 invalidKeys[]{
        Core::Key::Unknown, Core::Key::Space - 1, Core::Key::Menu + 1, Core::Key::Apostrophe + 1, Core::Key::GraveAccent + 1
    };
    for(const i32 key : invalidKeys){
        InputKeyBinding invalid = accepted;
        invalid.key = key;
        EXPECT_FALSE(m_bindings.set(&invalid, 1u));
        EXPECT_EQ(m_bindings.bindings().data(), previous);
        EXPECT_EQ(m_bindings.resolve(key, 0).command, InputCommand::None);
    }
    InputKeyBinding invalid = accepted;
    invalid.command = InputCommand::None;
    EXPECT_FALSE(m_bindings.set(&invalid, 1u));
    invalid.command = static_cast<InputCommand::Enum>(255u);
    EXPECT_FALSE(m_bindings.set(&invalid, 1u));
    invalid = accepted;
    invalid.selection = static_cast<InputSelectionPolicy::Enum>(255u);
    EXPECT_FALSE(m_bindings.set(&invalid, 1u));
    invalid = accepted;
    invalid.modifiers = Core::InputModifier::CapsLock;
    EXPECT_FALSE(m_bindings.set(&invalid, 1u));
    invalid = accepted;
    invalid.ignoredModifiers = Core::InputModifier::NumLock;
    EXPECT_FALSE(m_bindings.set(&invalid, 1u));
    invalid.modifiers = invalid.ignoredModifiers = Core::InputModifier::Shift;
    EXPECT_FALSE(m_bindings.set(&invalid, 1u));
    EXPECT_FALSE(m_bindings.set(nullptr, 1u));
    EXPECT_EQ(m_bindings.resolve(Core::Key::W, 0x0040).command, InputCommand::None);
    EXPECT_EQ(m_bindings.bindings().data(), previous);
    EXPECT_EQ(m_bindings.resolve(Core::Key::W, 0).command, InputCommand::Left);
}

TEST_F(UiInputBindingsTests, WorldKeyCodesRejectSparseNeighborsWithoutReplacingTheAcceptedProfile){
    const InputKeyBinding accepted[]{
        { Core::Key::World1, 0, 0, InputCommand::Left },
        { Core::Key::World2, 0, 0, InputCommand::Right },
    };
    ASSERT_TRUE(m_bindings.set(accepted, 2u));
    const InputKeyBinding* previous = m_bindings.bindings().data();
    const i32 invalidKeys[]{ Core::Key::GraveAccent + 1, Core::Key::World1 - 1, Core::Key::World2 + 1 };
    for(const i32 key : invalidKeys){
        InputKeyBinding invalid = accepted[0u];
        invalid.key = key;
        EXPECT_FALSE(m_bindings.set(&invalid, 1u));
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
    ASSERT_TRUE(m_bindings.set(custom.data(), s_InputMaxBindings));
    const InputKeyBinding* previous = m_bindings.bindings().data();
    EXPECT_FALSE(m_bindings.set(custom.data(), custom.size()));
    EXPECT_EQ(m_bindings.bindings().data(), previous);
    EXPECT_EQ(m_bindings.resolve(Core::Key::F16, s_InputBindingModifierMask).command, InputCommand::Right);
    EXPECT_EQ(m_bindings.resolve(Core::Key::F17, 0).command, InputCommand::None);
}

TEST_F(UiInputBindingsTests, EmptyAndAliasedProfileReplacementCanRecoverToDefaults){
    const InputKeyBinding custom[]{
        { Core::Key::W, 0, 0, InputCommand::Up },
        { Core::Key::F, 0, 0, InputCommand::Activate },
    };
    ASSERT_TRUE(m_bindings.set(custom, 2u));
    ASSERT_TRUE(m_bindings.set(m_bindings.bindings().data() + 1u, 1u));
    EXPECT_EQ(m_bindings.resolve(Core::Key::W, 0).command, InputCommand::None);
    EXPECT_EQ(m_bindings.resolve(Core::Key::F, 0).command, InputCommand::Activate);
    ASSERT_TRUE(m_bindings.set(nullptr, 0u));
    EXPECT_EQ(m_bindings.resolve(Core::Key::F, 0).command, InputCommand::None);
    EXPECT_EQ(m_bindings.resolve(Core::Key::Tab, 0).command, InputCommand::None);
    m_bindings.restoreDefaults();
    EXPECT_EQ(m_bindings.resolve(Core::Key::F, 0).command, InputCommand::None);
    EXPECT_EQ(m_bindings.resolve(Core::Key::Tab, Core::InputModifier::Shift).command, InputCommand::FocusPrevious);
}

TEST_F(UiInputBindingsTests, IgnoredShiftUsesExplicitSelectionPolicyWithoutChangingCommand){
    const InputKeyBinding custom[]{
        { Core::Key::W, 0, Core::InputModifier::Shift, InputCommand::Left, InputSelectionPolicy::Shift },
        { Core::Key::F, 0, Core::InputModifier::Shift, InputCommand::Right, InputSelectionPolicy::Always },
        { Core::Key::S, 0, Core::InputModifier::Shift, InputCommand::Down, InputSelectionPolicy::None },
    };
    ASSERT_TRUE(m_bindings.set(custom, 3u));
    EXPECT_FALSE(m_bindings.resolve(Core::Key::W, 0).extend);
    EXPECT_TRUE(m_bindings.resolve(Core::Key::W, Core::InputModifier::Shift | Core::InputModifier::CapsLock).extend);
    EXPECT_EQ(m_bindings.resolve(Core::Key::W, Core::InputModifier::Shift).command, InputCommand::Left);
    EXPECT_TRUE(m_bindings.resolve(Core::Key::F, 0).extend);
    EXPECT_TRUE(m_bindings.resolve(Core::Key::F, Core::InputModifier::Shift).extend);
    EXPECT_FALSE(m_bindings.resolve(Core::Key::S, Core::InputModifier::Shift).extend);
    EXPECT_EQ(m_bindings.resolve(Core::Key::W, Core::InputModifier::Shift | Core::InputModifier::Control).command, InputCommand::None);
}

TEST_F(UiInputBindingsTests, DefaultModifierBoundariesPreserveOsAndEditorAdmission){
    const InputCommandIntent reverse = m_bindings.resolve(Core::Key::Tab, s_InputBindingModifierMask);
    EXPECT_EQ(reverse.command, InputCommand::FocusPrevious);
    EXPECT_FALSE(reverse.edit);
    const InputCommandIntent altLeft = m_bindings.resolve(Core::Key::Left, Core::InputModifier::Alt | Core::InputModifier::Shift);
    EXPECT_EQ(altLeft.command, InputCommand::Left);
    EXPECT_TRUE(altLeft.extend);
    EXPECT_FALSE(altLeft.edit);
    EXPECT_FALSE(m_bindings.resolve(Core::Key::Up, Core::InputModifier::Control).edit);
    EXPECT_EQ(m_bindings.resolve(Core::Key::Enter, Core::InputModifier::Control).command, InputCommand::Submit);
    EXPECT_EQ(m_bindings.resolve(Core::Key::KeypadEnter, Core::InputModifier::Alt).command, InputCommand::Accept);
    EXPECT_FALSE(m_bindings.resolve(Core::Key::KeypadEnter, Core::InputModifier::Alt).edit);
    EXPECT_EQ(m_bindings.resolve(Core::Key::Delete, Core::InputModifier::Shift).command, InputCommand::Cut);
    EXPECT_EQ(m_bindings.resolve(Core::Key::Delete, Core::InputModifier::Shift | Core::InputModifier::Control).command, InputCommand::WordDelete);
    EXPECT_EQ(m_bindings.resolve(Core::Key::Insert, Core::InputModifier::Control | Core::InputModifier::Shift).command, InputCommand::None);
    EXPECT_EQ(m_bindings.resolve(Core::Key::A, Core::InputModifier::Control | Core::InputModifier::Alt).command, InputCommand::None);
    EXPECT_EQ(m_bindings.resolve(Core::Key::F10, Core::InputModifier::Shift | Core::InputModifier::Super).command, InputCommand::ContextMenu);
    EXPECT_EQ(m_bindings.resolve(Core::Key::F10, Core::InputModifier::Shift | Core::InputModifier::Control).command, InputCommand::None);
}

TEST_F(UiInputBindingsTests, ReplacingSpaceActivationDoesNotImplicitlyAdmitItsOrdinaryCharacter){
    const InputCommandIntent ordinarySpace = m_bindings.resolve(Core::Key::Space, 0);
    ASSERT_EQ(ordinarySpace.command, InputCommand::Activate);
    ASSERT_TRUE(ordinarySpace.allowText);
    InputKeyBinding activation{ .key = Core::Key::W, .command = InputCommand::Activate };
    ASSERT_TRUE(m_bindings.set(&activation, 1u));
    EXPECT_FALSE(m_bindings.resolve(Core::Key::W, 0).allowText);
    EXPECT_EQ(m_bindings.resolve(Core::Key::Space, 0).command, InputCommand::None);
    activation.allowText = true;
    ASSERT_TRUE(m_bindings.set(&activation, 1u));
    EXPECT_TRUE(m_bindings.resolve(Core::Key::W, 0).allowText);
    activation.allowText = false;
    ASSERT_TRUE(m_bindings.set(&activation, 1u));
    EXPECT_FALSE(m_bindings.resolve(Core::Key::W, 0).allowText);
    m_bindings.restoreDefaults();
    EXPECT_TRUE(m_bindings.resolve(Core::Key::Space, 0).allowText);
    EXPECT_EQ(m_bindings.resolve(Core::Key::W, 0).command, InputCommand::None);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

