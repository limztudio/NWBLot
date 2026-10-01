// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/input/router.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_control_input_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

HitTarget Host(const u64 id = 1u, const f32 x = 0.0f){
    HitTarget target;
    target.id = { id };
    target.declarationGeneration = 7u;
    target.rectangle = { x, 0.0f, 100.0f, 100.0f };
    target.clip = { 0.0f, 0.0f, 300.0f, 200.0f };
    target.focusable = true;
    target.control = { 11u, 21u, 31u };
    target.navigable = true;
    target.scrollable = true;
    target.scrollStep = 72.0;
    target.pageRows = 4u;
    target.gestureMaximum = 2399900.0;
    return target;
}

HitTarget Part(const HitTarget& host, const u64 id, const Rect& rectangle, const u64 value = 0u){
    HitTarget target;
    target.id = { id };
    target.rectangle = rectangle;
    target.clip = host.clip;
    target.declarationGeneration = host.declarationGeneration;
    target.paintOrder = 1u;
    target.control = host.control;
    target.owner = host.id;
    target.ownerDeclarationGeneration = host.declarationGeneration;
    target.value = value;
    target.activatable = true;
    return target;
}

InputEvent Pointer(const InputEventType::Enum type, const Point& position = { 10.0f, 10.0f }){
    InputEvent event;
    event.type = type;
    event.position = position;
    return event;
}

InputEvent Key(const InputEventType::Enum type, const InputKey::Enum key, const bool repeat = false){
    InputEvent event;
    event.type = type;
    event.key = key;
    event.repeat = repeat;
    return event;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiControlInputTests : public testing::Test{
public:
    UiControlInputTests()
        : m_arena(Name("tests/ui/control_input"))
        , m_router(m_arena)
    {
        m_targets[0u] = Host();
        m_targets[1u] = Part(m_targets[0u], 2u, { 0.0f, 0.0f, 80.0f, 20.0f }, 100000u);
        m_targets[2u] = Part(m_targets[0u], 3u, { 90.0f, 20.0f, 10.0f, 20.0f });
        m_targets[2u].activatable = false;
        m_targets[2u].pointerGesture = true;
        m_targets[2u].gestureReference = { 90.0f, 0.0f, 10.0f, 100.0f };
        m_targets[2u].gestureMaximum = m_targets[0u].gestureMaximum;
    }


protected:
    [[nodiscard]] InputRoutingResult send(const InputEvent& event){
        EXPECT_TRUE(m_router.queue(event));
        return m_router.process();
    }

    [[nodiscard]] InputRoutingResult wheel(const f64 delta = -1.0, const Point& position = { 10.0f, 10.0f }){
        InputEvent event = Pointer(InputEventType::PointerWheel, position);
        event.scrollY = delta;
        return send(event);
    }

    void click(const Point& position = { 10.0f, 10.0f }){
        EXPECT_TRUE(send(Pointer(InputEventType::PrimaryDown, position)).pointerConsumed);
        EXPECT_TRUE(send(Pointer(InputEventType::PrimaryUp, position)).pointerConsumed);
    }

    void focus(){
        EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Tab)).keyboardConsumed);
        EXPECT_TRUE(send(Key(InputEventType::KeyUp, InputKey::Tab)).keyboardConsumed);
    }

    void revise(){
        for(auto& target : m_targets)
            ++target.control.contentRevision;
    }

    [[nodiscard]] bool take(ControlAction& action){
        return m_router.consumeControlAction(m_targets[0u].id, m_targets[0u].declarationGeneration, m_targets[0u].control, action);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    InputRouter m_router;
    Array<HitTarget, 3u> m_targets;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiControlInputTests, EmptyAndCompleteTokensAreDistinctFromPartialLifetimes){
    const ControlToken empty;
    EXPECT_TRUE(empty.empty());
    EXPECT_FALSE(empty.valid());
    const ControlToken valid{ 1u, 2u, 3u };
    EXPECT_TRUE(valid.valid());
    EXPECT_FALSE(valid.empty());
    EXPECT_EQ(valid, (ControlToken{ 1u, 2u, 3u }));
    EXPECT_NE(valid, (ControlToken{ 4u, 2u, 3u }));
    EXPECT_NE(valid, (ControlToken{ 1u, 4u, 3u }));
    EXPECT_NE(valid, (ControlToken{ 1u, 2u, 4u }));
    EXPECT_FALSE((ControlToken{ 0u, 2u, 3u }).valid());
    EXPECT_FALSE((ControlToken{ 0u, 2u, 3u }).empty());
}

