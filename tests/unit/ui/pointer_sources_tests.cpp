// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/input/router.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_pointer_sources_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

static constexpr InputSource s_First{ 11u, 1u };
static constexpr InputSource s_Second{ 11u, 2u };
static constexpr InputSource s_Cursor{ 12u, 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static HitTarget Target(const u64 id, const f32 x){
    HitTarget target;
    target.id = { id };
    target.declarationGeneration = 7u;
    target.rectangle = { x, 10.0f, 80.0f, 60.0f };
    target.clip = { 0.0f, 0.0f, 300.0f, 200.0f };
    target.focusable = true;
    target.activatable = true;
    target.pointerGesture = true;
    target.contextMenu = true;
    target.value = id * 17u;
    return target;
}

[[nodiscard]] static PointerInput Pointer(const PointerPhase::Enum phase, const InputSource& source, const Point& position = { 20.0f, 20.0f }, const PointerIntent::Enum intent = PointerIntent::Activate, const bool hoverCapable = false){
    PointerInput input;
    input.phase = phase;
    input.source = source;
    input.position = position;
    input.intent = intent;
    input.hoverCapable = hoverCapable;
    return input;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiPointerSourceTests : public testing::Test{
public:
    UiPointerSourceTests()
        : m_arena(Name("tests/ui/pointer_sources"))
        , m_router(m_arena)
        , m_targets{ Target(1u, 10.0f), Target(2u, 110.0f) }
    {}


protected:
    virtual void SetUp()override{
        ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    }

    InputRoutingResult send(const PointerInput& input){
        EXPECT_TRUE(m_router.queuePointer(input));
        return m_router.process();
    }

    void cancelCommand(){
        InputEvent command;
        command.type = InputEventType::CommandDown;
        command.source = { 21u, 1u };
        command.command = InputCommand::Cancel;
        ASSERT_TRUE(m_router.queue(command));
        EXPECT_TRUE(m_router.process().commandConsumed);
        command.type = InputEventType::CommandUp;
        command.command = InputCommand::None;
        ASSERT_TRUE(m_router.queue(command));
        EXPECT_TRUE(m_router.process().commandConsumed);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    InputRouter m_router;
    Array<HitTarget, 2u> m_targets;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPointerSourceTests, SourceMismatchCannotMoveReleaseOrCancelAnActiveGesture){
    ASSERT_TRUE(send(Pointer(PointerPhase::Press, s_First)).pointerConsumed);
    const auto initial = m_router.consumePointerGesture(m_targets[0u].id, 7u);
    ASSERT_TRUE(initial);
    EXPECT_FALSE(send(Pointer(PointerPhase::Move, s_Second, { 140.0f, 30.0f })).pointerRouted);
    EXPECT_FALSE(send(Pointer(PointerPhase::Release, s_Second)).pointerRouted);
    EXPECT_FALSE(send(Pointer(PointerPhase::Cancel, s_Second)).pointerRouted);
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    EXPECT_EQ(m_router.activePointerSource(), s_First);
    EXPECT_TRUE(m_router.pressed(m_targets[0u].id));
    EXPECT_FALSE(m_router.consumePointerGesture(m_targets[0u].id, 7u));
    EXPECT_TRUE(m_router.actions().empty());
    ASSERT_TRUE(send(Pointer(PointerPhase::Move, s_First, { 30.0f, 25.0f })).pointerRouted);
    const auto moved = m_router.consumePointerGesture(m_targets[0u].id, 7u);
    ASSERT_TRUE(moved);
    EXPECT_EQ(moved->id.sequence, initial->id.sequence);
    EXPECT_EQ(moved->value, 17u);
    EXPECT_FLOAT_EQ(moved->position.x, 30.0f);
    EXPECT_FLOAT_EQ(moved->position.y, 25.0f);
    ASSERT_TRUE(send(Pointer(PointerPhase::Release, s_First, { 30.0f, 25.0f })).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(m_targets[0u].id));
    EXPECT_FALSE(m_router.consumeActivation(m_targets[1u].id));
}

TEST_F(UiPointerSourceTests, CompetingUiPressStaysInertUntilItsOwnReleaseAfterTheOwnerEnds){
    ASSERT_TRUE(send(Pointer(PointerPhase::Press, s_First)).pointerConsumed);
    const Point second{ 130.0f, 20.0f };
    EXPECT_TRUE(send(Pointer(PointerPhase::Press, s_Second, second)).pointerConsumed);
    EXPECT_TRUE(m_router.ownsPointerSource(s_Second));
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    EXPECT_FALSE(m_router.pressed(m_targets[1u].id));
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_First)).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(m_targets[0u].id));
    EXPECT_FALSE(send(Pointer(PointerPhase::Move, s_Second, second)).pointerRouted);
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_Second, second)).pointerConsumed);
    EXPECT_FALSE(m_router.ownsPointerSource(s_Second));
    EXPECT_FALSE(m_router.consumeActivation(m_targets[1u].id));
    EXPECT_TRUE(send(Pointer(PointerPhase::Press, s_Second, second)).pointerConsumed);
    EXPECT_EQ(m_router.capture(), m_targets[1u].id);
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_Second, second)).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(m_targets[1u].id));
}

