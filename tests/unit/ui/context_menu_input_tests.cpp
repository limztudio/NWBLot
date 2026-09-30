// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/input/router.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_context_menu_input_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

[[nodiscard]] static HitTarget Target(const u64 id = 1u, const Rect& rectangle = { 10.0f, 10.0f, 40.0f, 20.0f }){
    HitTarget target;
    target.id = { id };
    target.declarationGeneration = 3u;
    target.rectangle = rectangle;
    target.clip = { 0.0f, 0.0f, 200.0f, 200.0f };
    target.focusable = true;
    target.activatable = true;
    target.control = { 7u, 8u, 9u };
    target.contextMenu = true;
    return target;
}

[[nodiscard]] static InputEvent Pointer(const InputEventType::Enum type, const Point& position = { 15.0f, 15.0f }){
    InputEvent event;
    event.type = type;
    event.position = position;
    return event;
}

[[nodiscard]] static InputEvent Key(const InputKey::Enum key, const bool shift = false,
    const bool control = false, const bool alt = false, const bool repeat = false){
    InputEvent event;
    event.type = InputEventType::KeyDown;
    event.key = key;
    event.shift = shift;
    event.control = control;
    event.alt = alt;
    event.repeat = repeat;
    return event;
}

[[nodiscard]] static PopupScope Scope(const u64 openGeneration = 1u){
    PopupScope scope;
    scope.token = { { 100u }, 11u, 12u, openGeneration };
    scope.bounds = { 40.0f, 40.0f, 100.0f, 100.0f };
    scope.viewport = { 0.0f, 0.0f, 200.0f, 200.0f };
    scope.layer = 1u;
    return scope;
}

[[nodiscard]] static Array<HitTarget, 3u> PopupTargets(const PopupScope& scope){
    HitTarget barrier;
    barrier.id = scope.token.widget;
    barrier.declarationGeneration = scope.token.declarationGeneration;
    barrier.rectangle = scope.bounds;
    barrier.clip = scope.viewport;
    barrier.popup = scope.token;
    barrier.layer = scope.layer;
    HitTarget child = Target(101u, { 50.0f, 50.0f, 40.0f, 20.0f });
    child.clip = scope.bounds;
    child.paintOrder = 1u;
    child.popup = scope.token;
    child.layer = scope.layer;
    return { Target(), barrier, child };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiContextMenuInputTests : public testing::Test{
public:
    UiContextMenuInputTests()
        : m_arena(Name("tests/ui/context_menu_input"))
        , m_router(m_arena)
    {}


protected:
    [[nodiscard]] InputRoutingResult send(const InputEvent& event){
        EXPECT_TRUE(m_router.queue(event));
        return m_router.process();
    }

    void focus(){
        EXPECT_TRUE(send(Key(InputKey::Tab)).keyboardConsumed);
        EXPECT_TRUE(release(InputKey::Tab).keyboardConsumed);
    }

    [[nodiscard]] InputRoutingResult release(const InputKey::Enum key){
        InputEvent event;
        event.type = InputEventType::KeyUp;
        event.key = key;
        return send(event);
    }

    [[nodiscard]] bool take(ContextMenuAction& action, const u64 id = 1u, const u64 declaration = 3u){
        return m_router.consumeContextMenu({ id }, declaration, action);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    InputRouter m_router;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiContextMenuInputTests, SecondaryPressCopiesAcceptedLifetimeWithoutActivationOrEditorSelection){
    HitTarget target = Target();
    target.textEditable = true;
    target.pointerGesture = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 42u));
    focus();
    const InputRoutingResult down = send(Pointer(InputEventType::SecondaryDown, { 16.0f, 18.0f }));
    EXPECT_TRUE(down.pointerConsumed);
    EXPECT_TRUE(m_router.secondaryDown());
    EXPECT_FALSE(m_router.primaryDown());
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_EQ(m_router.focus(), target.id);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(m_router.controlActions().empty());
    PointerGesture gesture;
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, target.declarationGeneration, gesture));
    ContextMenuAction action;
    ASSERT_TRUE(take(action));
    EXPECT_TRUE(action.id.valid());
    EXPECT_EQ(action.id.target, target.id);
    EXPECT_EQ(action.id.declarationGeneration, target.declarationGeneration);
    EXPECT_EQ(action.id.layoutGeneration, 42u);
    EXPECT_EQ(action.popup, target.popup);
    EXPECT_EQ(action.control, target.control);
    EXPECT_FLOAT_EQ(action.position.x, 16.0f);
    EXPECT_FLOAT_EQ(action.position.y, 18.0f);
    EXPECT_FALSE(action.keyboard);
    EXPECT_FALSE(take(action));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryUp)).pointerConsumed);
    EXPECT_FALSE(take(action));
    EXPECT_FALSE(m_router.consumeActivation(target.id));
}

