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
    PointerGesture gesture;
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, target.declarationGeneration, gesture));
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
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 13u, gesture));
    EXPECT_TRUE(gesture.id.valid());
    EXPECT_EQ(gesture.id.target, target.id);
    EXPECT_EQ(gesture.id.declarationGeneration, 13u);
    EXPECT_EQ(gesture.id.layoutGeneration, 7u);
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(gesture.origin.x, 15.0f);
    EXPECT_FLOAT_EQ(gesture.origin.y, 25.0f);
    EXPECT_FLOAT_EQ(gesture.position.x, 110.0f);
    EXPECT_FLOAT_EQ(gesture.position.y, 120.0f);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.x, 10.0f);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.y, 20.0f);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.width, 80.0f);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.height, 30.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.x, gesture.targetRectangle.x);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.y, gesture.targetRectangle.y);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.width, gesture.targetRectangle.width);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.height, gesture.targetRectangle.height);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 13u, gesture));
    EXPECT_TRUE(m_router.actions().empty());
}

TEST_F(UiPointerGestureTests, ActiveUpdateConsumesOnceAndPreservesProgressAcrossCoalescedMoves){
    const HitTarget target = GestureTarget();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    PointerGesture gesture;
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
    const InputActionId press = gesture.id;
    EXPECT_EQ(gesture.state, PointerGestureState::Active);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove)).pointerConsumed);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove, { 40.0f, 50.0f })).pointerConsumed);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove, { 70.0f, 90.0f })).pointerConsumed);
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_EQ(gesture.id.sequence, press.sequence);
    EXPECT_EQ(gesture.id.layoutGeneration, press.layoutGeneration);
    EXPECT_FLOAT_EQ(gesture.origin.x, 15.0f);
    EXPECT_FLOAT_EQ(gesture.position.x, 70.0f);
    EXPECT_FLOAT_EQ(gesture.position.y, 90.0f);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 70.0f, 90.0f })).pointerConsumed);
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_EQ(gesture.id.sequence, press.sequence);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u, gesture));
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
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, first));
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, second));
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, third));
    EXPECT_EQ(first.state, PointerGestureState::Completed);
    EXPECT_EQ(second.state, PointerGestureState::Completed);
    EXPECT_EQ(third.state, PointerGestureState::Active);
    EXPECT_LT(first.id.sequence, second.id.sequence);
    EXPECT_LT(second.id.sequence, third.id.sequence);
    EXPECT_FLOAT_EQ(first.origin.x, 15.0f);
    EXPECT_FLOAT_EQ(second.origin.x, 25.0f);
    EXPECT_FLOAT_EQ(third.origin.x, 35.0f);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u, third));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 130.0f, 160.0f })).pointerConsumed);
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, third));
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
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_EQ(gesture.id.layoutGeneration, 7u);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.x, 10.0f);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.width, 80.0f);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown, { 100.0f, 130.0f })).pointerConsumed);
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
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
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
    const u64 sequence = gesture.id.sequence;
    EXPECT_FLOAT_EQ(gesture.targetRectangle.height, 30.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.height, 130.0f);
    target.rectangle = { 70.0f, 90.0f, 100.0f, 40.0f };
    target.gestureReference = { 70.0f, 90.0f, 100.0f, 180.0f };
    EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove, { 30.0f, 40.0f })).pointerConsumed);
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.x, 10.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.height, 130.0f);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 9u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 110.0f, 120.0f })).pointerConsumed);
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_EQ(gesture.id.sequence, sequence);
    EXPECT_EQ(gesture.id.layoutGeneration, 7u);
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.height, 30.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.x, 10.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.y, 20.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.width, 80.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.height, 130.0f);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown, { 80.0f, 100.0f })).pointerConsumed);
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
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
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.height, 130.0f);
    target.gestureReference = { 42.0f, 44.0f, 0.0f, 0.0f };
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
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
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
    const u64 firstSequence = gesture.id.sequence;
    m_router.invalidateTarget(target.id);
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(m_router.primaryDown());
    EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove, { 110.0f, 120.0f })).pointerConsumed);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 110.0f, 120.0f })).pointerConsumed);
    target.declarationGeneration = 2u;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 2u, gesture));
    complete();
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 2u, gesture));
    EXPECT_GT(gesture.id.sequence, firstSequence);
    EXPECT_EQ(gesture.id.declarationGeneration, 2u);
}

TEST_F(UiPointerGestureTests, NewLifetimeDisabledAndNonGestureDeclarationsDiscardOldRecords){
    HitTarget target = GestureTarget();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    complete();
    target.declarationGeneration = 2u;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    PointerGesture gesture;
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 2u, gesture));
    complete();
    target.pointerGesture = false;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 3u));
    target.pointerGesture = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 4u));
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 2u, gesture));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    target.enabled = false;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 5u));
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp)).pointerConsumed);
    target.enabled = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 6u));
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 2u, gesture));
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
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
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
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
    const u64 firstSequence = gesture.id.sequence;
    complete();
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    const InputRoutingResult lost = send({ InputEventType::FocusLost, {} });
    EXPECT_TRUE(lost.pointerConsumed);
    EXPECT_TRUE(lost.keyboardConsumed);
    EXPECT_FALSE(lost.capture.valid());
    EXPECT_FALSE(lost.focus.valid());
    EXPECT_FALSE(m_router.primaryDown());
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u, gesture));
    complete();
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_GT(gesture.id.sequence, firstSequence);
    m_router.reset();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    complete();
    PointerGesture afterReset;
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, afterReset));
    EXPECT_GT(afterReset.id.sequence, gesture.id.sequence);
}