TEST_F(UiPointerSourceTests, SceneAdmissionCannotTransferToAChangedUiLayoutDuringTheHold){
    const Point scene{ 250.0f, 20.0f };
    EXPECT_FALSE(send(Pointer(PointerPhase::Press, s_First, scene)).pointerConsumed);
    EXPECT_FALSE(m_router.ownsPointerSource(s_First));
    m_targets[0u].rectangle.x = 230.0f;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_FALSE(send(Pointer(PointerPhase::Move, s_First, scene)).pointerConsumed);
    EXPECT_FALSE(send(Pointer(PointerPhase::Release, s_First, scene)).pointerConsumed);
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(send(Pointer(PointerPhase::Press, s_First, scene)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_First, scene)).pointerConsumed);
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_EQ(m_router.actions().front().id.target, m_targets[0u].id);
    EXPECT_EQ(m_router.actions().front().id.layoutGeneration, 2u);
}

TEST_F(UiPointerSourceTests, TouchEscapeCancellationRetainsItsReleaseOwnerWithoutSubmitting){
    EXPECT_TRUE(send(Pointer(PointerPhase::Press, s_First)).pointerConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(m_router.pressed(m_targets[0u].id));
    EXPECT_FALSE(m_router.pointerKnown());
    EXPECT_FALSE(m_router.hover().valid());
    cancelCommand();
    EXPECT_TRUE(m_router.ownsPointerSource(s_First));
    EXPECT_FALSE(m_router.pressed(m_targets[0u].id));
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_First)).pointerConsumed);
    EXPECT_FALSE(m_router.ownsPointerSource(s_First));
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(m_router.pointerKnown());
    EXPECT_FALSE(m_router.hover().valid());
    EXPECT_TRUE(send(Pointer(PointerPhase::Press, s_First)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_First)).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(m_targets[0u].id));
}

TEST_F(UiPointerSourceTests, RecreatedCapturedOwnerCannotReviveTheOriginalContactRelease){
    EXPECT_TRUE(send(Pointer(PointerPhase::Press, s_First)).pointerConsumed);
    m_router.invalidateTarget(m_targets[0u].id);
    ++m_targets[0u].declarationGeneration;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    --m_targets[0u].declarationGeneration;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 3u));
    EXPECT_TRUE(m_router.ownsPointerSource(s_First));
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_First)).pointerConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(m_router.consumePointerGesture(m_targets[0u].id, 7u));
    EXPECT_TRUE(send(Pointer(PointerPhase::Press, s_First)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_First)).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(m_targets[0u].id));
}