TEST_F(UiContextMenuInputTests, SecondaryMenuOnAnotherParentAnchorPreservesAndRestoresPriorFocus){
    const PopupScope parent = Scope();
    const auto parentTargets = PopupTargets(parent);
    HitTarget anchor = Target(102u, { 100.0f, 55.0f, 25.0f, 20.0f });
    anchor.popup = parent.token;
    anchor.layer = parent.layer;
    anchor.clip = parent.bounds;
    anchor.paintOrder = 2u;
    const Array<HitTarget, 4u> closed = { parentTargets[0u], parentTargets[1u], parentTargets[2u], anchor };
    ASSERT_TRUE(m_router.commitTargets(closed.data(), closed.size(), 1u, &parent, 1u));
    const WidgetId previousFocus = parentTargets[2u].id;
    ASSERT_EQ(m_router.focus(), previousFocus);
    ASSERT_NE(previousFocus, anchor.id);
    const Point position{ 110.0f, 60.0f };
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown, position)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryUp, position)).pointerConsumed);
    EXPECT_EQ(m_router.focus(), previousFocus);
    ContextMenuAction action;
    ASSERT_TRUE(take(action, anchor.id.value, anchor.declarationGeneration));
    EXPECT_EQ(action.popup, parent.token);
    EXPECT_FALSE(action.keyboard);
    EXPECT_TRUE(m_router.actions().empty());

    PopupScope menu;
    menu.token = { { 200u }, 21u, 31u, 1u };
    menu.parent = parent.token;
    menu.bounds = { 145.0f, 110.0f, 50.0f, 60.0f };
    menu.viewport = parent.viewport;
    menu.layer = 2u;
    HitTarget barrier;
    barrier.id = menu.token.widget;
    barrier.declarationGeneration = menu.token.declarationGeneration;
    barrier.rectangle = menu.bounds;
    barrier.clip = menu.viewport;
    barrier.popup = menu.token;
    barrier.layer = menu.layer;
    barrier.paintOrder = 3u;
    HitTarget command = Target(201u, { 150.0f, 115.0f, 40.0f, 20.0f });
    command.popup = menu.token;
    command.layer = menu.layer;
    command.clip = menu.bounds;
    command.paintOrder = 4u;
    const Array<HitTarget, 6u> opened = { closed[0u], closed[1u], closed[2u], closed[3u], barrier, command };
    const Array<PopupScope, 2u> scopes = { parent, menu };
    ASSERT_TRUE(m_router.commitTargets(opened.data(), opened.size(), 2u, scopes.data(), scopes.size()));
    EXPECT_EQ(m_router.focus(), command.id);
    EXPECT_TRUE(send(Key(InputKey::Escape)).keyboardConsumed);
    EXPECT_TRUE(release(InputKey::Escape).keyboardConsumed);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    ASSERT_TRUE(m_router.consumePopupDismissal(menu.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::Escape);
    EXPECT_FALSE(m_router.consumePopupDismissal(parent.token, reason));
    ASSERT_TRUE(m_router.commitTargets(closed.data(), closed.size(), 3u, &parent, 1u));
    EXPECT_EQ(m_router.focus(), previousFocus);
    EXPECT_NE(m_router.focus(), anchor.id);
    EXPECT_FALSE(m_router.consumeActivation(anchor.id));
}

TEST_F(UiContextMenuInputTests, HeldSecondaryConsumesMovementLeaveAndReleaseOutsideTheTarget){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    EXPECT_TRUE(m_router.pointerKnown());
    EXPECT_FLOAT_EQ(m_router.pointerPosition().x, 15.0f);
    EXPECT_TRUE(send(Pointer(InputEventType::PointerLeave)).pointerConsumed);
    EXPECT_FALSE(m_router.pointerKnown());
    EXPECT_TRUE(m_router.secondaryDown());
    EXPECT_TRUE(m_router.wantsPointer());
    EXPECT_TRUE(send(Pointer(InputEventType::PointerMove, { 180.0f, 180.0f })).pointerConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryUp, { 180.0f, 180.0f })).pointerConsumed);
    EXPECT_FALSE(m_router.secondaryDown());
    EXPECT_FALSE(m_router.wantsPointer());
}