TEST_F(UiControlInputTests, WheelCopiesAcceptedStepWithoutChangingFocusOrCreatingActivation){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 5u));
    EXPECT_TRUE(wheel(-0.25).pointerConsumed);
    EXPECT_FALSE(m_router.focus().valid());
    m_targets[0u].scrollStep = 120.0;
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::Wheel);
    EXPECT_DOUBLE_EQ(action.delta, -0.25);
    EXPECT_DOUBLE_EQ(action.step, 72.0);
    EXPECT_EQ(action.pageRows, 4u);
    EXPECT_EQ(action.id.layoutGeneration, 5u);
    EXPECT_TRUE(m_router.actions().empty());
}

TEST_F(UiControlInputTests, EmptyViewportSpaceAndScrollbarRouteWheelToTheirHost){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(wheel(-1.0, { 50.0f, 80.0f }).pointerConsumed);
    EXPECT_TRUE(wheel(1.0, { 95.0f, 25.0f }).pointerConsumed);
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.source, m_targets[0u].id);
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.source, m_targets[2u].id);
}

TEST_F(UiControlInputTests, OverlappingUnrelatedControlBlocksUnderlyingWheelRouting){
    HitTarget cover = Host(4u);
    cover.paintOrder = 10u;
    cover.control = {};
    cover.navigable = false;
    cover.scrollable = false;
    const Array<HitTarget, 4u> targets{ m_targets[0u], m_targets[1u], m_targets[2u], cover };
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u));
    EXPECT_TRUE(wheel().pointerConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
}

TEST_F(UiControlInputTests, HorizontalOnlyWheelIsConsumedWithoutVerticalMutation){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    InputEvent event = Pointer(InputEventType::PointerWheel);
    event.scrollX = 2.0;
    EXPECT_TRUE(send(event).pointerConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    event.position = { 200.0f, 180.0f };
    EXPECT_FALSE(send(event).pointerConsumed);
}

TEST_F(UiControlInputTests, SceneHeldPrimarySequenceCannotStartScrollingWhenPointerCrossesList){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_FALSE(send(Pointer(InputEventType::PrimaryDown, { 200.0f, 180.0f })).pointerConsumed);
    EXPECT_FALSE(wheel().pointerConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_FALSE(send(Pointer(InputEventType::PrimaryUp)).pointerConsumed);
    EXPECT_TRUE(wheel().pointerConsumed);
}

TEST_F(UiControlInputTests, NavigationCopiesAcceptedPageSizeAndSubmissionDoesNotRepeat){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    m_targets[0u].pageRows = 30u;
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::PageDown)).keyboardConsumed);
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::PageDown, true)).keyboardConsumed);
    EXPECT_TRUE(send(Key(InputEventType::KeyUp, InputKey::PageDown)).keyboardConsumed);
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Enter)).keyboardConsumed);
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Enter, true)).keyboardConsumed);
    EXPECT_TRUE(send(Key(InputEventType::KeyUp, InputKey::Enter)).keyboardConsumed);
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::PageDown);
    EXPECT_EQ(action.pageRows, 4u);
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::PageDown);
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::Submit);
    EXPECT_FALSE(take(action));
}

TEST_F(UiControlInputTests, HeldNavigationCannotRetargetAnotherFocusedList){
    HitTarget second = Host(4u, 140.0f);
    const Array<HitTarget, 2u> targets{ m_targets[0u], second };
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u));
    focus();
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down)).keyboardConsumed);
    ControlAction action;
    ASSERT_TRUE(take(action));
    focus();
    EXPECT_EQ(m_router.focus(), second.id);
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down, true)).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(send(Key(InputEventType::KeyUp, InputKey::Down)).keyboardConsumed);
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down)).keyboardConsumed);
    ASSERT_TRUE(m_router.consumeControlAction(second.id, second.declarationGeneration, second.control, action));
    EXPECT_EQ(action.kind, ControlActionKind::Down);
}

