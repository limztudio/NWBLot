// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/input/router.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_popup_router_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


HitTarget Button(const u64 id, const Rect& rectangle, const u32 paintOrder = 0u){
    HitTarget target;
    target.id = { id };
    target.rectangle = rectangle;
    target.clip = { 0.0f, 0.0f, 200.0f, 120.0f };
    target.paintOrder = paintOrder;
    target.focusable = true;
    target.activatable = true;
    return target;
}

PopupScope Scope(const u64 widget = 100u, const u32 layer = 3u, const u64 openGeneration = 1u){
    PopupScope scope;
    scope.token = { { widget }, 10u, 20u, openGeneration };
    scope.bounds = { 40.0f, 40.0f, 100.0f, 70.0f };
    scope.viewport = { 0.0f, 0.0f, 200.0f, 120.0f };
    scope.layer = layer;
    return scope;
}

Array<HitTarget, 2u> BaseTargets(){
    return { Button(1u, { 10.0f, 10.0f, 30.0f, 20.0f }), Button(2u, { 60.0f, 10.0f, 30.0f, 20.0f }) };
}

HitTarget Barrier(const PopupScope& scope){
    HitTarget target;
    target.id = scope.token.widget;
    target.declarationGeneration = scope.token.declarationGeneration;
    target.rectangle = scope.bounds;
    target.clip = scope.viewport;
    target.paintOrder = 10u;
    target.popup = scope.token;
    target.layer = scope.layer;
    return target;
}

HitTarget Child(const PopupScope& scope, const u64 id, const Rect& rectangle, const u32 paintOrder){
    HitTarget target = Button(id, rectangle, paintOrder);
    target.clip = scope.bounds;
    target.popup = scope.token;
    target.layer = scope.layer;
    return target;
}

Array<HitTarget, 5u> Targets(const PopupScope& scope){
    const auto base = BaseTargets();
    return { base[0u], base[1u], Barrier(scope),
        Child(scope, scope.token.widget.value + 1u, { 50.0f, 50.0f, 30.0f, 20.0f }, 11u),
        Child(scope, scope.token.widget.value + 2u, { 90.0f, 50.0f, 30.0f, 20.0f }, 12u) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiPopupRouterTests : public testing::Test{
public:
    UiPopupRouterTests()
        : m_arena(Name("tests/ui/popup_router"))
        , m_router(m_arena)
    {}


protected:
    [[nodiscard]] InputRoutingResult send(const InputEvent& event){
        EXPECT_TRUE(m_router.queue(event));
        return m_router.process();
    }

    void key(const Core::Key::Enum key, const bool shift = false){
        EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = key, .shift = shift }).keyboardConsumed);
        EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = key, .shift = shift }).keyboardConsumed);
    }

    void click(const Point& position){
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = position }).pointerConsumed);
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = position }).pointerConsumed);
    }

    [[nodiscard]] bool install(const PopupScope& scope, const u64 generation){
        const auto targets = Targets(scope);
        return m_router.commitTargets(targets.data(), targets.size(), generation, &scope, 1u);
    }

    void focusBase(){
        const auto targets = BaseTargets();
        ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u));
        key(Core::Key::Tab);
        ASSERT_EQ(m_router.focus().value, 1u);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    InputRouter m_router;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPopupRouterTests, AcceptedPopupBlocksUnderlyingHitTargetsDespiteTheirLaterPaintOrder){
    const PopupScope scope = Scope();
    auto targets = Targets(scope);
    targets[0u].rectangle = scope.viewport;
    targets[0u].paintOrder = Limit<u32>::s_Max;
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u, &scope, 1u));
    EXPECT_TRUE(m_router.hasPopup());
    EXPECT_TRUE(m_router.wantsKeyboard());
    EXPECT_TRUE(m_router.wantsPointer());
    EXPECT_EQ(m_router.focus().value, 101u);
    EXPECT_EQ(m_router.hitTest({ 55.0f, 55.0f }).value, 101u);
    EXPECT_EQ(m_router.hitTest({ 45.0f, 45.0f }).value, 100u);
    EXPECT_FALSE(m_router.hitTest({ 15.0f, 15.0f }).valid());
    EXPECT_TRUE(m_router.wouldConsumePointer({ 15.0f, 15.0f }));
    click({ 55.0f, 55.0f });
    EXPECT_TRUE(m_router.consumeActivation({ 101u }));
    EXPECT_FALSE(m_router.consumeActivation({ 1u }));
}