TEST_F(UiContextMenuInputTests, DuplicateSecondaryDownCannotTriggerAnotherTargetDuringTheSamePress){
    const Array<HitTarget, 2u> targets = { Target(), Target(2u, { 70.0f, 10.0f, 40.0f, 20.0f }) };
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    ContextMenuAction action;
    ASSERT_TRUE(take(action));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown, { 75.0f, 15.0f })).pointerConsumed);
    EXPECT_FALSE(take(action));
    EXPECT_FALSE(take(action, 2u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryUp, { 75.0f, 15.0f })).pointerConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown, { 75.0f, 15.0f })).pointerConsumed);
    ASSERT_TRUE(take(action, 2u));
    EXPECT_EQ(action.id.target, targets[1u].id);
}

TEST_F(UiContextMenuInputTests, OrdinaryAcceptedSurfaceConsumesSecondaryWithoutAContextTrigger){
    HitTarget target = Target();
    target.contextMenu = false;
    target.pointerGesture = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryUp)).pointerConsumed);
    ContextMenuAction action;
    EXPECT_FALSE(take(action));
    EXPECT_FALSE(m_router.consumeActivation(target.id));
    PointerGesture gesture;
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, target.declarationGeneration, gesture));
}

TEST_F(UiContextMenuInputTests, MenuAndShiftF10UseTheAcceptedKeyboardAnchorOncePerPress){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 5u));
    focus();
    EXPECT_FALSE(m_router.pointerKnown());
    for(const InputKey::Enum key : { InputKey::Menu, InputKey::F10 }){
        const bool shift = key == InputKey::F10;
        EXPECT_TRUE(send(Key(key, shift)).keyboardConsumed);
        EXPECT_TRUE(m_router.ownsKey(key));
        ContextMenuAction action;
        ASSERT_TRUE(take(action));
        EXPECT_TRUE(action.keyboard);
        EXPECT_FLOAT_EQ(action.position.x, target.rectangle.x);
        EXPECT_FLOAT_EQ(action.position.y, target.rectangle.y + target.rectangle.height);
        EXPECT_EQ(action.control, target.control);
        EXPECT_EQ(action.id.layoutGeneration, 5u);
        EXPECT_TRUE(send(Key(key, shift, false, false, true)).keyboardConsumed);
        EXPECT_TRUE(send(Key(key, shift)).keyboardConsumed);
        EXPECT_FALSE(take(action));
        EXPECT_TRUE(release(key).keyboardConsumed);
        EXPECT_FALSE(m_router.ownsKey(key));
    }
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(m_router.controlActions().empty());
}

TEST_F(UiContextMenuInputTests, OtherF10ModifierCombinationsStayWithTheNormalFocusedOwner){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    focus();
    const Array<InputEvent, 5u> events = {
        Key(InputKey::F10), Key(InputKey::F10, false, true), Key(InputKey::F10, false, false, true),
        Key(InputKey::F10, true, true), Key(InputKey::F10, true, false, true)
    };
    ContextMenuAction action;
    for(const auto& event : events){
        EXPECT_TRUE(send(event).keyboardConsumed);
        EXPECT_FALSE(take(action));
        EXPECT_TRUE(release(InputKey::F10).keyboardConsumed);
    }
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(m_router.controlActions().empty());
}

TEST_F(UiContextMenuInputTests, UnfocusedOrUnboundContextKeysDoNotCreateTriggers){
    HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_FALSE(send(Key(InputKey::Menu)).keyboardConsumed);
    EXPECT_FALSE(release(InputKey::Menu).keyboardConsumed);
    EXPECT_FALSE(send(Key(InputKey::F10, true)).keyboardConsumed);
    EXPECT_FALSE(release(InputKey::F10).keyboardConsumed);
    target.contextMenu = false;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    focus();
    EXPECT_TRUE(send(Key(InputKey::Menu)).keyboardConsumed);
    EXPECT_TRUE(release(InputKey::Menu).keyboardConsumed);
    ContextMenuAction action;
    EXPECT_FALSE(take(action));
}

