// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/input/router.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_input_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

HitTarget Target(const u64 value, const Rect& rectangle = { 10.0f, 10.0f, 20.0f, 20.0f }){
    HitTarget target;
    target.id = { value };
    target.rectangle = rectangle;
    target.clip = { 0.0f, 0.0f, 100.0f, 100.0f };
    target.focusable = true;
    target.activatable = true;
    return target;
}

InputEvent PointerEvent(const InputEventType::Enum type, const Point& position = { 15.0f, 15.0f }){
    return { type, position };
}

InputEvent KeyEvent(const InputEventType::Enum type, const InputKey::Enum key, const bool shift = false, const bool repeat = false){
    return { type, {}, key, shift, repeat };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiInputTests : public testing::Test{
public:
    UiInputTests()
        : m_arena(Name("tests/ui/input"))
        , m_router(m_arena)
    {}


protected:
    InputRoutingResult send(const InputEvent& event){
        EXPECT_TRUE(m_router.queue(event));
        return m_router.process();
    }

    void click(const Point& position = { 15.0f, 15.0f }){
        EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown, position)).pointerConsumed);
        EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, position)).pointerConsumed);
    }

    void tab(const bool reverse = false){
        EXPECT_TRUE(send(KeyEvent(InputEventType::KeyDown, InputKey::Tab, reverse)).keyboardConsumed);
        EXPECT_TRUE(send(KeyEvent(InputEventType::KeyUp, InputKey::Tab, reverse)).keyboardConsumed);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    InputRouter m_router;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiInputTests, AcceptedTargetLookupPreservesCommittedGeometryAcrossFailureRetirementAndReset){
    const HitTarget initial[]{ Target(50u), Target(10u), Target(90u) };
    ASSERT_TRUE(m_router.commitTargets(initial, 3u, 1u));
    EXPECT_EQ(m_router.findTarget({}), nullptr);
    EXPECT_EQ(m_router.findTarget({ 20u }), nullptr);
    EXPECT_EQ(m_router.findTarget({ 10u }, initial[1u].declarationGeneration + 1u), nullptr);
    const HitTarget* accepted = m_router.findTarget({ 10u }, initial[1u].declarationGeneration);
    ASSERT_NE(accepted, nullptr);
    EXPECT_EQ(accepted, &m_router.targets()[1u]);

    HitTarget duplicate[]{ Target(70u), Target(70u) };
    duplicate[0u].rectangle.x = 40.0f;
    EXPECT_FALSE(m_router.commitTargets(duplicate, 2u, 2u));
    EXPECT_EQ(m_router.findTarget({ 10u }), accepted);
    EXPECT_EQ(m_router.findTarget({ 70u }), nullptr);
    EXPECT_FLOAT_EQ(accepted->rectangle.x, initial[1u].rectangle.x);

    m_router.invalidateTarget({ 50u });
    EXPECT_EQ(m_router.findTarget({ 50u }), nullptr);
    ASSERT_NE(m_router.findTarget({ 10u }), nullptr);
    EXPECT_EQ(m_router.findTarget({ 10u }), &m_router.targets()[0u]);
    ASSERT_NE(m_router.findTarget({ 90u }), nullptr);
    EXPECT_EQ(m_router.findTarget({ 90u }), &m_router.targets()[1u]);

    HitTarget replacement = Target(10u);
    replacement.declarationGeneration = initial[1u].declarationGeneration + 1u;
    replacement.rectangle.x = 60.0f;
    ASSERT_TRUE(m_router.commitTargets(&replacement, 1u, 2u));
    EXPECT_EQ(m_router.findTarget({ 10u }, initial[1u].declarationGeneration), nullptr);
    ASSERT_NE(m_router.findTarget({ 10u }, replacement.declarationGeneration), nullptr);
    EXPECT_FLOAT_EQ(m_router.findTarget({ 10u })->rectangle.x, 60.0f);
    EXPECT_EQ(m_router.findTarget({ 90u }), nullptr);
    m_router.reset();
    EXPECT_EQ(m_router.findTarget({ 10u }), nullptr);
}

