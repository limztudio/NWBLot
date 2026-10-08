// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_pointer_gesture_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

HitTarget GestureTarget(const u64 value = 1u){
    HitTarget target;
    target.id = { value };
    target.rectangle = { 10.0f, 20.0f, 80.0f, 30.0f };
    target.clip = { 0.0f, 0.0f, 200.0f, 200.0f };
    target.pointerGesture = true;
    return target;
}

InputEvent PointerEvent(const InputEventType::Enum type, const Point& position = { 15.0f, 25.0f }){
    return { type, position };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiPointerGestureTests : public testing::Test{
public:
    UiPointerGestureTests()
        : m_arena(Name("tests/ui/pointer_gesture"))
        , m_router(m_arena)
        , m_context(m_arena)
    {}


protected:
    InputRoutingResult send(const InputEvent& event){
        EXPECT_TRUE(m_router.queue(event));
        return m_router.process();
    }

    InputRoutingResult sendToContext(const InputEvent& event){
        EXPECT_TRUE(m_context.input().queue(event));
        return m_context.input().process();
    }

    void complete(const Point& position = { 45.0f, 55.0f }){
        EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
        EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove, position)).pointerConsumed);
        EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, position)).pointerConsumed);
    }

    WidgetState prepare(const u64 generation, const WidgetRoot& root = { 91u, 1u }){
        EXPECT_TRUE(m_context.beginFrame(generation));
        EXPECT_TRUE(m_context.beginRoot(root));
        const WidgetState* state = m_context.declare("title", WidgetKind::Panel);
        EXPECT_NE(state, nullptr);
        if(state == nullptr)
            return {};
        const WidgetState snapshot = *state;
        EXPECT_TRUE(m_context.addTarget(snapshot, GestureTarget()));
        EXPECT_TRUE(m_context.endRoot());
        EXPECT_TRUE(m_context.finishFrame());
        return snapshot;
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    InputRouter m_router;
    Context m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPointerGestureTests, BarrierCaptureDoesNotImplicitlyCreateAGesture){
    HitTarget target = GestureTarget();
    target.pointerGesture = false;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    complete();
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, target.declarationGeneration));
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(m_router.primaryDown());
}

TEST_F(UiPointerGestureTests, QuickPressMoveReleaseRetainsCommittedBaselineAndFinalDelta){
    HitTarget target = GestureTarget();
    target.declarationGeneration = 13u;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 7u));
    ASSERT_TRUE(m_router.queue(PointerEvent(InputEventType::PrimaryDown)));
    ASSERT_TRUE(m_router.queue(PointerEvent(InputEventType::PointerMove, { 90.0f, 80.0f })));
    ASSERT_TRUE(m_router.queue(PointerEvent(InputEventType::PrimaryUp, { 110.0f, 120.0f })));
    EXPECT_TRUE(m_router.process().pointerConsumed);
    EXPECT_FALSE(m_router.process().pointerConsumed);
    PointerGesture gesture;
    const auto gestureResult = m_router.consumePointerGesture(target.id, 13u);
    ASSERT_TRUE(gestureResult);
    gesture = *gestureResult;
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(gesture.origin.x, 15.0f);
    EXPECT_FLOAT_EQ(gesture.origin.y, 25.0f);
    EXPECT_FLOAT_EQ(gesture.position.x, 110.0f);
    EXPECT_FLOAT_EQ(gesture.position.y, 120.0f);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 13u));
    EXPECT_TRUE(m_router.actions().empty());
}

TEST_F(UiPointerGestureTests, ActiveUpdateConsumesOnceAndPreservesProgressAcrossCoalescedMoves){
    const HitTarget target = GestureTarget();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    PointerGesture gesture;
    const auto gestureResult1 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult1);
    gesture = *gestureResult1;
    const InputActionId press = gesture.id;
    EXPECT_EQ(gesture.state, PointerGestureState::Active);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove)).pointerConsumed);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove, { 40.0f, 50.0f })).pointerConsumed);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove, { 70.0f, 90.0f })).pointerConsumed);
    const auto gestureResult2 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult2);
    gesture = *gestureResult2;
    EXPECT_EQ(gesture.id.sequence, press.sequence);
    EXPECT_EQ(gesture.id.layoutGeneration, press.layoutGeneration);
    EXPECT_FLOAT_EQ(gesture.origin.x, 15.0f);
    EXPECT_FLOAT_EQ(gesture.position.x, 70.0f);
    EXPECT_FLOAT_EQ(gesture.position.y, 90.0f);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 70.0f, 90.0f })).pointerConsumed);
    const auto gestureResult3 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult3);
    gesture = *gestureResult3;
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_EQ(gesture.id.sequence, press.sequence);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u));
}