TEST_F(UiContextMenuInputTests, RepeatWithoutAnInitialPressCannotOpenAMenu){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    focus();
    EXPECT_TRUE(send(Key(InputKey::Menu, false, false, false, true)).keyboardConsumed);
    EXPECT_TRUE(release(InputKey::Menu).keyboardConsumed);
    EXPECT_TRUE(send(Key(InputKey::F10, true, false, false, true)).keyboardConsumed);
    EXPECT_TRUE(release(InputKey::F10).keyboardConsumed);
    ContextMenuAction action;
    EXPECT_FALSE(take(action));
}

TEST_F(UiContextMenuInputTests, SceneOwnedContextKeysCannotTransferToAnEditorThatReceivesFocusDuringTheHold){
    HitTarget target = Target();
    target.textEditable = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_FALSE(send(Key(InputKey::Menu)).keyboardConsumed);
    target.focusOnCommit = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    EXPECT_FALSE(send(Key(InputKey::Menu, false, false, false, true)).keyboardConsumed);
    EXPECT_FALSE(release(InputKey::Menu).keyboardConsumed);
    ContextMenuAction action;
    EXPECT_FALSE(take(action));
    EXPECT_TRUE(send(Key(InputKey::Menu)).keyboardConsumed);
    EXPECT_TRUE(take(action));
    EXPECT_TRUE(release(InputKey::Menu).keyboardConsumed);
    m_router.clearFocus();
    EXPECT_FALSE(send(Key(InputKey::F10)).keyboardConsumed);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 3u));
    EXPECT_FALSE(send(Key(InputKey::F10, true, false, false, true)).keyboardConsumed);
    EXPECT_FALSE(release(InputKey::F10).keyboardConsumed);
    EXPECT_FALSE(take(action));
    EXPECT_TRUE(send(Key(InputKey::F10, true)).keyboardConsumed);
    EXPECT_TRUE(take(action));
}

TEST_F(UiContextMenuInputTests, OnlyTheTopAcceptedHitMaySupplyAContextMenu){
    const HitTarget base = Target();
    HitTarget cover = Target(2u);
    cover.contextMenu = false;
    cover.paintOrder = 1u;
    const Array<HitTarget, 2u> targets = { base, cover };
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    ContextMenuAction action;
    EXPECT_FALSE(take(action));
    EXPECT_FALSE(take(action, 2u));
}

TEST_F(UiContextMenuInputTests, RowAndScrollbarPartsResolveTheContextMenuToTheirAcceptedControlHost){
    HitTarget host = Target(1u, { 10.0f, 10.0f, 80.0f, 70.0f });
    host.navigable = true;
    host.activatable = false;
    HitTarget part = Target(2u, { 10.0f, 10.0f, 40.0f, 20.0f });
    part.contextMenu = false;
    part.focusable = false;
    part.owner = host.id;
    part.ownerDeclarationGeneration = host.declarationGeneration;
    part.paintOrder = 1u;
    part.pointerGesture = true;
    const Array<HitTarget, 2u> targets = { host, part };
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown, { 25.0f, 15.0f })).pointerConsumed);
    ContextMenuAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.id.target, host.id);
    EXPECT_EQ(action.id.declarationGeneration, host.declarationGeneration);
    EXPECT_EQ(action.control, host.control);
    EXPECT_FLOAT_EQ(action.position.x, 25.0f);
    EXPECT_FALSE(take(action, part.id.value));
    EXPECT_TRUE(m_router.controlActions().empty());
    PointerGesture gesture;
    EXPECT_FALSE(m_router.consumePointerGesture(part.id, part.declarationGeneration, gesture));
}

TEST_F(UiContextMenuInputTests, DisabledAndClippedTargetsCannotReceiveContextTriggers){
    HitTarget target = Target();
    target.enabled = false;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_FALSE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    EXPECT_FALSE(send(Pointer(InputEventType::SecondaryUp)).pointerConsumed);
    target.enabled = true;
    target.clip = { 100.0f, 100.0f, 20.0f, 20.0f };
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    EXPECT_FALSE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    EXPECT_FALSE(send(Key(InputKey::Menu)).keyboardConsumed);
    ContextMenuAction action;
    EXPECT_FALSE(take(action));
}