TEST_F(UiPopupRouterTests, TabAndReverseTabWrapOnlyWithinTheAcceptedTopScope){
    const PopupScope scope = Scope();
    ASSERT_TRUE(install(scope, 1u));
    EXPECT_EQ(m_router.focus().value, 101u);
    key(Core::Key::Tab);
    EXPECT_EQ(m_router.focus().value, 102u);
    key(Core::Key::Tab);
    EXPECT_EQ(m_router.focus().value, 101u);
    key(Core::Key::Tab, true);
    EXPECT_EQ(m_router.focus().value, 102u);
    key(Core::Key::Enter);
    EXPECT_TRUE(m_router.consumeActivation({ 102u }));
    EXPECT_FALSE(m_router.consumeActivation({ 1u }));
    EXPECT_FALSE(m_router.consumeActivation({ 2u }));
}

TEST_F(UiPopupRouterTests, OptedInSingleStopPopupTabsToTheNextParentStopAfterAcceptance){
    focusBase();
    PopupScope scope = Scope();
    scope.dismissFocusTraversal = true;
    scope.focusAnchor = { 1u };
    scope.focusAnchorDeclarationGeneration = 1u;
    auto targets = Targets(scope);
    ASSERT_TRUE(m_router.commitTargets(targets.data(), 4u, 2u, &scope, 1u));
    ASSERT_EQ(m_router.focus().value, 101u);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Tab }).keyboardConsumed);
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Tab, .repeat = true }).keyboardConsumed);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_TRUE(m_router.consumePopupDismissal(scope.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::FocusTraversal);
    EXPECT_FALSE(m_router.consumePopupDismissal(scope.token, reason));
    const auto base = BaseTargets();
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 3u));
    EXPECT_EQ(m_router.focus().value, 2u);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Tab }).keyboardConsumed);
    EXPECT_FALSE(m_router.ownsKey(Core::Key::Tab));
}

TEST_F(UiPopupRouterTests, OptedInPopupReverseTabsToThePreviousParentStop){
    focusBase();
    PopupScope scope = Scope();
    scope.dismissFocusTraversal = true;
    scope.focusAnchor = { 1u };
    scope.focusAnchorDeclarationGeneration = 1u;
    auto targets = Targets(scope);
    ASSERT_TRUE(m_router.commitTargets(targets.data(), 4u, 2u, &scope, 1u));
    ASSERT_EQ(m_router.focus().value, 101u);
    key(Core::Key::Tab, true);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_TRUE(m_router.consumePopupDismissal(scope.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::FocusTraversal);
    const auto base = BaseTargets();
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 3u));
    EXPECT_EQ(m_router.focus().value, 2u);
}

TEST_F(UiPopupRouterTests, OptedInTwoStopPopupMovesInsideBeforeLeaving){
    focusBase();
    PopupScope scope = Scope();
    scope.dismissFocusTraversal = true;
    scope.focusAnchor = { 1u };
    scope.focusAnchorDeclarationGeneration = 1u;
    ASSERT_TRUE(install(scope, 2u));
    ASSERT_EQ(m_router.focus().value, 101u);
    key(Core::Key::Tab);
    EXPECT_EQ(m_router.focus().value, 102u);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_FALSE(m_router.consumePopupDismissal(scope.token, reason));
    key(Core::Key::Tab, true);
    EXPECT_EQ(m_router.focus().value, 101u);
    EXPECT_FALSE(m_router.consumePopupDismissal(scope.token, reason));
    key(Core::Key::Tab);
    key(Core::Key::Tab);
    EXPECT_TRUE(m_router.consumePopupDismissal(scope.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::FocusTraversal);
}

TEST_F(UiPopupRouterTests, EmptyPopupStillOwnsKeysAndOutsidePointerInput){
    const PopupScope scope = Scope();
    const auto base = BaseTargets();
    const HitTarget targets[]{ base[0u], base[1u], Barrier(scope) };
    ASSERT_TRUE(m_router.commitTargets(targets, 3u, 1u, &scope, 1u));
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_TRUE(m_router.wantsKeyboard());
    key(Core::Key::Tab);
    EXPECT_FALSE(m_router.focus().valid());
    key(Core::Key::Enter);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { 15.0f, 15.0f } }).pointerConsumed);
    key(Core::Key::Escape);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_TRUE(m_router.consumePopupDismissal(scope.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::Cancel);
}

