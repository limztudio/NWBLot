// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/input/router.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_router_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


HitTarget BaseHitTarget(){
    HitTarget target;
    target.id = { 1u };
    target.rectangle = { 4.0f, 4.0f, 20.0f, 18.0f };
    target.clip = { 0.0f, 0.0f, 400.0f, 300.0f };
    target.focusable = true;
    target.activatable = true;
    return target;
}

PopupScope Scope(const u64 widget, const u32 layer, const PopupToken& parent = {}){
    PopupScope scope;
    scope.token = { { widget }, 10u, 20u, 1u };
    scope.bounds = widget == 100u ? Rect{ 30.0f, 30.0f, 110.0f, 70.0f }
        : widget == 200u ? Rect{ 170.0f, 120.0f, 100.0f, 80.0f }
        : widget == 300u ? Rect{ 280.0f, 200.0f, 100.0f, 70.0f }
        : Rect{ 250.0f, 30.0f, 120.0f, 70.0f };
    scope.viewport = { 0.0f, 0.0f, 400.0f, 300.0f };
    scope.layer = layer;
    scope.parent = parent;
    return scope;
}

HitTarget Barrier(const PopupScope& scope, const u32 order){
    HitTarget target;
    target.id = scope.token.widget;
    target.declarationGeneration = scope.token.declarationGeneration;
    target.rectangle = scope.bounds;
    target.clip = scope.viewport;
    target.paintOrder = order;
    target.popup = scope.token;
    target.layer = scope.layer;
    return target;
}

HitTarget Child(const PopupScope& scope, const u64 offset, const u32 order, const bool navigation){
    HitTarget target;
    target.id = { scope.token.widget.value + offset };
    target.declarationGeneration = 10u;
    target.rectangle = { scope.bounds.x + 4.0f + static_cast<f32>(offset - 1u) * 35.0f,
        scope.bounds.y + 4.0f, 24.0f, 18.0f };
    target.clip = scope.bounds;
    target.paintOrder = order;
    target.popup = scope.token;
    target.layer = scope.layer;
    target.focusable = true;
    target.activatable = true;
    if(navigation && offset == 1u){
        target.navigable = true;
        target.contextMenu = true;
        target.control = { 600u, 700u, 800u };
    }
    return target;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNestedPopupRouterTests : public testing::Test{
public:
    UiNestedPopupRouterTests()
        : m_arena(Name("tests/ui/nested_popup_router"))
        , m_router(m_arena)
    {}


protected:
    [[nodiscard]] bool install(const PopupScope* scopes, const usize count, const u64 generation, const bool navigation = false){
        Array<HitTarget, 1u + 3u * s_InputMaxPopups> targets{};
        targets[0u] = BaseHitTarget();
        for(usize index = 0u; index < count; ++index){
            const u32 order = static_cast<u32>(1u + index * 3u);
            targets[order] = Barrier(scopes[index], order);
            targets[order + 1u] = Child(scopes[index], 1u, order + 1u, navigation);
            targets[order + 2u] = Child(scopes[index], 2u, order + 2u, navigation);
        }
        return m_router.commitTargets(targets.data(), 1u + count * 3u, generation, scopes, count);
    }

    [[nodiscard]] InputRoutingResult send(const InputEvent& event){
        EXPECT_TRUE(m_router.queue(event));
        return m_router.process();
    }

    void key(const Core::Key::Enum key, const bool shift = false){
        EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = key, .shift = shift }).keyboardConsumed);
        EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = key, .shift = shift }).keyboardConsumed);
    }

    void click(const Point& point){
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = point }).pointerConsumed);
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = point }).pointerConsumed);
    }

    void focusBase(){
        ASSERT_TRUE(install(nullptr, 0u, 1u));
        key(Core::Key::Tab);
        ASSERT_EQ(m_router.focus().value, 1u);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    InputRouter m_router;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiNestedPopupRouterTests, ChildMayExtendOutsideParentWhileFocusAndPointerStayInAcceptedTopScope){
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const PopupScope scopes[]{ parent, child };
    ASSERT_TRUE(install(scopes, 2u, 1u));
    ASSERT_EQ(m_router.popupCount(), 2u);
    ASSERT_NE(m_router.popupScope(child.token), nullptr);
    EXPECT_EQ(m_router.popupScope(child.token)->parent, parent.token);
    EXPECT_EQ(m_router.topPopupToken(), child.token);
    EXPECT_EQ(m_router.focus().value, 201u);
    EXPECT_EQ(m_router.hitTest({ 175.0f, 125.0f }).value, 201u);
    EXPECT_FALSE(m_router.hitTest({ 35.0f, 35.0f }).valid());
    key(Core::Key::Tab);
    EXPECT_EQ(m_router.focus().value, 202u);
    key(Core::Key::Tab);
    EXPECT_EQ(m_router.focus().value, 201u);
    key(Core::Key::Tab, true);
    EXPECT_EQ(m_router.focus().value, 202u);
    key(Core::Key::Enter);
    EXPECT_TRUE(m_router.consumeActivation({ 202u }));
    EXPECT_FALSE(m_router.consumeActivation({ 101u }));
}