TEST_F(UiInputTests, AbsentTargetInvalidationPreservesLiveFocusCaptureHoverAndQueuedActions){
    const HitTarget target = Target(1u);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 7u));
    click();
    ASSERT_EQ(m_router.actions().size(), 1u);
    const u64 actionSequence = m_router.actions()[0u].id.sequence;
    ASSERT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    const u64 hoverActivity = m_router.hoverActivityGeneration();
    m_router.invalidateTarget({ 999u });
    m_router.invalidateTarget({});
    EXPECT_EQ(m_router.layoutGeneration(), 7u);
    EXPECT_EQ(m_router.targets().size(), 1u);
    EXPECT_EQ(m_router.focus(), target.id);
    EXPECT_EQ(m_router.capture(), target.id);
    EXPECT_EQ(m_router.hover(), target.id);
    EXPECT_EQ(m_router.hoverActivityGeneration(), hoverActivity);
    EXPECT_TRUE(m_router.primaryDown());
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_EQ(m_router.actions()[0u].id.sequence, actionSequence);
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_FALSE(m_router.consumeActivation(target.id));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp)).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_FALSE(m_router.consumeActivation(target.id));
}

TEST_F(UiInputTests, FirstClickConsumesBeforeFocusExistsAndActivationIsNotReplayed){
    const HitTarget target = Target(1u);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 7u));
    EXPECT_FALSE(m_router.focus().valid());
    const InputRoutingResult pressed = send(PointerEvent(InputEventType::PrimaryDown));
    EXPECT_TRUE(pressed.pointerConsumed);
    EXPECT_TRUE(pressed.wantsPointer);
    EXPECT_TRUE(pressed.wantsKeyboard);
    EXPECT_EQ(pressed.hover.value, 1u);
    EXPECT_EQ(pressed.focus.value, 1u);
    EXPECT_EQ(pressed.capture.value, 1u);
    EXPECT_TRUE(m_router.primaryDown());
    EXPECT_TRUE(m_router.actions().empty());
    const InputRoutingResult released = send(PointerEvent(InputEventType::PrimaryUp));
    EXPECT_TRUE(released.pointerConsumed);
    EXPECT_FALSE(released.capture.valid());
    ASSERT_EQ(m_router.actions().size(), 1u);
    const InputAction action = m_router.actions()[0];
    EXPECT_TRUE(action.id.valid());
    EXPECT_EQ(action.id.target.value, 1u);
    EXPECT_EQ(action.id.declarationGeneration, 1u);
    EXPECT_EQ(action.id.layoutGeneration, 7u);
    EXPECT_EQ(action.source, InputActionSource::Pointer);
    EXPECT_FALSE(m_router.process().pointerConsumed);
    EXPECT_EQ(m_router.actions()[0].id.sequence, action.id.sequence);
    EXPECT_FALSE(m_router.commitTargets(&target, 1u, 7u));
    EXPECT_EQ(m_router.actions().size(), 1u);
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_FALSE(m_router.consumeActivation(target.id));
    EXPECT_TRUE(m_router.actions().empty());
}

TEST_F(UiInputTests, HitTestRespectsPaintOrderClippingDisabledTargetsAndHalfOpenBounds){
    HitTarget targets[]{ Target(1u), Target(2u), Target(3u), Target(4u) };
    for(HitTarget& target : targets)
        target.rectangle = { 0.0f, 0.0f, 100.0f, 100.0f };
    targets[0].paintOrder = 20u;
    targets[0].clip = { 40.0f, 40.0f, 20.0f, 20.0f };
    targets[1].paintOrder = 1u;
    targets[2].paintOrder = 100u;
    targets[2].enabled = false;
    targets[3].paintOrder = 20u;
    targets[3].clip = { 45.0f, 45.0f, 5.0f, 5.0f };
    ASSERT_TRUE(m_router.commitTargets(targets, 4u, 1u));
    EXPECT_EQ(m_router.hitTest({ 40.0f, 40.0f }).value, 1u);
    EXPECT_EQ(m_router.hitTest({ 45.0f, 45.0f }).value, 4u);
    EXPECT_EQ(m_router.hitTest({ 50.0f, 50.0f }).value, 1u);
    EXPECT_EQ(m_router.hitTest({ 60.0f, 60.0f }).value, 2u);
    EXPECT_EQ(m_router.hitTest({ 10.0f, 10.0f }).value, 2u);
    EXPECT_FALSE(m_router.hitTest({ 100.0f, 10.0f }).valid());
    EXPECT_FALSE(m_router.hitTest({ Limit<f32>::s_QuietNaN, 0.0f }).valid());
    EXPECT_TRUE(m_router.wouldConsumePointer({ 45.0f, 45.0f }));
    EXPECT_FALSE(m_router.wouldConsumePointer({ -1.0f, -1.0f }));
}

