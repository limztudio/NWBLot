// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/ui/native_input.h>
#include <tests/unit/ui/name_cases.adhd.h>

#include <impl/ecs_ui/toolkit/input/router.h>

#include <global/simdmath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_router_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


HitTarget BaseHitTarget()noexcept{
    HitTarget target;
    target.id = { 1u };
    target.rectangle = { 4.0f, 4.0f, 20.0f, 18.0f };
    target.clip = { 0.0f, 0.0f, 400.0f, 300.0f };
    target.focusable = true;
    target.activatable = true;
    return target;
}

PopupScope Scope(const u64 widget, const u32 layer, const PopupToken& parent = {})noexcept{
    PopupScope scope;
    scope.token = { { widget }, 10u, 20u, 1u };
    scope.bounds = widget == 100u ? Rect{ 30.0f, 30.0f, 110.0f, 70.0f } : widget == 200u ? Rect{ 170.0f, 120.0f, 100.0f, 80.0f } : widget == 300u ? Rect{ 280.0f, 200.0f, 100.0f, 70.0f } : Rect{ 250.0f, 30.0f, 120.0f, 70.0f };
    scope.viewport = { 0.0f, 0.0f, 400.0f, 300.0f };
    scope.layer = layer;
    scope.parent = parent;
    return scope;
}