TEST_F(UiPopupRouterTests, AutofocusFalseClearsUnderlyingFocusAndWaitsForExplicitTab){
    focusBase();
    PopupScope scope = Scope();
    scope.autofocus = false;
    ASSERT_TRUE(install(scope, 2u));
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_TRUE(m_router.wantsKeyboard());
    key(Core::Key::Enter);
    EXPECT_TRUE(m_router.actions().empty());
    key(Core::Key::Tab);
    EXPECT_EQ(m_router.focus().value, 101u);
}

TEST_F(UiPopupRouterTests, OutsideDismissalConsumesItsEntirePressAcrossAcceptedRemoval){
    focusBase();
    const PopupScope scope = Scope();
    ASSERT_TRUE(install(scope, 2u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 15.0f, 15.0f } }).pointerConsumed);
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(m_router.hasPopup());
    EXPECT_TRUE(m_router.wantsPointer());
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_TRUE(m_router.consumePopupDismissal(scope.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::OutsideClick);
    EXPECT_FALSE(m_router.consumePopupDismissal(scope.token, reason));
    const auto base = BaseTargets();
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 3u));
    EXPECT_FALSE(m_router.hasPopup());
    EXPECT_EQ(m_router.focus().value, 1u);
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { 15.0f, 15.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 15.0f, 15.0f } }).pointerConsumed);
    EXPECT_FALSE(m_router.consumeActivation({ 1u }));
    click({ 15.0f, 15.0f });
    EXPECT_TRUE(m_router.consumeActivation({ 1u }));
}

TEST_F(UiPopupRouterTests, EscapeDismissalPreservesHeldKeyOwnershipAcrossAcceptedRemoval){
    focusBase();
    const PopupScope scope = Scope();
    ASSERT_TRUE(install(scope, 2u));
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Escape }).keyboardConsumed);
    EXPECT_TRUE(m_router.ownsKey(Core::Key::Escape));
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_TRUE(m_router.consumePopupDismissal(scope.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::Cancel);
    const auto base = BaseTargets();
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 3u));
    EXPECT_EQ(m_router.focus().value, 1u);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Escape }).keyboardConsumed);
    EXPECT_FALSE(m_router.ownsKey(Core::Key::Escape));
    EXPECT_EQ(m_router.focus().value, 1u);
}

TEST_F(UiPopupRouterTests, NonDismissibleModalBlocksOutsideAndEscapeWithoutPublishingDismissals){
    PopupScope scope = Scope();
    scope.modal = true;
    scope.dismissOutside = false;
    scope.dismissCancel = false;
    ASSERT_TRUE(install(scope, 1u));
    click({ 15.0f, 15.0f });
    key(Core::Key::Escape);
    EXPECT_TRUE(m_router.hasPopup());
    EXPECT_EQ(m_router.focus().value, 101u);
    EXPECT_FALSE(m_router.dismissPopup(PopupDismissReason::OutsideClick));
    EXPECT_FALSE(m_router.dismissPopup(PopupDismissReason::Cancel));
    EXPECT_FALSE(m_router.dismissPopup(PopupDismissReason::None));
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_FALSE(m_router.consumePopupDismissal(scope.token, reason));
    EXPECT_TRUE(m_router.actions().empty());
}