TEST_F(UiInputTests, TransientHoverDepartureAndReentryAdvanceActivityBeforeTheNextPaint){
    HitTarget targets[]{ Target(1u), Target(2u, { 40.0f, 10.0f, 20.0f, 20.0f }) };
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, 1u));
    ASSERT_EQ(send(PointerEvent(InputEventType::PointerMove)).hover, targets[0].id);
    const u64 stable = m_router.hoverActivityGeneration();
    ASSERT_EQ(send(PointerEvent(InputEventType::PointerMove, { 16.0f, 16.0f })).hover, targets[0].id);
    EXPECT_EQ(m_router.hoverActivityGeneration(), stable);
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, 2u));
    EXPECT_EQ(m_router.hoverActivityGeneration(), stable);
    EXPECT_FALSE(m_router.commitTargets(nullptr, 1u, 3u));
    EXPECT_EQ(m_router.hoverActivityGeneration(), stable);

    ASSERT_TRUE(m_router.queue(PointerEvent(InputEventType::PointerMove, { 45.0f, 15.0f })));
    ASSERT_TRUE(m_router.queue(PointerEvent(InputEventType::PointerMove)));
    EXPECT_EQ(m_router.process().hover, targets[0].id);
    const u64 returned = m_router.hoverActivityGeneration();
    EXPECT_GT(returned, stable);

    ASSERT_TRUE(m_router.queue({ InputEventType::PointerLeave, {} }));
    ASSERT_TRUE(m_router.queue(PointerEvent(InputEventType::PointerMove)));
    EXPECT_EQ(m_router.process().hover, targets[0].id);
    EXPECT_GT(m_router.hoverActivityGeneration(), returned);
}

TEST_F(UiInputTests, HoverActivityUsesTheControlHostLifetimeAcrossOwnedParts){
    HitTarget targets[]{ Target(1u, { 10.0f, 10.0f, 80.0f, 20.0f }),
        Target(2u, { 20.0f, 10.0f, 10.0f, 20.0f }), Target(3u, { 40.0f, 10.0f, 10.0f, 20.0f }) };
    targets[0].control = { 11u, 12u, 13u };
    for(usize index = 1u; index < 3u; ++index){
        targets[index].owner = targets[0].id;
        targets[index].ownerDeclarationGeneration = targets[0].declarationGeneration;
        targets[index].control = targets[0].control;
        targets[index].focusable = false;
        targets[index].paintOrder = static_cast<u32>(index);
    }
    ASSERT_TRUE(m_router.commitTargets(targets, 3u, 1u));
    ASSERT_EQ(send(PointerEvent(InputEventType::PointerMove, { 15.0f, 15.0f })).hover, targets[0].id);
    const u64 ownerGeneration = m_router.hoverActivityGeneration();
    EXPECT_EQ(send(PointerEvent(InputEventType::PointerMove, { 25.0f, 15.0f })).hover, targets[1].id);
    EXPECT_EQ(send(PointerEvent(InputEventType::PointerMove, { 45.0f, 15.0f })).hover, targets[2].id);
    EXPECT_EQ(send(PointerEvent(InputEventType::PointerMove, { 80.0f, 15.0f })).hover, targets[0].id);
    EXPECT_EQ(m_router.hoverActivityGeneration(), ownerGeneration);
    ASSERT_TRUE(m_router.commitTargets(targets, 3u, 2u));
    EXPECT_EQ(m_router.hoverActivityGeneration(), ownerGeneration);

    targets[0].control.contentRevision = 14u;
    targets[1].control = targets[0].control;
    targets[2].control = targets[0].control;
    ASSERT_TRUE(m_router.commitTargets(targets, 3u, 3u));
    const u64 changedControl = m_router.hoverActivityGeneration();
    EXPECT_GT(changedControl, ownerGeneration);
    targets[0].declarationGeneration = 2u;
    targets[1].ownerDeclarationGeneration = 2u;
    targets[2].ownerDeclarationGeneration = 2u;
    ASSERT_TRUE(m_router.commitTargets(targets, 3u, 4u));
    EXPECT_GT(m_router.hoverActivityGeneration(), changedControl);
}