TEST_F(UiContextMenuInputTests, SceneOwnedSecondaryStaysWithTheSceneAfterAPopupAppears){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_FALSE(send(Pointer(InputEventType::SecondaryDown, { 180.0f, 180.0f })).pointerConsumed);
    const PopupScope scope = Scope();
    const auto targets = PopupTargets(scope);
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 2u, &scope, 1u));
    EXPECT_FALSE(send(Pointer(InputEventType::PointerMove, { 55.0f, 55.0f })).pointerConsumed);
    EXPECT_FALSE(m_router.wouldConsumePointer({ 55.0f, 55.0f }));
    EXPECT_FALSE(send(Pointer(InputEventType::PrimaryDown, { 55.0f, 55.0f })).pointerConsumed);
    EXPECT_FALSE(send(Pointer(InputEventType::PrimaryUp, { 55.0f, 55.0f })).pointerConsumed);
    EXPECT_FALSE(send(Pointer(InputEventType::SecondaryUp, { 55.0f, 55.0f })).pointerConsumed);
    EXPECT_TRUE(m_router.wantsPointer());
    ContextMenuAction action;
    EXPECT_FALSE(take(action, 101u));
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_FALSE(m_router.consumePopupDismissal(scope.token, reason));
    EXPECT_TRUE(m_router.actions().empty());
}

TEST_F(UiContextMenuInputTests, SceneOwnedPrimaryCannotTransferASecondaryPressOrWheelToAUiList){
    HitTarget target = Target();
    target.scrollable = true;
    target.navigable = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_FALSE(send(Pointer(InputEventType::PrimaryDown, { 180.0f, 180.0f })).pointerConsumed);
    EXPECT_FALSE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    InputEvent wheel = Pointer(InputEventType::PointerWheel);
    wheel.scrollY = 1.0;
    EXPECT_FALSE(send(wheel).pointerConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_FALSE(send(Pointer(InputEventType::PrimaryUp)).pointerConsumed);
    EXPECT_FALSE(send(wheel).pointerConsumed);
    EXPECT_FALSE(send(Pointer(InputEventType::SecondaryUp)).pointerConsumed);
    ContextMenuAction action;
    EXPECT_FALSE(take(action));
}

TEST_F(UiContextMenuInputTests, OutsideSecondaryDismissesTheTopPopupAndConsumesItsReleaseAfterRemoval){
    const PopupScope scope = Scope();
    const auto targets = PopupTargets(scope);
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u, &scope, 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    ASSERT_TRUE(m_router.consumePopupDismissal(scope.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::OutsideClick);
    ContextMenuAction action;
    EXPECT_FALSE(take(action));
    const HitTarget base = Target();
    ASSERT_TRUE(m_router.commitTargets(&base, 1u, 2u));
    EXPECT_TRUE(send(Pointer(InputEventType::PointerMove, { 180.0f, 180.0f })).pointerConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryUp, { 180.0f, 180.0f })).pointerConsumed);
    EXPECT_FALSE(m_router.wantsPointer());
    EXPECT_FALSE(m_router.consumeActivation(base.id));
}

TEST_F(UiContextMenuInputTests, PopupTriggersCopyTheExactScopeAndClosingItRetiresQueuedIntentions){
    const PopupScope scope = Scope();
    const auto targets = PopupTargets(scope);
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u, &scope, 1u));
    EXPECT_TRUE(send(Key(InputKey::Menu)).keyboardConsumed);
    ContextMenuAction action;
    ASSERT_TRUE(take(action, 101u));
    EXPECT_EQ(action.popup, scope.token);
    EXPECT_TRUE(release(InputKey::Menu).keyboardConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown, { 55.0f, 55.0f })).pointerConsumed);
    m_router.closePopup(scope.token);
    EXPECT_FALSE(take(action, 101u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryUp, { 180.0f, 180.0f })).pointerConsumed);
}

