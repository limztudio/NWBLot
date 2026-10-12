// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/input/router.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_pointer_hit_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

static constexpr InputSource s_Cursor{ 11u, 1u };
static constexpr InputSource s_OtherCursor{ 12u, 1u };
static constexpr InputSource s_Contact{ 13u, 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static HitTarget Target(const u64 id, const Rect& rectangle = { 0.0f, 0.0f, 100.0f, 100.0f }){
    HitTarget target;
    target.id = { id };
    target.declarationGeneration = 7u;
    target.rectangle = rectangle;
    target.clip = { 0.0f, 0.0f, 200.0f, 150.0f };
    target.focusable = true;
    target.activatable = true;
    target.pointerGesture = true;
    return target;
}

[[nodiscard]] static PointerInput Pointer(const PointerPhase::Enum phase, const InputSource& source, const Point& position, const bool hoverCapable = false, const PointerIntent::Enum intent = PointerIntent::Activate){
    PointerInput input;
    input.source = source;
    input.phase = phase;
    input.intent = intent;
    input.position = position;
    input.hoverCapable = hoverCapable;
    return input;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiPointerHitTests : public testing::Test{
public:
    UiPointerHitTests()
        : m_arena(Name("tests/ui/pointer_hit"))
        , m_router(m_arena)
    {}


protected:
    [[nodiscard]] InputRoutingResult send(const PointerInput& input){
        EXPECT_TRUE(m_router.queuePointer(input));
        return m_router.process();
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    InputRouter m_router;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPointerHitTests, VisibleOwnedPartCannotBypassItsClippedHostOrDisabledOverlaps){
    HitTarget targets[]{ Target(1u), Target(2u), Target(3u), Target(4u), Target(5u) };
    targets[1u].paintOrder = 20u;
    targets[1u].clip = { 40.0f, 40.0f, 10.0f, 10.0f };
    targets[2u].paintOrder = 100u;
    targets[2u].enabled = false;
    targets[3u].clip.width = 0.0f;
    targets[3u].control = { 14u, 15u, 16u };
    targets[4u].owner = targets[3u].id;
    targets[4u].ownerDeclarationGeneration = targets[3u].declarationGeneration;
    targets[4u].control = targets[3u].control;
    targets[4u].focusable = false;
    targets[4u].paintOrder = Limit<u32>::s_Max;
    targets[4u].value = 27u;
    ASSERT_TRUE(m_router.commitTargets(targets, 5u, 1u));
    EXPECT_EQ(m_router.hitTest({ 40.0f, 40.0f }), targets[1u].id);
    EXPECT_EQ(m_router.hitTest({ 50.0f, 45.0f }), targets[0u].id);
    EXPECT_EQ(m_router.hitTest({ 45.0f, 50.0f }), targets[0u].id);
    ASSERT_TRUE(send(Pointer(PointerPhase::Press, s_Cursor, { 45.0f, 45.0f }, true)).pointerConsumed);
    EXPECT_EQ(m_router.hover(), targets[1u].id);
    EXPECT_EQ(m_router.capture(), targets[1u].id);
    ASSERT_TRUE(send(Pointer(PointerPhase::Release, s_Cursor, { 45.0f, 45.0f }, true)).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(targets[1u].id));
    EXPECT_FALSE(m_router.consumeActivation(targets[4u].id));
    EXPECT_FALSE(m_router.consumeControlAction(targets[3u].id, 7u, targets[3u].control));

    targets[3u].clip.width = 100.0f;
    ASSERT_TRUE(m_router.commitTargets(targets, 5u, 2u));
    EXPECT_EQ(m_router.hover(), targets[4u].id);
    ASSERT_TRUE(send(Pointer(PointerPhase::Press, s_Cursor, { 45.0f, 45.0f }, true)).pointerConsumed);
    EXPECT_EQ(m_router.capture(), targets[4u].id);
    EXPECT_EQ(m_router.focus(), targets[3u].id);
    ASSERT_TRUE(send(Pointer(PointerPhase::Release, s_Cursor, { 45.0f, 45.0f }, true)).pointerConsumed);
    const auto action = m_router.consumeControlAction(targets[3u].id, 7u, targets[3u].control);
    ASSERT_TRUE(action);
    EXPECT_EQ(action->source, targets[4u].id);
    EXPECT_EQ(action->value, 27u);
    EXPECT_FALSE(m_router.consumeActivation(targets[1u].id));
}

TEST_F(UiPointerHitTests, OutsideTouchDismissalRefreshesAnIndependentCursorsPopupHover){
    for(const auto intent : { PointerIntent::Activate, PointerIntent::ContextMenu }){
        Core::Alloc::GlobalArena arena(Name("tests/ui/pointer_hit_popup"));
        InputRouter router(arena);
        PopupScope scope;
        scope.token = { { 100u }, 7u, 8u, 9u };
        scope.bounds = { 40.0f, 40.0f, 80.0f, 60.0f };
        scope.viewport = { 0.0f, 0.0f, 200.0f, 150.0f };
        scope.layer = 3u;
        const HitTarget base = Target(1u, scope.viewport);
        HitTarget popup = Target(100u, scope.bounds);
        popup.popup = scope.token;
        popup.layer = scope.layer;
        const HitTarget targets[]{ base, popup };
        ASSERT_TRUE(router.commitTargets(targets, 2u, 1u, &scope, 1u));
        ASSERT_TRUE(router.queuePointer(Pointer(PointerPhase::Move, s_Cursor, { 50.0f, 50.0f }, true)));
        ASSERT_EQ(router.process().hover, popup.id);
        const u64 hovered = router.hoverActivityGeneration();

        ASSERT_TRUE(router.queuePointer(Pointer(PointerPhase::Press, s_Contact, { 10.0f, 10.0f }, false, intent)));
        const auto dismissed = router.process();
        EXPECT_TRUE(dismissed.pointerConsumed);
        EXPECT_FALSE(dismissed.hover.valid());
        EXPECT_GT(router.hoverActivityGeneration(), hovered);
        EXPECT_TRUE(router.pointerKnown());
        EXPECT_FLOAT_EQ(router.pointerPosition().x, 50.0f);
        EXPECT_FLOAT_EQ(router.pointerPosition().y, 50.0f);
        const auto reason = router.consumePopupDismissal(scope.token);
        ASSERT_TRUE(reason);
        EXPECT_EQ(*reason, PopupDismissReason::OutsideClick);
        ASSERT_TRUE(router.commitTargets(&base, 1u, 2u));
        EXPECT_EQ(router.hover(), base.id);
        ASSERT_TRUE(router.queuePointer(Pointer(PointerPhase::Release, s_Contact, { 10.0f, 10.0f }, false, intent)));
        EXPECT_TRUE(router.process().pointerConsumed);
        EXPECT_FALSE(router.consumeActivation(base.id));
    }
}

TEST_F(UiPointerHitTests, HoverProviderChangesAndLayoutRelocationCannotReuseAnOlderTarget){
    HitTarget targets[]{ Target(1u, { 0.0f, 0.0f, 60.0f, 100.0f }), Target(2u, { 70.0f, 0.0f, 60.0f, 100.0f }) };
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, 1u));
    ASSERT_EQ(send(Pointer(PointerPhase::Move, s_Cursor, { 20.0f, 20.0f }, true)).hover, targets[0u].id);
    const u64 initial = m_router.hoverActivityGeneration();
    ASSERT_EQ(send(Pointer(PointerPhase::Move, s_OtherCursor, { 20.0f, 20.0f }, true)).hover, targets[0u].id);
    EXPECT_EQ(m_router.hoverActivityGeneration(), initial);
    EXPECT_EQ(send(Pointer(PointerPhase::Leave, s_Cursor, { 20.0f, 20.0f }, true)).hover, targets[0u].id);
    EXPECT_TRUE(m_router.pointerKnown());

    targets[0u].rectangle.x = 70.0f;
    targets[1u].rectangle.x = 0.0f;
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, 2u));
    EXPECT_EQ(m_router.hover(), targets[1u].id);
    const u64 relocated = m_router.hoverActivityGeneration();
    EXPECT_GT(relocated, initial);
    ASSERT_EQ(send(Pointer(PointerPhase::Move, s_OtherCursor, { 20.0f, 20.0f }, true)).hover, targets[1u].id);
    EXPECT_EQ(m_router.hoverActivityGeneration(), relocated);
    targets[1u].declarationGeneration = 8u;
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, 3u));
    EXPECT_EQ(m_router.hover(), targets[1u].id);
    EXPECT_GT(m_router.hoverActivityGeneration(), relocated);
    EXPECT_FALSE(send(Pointer(PointerPhase::Leave, s_OtherCursor, { 20.0f, 20.0f }, true)).hover.valid());
    EXPECT_FALSE(m_router.pointerKnown());
}