TEST_F(UiPointerSourceTests, ExactSourceCancellationPreservesOtherHoldsAndCompletedSnapshots){
    EXPECT_TRUE(send(Pointer(PointerPhase::Press, s_First)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_First)).pointerConsumed);
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_TRUE(send(Pointer(PointerPhase::Press, s_First)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(PointerPhase::Press, s_Second, { 130.0f, 20.0f })).pointerConsumed);
    send(Pointer(PointerPhase::Cancel, s_Second));
    EXPECT_FALSE(m_router.ownsPointerSource(s_Second));
    EXPECT_TRUE(m_router.ownsPointerSource(s_First));
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    send(Pointer(PointerPhase::Cancel, s_First));
    EXPECT_FALSE(m_router.ownsPointerSource(s_First));
    EXPECT_FALSE(m_router.primaryDown());
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(m_router.consumeActivation(m_targets[0u].id));
    EXPECT_FALSE(m_router.consumeActivation(m_targets[0u].id));
    const auto completed = m_router.consumePointerGesture(m_targets[0u].id, 7u);
    ASSERT_TRUE(completed);
    EXPECT_EQ(completed->state, PointerGestureState::Completed);
    EXPECT_EQ(completed->value, 17u);
    EXPECT_FALSE(m_router.consumePointerGesture(m_targets[0u].id, 7u));
    EXPECT_TRUE(send(Pointer(PointerPhase::Press, s_First)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_First)).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(m_targets[0u].id));
}

TEST_F(UiPointerSourceTests, TouchMovementAndSubmissionPreserveTheHoverCapableCursorGeometry){
    const Point cursor{ 140.0f, 30.0f };
    send(Pointer(PointerPhase::Move, s_Cursor, cursor, PointerIntent::Activate, true));
    EXPECT_TRUE(m_router.pointerKnown());
    ASSERT_EQ(m_router.hover(), m_targets[1u].id);
    EXPECT_TRUE(send(Pointer(PointerPhase::Press, s_First)).pointerConsumed);
    EXPECT_TRUE(m_router.pressed(m_targets[0u].id));
    EXPECT_EQ(m_router.hover(), m_targets[1u].id);
    send(Pointer(PointerPhase::Move, s_First, { 30.0f, 25.0f }));
    EXPECT_EQ(m_router.hover(), m_targets[1u].id);
    EXPECT_FLOAT_EQ(m_router.pointerPosition().x, cursor.x);
    EXPECT_FLOAT_EQ(m_router.pointerPosition().y, cursor.y);
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_First, { 30.0f, 25.0f })).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(m_targets[0u].id));
    EXPECT_TRUE(m_router.pointerKnown());
    EXPECT_EQ(m_router.hover(), m_targets[1u].id);
    EXPECT_FLOAT_EQ(m_router.pointerPosition().x, cursor.x);
    EXPECT_FLOAT_EQ(m_router.pointerPosition().y, cursor.y);
}

TEST_F(UiPointerSourceTests, ContextIntentCannotBeReleasedByAnotherRoleOrSource){
    EXPECT_TRUE(send(Pointer(PointerPhase::Press, s_First, { 20.0f, 20.0f }, PointerIntent::ContextMenu)).pointerConsumed);
    EXPECT_FALSE(m_router.consumeContextMenu(m_targets[0u].id, 7u));
    send(Pointer(PointerPhase::Release, s_First));
    send(Pointer(PointerPhase::Release, s_Second, { 20.0f, 20.0f }, PointerIntent::ContextMenu));
    EXPECT_TRUE(m_router.ownsPointerSource(s_First, PointerIntent::ContextMenu));
    EXPECT_FALSE(m_router.consumeContextMenu(m_targets[0u].id, 7u));
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_First, { 30.0f, 25.0f }, PointerIntent::ContextMenu)).pointerConsumed);
    const auto action = m_router.consumeContextMenu(m_targets[0u].id, 7u);
    ASSERT_TRUE(action);
    EXPECT_FLOAT_EQ(action->position.x, 20.0f);
    EXPECT_FLOAT_EQ(action->position.y, 20.0f);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(m_router.pointerKnown());
}

