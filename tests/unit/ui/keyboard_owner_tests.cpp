// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ui/input/router.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_keyboard_owner_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

[[nodiscard]] static HitTarget Editor(){
    HitTarget target;
    target.id = { 1u };
    target.declarationGeneration = 7u;
    target.rectangle = { 0.0f, 0.0f, 120.0f, 24.0f };
    target.clip = { 0.0f, 0.0f, 300.0f, 200.0f };
    target.focusable = true;
    target.textEditable = true;
    target.control = { 101u, 102u, 103u };
    return target;
}

[[nodiscard]] static HitTarget List(const u64 id = 2u, const f32 x = 0.0f){
    HitTarget target;
    target.id = { id };
    target.declarationGeneration = 11u;
    target.rectangle = { x, 40.0f, 120.0f, 120.0f };
    target.clip = { 0.0f, 0.0f, 300.0f, 200.0f };
    target.focusable = true;
    target.navigable = true;
    target.control = { 31u, 41u, 51u };
    target.pageRows = 5u;
    return target;
}

[[nodiscard]] static InputEvent Key(const InputEventType::Enum type, const InputKey::Enum key,
    const bool repeat = false){
    InputEvent event;
    event.type = type;
    event.key = key;
    event.repeat = repeat;
    return event;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiKeyboardOwnerTests : public testing::Test{
public:
    UiKeyboardOwnerTests()
        : m_arena(Name("tests/ui/keyboard_owner"))
        , m_router(m_arena)
    {
        m_targets = { Editor(), List(), List(3u, 140.0f) };
        m_targets[2u].control.instanceGeneration = 32u;
        bind(m_targets[1u]);
    }


protected:
    void bind(const HitTarget& owner){
        m_targets[0u].keyboardOwner = owner.id;
        m_targets[0u].keyboardOwnerDeclarationGeneration = owner.declarationGeneration;
        m_targets[0u].keyboardControl = owner.control;
    }

    void clearBinding(){
        m_targets[0u].keyboardOwner = {};
        m_targets[0u].keyboardOwnerDeclarationGeneration = 0u;
        m_targets[0u].keyboardControl = {};
    }

    [[nodiscard]] InputRoutingResult send(const InputEvent& event){
        EXPECT_TRUE(m_router.queue(event));
        return m_router.process();
    }

    void press(const InputKey::Enum key){
        EXPECT_TRUE(send(Key(InputEventType::KeyDown, key)).keyboardConsumed);
        EXPECT_TRUE(send(Key(InputEventType::KeyUp, key)).keyboardConsumed);
    }

    void focus(){
        InputEvent event;
        event.type = InputEventType::PrimaryDown;
        event.position = { 10.0f, 10.0f };
        EXPECT_TRUE(send(event).pointerConsumed);
        event.type = InputEventType::PrimaryUp;
        EXPECT_TRUE(send(event).pointerConsumed);
        EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    }

    [[nodiscard]] bool take(ControlAction& action, const usize owner = 1u){
        const HitTarget& target = m_targets[owner];
        return m_router.consumeControlAction(target.id, target.declarationGeneration, target.control, action);
    }

    [[nodiscard]] PopupScope popup(){
        PopupScope scope;
        scope.token = { { 10u }, 12u, 13u, 14u };
        scope.bounds = { 0.0f, 0.0f, 300.0f, 200.0f };
        scope.viewport = scope.bounds;
        scope.layer = 1u;
        return scope;
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    InputRouter m_router;
    Array<HitTarget, 3u> m_targets;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiKeyboardOwnerTests, NavigationCopiesOwnerLifetimeAndKeepsFocusInTheEditor){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    const Array<InputKey::Enum, 4u> keys{ InputKey::Up, InputKey::Down, InputKey::PageUp, InputKey::PageDown };
    const Array<ControlActionKind::Enum, 4u> kinds{ ControlActionKind::Up, ControlActionKind::Down,
        ControlActionKind::PageUp, ControlActionKind::PageDown };
    for(usize index = 0u; index < keys.size(); ++index){
        press(keys[index]);
        ControlAction action;
        ASSERT_TRUE(take(action));
        EXPECT_EQ(action.kind, kinds[index]);
        EXPECT_EQ(action.id.target, m_targets[1u].id);
        EXPECT_EQ(action.id.declarationGeneration, m_targets[1u].declarationGeneration);
        EXPECT_EQ(action.id.layoutGeneration, 1u);
        EXPECT_EQ(action.control, m_targets[1u].control);
        EXPECT_EQ(action.pageRows, 5u);
        EXPECT_EQ(action.source, m_targets[0u].id);
        EXPECT_EQ(action.sourceDeclarationGeneration, m_targets[0u].declarationGeneration);
        EXPECT_EQ(action.sourceControl, m_targets[0u].control);
        EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    }
    EXPECT_TRUE(m_router.actions().empty());
}

TEST_F(UiKeyboardOwnerTests, EnterIntentionSharesNavigationSequenceWithoutALegacyActivation){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    press(InputKey::Down);
    press(InputKey::Enter);
    press(InputKey::Up);
    ControlAction down;
    ControlAction submit;
    ControlAction up;
    ASSERT_TRUE(take(down));
    ASSERT_TRUE(take(submit));
    ASSERT_TRUE(take(up));
    EXPECT_EQ(down.kind, ControlActionKind::Down);
    EXPECT_EQ(submit.kind, ControlActionKind::Submit);
    EXPECT_EQ(up.kind, ControlActionKind::Up);
    EXPECT_LT(down.id.sequence, submit.id.sequence);
    EXPECT_LT(submit.id.sequence, up.id.sequence);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
}

TEST_F(UiKeyboardOwnerTests, EditingKeysRemainAtTheEditorWithoutListActions){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    const Array<InputKey::Enum, 10u> keys{ InputKey::Home, InputKey::End, InputKey::Left, InputKey::Right,
        InputKey::Space, InputKey::Backspace, InputKey::Delete, InputKey::A, InputKey::V, InputKey::Escape };
    for(const auto key : keys)
        press(key);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
}

TEST_F(UiKeyboardOwnerTests, NormalEditorWithoutABindingHasNoDelegatedControlActions){
    clearBinding();
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    press(InputKey::Down);
    press(InputKey::Enter);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(m_router.actions().empty());
}

TEST_F(UiKeyboardOwnerTests, AcceptedOwnerGeometryAndPageRowsRemainUntilTheCandidateIsCommitted){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    m_targets[1u].pageRows = 17u;
    m_targets[1u].rectangle.height = 180.0f;
    press(InputKey::PageDown);
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.pageRows, 5u);
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    press(InputKey::PageDown);
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.pageRows, 17u);
    EXPECT_EQ(action.id.layoutGeneration, 2u);
}