TEST_F(UiPopupRouterTests, ClosingTokenSuppressesActionsUntilItsAcceptedRemovalAndPublishesNoDismissal){
    focusBase();
    const PopupScope scope = Scope();
    ASSERT_TRUE(install(scope, 2u));
    click({ 55.0f, 55.0f });
    ASSERT_EQ(m_router.actions().size(), 1u);
    m_router.closePopup(scope.token);
    EXPECT_TRUE(m_router.hasPopup());
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.hitTest({ 55.0f, 55.0f }).valid());
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(m_router.wantsKeyboard());
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { 15.0f, 15.0f } }).pointerConsumed);
    key(Core::Key::Enter);
    EXPECT_TRUE(m_router.actions().empty());
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_FALSE(m_router.consumePopupDismissal(scope.token, reason));
    const auto base = BaseTargets();
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 3u));
    EXPECT_FALSE(m_router.hasPopup());
    EXPECT_EQ(m_router.focus().value, 1u);
}

TEST_F(UiPopupRouterTests, SameOpenEpochRetainsChosenChildFocusAcrossAcceptedGeometryUpdates){
    focusBase();
    PopupScope scope = Scope();
    ASSERT_TRUE(install(scope, 2u));
    key(Core::Key::Tab);
    ASSERT_EQ(m_router.focus().value, 102u);
    scope.bounds = { 60.0f, 30.0f, 100.0f, 70.0f };
    auto targets = Targets(scope);
    targets[3u].rectangle = { 70.0f, 40.0f, 30.0f, 20.0f };
    targets[4u].rectangle = { 110.0f, 40.0f, 30.0f, 20.0f };
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 3u, &scope, 1u));
    EXPECT_EQ(m_router.focus().value, 102u);
    EXPECT_EQ(m_router.hitTest({ 115.0f, 45.0f }).value, 102u);
    EXPECT_FALSE(m_router.hitTest({ 55.0f, 55.0f }).valid());
    m_router.closePopup(scope.token);
    const auto base = BaseTargets();
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 4u));
    EXPECT_EQ(m_router.focus().value, 1u);
}

TEST_F(UiPopupRouterTests, ReopenedEpochFencesOldCloseAndDismissalTokens){
    focusBase();
    const PopupScope original = Scope();
    ASSERT_TRUE(install(original, 2u));
    key(Core::Key::Escape);
    const PopupScope reopened = Scope(100u, 3u, 2u);
    ASSERT_TRUE(install(reopened, 3u));
    EXPECT_EQ(m_router.focus().value, 101u);
    m_router.closePopup(original.token);
    EXPECT_EQ(m_router.focus().value, 101u);
    EXPECT_EQ(m_router.hitTest({ 55.0f, 55.0f }).value, 101u);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_FALSE(m_router.consumePopupDismissal(reopened.token, reason));
    m_router.closePopup(reopened.token);
    const auto base = BaseTargets();
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 4u));
    EXPECT_EQ(m_router.focus().value, 1u);
}

TEST_F(UiPopupRouterTests, ReopenedEpochCannotReplayQueuedActivationWithTheSameChildIdAndDeclaration){
    const PopupScope original = Scope();
    ASSERT_TRUE(install(original, 1u));
    click({ 55.0f, 55.0f });
    ASSERT_EQ(m_router.actions().size(), 1u);
    const u64 originalSequence = m_router.actions()[0u].id.sequence;
    const PopupScope reopened = Scope(100u, 3u, 2u);
    ASSERT_TRUE(install(reopened, 2u));
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(m_router.consumeActivation({ 101u }));
    click({ 55.0f, 55.0f });
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_GT(m_router.actions()[0u].id.sequence, originalSequence);
    EXPECT_TRUE(m_router.consumeActivation({ 101u }));
    EXPECT_FALSE(m_router.consumeActivation({ 101u }));
}

