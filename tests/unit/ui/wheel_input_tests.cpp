// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/input/router.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_wheel_input_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

HitTarget WheelHost(){
    HitTarget target;
    target.id = { 1u };
    target.declarationGeneration = 7u;
    target.rectangle = { 0.0f, 0.0f, 100.0f, 100.0f };
    target.clip = { 0.0f, 0.0f, 300.0f, 200.0f };
    target.focusable = true;
    target.scrollable = true;
    target.control = { 11u, 21u, 31u };
    target.scrollStep = 48.0;
    target.gestureMaximum = 360.0;
    target.scrollStepX = 32.0;
    target.gestureMaximumX = 180.0;
    return target;
}

HitTarget WheelPart(const HitTarget& host){
    HitTarget part;
    part.id = { 2u };
    part.declarationGeneration = host.declarationGeneration;
    part.rectangle = { 90.0f, 20.0f, 10.0f, 20.0f };
    part.clip = host.clip;
    part.paintOrder = 1u;
    part.owner = host.id;
    part.ownerDeclarationGeneration = host.declarationGeneration;
    part.control = host.control;
    part.pointerGesture = true;
    part.gestureReference = { 90.0f, 0.0f, 10.0f, 100.0f };
    part.gestureMaximum = host.gestureMaximum;
    return part;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiWheelInputTests : public testing::Test{
public:
    UiWheelInputTests()
        : m_arena(Name("tests/ui/wheel_input"))
        , m_router(m_arena)
    {
        m_targets[0u] = WheelHost();
        m_targets[1u] = WheelPart(m_targets[0u]);
    }


protected:
    [[nodiscard]] InputRoutingResult wheel(const f64 deltaX, const f64 deltaY, const Point position = { 10.0f, 10.0f }){
        InputEvent event;
        event.type = InputEventType::PointerWheel;
        event.position = position;
        event.scrollX = deltaX;
        event.scrollY = deltaY;
        EXPECT_TRUE(m_router.queue(event));
        return m_router.process();
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    InputRouter m_router;
    Array<HitTarget, 2u> m_targets;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiWheelInputTests, DiagonalWheelCopiesBothAcceptedAxesIntoOneAction){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 5u));
    EXPECT_TRUE(wheel(0.25, -0.5, { 95.0f, 25.0f }).pointerConsumed);
    ASSERT_EQ(m_router.controlActions().size(), 1u);
    EXPECT_FALSE(m_router.focus().valid());
    m_targets[0u].scrollStep = 99.0;
    m_targets[0u].scrollStepX = 101.0;
    m_targets[0u].gestureMaximum = 999.0;
    m_targets[0u].gestureMaximumX = 1001.0;
    ControlAction action;
    const auto actionResult = m_router.consumeControlAction(m_targets[0u].id, 7u, m_targets[0u].control);
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_DOUBLE_EQ(action.delta, -0.5);
    EXPECT_DOUBLE_EQ(action.deltaX, 0.25);
    EXPECT_DOUBLE_EQ(action.step, 48.0);
    EXPECT_DOUBLE_EQ(action.stepX, 32.0);
    EXPECT_DOUBLE_EQ(action.maximum, 360.0);
    EXPECT_DOUBLE_EQ(action.maximumX, 180.0);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(m_router.actions().empty());
}

TEST_F(UiWheelInputTests, RightUpAndLeftDownKeepTheirNativeSignsAndSequence){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(wheel(1.0, 2.0).pointerConsumed);
    EXPECT_TRUE(wheel(-3.0, -4.0).pointerConsumed);
    ASSERT_EQ(m_router.controlActions().size(), 2u);
    ControlAction rightUp;
    ControlAction leftDown;
    const auto rightUpResult = m_router.consumeControlAction(m_targets[0u].id, 7u, m_targets[0u].control);
    ASSERT_TRUE(rightUpResult);
    rightUp = *rightUpResult;
    const auto leftDownResult = m_router.consumeControlAction(m_targets[0u].id, 7u, m_targets[0u].control);
    ASSERT_TRUE(leftDownResult);
    leftDown = *leftDownResult;
    EXPECT_DOUBLE_EQ(rightUp.deltaX, 1.0);
    EXPECT_DOUBLE_EQ(rightUp.delta, 2.0);
    EXPECT_DOUBLE_EQ(leftDown.deltaX, -3.0);
    EXPECT_DOUBLE_EQ(leftDown.delta, -4.0);
    EXPECT_EQ(leftDown.id.sequence, rightUp.id.sequence + 1u);
}