HitTarget Barrier(const PopupScope& scope, const u32 order)noexcept{
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

HitTarget Child(const PopupScope& scope, const u64 offset, const u32 order, const b8 navigation)noexcept{
    HitTarget target;
    target.id = { scope.token.widget.value + offset };
    target.declarationGeneration = 10u;
    const f32 insetX = scope.bounds.x + 4.0f;
    const f32 insetY = scope.bounds.y + 4.0f;
    target.rectangle = { static_cast<f32>(offset - 1u) * 35.0f + insetX, insetY, 24.0f, 18.0f };
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


class UiNestedPopupRouterTests : public UiTestSupport::RouterTestFixture{
public:
    UiNestedPopupRouterTests()
        : RouterTestFixture(Name(NWB::Tests::UiCases::s_UiCaseTestsUiNestedPopupRouter))
    {}


protected:
    [[nodiscard]] b8 install(const Span<const PopupScope> scopes, const u64 generation, const b8 navigation = false){
        Array<HitTarget, 1u + 3u * s_InputMaxPopups> targets{};
        targets[0u] = BaseHitTarget();
        for(usize index = 0u; index < scopes.size(); ++index){
            const u32 order = static_cast<u32>(1u + index * 3u);
            targets[order] = Barrier(scopes[index], order);
            targets[order + 1u] = Child(scopes[index], 1u, order + 1u, navigation);
            targets[order + 2u] = Child(scopes[index], 2u, order + 2u, navigation);
        }
        return m_router.commitTargets(Span(targets.data(), 1u + scopes.size() * 3u), generation, scopes);
    }

    void focusBase(){
        ASSERT_TRUE(install({}, 1u));
        key(Core::Key::Tab);
        ASSERT_EQ(m_router.focus().value, 1u);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiNestedPopupRouterTests, ChildMayExtendOutsideParentWhileFocusAndPointerStayInAcceptedTopScope){
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const Array<PopupScope, 2> scopes = { parent, child };
    ASSERT_TRUE(install(scopes, 1u));
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
    ASSERT_TRUE(install(Span(&parent, 1u), 2u));
    key(Core::Key::Tab);
    ASSERT_EQ(m_router.focus().value, 102u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const Array<PopupScope, 2> scopes = { parent, child };
    ASSERT_TRUE(install(scopes, 3u));
    ASSERT_EQ(m_router.focus().value, 201u);
    EXPECT_TRUE(send({ .position = {}, .key = Core::Key::Escape , .type = InputEventType::KeyDown}).commandConsumed);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    const auto reasonResult1 = m_router.consumePopupDismissal(child.token);
    ASSERT_TRUE(reasonResult1);
    reason = *reasonResult1;
    EXPECT_EQ(reason, PopupDismissReason::Cancel);
    EXPECT_FALSE(m_router.consumePopupDismissal(parent.token));
    EXPECT_FALSE(m_router.focus().valid());
    ASSERT_TRUE(install(Span(&parent, 1u), 4u));
    EXPECT_EQ(m_router.focus().value, 102u);
    EXPECT_TRUE(send({ .position = {}, .key = Core::Key::Escape, .type = InputEventType::KeyDown, .repeat = true }).commandConsumed);
    EXPECT_FALSE(m_router.consumePopupDismissal(parent.token));
    EXPECT_EQ(m_router.focus().value, 102u);
    EXPECT_TRUE(send({ .position = {}, .key = Core::Key::Escape , .type = InputEventType::KeyUp}).commandConsumed);
    key(Core::Key::Escape);
    const auto reasonResult2 = m_router.consumePopupDismissal(parent.token);
    ASSERT_TRUE(reasonResult2);
    reason = *reasonResult2;
    EXPECT_EQ(reason, PopupDismissReason::Cancel);
}

TEST_F(UiNestedPopupRouterTests, OptedInChildTabLeavesItsParentOpenAndAdvancesParentFocus){
    focusBase();
    const PopupScope parent = Scope(100u, 1u);
    ASSERT_TRUE(install(Span(&parent, 1u), 2u));
    key(Core::Key::Tab);
    ASSERT_EQ(m_router.focus().value, 102u);
    PopupScope child = Scope(200u, 2u, parent.token);
    child.dismissFocusTraversal = true;
    child.focusAnchor = { 102u };
    child.focusAnchorDeclarationGeneration = 10u;
    const Array<PopupScope, 2> scopes = { parent, child };
    ASSERT_TRUE(install(scopes, 3u));
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
    ASSERT_TRUE(install(Span(&parent, 1u), 4u));
    EXPECT_EQ(m_router.popupCount(), 1u);
    EXPECT_EQ(m_router.topPopupToken(), parent.token);
    EXPECT_EQ(m_router.focus().value, 101u);
}

TEST_F(UiNestedPopupRouterTests, OutsideChildPressOverParentConsumesReleaseWithoutParentActivation){
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const Array<PopupScope, 2> scopes = { parent, child };
    ASSERT_TRUE(install(scopes, 1u));
    EXPECT_TRUE(send({ .position = { 35.0f, 35.0f } , .type = InputEventType::PrimaryDown}).pointerConsumed);
    EXPECT_FALSE(m_router.capture().valid());
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    const auto reasonResult = m_router.consumePopupDismissal(child.token);
    ASSERT_TRUE(reasonResult);
    reason = *reasonResult;
    EXPECT_EQ(reason, PopupDismissReason::OutsideClick);
    EXPECT_FALSE(m_router.consumePopupDismissal(parent.token));
    ASSERT_TRUE(install(Span(&parent, 1u), 2u));
    EXPECT_TRUE(send({ .position = { 35.0f, 35.0f } , .type = InputEventType::PrimaryUp}).pointerConsumed);
    EXPECT_FALSE(m_router.consumeActivation({ 101u }));
    click({ 35.0f, 35.0f });
    EXPECT_TRUE(m_router.consumeActivation({ 101u }));
}

TEST_F(UiNestedPopupRouterTests, OutsideSecondarySequenceCannotOpenTheNewlyExposedParentMenu){
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const Array<PopupScope, 2> scopes = { parent, child };
    ASSERT_TRUE(install(scopes, 1u, true));
    EXPECT_TRUE(send({ .position = { 35.0f, 35.0f } , .type = InputEventType::SecondaryDown}).pointerConsumed);
    const auto reasonResult = m_router.consumePopupDismissal(child.token);
    ASSERT_TRUE(reasonResult);
    ASSERT_TRUE(install(Span(&parent, 1u), 2u, true));
    EXPECT_TRUE(send({ .position = { 35.0f, 35.0f } , .type = InputEventType::SecondaryUp}).pointerConsumed);
    ContextMenuAction action;
    EXPECT_FALSE(m_router.consumeContextMenu({ 101u }, 10u));
    EXPECT_TRUE(send({ .position = { 35.0f, 35.0f } , .type = InputEventType::SecondaryDown}).pointerConsumed);
    EXPECT_FALSE(m_router.consumeContextMenu({ 101u }, 10u));
    EXPECT_TRUE(send({ .position = { 35.0f, 35.0f } , .type = InputEventType::SecondaryUp}).pointerConsumed);
    const auto actionResult = m_router.consumeContextMenu({ 101u }, 10u);
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_EQ(action.popup, parent.token);
    EXPECT_EQ(action.origin, InputActionSource::Pointer);
}

TEST_F(UiNestedPopupRouterTests, ClosingAncestorPreservesUnrelatedTopChainAndItsAcceptedAction){
    focusBase();
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const PopupScope grandchild = Scope(300u, 3u, child.token);
    const PopupScope unrelated = Scope(400u, 4u);
    const Array<PopupScope, 4> scopes = { parent, child, grandchild, unrelated };
    ASSERT_TRUE(install(scopes, 2u));
    key(Core::Key::Enter);
    ASSERT_EQ(m_router.actions().size(), 1u);
    m_router.closePopup(parent.token);
    EXPECT_EQ(m_router.focus().value, 401u);
    EXPECT_EQ(m_router.hitTest({ 255.0f, 35.0f }).value, 401u);
    EXPECT_TRUE(m_router.consumeActivation({ 401u }));
    ASSERT_TRUE(install(Span(&unrelated, 1u), 3u));
    EXPECT_EQ(m_router.popupCount(), 1u);
    EXPECT_EQ(m_router.focus().value, 401u);
    EXPECT_EQ(m_router.popupScope(parent.token), nullptr);
    EXPECT_EQ(m_router.popupScope(child.token), nullptr);
    EXPECT_EQ(m_router.popupScope(grandchild.token), nullptr);
    ASSERT_TRUE(install({}, 4u));
    EXPECT_EQ(m_router.focus().value, 1u);
}

TEST_F(UiNestedPopupRouterTests, RetiringAncestorCancelsDescendantIntentionsAcrossRemovalAndIdenticalReturn){
    focusBase();
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const Array<PopupScope, 2> scopes = { parent, child };
    ASSERT_TRUE(install(scopes, 2u, true));
    EXPECT_TRUE(send({ .position = {}, .key = Core::Key::Down , .type = InputEventType::KeyDown}).commandConsumed);
    EXPECT_TRUE(send({ .position = {}, .key = Core::Key::Menu , .type = InputEventType::KeyDown}).commandConsumed);
    EXPECT_TRUE(send({ .position = { 175.0f, 125.0f } , .type = InputEventType::PrimaryDown}).pointerConsumed);
    EXPECT_TRUE(send({ .position = { 175.0f, 125.0f } , .type = InputEventType::SecondaryDown}).pointerConsumed);
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
    ASSERT_TRUE(install(scopes, 3u, true));
    EXPECT_TRUE(send({ .position = {}, .key = Core::Key::Down, .type = InputEventType::KeyDown, .repeat = true }).commandConsumed);
    EXPECT_TRUE(send({ .position = {}, .key = Core::Key::Menu , .type = InputEventType::KeyDown}).commandConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_FALSE(m_router.consumeContextMenu({ 201u }, 10u));
    EXPECT_TRUE(send({ .position = { 175.0f, 125.0f } , .type = InputEventType::PrimaryUp}).pointerConsumed);
    EXPECT_TRUE(send({ .position = { 175.0f, 125.0f } , .type = InputEventType::SecondaryUp}).pointerConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(send({ .position = {}, .key = Core::Key::Down , .type = InputEventType::KeyUp}).commandConsumed);
    EXPECT_TRUE(send({ .position = {}, .key = Core::Key::Menu , .type = InputEventType::KeyUp}).commandConsumed);
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
    const Array<PopupScope, 3> scopes = { parent, child, unrelated };
    ASSERT_TRUE(install(scopes, 1u));
    EXPECT_TRUE(send({ .position = { 255.0f, 35.0f } , .type = InputEventType::PrimaryDown}).pointerConsumed);
    ASSERT_EQ(m_router.capture().value, 401u);
    m_router.invalidateTarget(parent.token.widget);
    EXPECT_EQ(m_router.popupCount(), 1u);
    EXPECT_EQ(m_router.topPopupToken(), unrelated.token);
    EXPECT_EQ(m_router.focus().value, 401u);
    EXPECT_EQ(m_router.capture().value, 401u);
    EXPECT_TRUE(send({ .position = { 255.0f, 35.0f } , .type = InputEventType::PrimaryUp}).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation({ 401u }));
    EXPECT_FALSE(m_router.consumeActivation({ 201u }));
}

TEST_F(UiNestedPopupRouterTests, ChangingAncestorRequiresRenewedChildTokenAndFencesOldCapture){
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const Array<PopupScope, 2> original = { parent, child };
    ASSERT_TRUE(install(original, 1u));
    click({ 175.0f, 125.0f });
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_TRUE(send({ .position = { 175.0f, 125.0f } , .type = InputEventType::PrimaryDown}).pointerConsumed);
    PopupScope nextParent = parent;
    ++nextParent.token.openGeneration;
    PopupScope nextChild = child;
    const Array<PopupScope, 2> staleParent = { nextParent, nextChild };
    EXPECT_FALSE(install(staleParent, 2u));
    nextChild.parent = nextParent.token;
    const Array<PopupScope, 2> staleChild = { nextParent, nextChild };
    EXPECT_FALSE(install(staleChild, 2u));
    EXPECT_EQ(m_router.layoutGeneration(), 1u);
    EXPECT_EQ(m_router.focus().value, 201u);
    EXPECT_EQ(m_router.capture().value, 201u);
    ASSERT_EQ(m_router.actions().size(), 1u);
    ++nextChild.token.openGeneration;
    const Array<PopupScope, 2> renewed = { nextParent, nextChild };
    ASSERT_TRUE(install(renewed, 2u));
    EXPECT_EQ(m_router.focus().value, 201u);
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(send({ .position = { 175.0f, 125.0f } , .type = InputEventType::PrimaryUp}).pointerConsumed);
    EXPECT_FALSE(m_router.consumeActivation({ 201u }));
    click({ 175.0f, 125.0f });
    EXPECT_TRUE(m_router.consumeActivation({ 201u }));
}

TEST_F(UiNestedPopupRouterTests, InvalidOrMissingAncestryRejectsAtomicallyAgainstAcceptedNestedGeometry){
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const Array<PopupScope, 2> scopes = { parent, child };
    ASSERT_TRUE(install(scopes, 1u));
    click({ 175.0f, 125.0f });
    EXPECT_TRUE(send({ .position = { 175.0f, 125.0f } , .type = InputEventType::PrimaryDown}).pointerConsumed);
    Array<PopupScope, 5u> invalid{};
    invalid.fill(child);
    ++invalid[0u].parent.openGeneration;
    invalid[1u].parent.widget = {};
    invalid[2u].parent = { { 999u }, 10u, 20u, 1u };
    invalid[3u].parent = child.token;
    invalid[4u].parent = {};
    for(const auto& candidate : invalid){
        const Array<PopupScope, 2> rejected = { parent, candidate };
        EXPECT_FALSE(install(rejected, 2u));
        EXPECT_EQ(m_router.layoutGeneration(), 1u);
        EXPECT_EQ(m_router.focus().value, 201u);
        EXPECT_EQ(m_router.capture().value, 201u);
        EXPECT_EQ(m_router.hitTest({ 175.0f, 125.0f }).value, 201u);
        EXPECT_EQ(m_router.actions().size(), 1u);
    }
    EXPECT_FALSE(install(Span(&child, 1u), 2u));
    PopupScope forwardParent = parent;
    forwardParent.parent = child.token;
    const Array<PopupScope, 2> cyclic = { forwardParent, child };
    EXPECT_FALSE(install(cyclic, 2u));
    EXPECT_EQ(m_router.popupCount(), 2u);
    EXPECT_TRUE(send({ .position = { 175.0f, 125.0f } , .type = InputEventType::PrimaryUp}).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation({ 201u }));
}

TEST_F(UiNestedPopupRouterTests, NewDescendantsCannotRestoreInputBeneathAClosingAncestorLifetime){
    PopupScope parent = Scope(100u, 1u);
    PopupScope child = Scope(200u, 2u, parent.token);
    PopupScope grandchild = Scope(300u, 3u, child.token);
    const Array<PopupScope, 3> original = { parent, child, grandchild };
    ASSERT_TRUE(install(original, 1u));
    m_router.closePopup(parent.token);
    ++child.token.openGeneration;
    ++grandchild.token.openGeneration;
    grandchild.parent = child.token;
    const Array<PopupScope, 3> underClosingParent = { parent, child, grandchild };
    ASSERT_TRUE(install(underClosingParent, 2u));
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.hitTest({ 285.0f, 205.0f }).valid());
    EXPECT_TRUE(m_router.actions().empty());
    ++parent.token.openGeneration;
    m_router.fencePopup(parent.token);
    ++child.token.openGeneration;
    child.parent = parent.token;
    ++grandchild.token.openGeneration;
    grandchild.parent = child.token;
    const Array<PopupScope, 3> renewed = { parent, child, grandchild };
    ASSERT_TRUE(install(renewed, 3u));
    EXPECT_EQ(m_router.focus().value, 301u);
    EXPECT_EQ(m_router.hitTest({ 285.0f, 205.0f }).value, 301u);
}

TEST_F(UiNestedPopupRouterTests, FencingAncestorImmediatelySuppressesAllDescendantActionsAndCapture){
    const PopupScope parent = Scope(100u, 1u);
    const PopupScope child = Scope(200u, 2u, parent.token);
    const PopupScope grandchild = Scope(300u, 3u, child.token);
    const Array<PopupScope, 3> scopes = { parent, child, grandchild };
    ASSERT_TRUE(install(scopes, 1u));
    click({ 285.0f, 205.0f });
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_TRUE(send({ .position = { 285.0f, 205.0f } , .type = InputEventType::PrimaryDown}).pointerConsumed);
    PopupToken replaced = parent.token;
    ++replaced.instanceGeneration;
    m_router.fencePopup(replaced);
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(m_router.hitTest({ 285.0f, 205.0f }).valid());
    EXPECT_TRUE(send({ .position = { 285.0f, 205.0f } , .type = InputEventType::PrimaryUp}).pointerConsumed);
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
    const Array<HitTarget, 3> targets = { BaseHitTarget(), host, owner };
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, Span(&scope, 1u)));
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
    const Array<PopupScope, 3> scopes = { parent, child, grandchild };
    ASSERT_TRUE(install(scopes, 2u));
    EXPECT_TRUE(send({ .position = {} , .type = InputEventType::FocusLost}).commandConsumed);
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
    EXPECT_FALSE(send({ .position = {} , .type = InputEventType::FocusGained}).focus.valid());
    ASSERT_TRUE(install({}, 3u));
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.hasPopup());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