TEST_F(UiNestedPopupRouterTests, EscapeDismissesOnlyChildThenRestoresChosenParentFocusAfterAcceptance){
    focusBase();
    const PopupScope parent = Scope(100u, 1u);
    ASSERT_TRUE(install(&parent, 1u, 2u));
    key(Core::Key::Tab);
    ASSERT_EQ(m_router.focus().value, 102u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const PopupScope scopes[]{ parent, child };
    ASSERT_TRUE(install(scopes, 2u, 3u));
    ASSERT_EQ(m_router.focus().value, 201u);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Escape }).keyboardConsumed);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    const auto reasonResult1 = m_router.consumePopupDismissal(child.token);
    ASSERT_TRUE(reasonResult1);
    reason = *reasonResult1;
    EXPECT_EQ(reason, PopupDismissReason::Cancel);
    EXPECT_FALSE(m_router.consumePopupDismissal(parent.token));
    EXPECT_FALSE(m_router.focus().valid());
    ASSERT_TRUE(install(&parent, 1u, 4u));
    EXPECT_EQ(m_router.focus().value, 102u);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Escape, .repeat = true }).keyboardConsumed);
    EXPECT_FALSE(m_router.consumePopupDismissal(parent.token));
    EXPECT_EQ(m_router.focus().value, 102u);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Escape }).keyboardConsumed);
    key(Core::Key::Escape);
    const auto reasonResult2 = m_router.consumePopupDismissal(parent.token);
    ASSERT_TRUE(reasonResult2);
    reason = *reasonResult2;
    EXPECT_EQ(reason, PopupDismissReason::Cancel);
}

TEST_F(UiNestedPopupRouterTests, OptedInChildTabLeavesItsParentOpenAndAdvancesParentFocus){
    focusBase();
    const PopupScope parent = Scope(100u, 1u);
    ASSERT_TRUE(install(&parent, 1u, 2u));
    key(Core::Key::Tab);
    ASSERT_EQ(m_router.focus().value, 102u);
    PopupScope child = Scope(200u, 2u, parent.token);
    child.dismissFocusTraversal = true;
    child.focusAnchor = { 102u };
    child.focusAnchorDeclarationGeneration = 10u;
    const PopupScope scopes[]{ parent, child };
    ASSERT_TRUE(install(scopes, 2u, 3u));
    ASSERT_EQ(m_router.focus().value, 201u);
    key(Core::Key::Tab);
    EXPECT_EQ(m_router.focus().value, 202u);
    key(Core::Key::Tab);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    const auto reasonResult = m_router.consumePopupDismissal(child.token);
    ASSERT_TRUE(reasonResult);
    reason = *reasonResult;
    EXPECT_EQ(reason, PopupDismissReason::FocusTraversal);
    EXPECT_FALSE(m_router.consumePopupDismissal(parent.token));
    ASSERT_TRUE(install(&parent, 1u, 4u));
    EXPECT_EQ(m_router.popupCount(), 1u);
    EXPECT_EQ(m_router.topPopupToken(), parent.token);
    EXPECT_EQ(m_router.focus().value, 101u);
}

TEST_F(UiNestedPopupRouterTests, OutsideChildPressOverParentConsumesReleaseWithoutParentActivation){
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const PopupScope scopes[]{ parent, child };
    ASSERT_TRUE(install(scopes, 2u, 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 35.0f, 35.0f } }).pointerConsumed);
    EXPECT_FALSE(m_router.capture().valid());
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    const auto reasonResult = m_router.consumePopupDismissal(child.token);
    ASSERT_TRUE(reasonResult);
    reason = *reasonResult;
    EXPECT_EQ(reason, PopupDismissReason::OutsideClick);
    EXPECT_FALSE(m_router.consumePopupDismissal(parent.token));
    ASSERT_TRUE(install(&parent, 1u, 2u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 35.0f, 35.0f } }).pointerConsumed);
    EXPECT_FALSE(m_router.consumeActivation({ 101u }));
    click({ 35.0f, 35.0f });
    EXPECT_TRUE(m_router.consumeActivation({ 101u }));
}