TEST_F(UiPointerGestureTests, SeveralPressSequencesRemainOrderedWithSeparateOrigins){
    const HitTarget target = GestureTarget();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    complete({ 30.0f, 40.0f });
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown, { 25.0f, 35.0f })).pointerConsumed);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 60.0f, 70.0f })).pointerConsumed);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown, { 35.0f, 45.0f })).pointerConsumed);
    PointerGesture first;
    PointerGesture second;
    PointerGesture third;
    const auto firstResult = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(firstResult);
    first = *firstResult;
    const auto secondResult = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(secondResult);
    second = *secondResult;
    const auto thirdResult1 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(thirdResult1);
    third = *thirdResult1;
    EXPECT_EQ(first.state, PointerGestureState::Completed);
    EXPECT_EQ(second.state, PointerGestureState::Completed);
    EXPECT_EQ(third.state, PointerGestureState::Active);
    EXPECT_LT(first.id.sequence, second.id.sequence);
    EXPECT_LT(second.id.sequence, third.id.sequence);
    EXPECT_FLOAT_EQ(first.origin.x, 15.0f);
    EXPECT_FLOAT_EQ(second.origin.x, 25.0f);
    EXPECT_FLOAT_EQ(third.origin.x, 35.0f);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 130.0f, 160.0f })).pointerConsumed);
    const auto thirdResult2 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(thirdResult2);
    third = *thirdResult2;
    EXPECT_EQ(third.state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(third.position.x, 130.0f);
}

TEST_F(UiPointerGestureTests, ActiveGestureKeepsOriginalLayoutWhenNewGeometryIsPublished){
    HitTarget target = GestureTarget();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 7u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    target.rectangle = { 80.0f, 120.0f, 100.0f, 40.0f };
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 9u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 110.0f, 120.0f })).pointerConsumed);
    PointerGesture gesture;
    const auto gestureResult1 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult1);
    gesture = *gestureResult1;
    EXPECT_EQ(gesture.id.layoutGeneration, 7u);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.x, 10.0f);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.width, 80.0f);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown, { 100.0f, 130.0f })).pointerConsumed);
    const auto gestureResult2 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult2);
    gesture = *gestureResult2;
    EXPECT_EQ(gesture.id.layoutGeneration, 9u);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.x, 80.0f);
    EXPECT_FLOAT_EQ(gesture.origin.x, 100.0f);
}

TEST_F(UiPointerGestureTests, FullReferenceGeometryIsCopiedAtPressAndRemainsImmutableAfterLayoutChanges){
    HitTarget target = GestureTarget();
    target.gestureReference = { 10.0f, 20.0f, 80.0f, 130.0f };
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 7u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    PointerGesture gesture;
    const auto gestureResult1 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult1);
    gesture = *gestureResult1;
    const u64 sequence = gesture.id.sequence;
    EXPECT_FLOAT_EQ(gesture.targetRectangle.height, 30.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.height, 130.0f);
    target.rectangle = { 70.0f, 90.0f, 100.0f, 40.0f };
    target.gestureReference = { 70.0f, 90.0f, 100.0f, 180.0f };
    EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove, { 30.0f, 40.0f })).pointerConsumed);
    const auto gestureResult2 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult2);
    gesture = *gestureResult2;
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.x, 10.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.height, 130.0f);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 9u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 110.0f, 120.0f })).pointerConsumed);
    const auto gestureResult3 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult3);
    gesture = *gestureResult3;
    EXPECT_EQ(gesture.id.sequence, sequence);
    EXPECT_EQ(gesture.id.layoutGeneration, 7u);
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.height, 30.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.x, 10.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.y, 20.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.width, 80.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.height, 130.0f);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown, { 80.0f, 100.0f })).pointerConsumed);
    const auto gestureResult4 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult4);
    gesture = *gestureResult4;
    EXPECT_GT(gesture.id.sequence, sequence);
    EXPECT_EQ(gesture.id.layoutGeneration, 9u);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.height, 40.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.x, 70.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.height, 180.0f);
}