TEST_F(UiPointerHitTests, CrossTargetTouchDragKeepsCaptureWhileAnotherCursorRefreshesHover){
    const HitTarget targets[]{ Target(1u, { 0.0f, 0.0f, 60.0f, 100.0f }), Target(2u, { 70.0f, 0.0f, 60.0f, 100.0f }) };
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, 1u));
    ASSERT_EQ(send(Pointer(PointerPhase::Move, s_Cursor, { 20.0f, 20.0f }, true)).hover, targets[0u].id);
    ASSERT_TRUE(send(Pointer(PointerPhase::Press, s_Contact, { 80.0f, 20.0f })).pointerConsumed);
    ASSERT_EQ(m_router.capture(), targets[1u].id);
    const auto initial = m_router.consumePointerGesture(targets[1u].id, 7u);
    ASSERT_TRUE(initial);
    ASSERT_TRUE(send(Pointer(PointerPhase::Move, s_Contact, { 20.0f, 20.0f })).pointerConsumed);
    EXPECT_EQ(m_router.capture(), targets[1u].id);
    EXPECT_EQ(m_router.hover(), targets[0u].id);
    const auto moved = m_router.consumePointerGesture(targets[1u].id, 7u);
    ASSERT_TRUE(moved);
    EXPECT_EQ(moved->id.sequence, initial->id.sequence);
    EXPECT_FLOAT_EQ(moved->position.x, 20.0f);
    const auto cursor = send(Pointer(PointerPhase::Move, s_Cursor, { 80.0f, 20.0f }, true));
    EXPECT_FALSE(cursor.pointerRouted);
    EXPECT_EQ(cursor.hover, targets[1u].id);
    EXPECT_EQ(cursor.capture, targets[1u].id);

    ASSERT_TRUE(m_router.queuePointer(Pointer(PointerPhase::Move, s_Contact, { 20.0f, 20.0f })));
    ASSERT_TRUE(m_router.queuePointer(Pointer(PointerPhase::Move, s_Cursor, { 20.0f, 20.0f }, true)));
    ASSERT_TRUE(m_router.queuePointer(Pointer(PointerPhase::Release, s_Contact, { 80.0f, 20.0f })));
    const auto released = m_router.process();
    EXPECT_TRUE(released.pointerConsumed);
    EXPECT_FALSE(released.capture.valid());
    EXPECT_EQ(released.hover, targets[0u].id);
    EXPECT_FLOAT_EQ(m_router.pointerPosition().x, 20.0f);
    EXPECT_TRUE(m_router.consumeActivation(targets[1u].id));
    EXPECT_FALSE(m_router.consumeActivation(targets[0u].id));
    const auto completed = m_router.consumePointerGesture(targets[1u].id, 7u);
    ASSERT_TRUE(completed);
    EXPECT_EQ(completed->id.sequence, initial->id.sequence);
    EXPECT_EQ(completed->state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(completed->position.x, 80.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