TEST_F(UiNestedPopupRouterTests, OutsideSecondarySequenceCannotOpenTheNewlyExposedParentMenu){
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const PopupScope scopes[]{ parent, child };
    ASSERT_TRUE(install(scopes, 2u, 1u, true));
    EXPECT_TRUE(send({ .type = InputEventType::SecondaryDown, .position = { 35.0f, 35.0f } }).pointerConsumed);
    const auto reasonResult = m_router.consumePopupDismissal(child.token);
    ASSERT_TRUE(reasonResult);
    ASSERT_TRUE(install(&parent, 1u, 2u, true));
    EXPECT_TRUE(send({ .type = InputEventType::SecondaryUp, .position = { 35.0f, 35.0f } }).pointerConsumed);
    ContextMenuAction action;
    EXPECT_FALSE(m_router.consumeContextMenu({ 101u }, 10u));
    EXPECT_TRUE(send({ .type = InputEventType::SecondaryDown, .position = { 35.0f, 35.0f } }).pointerConsumed);
    const auto actionResult = m_router.consumeContextMenu({ 101u }, 10u);
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_EQ(action.popup, parent.token);
    EXPECT_FALSE(action.keyboard);
    EXPECT_TRUE(send({ .type = InputEventType::SecondaryUp, .position = { 35.0f, 35.0f } }).pointerConsumed);
}

TEST_F(UiNestedPopupRouterTests, ClosingAncestorPreservesUnrelatedTopChainAndItsAcceptedAction){
    focusBase();
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const PopupScope grandchild = Scope(300u, 3u, child.token);
    const PopupScope unrelated = Scope(400u, 4u);
    const PopupScope scopes[]{ parent, child, grandchild, unrelated };
    ASSERT_TRUE(install(scopes, 4u, 2u));
    key(Core::Key::Enter);
    ASSERT_EQ(m_router.actions().size(), 1u);
    m_router.closePopup(parent.token);
    EXPECT_EQ(m_router.focus().value, 401u);
    EXPECT_EQ(m_router.hitTest({ 255.0f, 35.0f }).value, 401u);
    EXPECT_TRUE(m_router.consumeActivation({ 401u }));
    ASSERT_TRUE(install(&unrelated, 1u, 3u));
    EXPECT_EQ(m_router.popupCount(), 1u);
    EXPECT_EQ(m_router.focus().value, 401u);
    EXPECT_EQ(m_router.popupScope(parent.token), nullptr);
    EXPECT_EQ(m_router.popupScope(child.token), nullptr);
    EXPECT_EQ(m_router.popupScope(grandchild.token), nullptr);
    ASSERT_TRUE(install(nullptr, 0u, 4u));
    EXPECT_EQ(m_router.focus().value, 1u);
}

TEST_F(UiNestedPopupRouterTests, RetiringAncestorCancelsDescendantIntentionsAcrossRemovalAndIdenticalReturn){
    focusBase();
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const PopupScope scopes[]{ parent, child };
    ASSERT_TRUE(install(scopes, 2u, 2u, true));
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Down }).keyboardConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Menu }).keyboardConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 175.0f, 125.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::SecondaryDown, .position = { 175.0f, 125.0f } }).pointerConsumed);
    ASSERT_EQ(m_router.capture().value, 201u);
    ASSERT_EQ(m_router.controlActions().size(), 1u);
    m_router.invalidateTarget(parent.token.widget);
    EXPECT_FALSE(m_router.hasPopup());
    EXPECT_EQ(m_router.popupCount(), 0u);
    ASSERT_EQ(m_router.targets().size(), 1u);
    EXPECT_EQ(m_router.focus().value, 1u);
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_FALSE(m_router.consumeContextMenu({ 201u }, 10u));
    ASSERT_TRUE(install(scopes, 2u, 3u, true));
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Down, .repeat = true }).keyboardConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Menu }).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_FALSE(m_router.consumeContextMenu({ 201u }, 10u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 175.0f, 125.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::SecondaryUp, .position = { 175.0f, 125.0f } }).pointerConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Down }).keyboardConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Menu }).keyboardConsumed);
    key(Core::Key::Down);
    ControlAction action;
    const auto actionResult = m_router.consumeControlAction({ 201u }, 10u, { 600u, 700u, 800u });
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_EQ(action.popup, child.token);
    EXPECT_EQ(action.kind, ControlActionKind::Down);
}