TEST_F(UiPointerSourceTests, QueuedTouchCapabilityChangesCannotPublishCursorHover){
    ASSERT_TRUE(m_router.queuePointer(Pointer(PointerPhase::Press, s_First)));
    const auto move = m_router.queuePointer(Pointer(PointerPhase::Move, s_First, { 30.0f, 25.0f }, PointerIntent::Activate, true));
    ASSERT_TRUE(move);
    EXPECT_FALSE(move->hoverCapable);
    const auto release = m_router.queuePointer(Pointer(PointerPhase::Release, s_First, { 30.0f, 25.0f }, PointerIntent::Activate, true));
    ASSERT_TRUE(release);
    EXPECT_FALSE(release->hoverCapable);
    const auto routed = m_router.process();
    EXPECT_TRUE(routed.pointerConsumed);
    EXPECT_TRUE(routed.pointerRouted);
    EXPECT_FALSE(m_router.pointerKnown());
    EXPECT_FALSE(m_router.hover().valid());
    EXPECT_TRUE(m_router.consumeActivation(m_targets[0u].id));
    EXPECT_FALSE(m_router.consumeActivation(m_targets[0u].id));
    const auto gesture = m_router.consumePointerGesture(m_targets[0u].id, 7u);
    ASSERT_TRUE(gesture);
    EXPECT_EQ(gesture->state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(gesture->origin.x, 20.0f);
    EXPECT_FLOAT_EQ(gesture->position.x, 30.0f);
}

TEST_F(UiPointerSourceTests, DuplicatePressCannotRerouteOrReplaceTheAcceptedGesture){
    ASSERT_TRUE(send(Pointer(PointerPhase::Press, s_First)).pointerConsumed);
    const auto initial = m_router.consumePointerGesture(m_targets[0u].id, 7u);
    ASSERT_TRUE(initial);
    PointerInput duplicate = Pointer(PointerPhase::Press, s_First, { 130.0f, 20.0f });
    duplicate.timestampMs = 200u;
    const auto normalized = m_router.queuePointer(duplicate);
    ASSERT_TRUE(normalized);
    EXPECT_TRUE(normalized->repeat);
    EXPECT_FALSE(m_router.process().pointerRouted);
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    EXPECT_TRUE(m_router.pressed(m_targets[0u].id));
    EXPECT_FALSE(m_router.consumePointerGesture(m_targets[0u].id, 7u));
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_First)).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(m_targets[0u].id));
    EXPECT_FALSE(m_router.consumeActivation(m_targets[0u].id));
    EXPECT_FALSE(m_router.consumeActivation(m_targets[1u].id));
    const auto completed = m_router.consumePointerGesture(m_targets[0u].id, 7u);
    ASSERT_TRUE(completed);
    EXPECT_EQ(completed->id.sequence, initial->id.sequence);
    EXPECT_EQ(completed->state, PointerGestureState::Completed);
    EXPECT_FLOAT_EQ(completed->origin.x, 20.0f);
    EXPECT_FLOAT_EQ(completed->position.x, 20.0f);
}

TEST_F(UiPointerSourceTests, QueuedTerminalDiscardedByResetRetiresTheContactWithoutSubmitting){
    for(const PointerPhase::Enum terminal : { PointerPhase::Release, PointerPhase::Cancel }){
        ASSERT_TRUE(send(Pointer(PointerPhase::Press, s_First)).pointerConsumed);
        ASSERT_TRUE(m_router.queuePointer(Pointer(terminal, s_First)));
        EXPECT_TRUE(m_router.ownsPointerSource(s_First));
        m_router.reset();
        EXPECT_FALSE(m_router.ownsPointerSource(s_First));
        EXPECT_FALSE(m_router.primaryDown());
        EXPECT_FALSE(m_router.activePointerSource().valid());
        EXPECT_TRUE(m_router.actions().empty());
        ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
        const auto fresh = m_router.queuePointer(Pointer(PointerPhase::Press, s_First));
        ASSERT_TRUE(fresh);
        EXPECT_FALSE(fresh->repeat);
        EXPECT_TRUE(m_router.process().pointerRouted);
        EXPECT_TRUE(m_router.pressed(m_targets[0u].id));
        EXPECT_TRUE(m_router.actions().empty());
        EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_First)).pointerConsumed);
        EXPECT_TRUE(m_router.consumeActivation(m_targets[0u].id));
        EXPECT_FALSE(m_router.consumeActivation(m_targets[0u].id));
    }
}