TEST_F(UiControlInputTests, ReplacedContentFencesCaptureActionsAndFocusUntilAcceptance){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    click();
    EXPECT_TRUE(wheel().pointerConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::PrimaryDown)).pointerConsumed);
    revise();
    m_router.fenceControl(m_targets[0u].id, m_targets[0u].declarationGeneration, m_targets[0u].control);
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(send(Pointer(InputEventType::PrimaryUp)).pointerConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    click();
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.control.contentRevision, 32u);
}

TEST_F(UiControlInputTests, SameControlFencePreservesAcceptedInteraction){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    click();
    m_router.fenceControl(m_targets[0u].id, m_targets[0u].declarationGeneration, m_targets[0u].control);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    ControlAction action;
    EXPECT_TRUE(take(action));
}

TEST_F(UiControlInputTests, CommitOfNewEpochRejectsOldPressWithoutLeakingItsRelease){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::PrimaryDown)).pointerConsumed);
    revise();
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(send(Pointer(InputEventType::PrimaryUp)).pointerConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
}

TEST_F(UiControlInputTests, ReplacedRowDeclarationRejectsItsQueuedClickWhileHostWheelSurvives){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    click();
    EXPECT_TRUE(wheel().pointerConsumed);
    ++m_targets[1u].declarationGeneration;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::Wheel);
    EXPECT_FALSE(take(action));
    click();
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::Activate);
    EXPECT_EQ(action.sourceDeclarationGeneration, m_targets[1u].declarationGeneration);
    EXPECT_FALSE(take(action));
}

TEST_F(UiControlInputTests, RemovedRowRejectsItsQueuedStableValueWhileHostWheelSurvives){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    click();
    EXPECT_TRUE(wheel().pointerConsumed);
    m_router.invalidateTarget(m_targets[1u].id);
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_EQ(action.kind, ControlActionKind::Wheel);
    EXPECT_FALSE(take(action));
    EXPECT_EQ(m_router.targets().size(), 2u);
}

TEST_F(UiControlInputTests, HostRetirementRemovesPartsAndGesturesButPreservesHeldOwnership){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    click();
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down)).keyboardConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::PrimaryDown, { 95.0f, 25.0f })).pointerConsumed);
    m_router.invalidateTarget(m_targets[0u].id);
    EXPECT_TRUE(m_router.targets().empty());
    EXPECT_TRUE(m_router.controlActions().empty());
    PointerGesture gesture;
    EXPECT_FALSE(m_router.consumePointerGesture(m_targets[2u].id, 7u, gesture));
    EXPECT_TRUE(send(Pointer(InputEventType::PrimaryUp, { 95.0f, 25.0f })).pointerConsumed);
    EXPECT_TRUE(send(Key(InputEventType::KeyUp, InputKey::Down)).keyboardConsumed);
}

TEST_F(UiControlInputTests, ThumbGestureRetainsAcceptedTrackAndMaximumAcrossResize){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::PrimaryDown, { 95.0f, 25.0f })).pointerConsumed);
    PointerGesture gesture;
    ASSERT_TRUE(m_router.consumePointerGesture(m_targets[2u].id, 7u, gesture));
    EXPECT_EQ(gesture.control, m_targets[0u].control);
    EXPECT_DOUBLE_EQ(gesture.maximum, 2399900.0);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.height, 100.0f);
    m_targets[2u].gestureMaximum = 1000.0;
    m_targets[2u].gestureReference.height = 150.0f;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_TRUE(send(Pointer(InputEventType::PointerMove, { 95.0f, 50.0f })).pointerConsumed);
    ASSERT_TRUE(m_router.consumePointerGesture(m_targets[2u].id, 7u, gesture));
    EXPECT_DOUBLE_EQ(gesture.maximum, 2399900.0);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.height, 100.0f);
    EXPECT_EQ(gesture.id.layoutGeneration, 1u);
}

TEST_F(UiControlInputTests, ChangedThumbEpochRejectsAQueuedCompletedGesture){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::PrimaryDown, { 95.0f, 25.0f })).pointerConsumed);
    EXPECT_TRUE(send(Pointer(InputEventType::PrimaryUp, { 95.0f, 30.0f })).pointerConsumed);
    revise();
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    PointerGesture gesture;
    EXPECT_FALSE(m_router.consumePointerGesture(m_targets[2u].id, 7u, gesture));
}