TEST_F(UiKeyboardOwnerTests, MissingDisabledAndNonNavigableOwnersRejectCandidateAcceptance){
    EXPECT_FALSE(m_router.commitTargets(&m_targets[0u], 1u, 1u));
    m_targets[1u].enabled = false;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    m_targets[1u].enabled = true;
    m_targets[1u].navigable = false;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    m_targets[1u].navigable = true;
    m_targets[1u].focusable = false;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(m_router.targets().empty());
}

TEST_F(UiKeyboardOwnerTests, PartialBindingAndWrongOwnerEpochRejectRatherThanSwallowingEditorKeys){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    ++m_targets[0u].keyboardOwnerDeclarationGeneration;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_EQ(m_router.layoutGeneration(), 1u);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    bind(m_targets[1u]);
    ++m_targets[0u].keyboardControl.contentRevision;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    bind(m_targets[1u]);
    m_targets[0u].keyboardOwner = {};
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    press(InputKey::Down);
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.id.layoutGeneration, 1u);
}

TEST_F(UiKeyboardOwnerTests, NonEditableDisabledAndPartSourcesCannotBorrowNavigation){
    m_targets[0u].textEditable = false;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    m_targets[0u].textEditable = true;
    m_targets[0u].enabled = false;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    m_targets[0u].enabled = true;
    m_targets[0u].focusable = false;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    m_targets[0u].focusable = true;
    m_targets[0u].owner = m_targets[1u].id;
    m_targets[0u].ownerDeclarationGeneration = m_targets[1u].declarationGeneration;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
}

TEST_F(UiKeyboardOwnerTests, SelfDelegationAndCompetingNavigationHostsAreRejected){
    m_targets[0u].keyboardOwner = m_targets[0u].id;
    m_targets[0u].keyboardOwnerDeclarationGeneration = m_targets[0u].declarationGeneration;
    m_targets[0u].keyboardControl = m_targets[0u].control;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    bind(m_targets[1u]);
    m_targets[0u].navigable = true;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
}