TEST_F(UiPointerGestureTests, MalformedReferenceRejectsPublicationAtomicallyAndEmptyReferenceFallsBack){
    HitTarget target = GestureTarget();
    target.gestureReference = { 10.0f, 20.0f, 80.0f, 130.0f };
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    const Rect invalidReferences[]{
        { 0.0f, 0.0f, -1.0f, 1.0f },
        { 0.0f, 0.0f, 1.0f, -1.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { Limit<f32>::s_QuietNaN, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, Limit<f32>::s_Infinity },
        { Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 1.0f },
    };
    for(const Rect& reference : invalidReferences){
        HitTarget invalid = target;
        invalid.gestureReference = reference;
        EXPECT_FALSE(m_router.commitTargets(&invalid, 1u, 2u));
        EXPECT_EQ(m_router.layoutGeneration(), 1u);
        EXPECT_EQ(m_router.capture(), target.id);
        EXPECT_TRUE(m_router.primaryDown());
    }
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 60.0f, 70.0f })).pointerConsumed);
    PointerGesture gesture;
    const auto gestureResult1 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult1);
    gesture = *gestureResult1;
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.height, 130.0f);
    target.gestureReference = { 42.0f, 44.0f, 0.0f, 0.0f };
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    const auto gestureResult2 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult2);
    gesture = *gestureResult2;
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.x, target.rectangle.x);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.y, target.rectangle.y);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.width, target.rectangle.width);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.height, target.rectangle.height);
}

TEST_F(UiPointerGestureTests, LifetimeRemovalCancelsGestureWhileReleaseOwnershipRemains){
    HitTarget target = GestureTarget();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    PointerGesture gesture;
    const auto gestureResult1 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult1);
    gesture = *gestureResult1;
    const u64 firstSequence = gesture.id.sequence;
    m_router.invalidateTarget(target.id);
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(m_router.primaryDown());
    EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove, { 110.0f, 120.0f })).pointerConsumed);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 110.0f, 120.0f })).pointerConsumed);
    target.declarationGeneration = 2u;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u));
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 2u));
    complete();
    const auto gestureResult2 = m_router.consumePointerGesture(target.id, 2u);
    ASSERT_TRUE(gestureResult2);
    gesture = *gestureResult2;
    EXPECT_GT(gesture.id.sequence, firstSequence);
    EXPECT_EQ(gesture.id.declarationGeneration, 2u);
}

TEST_F(UiPointerGestureTests, NewLifetimeDisabledAndNonGestureDeclarationsDiscardOldRecords){
    HitTarget target = GestureTarget();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    complete();
    target.declarationGeneration = 2u;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 2u));
    complete();
    target.pointerGesture = false;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 3u));
    target.pointerGesture = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 4u));
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 2u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    target.enabled = false;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 5u));
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp)).pointerConsumed);
    target.enabled = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 6u));
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 2u));
}

TEST_F(UiPointerGestureTests, PointerLeaveRetainsCaptureAndFinalOutsideRelease){
    const HitTarget target = GestureTarget();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    const InputRoutingResult left = send({ InputEventType::PointerLeave, {} });
    EXPECT_TRUE(left.pointerConsumed);
    EXPECT_TRUE(left.wantsPointer);
    EXPECT_FALSE(left.hover.valid());
    EXPECT_EQ(left.capture, target.id);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { -50.0f, 300.0f })).pointerConsumed);
    PointerGesture gesture;
    const auto gestureResult = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult);
    gesture = *gestureResult;
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(gesture.position.x, -50.0f);
    EXPECT_FLOAT_EQ(gesture.position.y, 300.0f);
}

TEST_F(UiPointerGestureTests, FocusLossCancelsActiveAndCompletedGesturesWithoutReusingSequences){
    HitTarget target = GestureTarget();
    target.focusable = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    complete();
    PointerGesture gesture;
    const auto gestureResult1 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult1);
    gesture = *gestureResult1;
    const u64 firstSequence = gesture.id.sequence;
    complete();
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    const InputRoutingResult lost = send({ InputEventType::FocusLost, {} });
    EXPECT_TRUE(lost.pointerConsumed);
    EXPECT_TRUE(lost.keyboardConsumed);
    EXPECT_FALSE(lost.capture.valid());
    EXPECT_FALSE(lost.focus.valid());
    EXPECT_FALSE(m_router.primaryDown());
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u));
    complete();
    const auto gestureResult2 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult2);
    gesture = *gestureResult2;
    EXPECT_GT(gesture.id.sequence, firstSequence);
    m_router.reset();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    complete();
    PointerGesture afterReset;
    const auto afterResetResult = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(afterResetResult);
    afterReset = *afterResetResult;
    EXPECT_GT(afterReset.id.sequence, gesture.id.sequence);
}