TEST_F(UiWheelInputTests, VerticalOnlyListConsumesPureXWithoutDeliveringAControlAction){
    m_targets[0u].scrollStepX = 0.0;
    m_targets[0u].gestureMaximumX = 0.0;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(wheel(2.0, 0.0).pointerConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_FALSE(wheel(2.0, 0.0, { 250.0f, 180.0f }).pointerConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
}

TEST_F(UiWheelInputTests, VerticalOnlyListDiagonalStillDeliversItsExactVerticalStep){
    m_targets[0u].scrollStepX = 0.0;
    m_targets[0u].gestureMaximumX = 0.0;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(wheel(2.0, 0.5).pointerConsumed);
    ControlAction action;
    const auto actionResult = m_router.consumeControlAction(m_targets[0u].id, 7u, m_targets[0u].control);
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_DOUBLE_EQ(action.delta, 0.5);
    EXPECT_DOUBLE_EQ(action.step, 48.0);
    EXPECT_DOUBLE_EQ(action.maximum, 360.0);
    EXPECT_DOUBLE_EQ(action.deltaX, 2.0);
    EXPECT_DOUBLE_EQ(action.stepX, 0.0);
    EXPECT_DOUBLE_EQ(action.maximumX, 0.0);
    EXPECT_TRUE(m_router.controlActions().empty());
}

TEST_F(UiWheelInputTests, DiagonalWheelOccupiesOnePositionBetweenGestureUpdates){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    ASSERT_TRUE(m_router.queue({ .type = InputEventType::PrimaryDown, .position = { 95.0f, 25.0f } }));
    EXPECT_TRUE(m_router.process().pointerConsumed);
    PointerGesture first;
    const auto firstResult = m_router.consumePointerGesture(m_targets[1u].id, 7u);
    ASSERT_TRUE(firstResult);
    first = *firstResult;
    EXPECT_TRUE(wheel(1.0, -1.0, { 95.0f, 25.0f }).pointerConsumed);
    ASSERT_EQ(m_router.controlActions().size(), 1u);
    ControlAction action;
    const auto actionResult = m_router.consumeControlAction(m_targets[0u].id, 7u, m_targets[0u].control);
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_EQ(action.id.sequence, first.updateSequence + 1u);
    ASSERT_TRUE(m_router.queue({ .type = InputEventType::PointerMove, .position = { 95.0f, 60.0f } }));
    EXPECT_TRUE(m_router.process().pointerConsumed);
    PointerGesture moved;
    const auto movedResult = m_router.consumePointerGesture(m_targets[1u].id, 7u);
    ASSERT_TRUE(movedResult);
    moved = *movedResult;
    EXPECT_EQ(moved.updateSequence, action.id.sequence + 1u);
    EXPECT_EQ(moved.id.sequence, first.id.sequence);
}

TEST_F(UiWheelInputTests, InvalidHorizontalMetadataRejectsPublicationAndPreservesPendingAction){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(wheel(1.0, -1.0).pointerConsumed);
    const HitTarget accepted = m_targets[0u];
    const f64 invalid[]{ -1.0, Limit<f64>::s_QuietNaN, Limit<f64>::s_Infinity };
    for(const f64 value : invalid){
        m_targets[0u] = accepted;
        m_targets[0u].scrollStepX = value;
        EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
        m_targets[0u] = accepted;
        m_targets[0u].gestureMaximumX = value;
        EXPECT_FALSE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    }
    EXPECT_EQ(m_router.layoutGeneration(), 1u);
    ASSERT_EQ(m_router.controlActions().size(), 1u);
    ControlAction action;
    const auto actionResult = m_router.consumeControlAction(accepted.id, 7u, accepted.control);
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_DOUBLE_EQ(action.deltaX, 1.0);
    EXPECT_DOUBLE_EQ(action.maximumX, 180.0);
}

TEST_F(UiWheelInputTests, SceneOwnedPressCannotAcquireEitherWheelAxisOverAHost){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    ASSERT_TRUE(m_router.queue({ .type = InputEventType::PrimaryDown, .position = { 250.0f, 180.0f } }));
    EXPECT_FALSE(m_router.process().pointerConsumed);
    EXPECT_FALSE(wheel(1.0, -1.0).pointerConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    ASSERT_TRUE(m_router.queue({ .type = InputEventType::PrimaryUp, .position = { 10.0f, 10.0f } }));
    EXPECT_FALSE(m_router.process().pointerConsumed);
    EXPECT_TRUE(wheel(1.0, -1.0).pointerConsumed);
    EXPECT_EQ(m_router.controlActions().size(), 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

