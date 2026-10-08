// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/input/router.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_control_lifetime_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiTextControlLifetimeTests : public testing::Test{
public:
    UiTextControlLifetimeTests()
        : m_arena(Name("tests/ui/text_control_lifetime"))
        , m_router(m_arena)
    {
        HitTarget& host = m_targets[0u];
        host.id = { 1u };
        host.declarationGeneration = 7u;
        host.rectangle = { 0.0f, 0.0f, 100.0f, 100.0f };
        host.clip = { 0.0f, 0.0f, 300.0f, 200.0f };
        host.focusable = true;
        host.textEditable = true;
        host.scrollable = true;
        host.control = { 11u, 21u, 31u };
        host.scrollStep = 24.0;
        host.scrollStepX = 16.0;
        HitTarget& part = m_targets[1u];
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
    }


protected:
    [[nodiscard]] InputRoutingResult send(const InputEvent& event){
        EXPECT_TRUE(m_router.queue(event));
        return m_router.process();
    }

    void revise(){
        for(auto& target : m_targets)
            ++target.control.contentRevision;
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    InputRouter m_router;
    Array<HitTarget, 2u> m_targets;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiTextControlLifetimeTests, AcceptedAuxiliaryEpochChangePreservesTextFocusAndMainCapture){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 10.0f, 10.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PointerWheel, .position = { 10.0f, 10.0f }, .scrollX = 1.0, .scrollY = -1.0 }).pointerConsumed);
    ASSERT_EQ(m_router.controlActions().size(), 1u);
    revise();
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    EXPECT_TRUE(m_router.controlActions().empty());
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { 160.0f, 150.0f } }).pointerConsumed);
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 160.0f, 150.0f } }).pointerConsumed);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_FALSE(m_router.capture().valid());
}

TEST_F(UiTextControlLifetimeTests, AcceptedAuxiliaryEpochChangeRetiresOwnedPartCaptureAndGesture){
    m_targets[1u].textEditable = true;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 95.0f, 25.0f } }).pointerConsumed);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_EQ(m_router.capture(), m_targets[1u].id);
    revise();
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_FALSE(m_router.consumePointerGesture(m_targets[1u].id, 7u));
}

TEST_F(UiTextControlLifetimeTests, PrepublicationFencePreservesTextCaptureAndRejectsOldWheelUntilAcceptance){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 10.0f, 10.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PointerWheel, .position = { 10.0f, 10.0f }, .scrollY = -1.0 }).pointerConsumed);
    revise();
    m_router.fenceControl(m_targets[0u].id, 7u, m_targets[0u].control);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    EXPECT_TRUE(m_router.controlActions().empty());
    ASSERT_EQ(m_router.targets().size(), 2u);
    EXPECT_TRUE(m_router.targets()[0u].enabled);
    EXPECT_FALSE(m_router.targets()[0u].scrollable);
    EXPECT_TRUE(m_router.targets()[0u].control.empty());
    EXPECT_FALSE(m_router.targets()[1u].enabled);
    EXPECT_TRUE(send({ .type = InputEventType::PointerWheel, .position = { 10.0f, 10.0f }, .scrollX = 1.0, .scrollY = -1.0 }).pointerConsumed);
    EXPECT_TRUE(m_router.controlActions().empty());
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    EXPECT_TRUE(send({ .type = InputEventType::PointerWheel, .position = { 10.0f, 10.0f }, .scrollX = 1.0 }).pointerConsumed);
    ControlAction action;
    const auto actionResult = m_router.consumeControlAction(m_targets[0u].id, 7u, m_targets[0u].control);
    ASSERT_TRUE(actionResult);
    action = *actionResult;
    EXPECT_EQ(action.control, m_targets[0u].control);
}

TEST_F(UiTextControlLifetimeTests, RepeatedAuxiliaryFenceKeepsTextCaptureBeforeAnyNewAcceptance){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 10.0f, 10.0f } }).pointerConsumed);
    revise();
    m_router.fenceControl(m_targets[0u].id, 7u, m_targets[0u].control);
    revise();
    m_router.fenceControl(m_targets[0u].id, 7u, m_targets[0u].control);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    EXPECT_TRUE(m_router.targets()[0u].enabled);
    EXPECT_FALSE(m_router.targets()[1u].enabled);
    EXPECT_TRUE(m_router.controlActions().empty());
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
}

TEST_F(UiTextControlLifetimeTests, PrepublicationFenceRetiresOwnedPartDragWithoutBlurringEditor){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 95.0f, 25.0f } }).pointerConsumed);
    revise();
    m_router.fenceControl(m_targets[0u].id, 7u, m_targets[0u].control);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_FALSE(m_router.consumePointerGesture(m_targets[1u].id, 7u));
}

TEST_F(UiTextControlLifetimeTests, DifferentDeclarationFenceStillRetiresTextFocusAndCapture){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 10.0f, 10.0f } }).pointerConsumed);
    m_router.fenceControl(m_targets[0u].id, 8u, m_targets[0u].control);
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_FALSE(m_router.targets()[0u].enabled);
    EXPECT_FALSE(m_router.targets()[1u].enabled);
}

TEST_F(UiTextControlLifetimeTests, ReplacedDeclarationStillRetiresTextFocusAndMainCapture){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 10.0f, 10.0f } }).pointerConsumed);
    ++m_targets[0u].declarationGeneration;
    ++m_targets[1u].ownerDeclarationGeneration;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.capture().valid());
}

TEST_F(UiTextControlLifetimeTests, DisabledTextHostCannotKeepFocusOrMainCapture){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 10.0f, 10.0f } }).pointerConsumed);
    revise();
    m_targets[0u].enabled = false;
    ASSERT_TRUE(m_router.commitTargets(&m_targets[0u], 1u, 2u));
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.capture().valid());
}

TEST_F(UiTextControlLifetimeTests, FullyClippedTextHostCannotKeepFocusOrMainCapture){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 10.0f, 10.0f } }).pointerConsumed);
    revise();
    for(auto& target : m_targets)
        target.clip = { 200.0f, 0.0f, 10.0f, 100.0f };
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.capture().valid());
}

TEST_F(UiTextControlLifetimeTests, RefreshedAuxiliaryTokenSupportsLaterOrdinaryControlRules){
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 10.0f, 10.0f } }).pointerConsumed);
    revise();
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 2u));
    m_targets[0u].textEditable = false;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 3u));
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_EQ(m_router.capture(), m_targets[0u].id);
    revise();
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 4u));
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.capture().valid());
}

TEST_F(UiTextControlLifetimeTests, NonTextScrollHostKeepsItsExistingFullFenceBehavior){
    m_targets[0u].textEditable = false;
    ASSERT_TRUE(m_router.commitTargets(m_targets.data(), m_targets.size(), 1u));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 10.0f, 10.0f } }).pointerConsumed);
    revise();
    m_router.fenceControl(m_targets[0u].id, 7u, m_targets[0u].control);
    EXPECT_FALSE(m_router.focus().valid());
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_FALSE(m_router.targets()[0u].enabled);
    EXPECT_FALSE(m_router.targets()[1u].enabled);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