TEST_F(UiNestedPopupRouterTests, RetiringLowerFamilyPreservesUnrelatedTopCaptureAndFocus){
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const PopupScope unrelated = Scope(400u, 3u);
    const PopupScope scopes[]{ parent, child, unrelated };
    ASSERT_TRUE(install(scopes, 3u, 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 255.0f, 35.0f } }).pointerConsumed);
    ASSERT_EQ(m_router.capture().value, 401u);
    m_router.invalidateTarget(parent.token.widget);
    EXPECT_EQ(m_router.popupCount(), 1u);
    EXPECT_EQ(m_router.topPopupToken(), unrelated.token);
    EXPECT_EQ(m_router.focus().value, 401u);
    EXPECT_EQ(m_router.capture().value, 401u);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 255.0f, 35.0f } }).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation({ 401u }));
    EXPECT_FALSE(m_router.consumeActivation({ 201u }));
}

TEST_F(UiNestedPopupRouterTests, ChangingAncestorRequiresRenewedChildTokenAndFencesOldCapture){
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const PopupScope original[]{ parent, child };
    ASSERT_TRUE(install(original, 2u, 1u));
    click({ 175.0f, 125.0f });
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 175.0f, 125.0f } }).pointerConsumed);
    PopupScope nextParent = parent;
    ++nextParent.token.openGeneration;
    PopupScope nextChild = child;
    const PopupScope staleParent[]{ nextParent, nextChild };
    EXPECT_FALSE(install(staleParent, 2u, 2u));
    nextChild.parent = nextParent.token;
    const PopupScope staleChild[]{ nextParent, nextChild };
    EXPECT_FALSE(install(staleChild, 2u, 2u));
    EXPECT_EQ(m_router.layoutGeneration(), 1u);
    EXPECT_EQ(m_router.focus().value, 201u);
    EXPECT_EQ(m_router.capture().value, 201u);
    ASSERT_EQ(m_router.actions().size(), 1u);
    ++nextChild.token.openGeneration;
    const PopupScope renewed[]{ nextParent, nextChild };
    ASSERT_TRUE(install(renewed, 2u, 2u));
    EXPECT_EQ(m_router.focus().value, 201u);
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 175.0f, 125.0f } }).pointerConsumed);
    EXPECT_FALSE(m_router.consumeActivation({ 201u }));
    click({ 175.0f, 125.0f });
    EXPECT_TRUE(m_router.consumeActivation({ 201u }));
}

TEST_F(UiNestedPopupRouterTests, InvalidOrMissingAncestryRejectsAtomicallyAgainstAcceptedNestedGeometry){
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const PopupScope scopes[]{ parent, child };
    ASSERT_TRUE(install(scopes, 2u, 1u));
    click({ 175.0f, 125.0f });
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 175.0f, 125.0f } }).pointerConsumed);
    Array<PopupScope, 5u> invalid{};
    invalid.fill(child);
    ++invalid[0u].parent.openGeneration;
    invalid[1u].parent.widget = {};
    invalid[2u].parent = { { 999u }, 10u, 20u, 1u };
    invalid[3u].parent = child.token;
    invalid[4u].parent = {};
    for(const auto& candidate : invalid){
        const PopupScope rejected[]{ parent, candidate };
        EXPECT_FALSE(install(rejected, 2u, 2u));
        EXPECT_EQ(m_router.layoutGeneration(), 1u);
        EXPECT_EQ(m_router.focus().value, 201u);
        EXPECT_EQ(m_router.capture().value, 201u);
        EXPECT_EQ(m_router.hitTest({ 175.0f, 125.0f }).value, 201u);
        EXPECT_EQ(m_router.actions().size(), 1u);
    }
    EXPECT_FALSE(install(&child, 1u, 2u));
    PopupScope forwardParent = parent;
    forwardParent.parent = child.token;
    const PopupScope cyclic[]{ forwardParent, child };
    EXPECT_FALSE(install(cyclic, 2u, 2u));
    EXPECT_EQ(m_router.popupCount(), 2u);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 175.0f, 125.0f } }).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation({ 201u }));
}