TEST_F(UiPopupRouterTests, ReopenedEpochCancelsOldCaptureWithoutTransferringItsReleaseToTheNewChild){
    const PopupScope original = Scope();
    ASSERT_TRUE(install(original, 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 55.0f, 55.0f } }).pointerConsumed);
    ASSERT_EQ(m_router.capture().value, 101u);
    const PopupScope reopened = Scope(100u, 3u, 2u);
    ASSERT_TRUE(install(reopened, 2u));
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 55.0f, 55.0f } }).pointerConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(m_router.consumeActivation({ 101u }));
    click({ 55.0f, 55.0f });
    EXPECT_TRUE(m_router.consumeActivation({ 101u }));
}

TEST_F(UiPopupRouterTests, HeldEscapeDoesNotDismissTheNewlyAcceptedLowerScopeOnRepeatOrDuplicateDown){
    const PopupScope lower = Scope();
    ASSERT_TRUE(install(lower, 1u));
    const PopupScope higher = Scope(200u, 5u);
    const auto lowerTargets = Targets(lower);
    const HitTarget targets[]{ lowerTargets[0u], lowerTargets[1u], lowerTargets[2u], lowerTargets[3u], lowerTargets[4u],
        Barrier(higher), Child(higher, 201u, { 50.0f, 50.0f, 30.0f, 20.0f }, 11u) };
    const PopupScope scopes[]{ lower, higher };
    ASSERT_TRUE(m_router.commitTargets(targets, 7u, 2u, scopes, 2u));
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Escape }).keyboardConsumed);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_TRUE(m_router.consumePopupDismissal(higher.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::Cancel);
    ASSERT_TRUE(install(lower, 3u));
    EXPECT_EQ(m_router.focus().value, 101u);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Escape, .repeat = true }).keyboardConsumed);
    EXPECT_FALSE(m_router.consumePopupDismissal(lower.token, reason));
    EXPECT_EQ(m_router.focus().value, 101u);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Escape }).keyboardConsumed);
    EXPECT_FALSE(m_router.consumePopupDismissal(lower.token, reason));
    EXPECT_EQ(m_router.focus().value, 101u);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Escape }).keyboardConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Escape }).keyboardConsumed);
    EXPECT_TRUE(m_router.consumePopupDismissal(lower.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::Cancel);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Escape }).keyboardConsumed);
}

TEST_F(UiPopupRouterTests, RemovalNeverRestoresDisabledOrRecreatedUnderlyingFocus){
    focusBase();
    const PopupScope scope = Scope();
    ASSERT_TRUE(install(scope, 2u));
    auto base = BaseTargets();
    base[0u].enabled = false;
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 3u));
    EXPECT_FALSE(m_router.focus().valid());
    base[0u].enabled = true;
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 4u));
    key(Core::Key::Tab);
    ASSERT_EQ(m_router.focus().value, 1u);
    const PopupScope reopened = Scope(100u, 3u, 2u);
    ASSERT_TRUE(install(reopened, 5u));
    base[0u].declarationGeneration = 2u;
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 6u));
    EXPECT_FALSE(m_router.focus().valid());
}

TEST_F(UiPopupRouterTests, RemovalNeverRestoresRemovedOrFullyClippedUnderlyingFocus){
    focusBase();
    const PopupScope scope = Scope();
    ASSERT_TRUE(install(scope, 2u));
    auto base = BaseTargets();
    ASSERT_TRUE(m_router.commitTargets(&base[1u], 1u, 3u));
    EXPECT_FALSE(m_router.focus().valid());
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 4u));
    key(Core::Key::Tab);
    ASSERT_EQ(m_router.focus().value, 1u);
    const PopupScope reopened = Scope(100u, 3u, 2u);
    ASSERT_TRUE(install(reopened, 5u));
    base[0u].clip.width = 0.0f;
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 6u));
    EXPECT_FALSE(m_router.focus().valid());
}