TEST_F(UiControlInputTests, FocusHintRestoresReplacedHostOnlyAtMatchingAcceptance){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    revise();
    m_targets[0u].focusOnCommit = true;
    m_router.fenceControl(m_targets[0u].id, 7u, m_targets[0u].control);
    EXPECT_FALSE(m_router.focus().valid());
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u, nullptr, 0u, m_router.focusLossGeneration()));
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
}

TEST_F(UiControlInputTests, FocusHintCannotStealAnotherFocusOrSurviveDelayedFocusLoss){
    HitTarget second = Host(4u, 140.0f);
    ASSERT_TRUE(m_router.commitTargets(&second, 1u, 1u));
    click({ 150.0f, 30.0f });
    m_targets[0u].focusOnCommit = true;
    const Array<HitTarget, 2u> targets{ m_targets[0u], second };
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 2u));
    EXPECT_EQ(m_router.focus(), second.id);
    const u64 preparedFocusLoss = m_router.focusLossGeneration();
    const InputRoutingResult lost = send({ InputEventType::FocusLost, {} });
    EXPECT_TRUE(lost.keyboardConsumed);
    const InputRoutingResult gained = send({ InputEventType::FocusGained, {} });
    EXPECT_FALSE(gained.keyboardConsumed);
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 3u, nullptr, 0u, preparedFocusLoss));
    EXPECT_FALSE(m_router.focus().valid());
}

TEST_F(UiControlInputTests, RepeatedNavigationDoesNotJumpToAcceptedReplacementEpoch){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down)).keyboardConsumed);
    revise();
    m_targets[0u].focusOnCommit = true;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down, true)).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(send(Key(InputEventType::KeyUp, InputKey::Down)).keyboardConsumed);
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down)).keyboardConsumed);
    ControlAction action;
    EXPECT_TRUE(take(action));
}

TEST_F(UiControlInputTests, InvalidOwnerReferencesRejectAtomicPublicationAndPreserveActions){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    click();
    ++m_targets[1u].ownerDeclarationGeneration;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_EQ(m_router.layoutGeneration(), 1u);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    ControlAction action;
    EXPECT_TRUE(take(action));
}

TEST_F(UiControlInputTests, MalformedControlMetadataRejectsPublicationBeforeChangingFocus){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    const HitTarget host = m_targets[0u];
    m_targets[0u].control.contentRevision = 0u;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    m_targets[0u] = host;
    m_targets[0u].scrollStep = Limit<f64>::s_QuietNaN;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    m_targets[0u] = host;
    m_targets[0u].pageRows = 0u;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    m_targets[0u] = host;
    m_targets[0u].gestureMaximum = -1.0;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_EQ(m_router.focus(), host.id);
    EXPECT_EQ(m_router.layoutGeneration(), 1u);
}

TEST_F(UiControlInputTests, MissingDisabledOrFocusablePartOwnerIsRejected){
    EXPECT_FALSE(m_router.commitTargets(&m_targets[1u], 1u, 1u));
    m_targets[0u].enabled = false;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    m_targets[0u].enabled = true;
    m_targets[1u].focusable = true;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    m_targets[1u].focusable = false;
    ++m_targets[1u].control.instanceGeneration;
    EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(m_router.targets().empty());
}

TEST_F(UiControlInputTests, InvalidWheelCoordinatesAndDeltasNeverReachAcceptedActions){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    InputEvent event = Pointer(InputEventType::PointerWheel);
    event.scrollY = Limit<f64>::s_QuietNaN;
    EXPECT_FALSE(m_router.queue(event));
    event.scrollY = -1.0;
    event.scrollX = Limit<f64>::s_Infinity;
    EXPECT_FALSE(m_router.queue(event));
    event.scrollX = 0.0;
    event.position.x = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(m_router.queue(event));
    EXPECT_TRUE(m_router.controlActions().empty());
}