TEST_F(UiInputTests, PressCaptureLossFocusLossAndResetFenceHoverActivity){
    const HitTarget target = Target(1u);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    ASSERT_EQ(send(PointerEvent(InputEventType::PointerMove)).hover, target.id);
    u64 previous = m_router.hoverActivityGeneration();
    ASSERT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    EXPECT_GT(m_router.hoverActivityGeneration(), previous);
    previous = m_router.hoverActivityGeneration();
    ASSERT_TRUE(send(PointerEvent(InputEventType::PrimaryUp)).pointerConsumed);
    EXPECT_EQ(m_router.hoverActivityGeneration(), previous);

    ASSERT_TRUE(send(PointerEvent(InputEventType::SecondaryDown)).pointerConsumed);
    EXPECT_GT(m_router.hoverActivityGeneration(), previous);
    previous = m_router.hoverActivityGeneration();
    ASSERT_TRUE(send(PointerEvent(InputEventType::SecondaryUp)).pointerConsumed);
    EXPECT_EQ(m_router.hoverActivityGeneration(), previous);

    send({ InputEventType::PointerCaptureLost, {} });
    EXPECT_GT(m_router.hoverActivityGeneration(), previous);
    previous = m_router.hoverActivityGeneration();
    send({ InputEventType::FocusLost, {} });
    EXPECT_GT(m_router.hoverActivityGeneration(), previous);
    previous = m_router.hoverActivityGeneration();
    m_router.reset();
    EXPECT_GT(m_router.hoverActivityGeneration(), previous);
    previous = m_router.hoverActivityGeneration();
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    send({ InputEventType::FocusGained, {} });
    EXPECT_EQ(send(PointerEvent(InputEventType::PointerMove)).hover, target.id);
    EXPECT_GT(m_router.hoverActivityGeneration(), previous);
}

TEST_F(UiInputTests, DragOutsideDoesNotActivateAndRemovalKeepsReleaseConsumption){
    const HitTarget target = Target(1u);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    const InputRoutingResult moved = send(PointerEvent(InputEventType::PointerMove, { 90.0f, 90.0f }));
    EXPECT_TRUE(moved.pointerConsumed);
    EXPECT_FALSE(moved.hover.valid());
    EXPECT_EQ(moved.capture.value, 1u);
    EXPECT_TRUE(m_router.wouldConsumePointer({ 90.0f, 90.0f }));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 90.0f, 90.0f })).pointerConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    ASSERT_TRUE(m_router.commitTargets(nullptr, 0u, 2u));
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_FALSE(m_router.focus().valid());
    const InputRoutingResult afterRemoval = send(PointerEvent(InputEventType::PointerMove, { 90.0f, 90.0f }));
    EXPECT_TRUE(afterRemoval.pointerConsumed);
    EXPECT_TRUE(afterRemoval.wantsPointer);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp)).pointerConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(send(PointerEvent(InputEventType::PointerMove)).pointerConsumed);
    EXPECT_FALSE(m_router.wouldConsumePointer({ 15.0f, 15.0f }));
}

TEST_F(UiInputTests, PointerLeaveClearsHoverButPreservesLogicalCaptureAndReleaseOwnership){
    const HitTarget target = Target(1u);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove)).pointerConsumed);
    const InputRoutingResult hoveredLeave = send({ InputEventType::PointerLeave, {} });
    EXPECT_FALSE(hoveredLeave.hover.valid());
    EXPECT_FALSE(hoveredLeave.wantsPointer);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    const InputRoutingResult capturedLeave = send({ InputEventType::PointerLeave, {} });
    EXPECT_TRUE(capturedLeave.pointerConsumed);
    EXPECT_TRUE(capturedLeave.wantsPointer);
    EXPECT_FALSE(capturedLeave.hover.valid());
    EXPECT_EQ(capturedLeave.capture.value, 1u);
    EXPECT_EQ(capturedLeave.focus.value, 1u);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 90.0f, 90.0f })).pointerConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(m_router.capture().valid());
}