TEST_F(UiPointerGestureTests, EscapeCancelsOnlyTheActiveGestureAndRetainsHeldReleaseOwnership){
    HitTarget target = GestureTarget();
    target.focusable = true;
    target.activatable = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    complete({ 40.0f, 40.0f });
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    const InputRoutingResult escaped = send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Escape });
    EXPECT_TRUE(escaped.keyboardConsumed);
    EXPECT_FALSE(escaped.capture.valid());
    EXPECT_FALSE(escaped.focus.valid());
    EXPECT_TRUE(m_router.primaryDown());
    PointerGesture gesture;
    const auto gestureResult = m_router.consumePointerGesture(target.id, target.declarationGeneration);
    ASSERT_TRUE(gestureResult);
    gesture = *gestureResult;
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(gesture.position.x, 40.0f);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, target.declarationGeneration));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove, { 180.0f, 180.0f })).pointerConsumed);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, target.declarationGeneration));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp)).pointerConsumed);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, target.declarationGeneration));
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Escape }).keyboardConsumed);
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_FALSE(m_router.consumeActivation(target.id));
}

TEST_F(UiPointerGestureTests, ActiveCaptureLossPreservesCompletedGesturesActionsAndKeyboardOwnership){
    HitTarget target = GestureTarget();
    target.focusable = true;
    target.activatable = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    complete({ 40.0f, 40.0f });
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Space }).keyboardConsumed);
    ASSERT_EQ(m_router.actions().size(), 2u);
    const InputRoutingResult lost = send({ InputEventType::PointerCaptureLost, {} });
    EXPECT_TRUE(lost.pointerConsumed);
    EXPECT_FALSE(lost.capture.valid());
    EXPECT_FALSE(lost.hover.valid());
    EXPECT_EQ(lost.focus, target.id);
    EXPECT_TRUE(lost.wantsKeyboard);
    EXPECT_TRUE(m_router.ownsKey(Core::Key::Space));
    EXPECT_FALSE(m_router.primaryDown());
    PointerGesture gesture;
    const auto gestureResult = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult);
    gesture = *gestureResult;
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(gesture.position.x, 40.0f);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u));
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Space }).keyboardConsumed);
    EXPECT_FALSE(m_router.ownsKey(Core::Key::Space));
    EXPECT_EQ(m_router.layoutGeneration(), 1u);
    EXPECT_EQ(m_router.targets().size(), 1u);
}

TEST_F(UiPointerGestureTests, CaptureLossAfterOrdinaryReleaseCannotEraseACompletedGesture){
    HitTarget target = GestureTarget();
    target.focusable = true;
    target.activatable = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    complete({ 40.0f, 40.0f });
    const InputRoutingResult lost = send({ InputEventType::PointerCaptureLost, {} });
    EXPECT_FALSE(lost.pointerConsumed);
    EXPECT_EQ(lost.focus, target.id);
    EXPECT_EQ(lost.hover, target.id);
    PointerGesture gesture;
    const auto gestureResult = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult);
    gesture = *gestureResult;
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_TRUE(m_router.consumeActivation(target.id));
}

TEST_F(UiPointerGestureTests, LargeMoveBurstCoalescesWithoutExhaustingGestureCapacity){
    const HitTarget target = GestureTarget();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_FALSE(send(PointerEvent(InputEventType::PrimaryDown)).gestureOverflow);
    for(usize index = 0u; index < s_InputMaxPointerGestures * 4u; ++index){
        const Point position{ static_cast<f32>(index), static_cast<f32>(index * 2u) };
        EXPECT_FALSE(send(PointerEvent(InputEventType::PointerMove, position)).gestureOverflow);
    }
    PointerGesture gesture;
    const auto gestureResult1 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult1);
    gesture = *gestureResult1;
    EXPECT_FLOAT_EQ(gesture.position.x, static_cast<f32>(s_InputMaxPointerGestures * 4u - 1u));
    EXPECT_EQ(gesture.state, PointerGestureState::Active);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 110.0f, 120.0f })).pointerConsumed);
    const auto gestureResult2 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult2);
    gesture = *gestureResult2;
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
}