TEST_F(UiControlInputTests, BoundedControlActionsUseOneSharedSequenceAndOverflowOncePerProcess){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    for(usize index = 0u; index < s_InputMaxControlActions; ++index)
        EXPECT_FALSE(wheel().activationOverflow);
    EXPECT_TRUE(wheel().activationOverflow);
    EXPECT_EQ(m_router.controlActions().size(), s_InputMaxControlActions);
    EXPECT_FALSE(m_router.process().activationOverflow);
    u64 previousSequence = 0u;
    ControlAction action;
    for(usize index = 0u; index < s_InputMaxControlActions; ++index){
        ASSERT_TRUE(take(action));
        EXPECT_GT(action.id.sequence, previousSequence);
        previousSequence = action.id.sequence;
    }
    EXPECT_FALSE(take(action));
    EXPECT_FALSE(wheel().activationOverflow);
    ASSERT_TRUE(take(action));
    EXPECT_GT(action.id.sequence, previousSequence);
}

TEST_F(UiControlInputTests, FocusLossCancelsActionsAndResetDoesNotRestartTheirSequence){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(wheel().pointerConsumed);
    const u64 previousSequence = m_router.controlActions()[0u].id.sequence;
    focus();
    EXPECT_TRUE(send({ InputEventType::FocusLost, {} }).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    m_router.reset();
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(wheel().pointerConsumed);
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_GT(action.id.sequence, previousSequence);
}

TEST_F(UiControlInputTests, AcceptedPopupMaskOwnsWheelAndListFocusAboveLowerHosts){
    PopupScope popup;
    popup.token = { { 10u }, 5u, 6u, 7u };
    popup.bounds = { 140.0f, 0.0f, 100.0f, 100.0f };
    popup.viewport = m_targets[0u].clip;
    popup.layer = 1u;
    HitTarget barrier;
    barrier.id = popup.token.widget;
    barrier.declarationGeneration = popup.token.declarationGeneration;
    barrier.rectangle = popup.bounds;
    barrier.clip = popup.viewport;
    barrier.popup = popup.token;
    barrier.layer = popup.layer;
    HitTarget upper = Host(4u, 140.0f);
    upper.popup = popup.token;
    upper.layer = popup.layer;
    upper.paintOrder = 1u;
    const Array<HitTarget, 3u> targets{ m_targets[0u], barrier, upper };
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u, &popup, 1u));
    EXPECT_EQ(m_router.focus(), upper.id);
    EXPECT_TRUE(wheel().pointerConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(wheel(-1.0, { 150.0f, 20.0f }).pointerConsumed);
    ControlAction action;
    ASSERT_TRUE(m_router.consumeControlAction(upper.id, upper.declarationGeneration, upper.control, action));
    EXPECT_EQ(action.popup, popup.token);
    EXPECT_EQ(action.kind, ControlActionKind::Wheel);
}

TEST_F(UiControlInputTests, ClosingPopupBlocksFocusHintAndHeldNavigationCannotReachReopenedScope){
    PopupScope popup;
    popup.token = { { 10u }, 5u, 6u, 7u };
    popup.bounds = { 140.0f, 0.0f, 100.0f, 100.0f };
    popup.viewport = m_targets[0u].clip;
    popup.layer = 1u;
    HitTarget barrier;
    barrier.id = popup.token.widget;
    barrier.declarationGeneration = popup.token.declarationGeneration;
    barrier.rectangle = popup.bounds;
    barrier.clip = popup.viewport;
    barrier.popup = popup.token;
    barrier.layer = popup.layer;
    HitTarget upper = Host(4u, 140.0f);
    upper.popup = popup.token;
    upper.layer = popup.layer;
    upper.paintOrder = 1u;
    m_targets[0u].focusOnCommit = true;
    Array<HitTarget, 3u> targets{ m_targets[0u], barrier, upper };
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u, &popup, 1u));
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down)).keyboardConsumed);
    m_router.closePopup(popup.token);
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 2u, &popup, 1u));
    EXPECT_FALSE(m_router.focus().valid());
    ++popup.token.openGeneration;
    targets[1u].popup = popup.token;
    targets[2u].popup = popup.token;
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 3u, &popup, 1u));
    EXPECT_EQ(m_router.focus(), upper.id);
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down, true)).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(send(Key(InputEventType::KeyUp, InputKey::Down)).keyboardConsumed);
}