TEST_F(UiInputTests, NewDeclarationAndDisabledTargetsCancelPendingStateAndActions){
    HitTarget target = Target(1u);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    click();
    ASSERT_EQ(m_router.actions().size(), 1u);
    target.declarationGeneration = 2u;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 2u));
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(m_router.consumeActivation(target.id));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    target.enabled = false;
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 3u));
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp)).pointerConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(m_router.hitTest({ 15.0f, 15.0f }).valid());
}

TEST_F(UiInputTests, InvalidatedDeclarationPrunesActionsAndCaptureWithoutAdvancingLayout){
    const HitTarget targets[]{ Target(1u), Target(2u, { 40.0f, 10.0f, 20.0f, 20.0f }) };
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, 7u));
    click();
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyDown, InputKey::Space)).keyboardConsumed);
    m_router.invalidateTarget(targets[0].id);
    EXPECT_EQ(m_router.layoutGeneration(), 7u);
    ASSERT_EQ(m_router.targets().size(), 1u);
    EXPECT_EQ(m_router.targets()[0].id.value, 2u);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_FALSE(m_router.hitTest({ 15.0f, 15.0f }).valid());
    EXPECT_EQ(m_router.hitTest({ 45.0f, 15.0f }).value, 2u);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp)).pointerConsumed);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyUp, InputKey::Space)).keyboardConsumed);
    m_router.invalidateTarget({ 999u });
    EXPECT_EQ(m_router.layoutGeneration(), 7u);
    EXPECT_EQ(m_router.targets().size(), 1u);
}

TEST_F(UiInputTests, PanelBlocksPointerAndClearsKeyboardFocusWithoutActivating){
    HitTarget targets[]{ Target(1u), Target(2u, { 40.0f, 10.0f, 20.0f, 20.0f }) };
    targets[1].focusable = false;
    targets[1].activatable = false;
    ASSERT_TRUE(m_router.commitTargets(targets, 2u, 1u));
    click();
    ASSERT_EQ(m_router.focus().value, 1u);
    ASSERT_TRUE(m_router.consumeActivation(targets[0].id));
    const InputRoutingResult panel = send(PointerEvent(InputEventType::PrimaryDown, { 45.0f, 15.0f }));
    EXPECT_TRUE(panel.pointerConsumed);
    EXPECT_FALSE(panel.focus.valid());
    EXPECT_EQ(panel.capture.value, 2u);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp, { 45.0f, 15.0f })).pointerConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(send(KeyEvent(InputEventType::KeyDown, InputKey::Enter)).keyboardConsumed);
    EXPECT_FALSE(send(KeyEvent(InputEventType::KeyUp, InputKey::Enter)).keyboardConsumed);
}

TEST_F(UiInputTests, TabOrderWrapsInBothDirectionsAndSkipsDisabledOrInvisibleTargets){
    HitTarget targets[]{ Target(1u), Target(2u), Target(3u), Target(4u), Target(5u) };
    targets[1].enabled = false;
    targets[2].focusable = false;
    targets[3].clip = { 90.0f, 90.0f, 10.0f, 10.0f };
    targets[0].paintOrder = 100u;
    targets[4].paintOrder = 0u;
    ASSERT_TRUE(m_router.commitTargets(targets, 5u, 1u));
    tab();
    EXPECT_EQ(m_router.focus().value, 1u);
    tab();
    EXPECT_EQ(m_router.focus().value, 5u);
    tab();
    EXPECT_EQ(m_router.focus().value, 1u);
    tab(true);
    EXPECT_EQ(m_router.focus().value, 5u);
    tab(true);
    EXPECT_EQ(m_router.focus().value, 1u);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyDown, InputKey::Escape)).keyboardConsumed);
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyUp, InputKey::Escape)).keyboardConsumed);
    tab(true);
    EXPECT_EQ(m_router.focus().value, 5u);
    ASSERT_TRUE(m_router.commitTargets(nullptr, 0u, 2u));
    EXPECT_FALSE(send(KeyEvent(InputEventType::KeyDown, InputKey::Tab)).keyboardConsumed);
    EXPECT_FALSE(send(KeyEvent(InputEventType::KeyUp, InputKey::Tab)).keyboardConsumed);
}