TEST_F(UiPointerGestureTests, FullGestureQueueDropsNewPressAndReportsOverflowWithoutReplayingIt){
    const HitTarget target = GestureTarget();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    for(usize index = 0u; index < s_InputMaxPointerGestures; ++index){
        EXPECT_FALSE(send(PointerEvent(InputEventType::PrimaryDown)).gestureOverflow);
        EXPECT_FALSE(send(PointerEvent(InputEventType::PrimaryUp)).gestureOverflow);
    }
    const InputRoutingResult overflow = send(PointerEvent(InputEventType::PrimaryDown));
    EXPECT_TRUE(overflow.pointerConsumed);
    EXPECT_TRUE(overflow.gestureOverflow);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove, { 130.0f, 150.0f })).pointerConsumed);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 130.0f, 150.0f })).pointerConsumed);
    PointerGesture gesture;
    u64 previousSequence = 0u;
    for(usize index = 0u; index < s_InputMaxPointerGestures; ++index){
        const auto gestureResult1 = m_router.consumePointerGesture(target.id, 1u);
        ASSERT_TRUE(gestureResult1);
        gesture = *gestureResult1;
        EXPECT_GT(gesture.id.sequence, previousSequence);
        previousSequence = gesture.id.sequence;
        EXPECT_EQ(gesture.state, PointerGestureState::Completed);
        EXPECT_FLOAT_EQ(gesture.position.x, 15.0f);
    }
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u));
    complete();
    const auto gestureResult2 = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult2);
    gesture = *gestureResult2;
    EXPECT_GT(gesture.id.sequence, previousSequence);
    EXPECT_FLOAT_EQ(gesture.position.x, 45.0f);
}

TEST_F(UiPointerGestureTests, GestureAdmissionUsesTheTopClippedTargetAndLifetimeConsumptionIsExact){
    HitTarget targets[]{ GestureTarget(1u), GestureTarget(2u) };
    targets[1].paintOrder = 1u;
    targets[1].clip = { 10.0f, 20.0f, 10.0f, 10.0f };
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, 1u));
    complete();
    PointerGesture gesture;
    EXPECT_FALSE(m_router.consumePointerGesture(targets[0].id, 1u));
    EXPECT_FALSE(m_router.consumePointerGesture(targets[1].id, 0u));
    EXPECT_FALSE(m_router.consumePointerGesture(targets[1].id, 2u));
    const auto gestureResult1 = m_router.consumePointerGesture(targets[1].id, 1u);
    ASSERT_TRUE(gestureResult1);
    gesture = *gestureResult1;
    EXPECT_EQ(gesture.id.target, targets[1].id);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown, { 25.0f, 25.0f })).pointerConsumed);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 30.0f, 30.0f })).pointerConsumed);
    const auto gestureResult2 = m_router.consumePointerGesture(targets[0].id, 1u);
    ASSERT_TRUE(gestureResult2);
    gesture = *gestureResult2;
    EXPECT_EQ(gesture.id.target, targets[0].id);
}

TEST_F(UiPointerGestureTests, InvalidLayoutPublicationPreservesTheActiveGesture){
    const HitTarget target = GestureTarget();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 7u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    HitTarget invalid[]{ GestureTarget(2u), GestureTarget(2u) };
    EXPECT_FALSE(m_router.commitTargets(invalid, 2u, 8u));
    EXPECT_FALSE(m_router.commitTargets(&target, 1u, 7u));
    EXPECT_EQ(m_router.capture(), target.id);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 90.0f, 100.0f })).pointerConsumed);
    PointerGesture gesture;
    const auto gestureResult = m_router.consumePointerGesture(target.id, 1u);
    ASSERT_TRUE(gestureResult);
    gesture = *gestureResult;
    EXPECT_EQ(gesture.id.layoutGeneration, 7u);
    EXPECT_FLOAT_EQ(gesture.position.x, 90.0f);
}