TEST_F(UiPopupRouterTests, HigherLayerScopeBlocksLowerChildrenAndRestoresTheirSavedFocus){
    focusBase();
    const PopupScope lower = Scope();
    ASSERT_TRUE(install(lower, 2u));
    key(Core::Key::Tab);
    ASSERT_EQ(m_router.focus().value, 102u);
    const PopupScope higher = Scope(200u, 5u);
    const auto lowerTargets = Targets(lower);
    const HitTarget targets[]{ lowerTargets[0u], lowerTargets[1u], lowerTargets[2u], lowerTargets[3u], lowerTargets[4u],
        Barrier(higher), Child(higher, 201u, { 50.0f, 50.0f, 30.0f, 20.0f }, 11u) };
    const PopupScope scopes[]{ lower, higher };
    ASSERT_TRUE(m_router.commitTargets(targets, 7u, 3u, scopes, 2u));
    EXPECT_EQ(m_router.focus().value, 201u);
    EXPECT_EQ(m_router.hitTest({ 55.0f, 55.0f }).value, 201u);
    key(Core::Key::Tab);
    EXPECT_EQ(m_router.focus().value, 201u);
    m_router.closePopup(higher.token);
    EXPECT_FALSE(m_router.hitTest({ 55.0f, 55.0f }).valid());
    ASSERT_TRUE(install(lower, 4u));
    EXPECT_EQ(m_router.focus().value, 102u);
    m_router.closePopup(lower.token);
    const auto base = BaseTargets();
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 5u));
    EXPECT_EQ(m_router.focus().value, 1u);
}

TEST_F(UiPopupRouterTests, RemovingLowerWhileRetainingHigherSplicesTheSavedBaseFocusChain){
    focusBase();
    const PopupScope lower = Scope();
    ASSERT_TRUE(install(lower, 2u));
    key(Core::Key::Tab);
    ASSERT_EQ(m_router.focus().value, 102u);
    const PopupScope higher = Scope(200u, 5u);
    const auto lowerTargets = Targets(lower);
    const HitTarget targets[]{ lowerTargets[0u], lowerTargets[1u], lowerTargets[2u], lowerTargets[3u], lowerTargets[4u],
        Barrier(higher), Child(higher, 201u, { 50.0f, 50.0f, 30.0f, 20.0f }, 11u) };
    const PopupScope scopes[]{ lower, higher };
    ASSERT_TRUE(m_router.commitTargets(targets, 7u, 3u, scopes, 2u));
    ASSERT_EQ(m_router.focus().value, 201u);
    ASSERT_TRUE(install(higher, 4u));
    EXPECT_EQ(m_router.focus().value, 201u);
    m_router.closePopup(higher.token);
    const auto base = BaseTargets();
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 5u));
    EXPECT_FALSE(m_router.hasPopup());
    EXPECT_EQ(m_router.focus().value, 1u);
}

TEST_F(UiPopupRouterTests, InsertingLowerBeneathRetainedTopKeepsTopFocusAndTheOriginalBaseRestoreTarget){
    focusBase();
    const PopupScope higher = Scope(200u, 5u);
    ASSERT_TRUE(install(higher, 2u));
    key(Core::Key::Tab);
    ASSERT_EQ(m_router.focus().value, 202u);
    const PopupScope lower = Scope();
    const auto lowerTargets = Targets(lower);
    const HitTarget targets[]{ lowerTargets[0u], lowerTargets[1u], lowerTargets[2u], lowerTargets[3u], lowerTargets[4u],
        Barrier(higher), Child(higher, 201u, { 50.0f, 50.0f, 30.0f, 20.0f }, 11u),
        Child(higher, 202u, { 90.0f, 50.0f, 30.0f, 20.0f }, 12u) };
    const PopupScope scopes[]{ lower, higher };
    ASSERT_TRUE(m_router.commitTargets(targets, 8u, 3u, scopes, 2u));
    EXPECT_EQ(m_router.focus().value, 202u);
    m_router.closePopup(higher.token);
    ASSERT_TRUE(install(lower, 4u));
    EXPECT_EQ(m_router.focus().value, 101u);
    m_router.closePopup(lower.token);
    const auto base = BaseTargets();
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 5u));
    EXPECT_FALSE(m_router.hasPopup());
    EXPECT_EQ(m_router.focus().value, 1u);
}