TEST_F(UiNestedPopupRouterTests, NewDescendantsCannotRestoreInputBeneathAClosingAncestorLifetime){
    PopupScope parent = Scope(100u, 1u);
    PopupScope child = Scope(200u, 2u, parent.token);
    PopupScope grandchild = Scope(300u, 3u, child.token);
    const PopupScope original[]{ parent, child, grandchild };
    ASSERT_TRUE(install(original, 3u, 1u));
    m_router.closePopup(parent.token);
    ++child.token.openGeneration;
    ++grandchild.token.openGeneration;
    grandchild.parent = child.token;
    const PopupScope underClosingParent[]{ parent, child, grandchild };
    ASSERT_TRUE(install(underClosingParent, 3u, 2u));
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.hitTest({ 285.0f, 205.0f }).valid());
    EXPECT_TRUE(m_router.actions().empty());
    ++parent.token.openGeneration;
    m_router.fencePopup(parent.token);
    ++child.token.openGeneration;
    child.parent = parent.token;
    ++grandchild.token.openGeneration;
    grandchild.parent = child.token;
    const PopupScope renewed[]{ parent, child, grandchild };
    ASSERT_TRUE(install(renewed, 3u, 3u));
    EXPECT_EQ(m_router.focus().value, 301u);
    EXPECT_EQ(m_router.hitTest({ 285.0f, 205.0f }).value, 301u);
}

TEST_F(UiNestedPopupRouterTests, FencingAncestorImmediatelySuppressesAllDescendantActionsAndCapture){
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const PopupScope grandchild = Scope(300u, 3u, child.token);
    const PopupScope scopes[]{ parent, child, grandchild };
    ASSERT_TRUE(install(scopes, 3u, 1u));
    click({ 285.0f, 205.0f });
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 285.0f, 205.0f } }).pointerConsumed);
    PopupToken replaced = parent.token;
    ++replaced.instanceGeneration;
    m_router.fencePopup(replaced);
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(m_router.hitTest({ 285.0f, 205.0f }).valid());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 285.0f, 205.0f } }).pointerConsumed);
    EXPECT_FALSE(m_router.consumeActivation({ 301u }));
}

TEST_F(UiNestedPopupRouterTests, RetiringAnAlreadyRemovedPopupOwnerPartStillRestoresUnderlyingFocus){
    focusBase();
    const PopupScope scope = Scope(100u, 1u);
    const HitTarget host = Child(scope, 1u, 1u, true);
    HitTarget owner = Barrier(scope, 2u);
    owner.owner = host.id;
    owner.ownerDeclarationGeneration = host.declarationGeneration;
    owner.control = host.control;
    const HitTarget targets[]{ BaseHitTarget(), host, owner };
    ASSERT_TRUE(m_router.commitTargets(targets, 3u, 2u, &scope, 1u));
    m_router.invalidateTarget(host.id);
    EXPECT_EQ(m_router.targets().size(), 1u);
    EXPECT_EQ(m_router.findTarget(owner.id), nullptr);
    EXPECT_EQ(m_router.popupCount(), 1u);
    EXPECT_FALSE(m_router.focus().valid());
    m_router.invalidateTarget(owner.id);
    EXPECT_EQ(m_router.popupCount(), 0u);
    EXPECT_EQ(m_router.focus(), BaseHitTarget().id);
    EXPECT_EQ(m_router.targets().size(), 1u);
}

TEST_F(UiNestedPopupRouterTests, NativeFocusLossDismissesWholeFamilyWithoutRestoringUnderlyingFocus){
    focusBase();
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const PopupScope grandchild = Scope(300u, 3u, child.token);
    const PopupScope scopes[]{ parent, child, grandchild };
    ASSERT_TRUE(install(scopes, 3u, 2u));
    EXPECT_TRUE(send({ .type = InputEventType::FocusLost, .position = {} }).keyboardConsumed);
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.hitTest({ 285.0f, 205.0f }).valid());
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    for(const auto& scope : scopes){
        const auto reasonResult = m_router.consumePopupDismissal(scope.token);
        ASSERT_TRUE(reasonResult);
        reason = *reasonResult;
        EXPECT_EQ(reason, PopupDismissReason::FocusLost);
        EXPECT_FALSE(m_router.consumePopupDismissal(scope.token));
    }
    EXPECT_FALSE(send({ .type = InputEventType::FocusGained, .position = {} }).focus.valid());
    ASSERT_TRUE(install(nullptr, 0u, 3u));
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.hasPopup());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