TEST_F(UiPointerGestureTests, ContextRejectsStaleForeignAndWrongKindDeclarationsWithoutConsuming){
    const WidgetState original = prepare(1u);
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(sendToContext(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    EXPECT_TRUE(sendToContext(PointerEvent(InputEventType::PrimaryUp, { 90.0f, 100.0f })).pointerConsumed);
    PointerGesture gesture;
    EXPECT_FALSE(m_context.takePointerGesture(original, true));
    ASSERT_TRUE(m_context.beginFrame(2u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    const WidgetState* current = m_context.declare("title", WidgetKind::Panel);
    ASSERT_NE(current, nullptr);
    EXPECT_FALSE(m_context.takePointerGesture(original, true));
    WidgetState invalid = *current;
    ++invalid.root.generation;
    EXPECT_FALSE(m_context.takePointerGesture(invalid, true));
    invalid = *current;
    ++invalid.declarationGeneration;
    EXPECT_FALSE(m_context.takePointerGesture(invalid, true));
    invalid = *current;
    invalid.kind = WidgetKind::Button;
    EXPECT_FALSE(m_context.takePointerGesture(invalid, true));
    const auto gestureResult = m_context.takePointerGesture(*current, true);
    ASSERT_TRUE(gestureResult);
    gesture = *gestureResult;
    EXPECT_EQ(gesture.id.target, current->id);
    EXPECT_EQ(gesture.id.declarationGeneration, current->declarationGeneration);
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_FALSE(m_context.takePointerGesture(*current, true));
}

TEST_F(UiPointerGestureTests, ContextRetainsTheFinalGestureWhileItsGpuCandidateIsPending){
    const WidgetState original = prepare(1u);
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(sendToContext(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    ASSERT_TRUE(m_context.beginFrame(2u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    const WidgetState* state = m_context.declare("title", WidgetKind::Panel);
    ASSERT_NE(state, nullptr);
    PointerGesture gesture;
    const auto gestureResult1 = m_context.takePointerGesture(*state, true);
    ASSERT_TRUE(gestureResult1);
    gesture = *gestureResult1;
    const u64 sequence = gesture.id.sequence;
    HitTarget candidate = GestureTarget();
    candidate.rectangle.x = 100.0f;
    ASSERT_TRUE(m_context.addTarget(*state, candidate));
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    EXPECT_FALSE(m_context.beginFrame(3u));
    EXPECT_TRUE(sendToContext(PointerEvent(InputEventType::PointerMove, { 130.0f, 140.0f })).pointerConsumed);
    EXPECT_TRUE(sendToContext(PointerEvent(InputEventType::PrimaryUp, { 150.0f, 160.0f })).pointerConsumed);
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_TRUE(m_context.beginFrame(3u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    state = m_context.declare("title", WidgetKind::Panel);
    ASSERT_NE(state, nullptr);
    const auto gestureResult2 = m_context.takePointerGesture(*state, true);
    ASSERT_TRUE(gestureResult2);
    gesture = *gestureResult2;
    EXPECT_EQ(gesture.id.sequence, sequence);
    EXPECT_EQ(gesture.id.layoutGeneration, 1u);
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.x, 10.0f);
    EXPECT_FLOAT_EQ(gesture.position.x, 150.0f);
    EXPECT_FALSE(m_context.takePointerGesture(*state, true));
}

TEST_F(UiPointerGestureTests, ContextDisablingTheCurrentDeclarationCancelsItsHeldGesture){
    const WidgetState original = prepare(1u);
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(sendToContext(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    ASSERT_TRUE(m_context.beginFrame(2u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    const WidgetState* state = m_context.declare("title", WidgetKind::Panel);
    ASSERT_NE(state, nullptr);
    EXPECT_FALSE(m_context.takePointerGesture(*state, false));
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_TRUE(m_context.input().primaryDown());
    EXPECT_TRUE(sendToContext(PointerEvent(InputEventType::PrimaryUp)).pointerConsumed);
    EXPECT_FALSE(m_context.takePointerGesture(*state, true));
}

TEST_F(UiPointerGestureTests, ContextRootRetirementCancelsPendingGestureAndReplacementCannotReplayIt){
    const WidgetState original = prepare(1u);
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(sendToContext(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    EXPECT_TRUE(sendToContext(PointerEvent(InputEventType::PrimaryUp, { 90.0f, 100.0f })).pointerConsumed);
    m_context.retainRoots(nullptr, 0u);
    EXPECT_TRUE(m_context.states().entries().empty());
    const WidgetState replacement = prepare(2u);
    EXPECT_EQ(replacement.id, original.id);
    EXPECT_GT(replacement.declarationGeneration, original.declarationGeneration);
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_TRUE(m_context.beginFrame(3u));
    ASSERT_TRUE(m_context.beginRoot(replacement.root));
    const WidgetState* current = m_context.declare("title", WidgetKind::Panel);
    ASSERT_NE(current, nullptr);
    EXPECT_FALSE(m_context.takePointerGesture(*current, true));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