TEST_F(UiPopupRouterTests, FocusLossDismissesAllScopesWithoutRestoringUnderlyingFocus){
    focusBase();
    const PopupScope lower = Scope();
    ASSERT_TRUE(install(lower, 2u));
    const PopupScope higher = Scope(200u, 5u);
    const auto lowerTargets = Targets(lower);
    const HitTarget targets[]{ lowerTargets[0u], lowerTargets[1u], lowerTargets[2u], lowerTargets[3u], lowerTargets[4u],
        Barrier(higher), Child(higher, 201u, { 50.0f, 50.0f, 30.0f, 20.0f }, 11u) };
    const PopupScope scopes[]{ lower, higher };
    ASSERT_TRUE(m_router.commitTargets(targets, 7u, 3u, scopes, 2u));
    EXPECT_TRUE(send({ .type = InputEventType::FocusLost, .position = {} }).keyboardConsumed);
    EXPECT_FALSE(m_router.focus().valid());
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_TRUE(m_router.consumePopupDismissal(lower.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::FocusLost);
    EXPECT_TRUE(m_router.consumePopupDismissal(higher.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::FocusLost);
    const auto base = BaseTargets();
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 4u));
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.hasPopup());
}

TEST_F(UiPopupRouterTests, FocusLossWhileAlreadyClosingStillSuppressesSavedFocusRestoration){
    focusBase();
    const PopupScope scope = Scope();
    ASSERT_TRUE(install(scope, 2u));
    key(Core::Key::Escape);
    EXPECT_TRUE(m_router.hasPopup());
    EXPECT_TRUE(send({ .type = InputEventType::FocusLost, .position = {} }).keyboardConsumed);
    const auto base = BaseTargets();
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 3u));
    EXPECT_FALSE(m_router.hasPopup());
    EXPECT_FALSE(m_router.focus().valid());
}

TEST_F(UiPopupRouterTests, InvalidatingPopupOwnerRetiresItsScopeActionsAndHeldCaptureImmediately){
    const PopupScope scope = Scope();
    ASSERT_TRUE(install(scope, 7u));
    click({ 55.0f, 55.0f });
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 55.0f, 55.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Space }).keyboardConsumed);
    ASSERT_EQ(m_router.actions().size(), 2u);
    m_router.invalidateTarget(scope.token.widget);
    EXPECT_FALSE(m_router.hasPopup());
    EXPECT_EQ(m_router.layoutGeneration(), 7u);
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(m_router.hitTest({ 55.0f, 55.0f }).valid());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 55.0f, 55.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Space }).keyboardConsumed);
    EXPECT_FALSE(m_router.consumeActivation({ 101u }));
}

TEST_F(UiPopupRouterTests, InvalidScopeDescriptionsRejectAtomicallyWithoutChangingAcceptedInteraction){
    const PopupScope scope = Scope();
    const auto targets = Targets(scope);
    ASSERT_TRUE(install(scope, 1u));
    key(Core::Key::Enter);
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 55.0f, 55.0f } }).pointerConsumed);
    Array<PopupScope, 10u> invalid;
    for(PopupScope& candidate : invalid)
        candidate = scope;
    invalid[0u].token.widget = {};
    invalid[1u].token.declarationGeneration = 0u;
    invalid[2u].token.instanceGeneration = 0u;
    invalid[3u].token.openGeneration = 0u;
    invalid[4u].layer = 0u;
    invalid[5u].bounds.width = 0.0f;
    invalid[6u].viewport.height = 0.0f;
    invalid[7u].bounds.x = Limit<f32>::s_QuietNaN;
    invalid[8u].bounds.x = -1.0f;
    invalid[9u].bounds.height = -1.0f;
    for(const PopupScope& candidate : invalid){
        EXPECT_FALSE(m_router.commitTargets(targets.data(), targets.size(), 2u, &candidate, 1u));
        EXPECT_EQ(m_router.layoutGeneration(), 1u);
        EXPECT_TRUE(m_router.hasPopup());
        EXPECT_EQ(m_router.focus().value, 101u);
        EXPECT_EQ(m_router.capture().value, 101u);
        EXPECT_EQ(m_router.hitTest({ 55.0f, 55.0f }).value, 101u);
        EXPECT_EQ(m_router.actions().size(), 1u);
    }
    EXPECT_FALSE(m_router.commitTargets(targets.data(), targets.size(), 2u, nullptr, 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 55.0f, 55.0f } }).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation({ 101u }));
}