TEST_F(UiInputTests, KeyboardActivationIgnoresNativeRepeatsAndDuplicateDowns){
    const HitTarget target = Target(1u);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    tab();
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyDown, InputKey::Enter)).keyboardConsumed);
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_EQ(m_router.actions()[0].source, InputActionSource::Keyboard);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyDown, InputKey::Enter, false, true)).keyboardConsumed);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyDown, InputKey::Enter)).keyboardConsumed);
    EXPECT_EQ(m_router.actions().size(), 1u);
    EXPECT_FALSE(m_router.process().keyboardConsumed);
    EXPECT_EQ(m_router.actions().size(), 1u);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyUp, InputKey::Enter)).keyboardConsumed);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyDown, InputKey::Enter)).keyboardConsumed);
    EXPECT_EQ(m_router.actions().size(), 2u);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyUp, InputKey::Enter)).keyboardConsumed);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyDown, InputKey::Space)).keyboardConsumed);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyUp, InputKey::Space)).keyboardConsumed);
    EXPECT_EQ(m_router.actions().size(), 3u);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyDown, InputKey::Space, false, true)).keyboardConsumed);
    EXPECT_EQ(m_router.actions().size(), 3u);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyUp, InputKey::Space)).keyboardConsumed);
    ASSERT_TRUE(m_router.commitTargets(nullptr, 0u, 2u));
    EXPECT_TRUE(m_router.actions().empty());
}

TEST_F(UiInputTests, HeldUiKeysKeepReleaseOwnershipAfterFocusedTargetRemoval){
    const HitTarget target = Target(1u);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    tab();
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyDown, InputKey::Space)).keyboardConsumed);
    ASSERT_TRUE(m_router.commitTargets(nullptr, 0u, 2u));
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_TRUE(m_router.actions().empty());
    const InputRoutingResult repeated = send(KeyEvent(InputEventType::KeyDown, InputKey::Space, false, true));
    EXPECT_TRUE(repeated.keyboardConsumed);
    EXPECT_TRUE(repeated.wantsKeyboard);
    EXPECT_TRUE(m_router.actions().empty());
    const InputRoutingResult released = send(KeyEvent(InputEventType::KeyUp, InputKey::Space));
    EXPECT_TRUE(released.keyboardConsumed);
    EXPECT_FALSE(released.wantsKeyboard);
    EXPECT_FALSE(send(KeyEvent(InputEventType::KeyDown, InputKey::Space)).keyboardConsumed);
}

TEST_F(UiInputTests, ClearingKeyboardFocusPreservesAcceptedActionsCaptureAndHeldReleaseOwnership){
    const HitTarget target = Target(1u);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    click();
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyDown, InputKey::Space)).keyboardConsumed);
    ASSERT_EQ(m_router.actions().size(), 2u);
    EXPECT_TRUE(m_router.ownsKey(InputKey::Space));
    EXPECT_FALSE(m_router.ownsKey(InputKey::None));
    EXPECT_FALSE(m_router.ownsKey(static_cast<InputKey::Enum>(255u)));
    m_router.clearFocus();
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_TRUE(m_router.ownsKey(InputKey::Space));
    EXPECT_EQ(m_router.capture().value, 1u);
    EXPECT_TRUE(m_router.primaryDown());
    EXPECT_EQ(m_router.actions().size(), 2u);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyUp, InputKey::Space)).keyboardConsumed);
    EXPECT_FALSE(m_router.ownsKey(InputKey::Space));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp)).pointerConsumed);
    ASSERT_EQ(m_router.actions().size(), 3u);
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_FALSE(m_router.consumeActivation(target.id));
}