TEST_F(UiContextMenuInputTests, PopupReplacementCannotReplayAContextActionOrItsHeldPointerPress){
    const PopupScope first = Scope();
    const auto targets = PopupTargets(first);
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u, &first, 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown, { 55.0f, 55.0f })).pointerConsumed);
    const PopupScope replacement = Scope(2u);
    const auto replacementTargets = PopupTargets(replacement);
    ASSERT_TRUE(m_router.commitTargets(replacementTargets.data(), replacementTargets.size(), 2u, &replacement, 1u));
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 3u, &first, 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown, { 55.0f, 55.0f })).pointerConsumed);
    ContextMenuAction action;
    EXPECT_FALSE(take(action, 101u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryUp, { 55.0f, 55.0f })).pointerConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown, { 55.0f, 55.0f })).pointerConsumed);
    ASSERT_TRUE(take(action, 101u));
    EXPECT_EQ(action.popup, first.token);
}

TEST_F(UiContextMenuInputTests, DeclarationReplacementDiscardsIntentionsButPreservesHeldReleaseConsumption){
    HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    ++target.declarationGeneration;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    ContextMenuAction action;
    EXPECT_FALSE(take(action, 1u, target.declarationGeneration));
    --target.declarationGeneration;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 3u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    EXPECT_FALSE(take(action));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryUp)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    EXPECT_TRUE(take(action));
}

TEST_F(UiContextMenuInputTests, ControlRebindRetiresKeyboardIntentionsEvenWhenTheOldTokenReturns){
    HitTarget target = Target();
    target.focusOnCommit = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(Key(InputKey::Menu)).keyboardConsumed);
    ++target.control.contentRevision;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    --target.control.contentRevision;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 3u));
    EXPECT_TRUE(send(Key(InputKey::Menu, false, false, false, true)).keyboardConsumed);
    ContextMenuAction action;
    EXPECT_FALSE(take(action));
    EXPECT_TRUE(release(InputKey::Menu).keyboardConsumed);
    EXPECT_TRUE(send(Key(InputKey::Menu)).keyboardConsumed);
    EXPECT_TRUE(take(action));
}

TEST_F(UiContextMenuInputTests, DisablingOrOmittingAnAnchorRetiresItsPendingActionPermanently){
    HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    target.enabled = false;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    target.enabled = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 3u));
    ContextMenuAction action;
    EXPECT_FALSE(take(action));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryUp)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    ASSERT_TRUE(m_router.commitTargets(nullptr, 0u, 4u));
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 5u));
    EXPECT_FALSE(take(action));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryUp, { 180.0f, 180.0f })).pointerConsumed);
}

TEST_F(UiContextMenuInputTests, ExplicitTargetInvalidationAndControlFencesPruneContextActions){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    m_router.invalidateTarget(target.id);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    ContextMenuAction action;
    EXPECT_FALSE(take(action));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryUp)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    ControlToken changed = target.control;
    ++changed.contentRevision;
    m_router.fenceControl(target.id, target.declarationGeneration, changed);
    EXPECT_FALSE(take(action));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryUp)).pointerConsumed);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 3u));
    EXPECT_FALSE(take(action));
}

TEST_F(UiContextMenuInputTests, ConsumptionRequiresTheExactDeclarationAndKeepsTheAcceptedAnchorAfterMovement){
    HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 8u));
    focus();
    EXPECT_TRUE(send(Key(InputKey::Menu)).keyboardConsumed);
    ContextMenuAction action;
    action.id.target = { 999u };
    EXPECT_FALSE(take(action, 1u, 0u));
    EXPECT_FALSE(take(action, 1u, 4u));
    EXPECT_EQ(action.id.target.value, 999u);
    target.rectangle = { 80.0f, 80.0f, 30.0f, 30.0f };
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 9u));
    ASSERT_TRUE(take(action));
    EXPECT_FLOAT_EQ(action.position.x, 10.0f);
    EXPECT_FLOAT_EQ(action.position.y, 30.0f);
    EXPECT_EQ(action.id.layoutGeneration, 8u);
}

TEST_F(UiContextMenuInputTests, FailedLayoutPublicationPreservesAnAlreadyAcceptedContextTrigger){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    HitTarget invalid = target;
    invalid.rectangle.width = -1.0f;
    EXPECT_FALSE(m_router.commitTargets(&invalid, 1u, 2u));
    EXPECT_EQ(m_router.layoutGeneration(), 1u);
    ContextMenuAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.id.layoutGeneration, 1u);
}