TEST_F(UiPopupRouterTests, MissingOwnersDuplicateScopesAndMismatchedScopedTargetsRejectAtomically){
    const PopupScope scope = Scope();
    const auto accepted = Targets(scope);
    ASSERT_TRUE(install(scope, 1u));
    const PopupScope duplicate[]{ scope, scope };
    EXPECT_FALSE(m_router.commitTargets(accepted.data(), accepted.size(), 2u, duplicate, 2u));
    EXPECT_FALSE(m_router.commitTargets(accepted.data(), accepted.size(), 2u));
    for(usize mismatch = 0u; mismatch < 7u; ++mismatch){
        auto invalid = accepted;
        switch(mismatch){
        case 0u: ++invalid[3u].popup.openGeneration; break;
        case 1u: ++invalid[3u].layer; break;
        case 2u: invalid[0u].layer = 1u; break;
        case 3u: invalid[0u].popup.widget = scope.token.widget; break;
        case 4u: invalid[2u].id = { 150u }; break;
        case 5u: ++invalid[2u].declarationGeneration; break;
        case 6u: invalid[2u].popup = {}; break;
        }
        EXPECT_FALSE(m_router.commitTargets(invalid.data(), invalid.size(), 2u, &scope, 1u));
        EXPECT_EQ(m_router.layoutGeneration(), 1u);
        EXPECT_EQ(m_router.focus().value, 101u);
        EXPECT_EQ(m_router.hitTest({ 55.0f, 55.0f }).value, 101u);
    }
    auto invalid = accepted;
    invalid[4u] = invalid[2u];
    EXPECT_FALSE(m_router.commitTargets(invalid.data(), invalid.size(), 2u, &scope, 1u));
    EXPECT_TRUE(install(scope, 2u));
}

TEST_F(UiPopupRouterTests, PopupCountBoundRejectsWithoutReplacingTheAcceptedScope){
    const PopupScope scope = Scope();
    ASSERT_TRUE(install(scope, 1u));
    Array<PopupScope, s_InputMaxPopups + 1u> overflow;
    for(usize index = 0u; index < overflow.size(); ++index)
        overflow[index] = Scope(200u + index, static_cast<u32>(index + 1u));
    const auto targets = Targets(scope);
    EXPECT_FALSE(m_router.commitTargets(targets.data(), targets.size(), 2u, overflow.data(), overflow.size()));
    EXPECT_EQ(m_router.layoutGeneration(), 1u);
    EXPECT_EQ(m_router.focus().value, 101u);
    EXPECT_TRUE(m_router.hasPopup());
}

TEST_F(UiPopupRouterTests, ResetDropsOwnedScopesDismissalsAndInteractionBeforeLayoutReuse){
    const PopupScope scope = Scope();
    ASSERT_TRUE(install(scope, 7u));
    key(Core::Key::Escape);
    m_router.reset();
    EXPECT_FALSE(m_router.hasPopup());
    EXPECT_FALSE(m_router.wantsKeyboard());
    EXPECT_FALSE(m_router.wantsPointer());
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_EQ(m_router.layoutGeneration(), 0u);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_FALSE(m_router.consumePopupDismissal(scope.token, reason));
    const auto base = BaseTargets();
    ASSERT_TRUE(m_router.commitTargets(base.data(), base.size(), 1u));
    EXPECT_EQ(m_router.hitTest({ 15.0f, 15.0f }).value, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