TEST_F(UiInputTests, FocusLossAndResetCancelInteractionWithoutReusingActionSequences){
    const HitTarget target = Target(1u);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    click();
    ASSERT_EQ(m_router.actions().size(), 1u);
    const u64 firstSequence = m_router.actions()[0].id.sequence;
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyDown, InputKey::Space)).keyboardConsumed);
    const InputRoutingResult lost = send({ InputEventType::FocusLost, {} });
    EXPECT_TRUE(lost.pointerConsumed);
    EXPECT_TRUE(lost.keyboardConsumed);
    EXPECT_FALSE(lost.wantsPointer);
    EXPECT_FALSE(lost.wantsKeyboard);
    EXPECT_FALSE(lost.hover.valid());
    EXPECT_FALSE(lost.focus.valid());
    EXPECT_FALSE(lost.capture.valid());
    EXPECT_FALSE(m_router.primaryDown());
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_FALSE(send(KeyEvent(InputEventType::KeyUp, InputKey::Space)).keyboardConsumed);
    ASSERT_TRUE(m_router.queue(PointerEvent(InputEventType::PrimaryDown)));
    m_router.reset();
    EXPECT_EQ(m_router.layoutGeneration(), 0u);
    EXPECT_TRUE(m_router.targets().empty());
    EXPECT_FALSE(m_router.process().pointerConsumed);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    click();
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_GT(m_router.actions()[0].id.sequence, firstSequence);
}

TEST_F(UiInputTests, EventsUseCommittedTargetsAndScenePressCannotTransferToNewLayout){
    const HitTarget oldTarget = Target(1u);
    HitTarget nextTarget = Target(2u, { 40.0f, 10.0f, 20.0f, 20.0f });
    ASSERT_TRUE(m_router.commitTargets(&oldTarget, 1u, 1u));
    EXPECT_FALSE(m_router.hitTest({ 45.0f, 15.0f }).valid());
    EXPECT_FALSE(send(PointerEvent(InputEventType::PrimaryDown, { 45.0f, 15.0f })).pointerConsumed);
    ASSERT_TRUE(m_router.commitTargets(&nextTarget, 1u, 2u));
    EXPECT_EQ(m_router.hitTest({ 45.0f, 15.0f }).value, 2u);
    EXPECT_FALSE(send(PointerEvent(InputEventType::PrimaryUp, { 45.0f, 15.0f })).pointerConsumed);
    EXPECT_TRUE(m_router.actions().empty());
    click({ 45.0f, 15.0f });
    ASSERT_EQ(m_router.actions().size(), 1u);
    EXPECT_EQ(m_router.actions()[0].id.target.value, 2u);
    EXPECT_EQ(m_router.actions()[0].id.layoutGeneration, 2u);
    nextTarget.rectangle.x = 60.0f;
    ASSERT_TRUE(m_router.commitTargets(&nextTarget, 1u, 3u));
    EXPECT_EQ(m_router.actions().size(), 1u);
    EXPECT_TRUE(m_router.consumeActivation(nextTarget.id));
}

TEST_F(UiInputTests, InvalidTargetPublicationPreservesAcceptedLayoutCaptureAndFocus){
    const HitTarget target = Target(1u);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryDown)).pointerConsumed);
    HitTarget invalid[]{ Target(2u), Target(2u) };
    EXPECT_FALSE(m_router.commitTargets(invalid, 2u, 2u));
    invalid[1].id.value = 3u;
    invalid[1].rectangle.width = -1.0f;
    EXPECT_FALSE(m_router.commitTargets(invalid, 2u, 2u));
    invalid[1].rectangle.width = Limit<f32>::s_Infinity;
    EXPECT_FALSE(m_router.commitTargets(invalid, 2u, 2u));
    invalid[1].rectangle = { 0.0f, 0.0f, 1.0f, 1.0f };
    invalid[1].declarationGeneration = 0u;
    EXPECT_FALSE(m_router.commitTargets(invalid, 2u, 2u));
    invalid[1].declarationGeneration = 1u;
    invalid[1].id.value = 0u;
    EXPECT_FALSE(m_router.commitTargets(invalid, 2u, 2u));
    EXPECT_FALSE(m_router.commitTargets(nullptr, 1u, 2u));
    EXPECT_FALSE(m_router.commitTargets(&target, s_InputMaxTargets + 1u, 2u));
    EXPECT_FALSE(m_router.commitTargets(&target, 1u, 0u));
    EXPECT_FALSE(m_router.commitTargets(&target, 1u, 1u));
    EXPECT_EQ(m_router.layoutGeneration(), 1u);
    ASSERT_EQ(m_router.targets().size(), 1u);
    EXPECT_EQ(m_router.focus().value, 1u);
    EXPECT_EQ(m_router.capture().value, 1u);
    EXPECT_EQ(m_router.hitTest({ 15.0f, 15.0f }).value, 1u);
    EXPECT_TRUE(send(PointerEvent(InputEventType::PrimaryUp)).pointerConsumed);
    EXPECT_TRUE(m_router.consumeActivation(target.id));
}