TEST_F(UiPointerSourceTests, QueuedReleaseBeforeResetReclaimsAFullSourceSlotAndRejectsOverflowAtomically){
    for(usize index = 0u; index < s_InputMaxSources; ++index)
        ASSERT_TRUE(send(Pointer(PointerPhase::Press, { 11u, index + 1u })).pointerConsumed);
    ASSERT_TRUE(m_router.queuePointer(Pointer(PointerPhase::Release, s_First)));
    m_router.reset();
    EXPECT_FALSE(m_router.ownsPointerSource(s_First));
    EXPECT_TRUE(m_router.ownsPointerSource(s_Second));
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    const InputSource replacement{ 11u, s_InputMaxSources + 1u };
    const InputSource overflow{ 11u, s_InputMaxSources + 2u };
    ASSERT_TRUE(m_router.queuePointer(Pointer(PointerPhase::Press, replacement)));
    EXPECT_FALSE(m_router.queuePointer(Pointer(PointerPhase::Press, overflow)));
    EXPECT_TRUE(m_router.process().pointerRouted);
    EXPECT_EQ(m_router.activePointerSource(), replacement);
    EXPECT_TRUE(m_router.ownsPointerSource(replacement));
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, replacement)).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(m_targets[0u].id));
    const auto retried = m_router.queuePointer(Pointer(PointerPhase::Press, overflow));
    ASSERT_TRUE(retried);
    EXPECT_FALSE(retried->repeat);
    EXPECT_TRUE(m_router.process().pointerRouted);
    EXPECT_EQ(m_router.activePointerSource(), overflow);
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, overflow)).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(m_targets[0u].id));
    EXPECT_FALSE(m_router.consumeActivation(m_targets[0u].id));
}

TEST_F(UiPointerSourceTests, DiscardedQueuedPressCannotAcquireUiOwnershipOnItsRepeatAfterReset){
    ASSERT_TRUE(m_router.queuePointer(Pointer(PointerPhase::Press, s_First)));
    m_router.reset();
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    const auto repeated = m_router.queuePointer(Pointer(PointerPhase::Press, s_First));
    ASSERT_TRUE(repeated);
    EXPECT_TRUE(repeated->repeat);
    const auto routed = m_router.process();
    EXPECT_FALSE(routed.pointerConsumed);
    EXPECT_FALSE(routed.pointerRouted);
    EXPECT_FALSE(m_router.ownsPointerSource(s_First));
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_FALSE(send(Pointer(PointerPhase::Move, s_First, { 30.0f, 25.0f })).pointerConsumed);
    EXPECT_FALSE(send(Pointer(PointerPhase::Release, s_First, { 30.0f, 25.0f })).pointerConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(send(Pointer(PointerPhase::Press, s_First)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_First)).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(m_targets[0u].id));
}

TEST_F(UiPointerSourceTests, CancelConsumesARetainedUiContactWithoutClaimingAnIdleCursorHover){
    ASSERT_TRUE(send(Pointer(PointerPhase::Press, s_First)).pointerConsumed);
    ASSERT_TRUE(send(Pointer(PointerPhase::Press, s_Second)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_First)).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(m_targets[0u].id));
    m_router.clearFocus();
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(m_router.ownsPointerSource(s_Second));
    cancelCommand();
    EXPECT_TRUE(m_router.ownsPointerSource(s_Second));
    EXPECT_TRUE(send(Pointer(PointerPhase::Release, s_Second)).pointerConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    send(Pointer(PointerPhase::Move, s_Cursor, { 20.0f, 20.0f }, PointerIntent::Activate, true));
    ASSERT_EQ(m_router.hover(), m_targets[0u].id);
    InputEvent cancel;
    cancel.type = InputEventType::CommandDown;
    cancel.source = { 21u, 1u };
    cancel.command = InputCommand::Cancel;
    ASSERT_TRUE(m_router.queue(cancel));
    EXPECT_FALSE(m_router.process().commandConsumed);
    cancel.type = InputEventType::CommandUp;
    cancel.command = InputCommand::None;
    ASSERT_TRUE(m_router.queue(cancel));
    EXPECT_FALSE(m_router.process().commandConsumed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