TEST_F(UiKeyboardOwnerTests, RebindingEditorRetiresQueuedActionsAndTheHeldInitialOwnerPermanently){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down)).keyboardConsumed);
    ASSERT_EQ(m_router.controlActions().size(), 1u);
    bind(m_targets[2u]);
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_TRUE(m_router.controlActions().empty());
    bind(m_targets[1u]);
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 3u));
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down, true)).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(send(Key(InputEventType::KeyUp, InputKey::Down)).keyboardConsumed);
    press(InputKey::Down);
    ControlAction action;
    EXPECT_TRUE(take(action));
}

TEST_F(UiKeyboardOwnerTests, OmittedEditorRetiresItsActionsEvenWhenTheListSurvives){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down)).keyboardConsumed);
    ASSERT_TRUE(m_router.commitTargets(&m_targets[1u], 2u, 2u));
    EXPECT_TRUE(m_router.controlActions().empty());
    m_targets[1u].focusOnCommit = true;
    ASSERT_TRUE(m_router.commitTargets(&m_targets[1u], 2u, 3u));
    EXPECT_EQ(m_router.focus(), m_targets[1u].id);
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down, true)).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(send(Key(InputEventType::KeyUp, InputKey::Down)).keyboardConsumed);
}

TEST_F(UiKeyboardOwnerTests, RemovingTheBindingFencesHeldNavigationWhileOrdinaryEditingContinues){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down)).keyboardConsumed);
    clearBinding();
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_TRUE(m_router.controlActions().empty());
    press(InputKey::Left);
    bind(m_targets[1u]);
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 3u));
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down, true)).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(send(Key(InputEventType::KeyUp, InputKey::Down)).keyboardConsumed);
}

TEST_F(UiKeyboardOwnerTests, ReplacedEditorDeclarationOrModelTokenRetiresCopiedActions){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    press(InputKey::Down);
    ++m_targets[0u].declarationGeneration;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_TRUE(m_router.controlActions().empty());
    focus();
    press(InputKey::Down);
    ++m_targets[0u].control.instanceGeneration;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 3u));
    EXPECT_TRUE(m_router.controlActions().empty());
}

TEST_F(UiKeyboardOwnerTests, FenceControlRetiresTheDelegateWithoutStealingEditorFocus){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down)).keyboardConsumed);
    ControlToken replacement = m_targets[1u].control;
    ++replacement.contentRevision;
    m_router.fenceControl(m_targets[1u].id, m_targets[1u].declarationGeneration, replacement);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down, true)).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(send(Key(InputEventType::KeyUp, InputKey::Down)).keyboardConsumed);
}

TEST_F(UiKeyboardOwnerTests, PopupDelegationRetainsItsAcceptedScopeAndCannotCrossLayers){
    PopupScope scope = popup();
    for(auto& target : m_targets){
        target.popup = scope.token;
        target.layer = scope.layer;
    }
    HitTarget barrier;
    barrier.id = scope.token.widget;
    barrier.declarationGeneration = scope.token.declarationGeneration;
    barrier.rectangle = scope.bounds;
    barrier.clip = scope.viewport;
    barrier.popup = scope.token;
    barrier.layer = scope.layer;
    Array<HitTarget, 4u> targets{ barrier, m_targets[0u], m_targets[1u], m_targets[2u] };
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u, &scope, 1u));
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    press(InputKey::Down);
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.popup, scope.token);
    targets[2u].popup = {};
    targets[2u].layer = 0u;
    EXPECT_FALSE(m_router.commitTargets(targets.data(), targets.size(), 2u, &scope, 1u));
    EXPECT_EQ(m_router.layoutGeneration(), 1u);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
}

TEST_F(UiKeyboardOwnerTests, HeldEnterCannotSubmitAgainUntilItsConsumedRelease){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Enter)).keyboardConsumed);
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::Submit);
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Enter, true)).keyboardConsumed);
    EXPECT_FALSE(take(action));
    EXPECT_TRUE(send(Key(InputEventType::KeyUp, InputKey::Enter)).keyboardConsumed);
    press(InputKey::Enter);
    EXPECT_TRUE(take(action));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