TEST_F(UiContextMenuInputTests, NativeFocusLossCancelsPendingContextActionsAndHeldOwners){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    focus();
    EXPECT_TRUE(send(Key(InputKey::Menu)).keyboardConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::FocusLost)).pointerConsumed);
    EXPECT_FALSE(m_router.windowFocused());
    EXPECT_FALSE(m_router.secondaryDown());
    EXPECT_FALSE(m_router.pointerKnown());
    EXPECT_FALSE(m_router.ownsKey(InputKey::Menu));
    ContextMenuAction action;
    EXPECT_FALSE(take(action));
    EXPECT_FALSE(release(InputKey::Menu).keyboardConsumed);
    EXPECT_FALSE(send(Pointer(InputEventType::FocusGained)).pointerConsumed);
    EXPECT_TRUE(m_router.windowFocused());
    focus();
    EXPECT_FALSE(take(action));
    EXPECT_TRUE(send(Key(InputKey::Menu)).keyboardConsumed);
    EXPECT_TRUE(take(action));
}

TEST_F(UiContextMenuInputTests, CaptureLossClearsTheSecondaryHoldAndPointerKnowledge){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    ContextMenuAction action;
    ASSERT_TRUE(take(action));
    EXPECT_TRUE(send(Pointer(InputEventType::PointerCaptureLost)).pointerConsumed);
    EXPECT_FALSE(m_router.secondaryDown());
    EXPECT_FALSE(m_router.pointerKnown());
    EXPECT_FALSE(send(Pointer(InputEventType::SecondaryUp, { 180.0f, 180.0f })).pointerConsumed);
    EXPECT_FALSE(take(action));
}

TEST_F(UiContextMenuInputTests, ResetDropsPendingTriggersAndKeepsActionSequencesMonotonic){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    ContextMenuAction first;
    ASSERT_TRUE(take(first));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryUp)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    m_router.reset();
    EXPECT_FALSE(m_router.secondaryDown());
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    ContextMenuAction action;
    EXPECT_FALSE(take(action));
    EXPECT_TRUE(send(Pointer(InputEventType::SecondaryDown)).pointerConsumed);
    ASSERT_TRUE(take(action));
    EXPECT_GT(action.id.sequence, first.id.sequence);
}

TEST_F(UiContextMenuInputTests, ContextTriggerQueueIsBoundedAndReportsOverflowWithoutRepeatingThePress){
    const HitTarget target = Target();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    focus();
    for(usize index = 0u; index < s_InputMaxContextMenuActions; ++index){
        EXPECT_FALSE(send(Key(InputKey::Menu)).activationOverflow);
        EXPECT_TRUE(release(InputKey::Menu).keyboardConsumed);
    }
    const InputRoutingResult overflow = send(Key(InputKey::Menu));
    EXPECT_TRUE(overflow.keyboardConsumed);
    EXPECT_TRUE(overflow.activationOverflow);
    ContextMenuAction action;
    usize count = 0u;
    u64 sequence = 0u;
    while(take(action)){
        EXPECT_GT(action.id.sequence, sequence);
        sequence = action.id.sequence;
        ++count;
    }
    EXPECT_EQ(count, s_InputMaxContextMenuActions);
    EXPECT_FALSE(send(Key(InputKey::Menu, false, false, false, true)).activationOverflow);
    EXPECT_FALSE(take(action));
    EXPECT_TRUE(release(InputKey::Menu).keyboardConsumed);
    EXPECT_TRUE(send(Key(InputKey::Menu)).keyboardConsumed);
    EXPECT_TRUE(take(action));
}

TEST_F(UiContextMenuInputTests, SecondaryInputRejectsNonfiniteCoordinatesAndNewKeyBoundsRemainValidated){
    EXPECT_FALSE(m_router.queue(Pointer(InputEventType::SecondaryDown, { Limit<f32>::s_QuietNaN, 1.0f })));
    EXPECT_FALSE(m_router.queue(Pointer(InputEventType::SecondaryUp, { 1.0f, Limit<f32>::s_Infinity })));
    EXPECT_FALSE(m_router.queue(Key(static_cast<InputKey::Enum>(static_cast<u8>(InputKey::F10) + 1u))));
    EXPECT_FALSE(m_router.ownsKey(static_cast<InputKey::Enum>(static_cast<u8>(InputKey::F10) + 1u)));
    EXPECT_TRUE(m_router.queue(Key(InputKey::Menu)));
    EXPECT_TRUE(m_router.queue(Key(InputKey::F10, true)));
    EXPECT_FALSE(m_router.process().keyboardConsumed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