TEST_F(UiControlInputTests, LegacyActivationAlsoFencesItsCopiedControlEpoch){
    HitTarget target = Host();
    target.navigable = false;
    target.scrollable = false;
    target.activatable = true;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    click();
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_EQ(m_router.actions()[0u].control, target.control);
    ++target.control.instanceGeneration;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(m_router.consumeActivation(target.id));
}

TEST_F(UiControlInputTests, ControlLegacyAndGestureActionsShareOneMonotonicSequence){
    HitTarget button = Host(4u, 140.0f);
    button.control = {};
    button.navigable = false;
    button.scrollable = false;
    button.activatable = true;
    const Array<HitTarget, 4u> targets{ m_targets[0u], m_targets[1u], m_targets[2u], button };
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u));
    click({ 150.0f, 10.0f });
    ASSERT_EQ(m_router.actions().size(), 1u);
    const u64 legacySequence = m_router.actions()[0u].id.sequence;
    EXPECT_TRUE(wheel().pointerConsumed);
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_GT(action.id.sequence, legacySequence);
    const u64 wheelSequence = action.id.sequence;
    EXPECT_TRUE(send(Pointer(InputEventType::PrimaryDown, { 95.0f, 25.0f })).pointerConsumed);
    PointerGesture gesture;
    ASSERT_TRUE(m_router.consumePointerGesture(m_targets[2u].id, 7u, gesture));
    EXPECT_GT(gesture.id.sequence, wheelSequence);
}

TEST_F(UiControlInputTests, NativeUnfocusedAcceptanceCannotApplyAControlFocusHint){
    const InputRoutingResult lost = send({ InputEventType::FocusLost, {} });
    EXPECT_FALSE(lost.keyboardConsumed);
    m_targets[0u].focusOnCommit = true;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u, nullptr, 0u, m_router.focusLossGeneration()));
    EXPECT_FALSE(m_router.focus().valid());
}

TEST_F(UiControlInputTests, RetiredHeldKeyOwnerCannotReviveWhenItsOriginalTokenReturns){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    focus();
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down)).keyboardConsumed);
    const ControlToken original = m_targets[0u].control;
    revise();
    m_targets[0u].focusOnCommit = true;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    for(auto& target : m_targets)
        target.control = original;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 3u));
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down, true)).keyboardConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(send(Key(InputEventType::KeyUp, InputKey::Down)).keyboardConsumed);
    EXPECT_TRUE(send(Key(InputEventType::KeyDown, InputKey::Down)).keyboardConsumed);
    ControlAction action;
    EXPECT_TRUE(take(action));
}

TEST_F(UiControlInputTests, GestureUpdateSequenceOrdersLatestMotionAfterAnInterveningWheel){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send(Pointer(InputEventType::PrimaryDown, { 95.0f, 25.0f })).pointerConsumed);
    PointerGesture initial;
    ASSERT_TRUE(m_router.consumePointerGesture(m_targets[2u].id, 7u, initial));
    EXPECT_EQ(initial.updateSequence, initial.id.sequence);
    EXPECT_TRUE(wheel(1.0, { 95.0f, 25.0f }).pointerConsumed);
    ControlAction action;
    ASSERT_TRUE(take(action));
    EXPECT_GT(action.id.sequence, initial.updateSequence);
    EXPECT_TRUE(send(Pointer(InputEventType::PointerMove, { 95.0f, 70.0f })).pointerConsumed);
    PointerGesture moved;
    ASSERT_TRUE(m_router.consumePointerGesture(m_targets[2u].id, 7u, moved));
    EXPECT_EQ(moved.id.sequence, initial.id.sequence);
    EXPECT_GT(moved.updateSequence, action.id.sequence);
    EXPECT_TRUE(send(Pointer(InputEventType::PrimaryUp, { 95.0f, 70.0f })).pointerConsumed);
    PointerGesture completed;
    ASSERT_TRUE(m_router.consumePointerGesture(m_targets[2u].id, 7u, completed));
    EXPECT_GT(completed.updateSequence, moved.updateSequence);
    EXPECT_EQ(completed.id.sequence, initial.id.sequence);
    EXPECT_EQ(completed.state, PointerGestureState::Completed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