TEST_F(UiPointerGestureTests, ActiveCaptureLossPreservesCompletedGesturesActionsAndKeyboardOwnership){
    HitTarget target = GestureTarget();
    target.focusable = true;
    target.activatable = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    complete({ 40.0f, 40.0f });
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = InputKey::Space }).keyboardConsumed);
    ASSERT_EQ(m_router.actions().size(), 2u);
    const InputRoutingResult lost = send({ InputEventType::PointerCaptureLost, {} });
    EXPECT_TRUE(lost.pointerConsumed);
    EXPECT_FALSE(lost.capture.valid());
    EXPECT_FALSE(lost.hover.valid());
    EXPECT_EQ(lost.focus, target.id);
    EXPECT_TRUE(lost.wantsKeyboard);
    EXPECT_TRUE(m_router.ownsKey(InputKey::Space));
    EXPECT_FALSE(m_router.primaryDown());
    PointerGesture gesture;
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(gesture.position.x, 40.0f);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = InputKey::Space }).keyboardConsumed);
    EXPECT_FALSE(m_router.ownsKey(InputKey::Space));
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
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
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
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_FLOAT_EQ(gesture.position.x, static_cast<f32>(s_InputMaxPointerGestures * 4u - 1u));
    EXPECT_EQ(gesture.state, PointerGestureState::Active);
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 110.0f, 120.0f })).pointerConsumed);
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
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
        ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
        EXPECT_GT(gesture.id.sequence, previousSequence);
        previousSequence = gesture.id.sequence;
        EXPECT_EQ(gesture.state, PointerGestureState::Completed);
        EXPECT_FLOAT_EQ(gesture.position.x, 15.0f);
    }
    EXPECT_FALSE(m_router.consumePointerGesture(target.id, 1u, gesture));
    complete();
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
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
    gesture.id.sequence = 99u;
    EXPECT_FALSE(m_router.consumePointerGesture(targets[0].id, 1u, gesture));
    EXPECT_FALSE(m_router.consumePointerGesture(targets[1].id, 0u, gesture));
    EXPECT_FALSE(m_router.consumePointerGesture(targets[1].id, 2u, gesture));
    EXPECT_EQ(gesture.id.sequence, 99u);
    ASSERT_TRUE(m_router.consumePointerGesture(targets[1].id, 1u, gesture));
    EXPECT_EQ(gesture.id.target, targets[1].id);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown, { 25.0f, 25.0f })).pointerConsumed);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 30.0f, 30.0f })).pointerConsumed);
    ASSERT_TRUE(m_router.consumePointerGesture(targets[0].id, 1u, gesture));
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
    ASSERT_TRUE(m_router.consumePointerGesture(target.id, 1u, gesture));
    EXPECT_EQ(gesture.id.layoutGeneration, 7u);
    EXPECT_FLOAT_EQ(gesture.position.x, 90.0f);
}

TEST_F(UiPointerGestureTests, ContextRejectsStaleForeignAndWrongKindDeclarationsWithoutConsuming){
    const WidgetState original = prepare(1u);
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(sendToContext(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    EXPECT_TRUE(sendToContext(PointerEvent(InputEventType::PrimaryUp, { 90.0f, 100.0f })).pointerConsumed);
    PointerGesture gesture;
    EXPECT_FALSE(m_context.takePointerGesture(original, true, gesture));
    ASSERT_TRUE(m_context.beginFrame(2u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    const WidgetState* current = m_context.declare("title", WidgetKind::Panel);
    ASSERT_NE(current, nullptr);
    EXPECT_FALSE(m_context.takePointerGesture(original, true, gesture));
    WidgetState invalid = *current;
    ++invalid.root.generation;
    EXPECT_FALSE(m_context.takePointerGesture(invalid, true, gesture));
    invalid = *current;
    ++invalid.declarationGeneration;
    EXPECT_FALSE(m_context.takePointerGesture(invalid, true, gesture));
    invalid = *current;
    invalid.kind = WidgetKind::Button;
    EXPECT_FALSE(m_context.takePointerGesture(invalid, true, gesture));
    ASSERT_TRUE(m_context.takePointerGesture(*current, true, gesture));
    EXPECT_EQ(gesture.id.target, current->id);
    EXPECT_EQ(gesture.id.declarationGeneration, current->declarationGeneration);
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_FALSE(m_context.takePointerGesture(*current, true, gesture));
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
    ASSERT_TRUE(m_context.takePointerGesture(*state, true, gesture));
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
    ASSERT_TRUE(m_context.takePointerGesture(*state, true, gesture));
    EXPECT_EQ(gesture.id.sequence, sequence);
    EXPECT_EQ(gesture.id.layoutGeneration, 1u);
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.x, 10.0f);
    EXPECT_FLOAT_EQ(gesture.position.x, 150.0f);
    EXPECT_FALSE(m_context.takePointerGesture(*state, true, gesture));
}

TEST_F(UiPointerGestureTests, ContextDisablingTheCurrentDeclarationCancelsItsHeldGesture){
    const WidgetState original = prepare(1u);
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(sendToContext(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    ASSERT_TRUE(m_context.beginFrame(2u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    const WidgetState* state = m_context.declare("title", WidgetKind::Panel);
    ASSERT_NE(state, nullptr);
    PointerGesture gesture;
    EXPECT_FALSE(m_context.takePointerGesture(*state, false, gesture));
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_TRUE(m_context.input().primaryDown());
    EXPECT_TRUE(sendToContext(PointerEvent(InputEventType::PrimaryUp)).pointerConsumed);
    EXPECT_FALSE(m_context.takePointerGesture(*state, true, gesture));
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
    PointerGesture gesture;
    EXPECT_FALSE(m_context.takePointerGesture(*current, true, gesture));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