TEST_F(UiInputTests, TargetAndEventLimitsRejectOverflowAndAllowSubsequentProcessing){
    InputVector<HitTarget> targets(m_arena);
    targets.reserve(s_InputMaxTargets);
    for(usize index = 0u; index < s_InputMaxTargets; ++index)
        targets.push_back(Target(static_cast<u64>(index) + 1u));
    ASSERT_TRUE(m_router.commitTargets(targets.data(), targets.size(), 1u));
    EXPECT_FALSE(m_router.commitTargets(targets.data(), targets.size() + 1u, 2u));
    EXPECT_EQ(m_router.targets().size(), s_InputMaxTargets);
    for(usize index = 0u; index < s_InputMaxEvents; ++index)
        ASSERT_TRUE(m_router.queue(PointerEvent(InputEventType::PointerMove)));
    EXPECT_FALSE(m_router.queue(PointerEvent(InputEventType::PointerMove)));
    EXPECT_TRUE(m_router.process().pointerConsumed);
    EXPECT_EQ(m_router.hover().value, s_InputMaxTargets);
    EXPECT_FALSE(m_router.queue(PointerEvent(InputEventType::PointerMove, { Limit<f32>::s_QuietNaN, 1.0f })));
    EXPECT_FALSE(m_router.queue({ static_cast<InputEventType::Enum>(255u), {} }));
    EXPECT_FALSE(m_router.queue(KeyEvent(InputEventType::KeyDown, InputKey::None)));
    EXPECT_TRUE(send(PointerEvent(InputEventType::PointerMove)).pointerConsumed);
}

TEST_F(UiInputTests, ActionLimitReportsOverflowWithoutReplayingDroppedInput){
    const HitTarget target = Target(1u);
    ASSERT_TRUE(m_router.commitTargets(&target, 1u, 1u));
    tab();
    for(usize index = 0u; index < s_InputMaxActions; ++index){
        const InputRoutingResult activated = send(KeyEvent(InputEventType::KeyDown, InputKey::Enter));
        EXPECT_TRUE(activated.keyboardConsumed);
        EXPECT_FALSE(activated.activationOverflow);
        EXPECT_TRUE(send(KeyEvent(InputEventType::KeyUp, InputKey::Enter)).keyboardConsumed);
    }
    ASSERT_EQ(m_router.actions().size(), s_InputMaxActions);
    const InputRoutingResult overflow = send(KeyEvent(InputEventType::KeyDown, InputKey::Enter));
    EXPECT_TRUE(overflow.keyboardConsumed);
    EXPECT_TRUE(overflow.activationOverflow);
    EXPECT_EQ(m_router.actions().size(), s_InputMaxActions);
    EXPECT_TRUE(send(KeyEvent(InputEventType::KeyUp, InputKey::Enter)).keyboardConsumed);
    EXPECT_TRUE(m_router.consumeActivation(target.id));
    EXPECT_FALSE(m_router.process().activationOverflow);
    EXPECT_EQ(m_router.actions().size(), s_InputMaxActions - 1u);
    EXPECT_FALSE(send(KeyEvent(InputEventType::KeyDown, InputKey::Enter)).activationOverflow);
    EXPECT_EQ(m_router.actions().size(), s_InputMaxActions);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

