// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/input/router.h>

#include <global/bit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_pointer_gesture_value_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

class UiPointerGestureValueTests : public testing::Test{
public:
    UiPointerGestureValueTests();


protected:
    [[nodiscard]] InputRoutingResult send(const InputEvent& event);
    [[nodiscard]] bool publish(usize count = 2u);
    [[nodiscard]] bool take(PointerGesture& gesture);
    [[nodiscard]] bool takeControl(ControlAction& action);
    void focusHost();


protected:
    Core::Alloc::GlobalArena m_arena;
    InputRouter m_router;
    Array<HitTarget, 2u> m_targets;
    u64 m_generation = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiPointerGestureValueTests::UiPointerGestureValueTests()
    : m_arena(Name("tests/ui/pointer_gesture_value"))
    , m_router(m_arena)
{
    HitTarget& host = m_targets[0u];
    host.id = { 1u };
    host.declarationGeneration = 7u;
    host.rectangle = { 0.0f, 0.0f, 200.0f, 100.0f };
    host.clip = { 0.0f, 0.0f, 300.0f, 200.0f };
    host.focusable = true;
    host.control = { 11u, 21u, 31u };
    host.navigable = true;
    host.horizontalNavigation = true;

    HitTarget& thumb = m_targets[1u];
    thumb.id = { 2u };
    thumb.declarationGeneration = host.declarationGeneration;
    thumb.rectangle = { 20.0f, 20.0f, 40.0f, 20.0f };
    thumb.clip = host.clip;
    thumb.paintOrder = 1u;
    thumb.pointerGesture = true;
    thumb.gestureReference = { 10.0f, 20.0f, 180.0f, 20.0f };
    thumb.control = host.control;
    thumb.owner = host.id;
    thumb.ownerDeclarationGeneration = host.declarationGeneration;
    thumb.value = BitCast<u64>(-0.0);
    thumb.gestureMaximum = 9.0;
}

InputRoutingResult UiPointerGestureValueTests::send(const InputEvent& event){
    EXPECT_TRUE(m_router.queue(event));
    return m_router.process();
}

bool UiPointerGestureValueTests::publish(const usize count){
    const u64 generation = m_generation + 1u;
    if(!m_router.commitTargets(m_targets.data(), count, generation))
        return false;
    m_generation = generation;
    return true;
}

bool UiPointerGestureValueTests::take(PointerGesture& gesture){
    const HitTarget& thumb = m_targets[1u];
    return m_router.consumePointerGesture(thumb.id, thumb.declarationGeneration, gesture);
}

bool UiPointerGestureValueTests::takeControl(ControlAction& action){
    const HitTarget& host = m_targets[0u];
    return m_router.consumeControlAction(host.id, host.declarationGeneration, host.control, action);
}

void UiPointerGestureValueTests::focusHost(){
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 5.0f, 5.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 5.0f, 5.0f } }).pointerConsumed);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPointerGestureValueTests, AppendedPayloadPreservesExistingAggregatePositionsAndDefaults){
    const PointerGesture legacy{
        { { 2u }, 7u, 13u, 17u }, { 30.0f, 30.0f }, { 40.0f, 30.0f },
        { 20.0f, 20.0f, 40.0f, 20.0f }, { 10.0f, 20.0f, 180.0f, 20.0f },
        PointerGestureState::Completed, {}, { 11u, 21u, 31u }, 9.0, 23u
    };
    EXPECT_EQ(legacy.id.sequence, 17u);
    EXPECT_EQ(legacy.state, PointerGestureState::Completed);
    EXPECT_DOUBLE_EQ(legacy.maximum, 9.0);
    EXPECT_EQ(legacy.updateSequence, 23u);
    EXPECT_EQ(legacy.value, 0u);
    EXPECT_EQ(PointerGesture{}.value, 0u);
}

TEST_F(UiPointerGestureValueTests, InitialPressCopiesAcceptedOpaqueBitsWithoutNumericInterpretation){
    const Array<u64, 6u> values{
        0u, BitCast<u64>(-0.0), BitCast<u64>(Limit<f64>::s_Max),
        0xfedcba9876543210u, 0x7ff8000000000042u, Limit<u64>::s_Max
    };
    for(const u64 value : values){
        SCOPED_TRACE(value);
        m_targets[1u].value = value;
        ASSERT_TRUE(publish());
        m_targets[1u].value = ~value;
        EXPECT_EQ(m_router.targets()[1u].value, value);
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }).pointerConsumed);
        PointerGesture gesture;
        ASSERT_TRUE(take(gesture));
        EXPECT_EQ(gesture.value, value);
        EXPECT_EQ(gesture.state, PointerGestureState::Active);
        EXPECT_EQ(gesture.id.layoutGeneration, m_generation);
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 30.0f, 30.0f } }).pointerConsumed);
        ASSERT_TRUE(take(gesture));
        EXPECT_EQ(gesture.value, value);
        EXPECT_EQ(gesture.state, PointerGestureState::Completed);
        EXPECT_FALSE(take(gesture));
    }
}

TEST_F(UiPointerGestureValueTests, ActiveUpdatesAndOutsideCompletionKeepOriginalPayload){
    ASSERT_TRUE(publish());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }).pointerConsumed);
    PointerGesture gesture;
    ASSERT_TRUE(take(gesture));
    const PointerGesture baseline = gesture;
    EXPECT_EQ(baseline.value, BitCast<u64>(-0.0));
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { 30.0f, 30.0f } }).pointerConsumed);
    EXPECT_FALSE(take(gesture));
    m_targets[1u].value = 91u;
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { 70.0f, 30.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { 110.0f, 50.0f } }).pointerConsumed);
    ASSERT_TRUE(take(gesture));
    EXPECT_EQ(gesture.value, baseline.value);
    EXPECT_EQ(gesture.id.sequence, baseline.id.sequence);
    EXPECT_EQ(gesture.id.layoutGeneration, baseline.id.layoutGeneration);
    EXPECT_GT(gesture.updateSequence, baseline.updateSequence);
    EXPECT_FLOAT_EQ(gesture.origin.x, 30.0f);
    EXPECT_FLOAT_EQ(gesture.position.x, 110.0f);
    EXPECT_FLOAT_EQ(gesture.position.y, 50.0f);
    const u64 moveSequence = gesture.updateSequence;
    EXPECT_FALSE(take(gesture));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 260.0f, 180.0f } }).pointerConsumed);
    ASSERT_TRUE(take(gesture));
    EXPECT_EQ(gesture.value, baseline.value);
    EXPECT_EQ(gesture.id.sequence, baseline.id.sequence);
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_GT(gesture.updateSequence, moveSequence);
    EXPECT_FLOAT_EQ(gesture.position.x, 260.0f);
    EXPECT_FALSE(take(gesture));
    EXPECT_TRUE(m_router.actions().empty());
    EXPECT_TRUE(m_router.controlActions().empty());
}

TEST_F(UiPointerGestureValueTests, RepaintingValueAndThumbGeometryDoesNotReplaceAnActivePressBaseline){
    ASSERT_TRUE(publish());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }).pointerConsumed);
    PointerGesture gesture;
    ASSERT_TRUE(take(gesture));
    const PointerGesture baseline = gesture;
    m_targets[1u].value = BitCast<u64>(Limit<f64>::s_Max);
    m_targets[1u].rectangle = { 120.0f, 20.0f, 40.0f, 20.0f };
    m_targets[1u].gestureReference = { 90.0f, 20.0f, 100.0f, 20.0f };
    m_targets[1u].gestureMaximum = 19.0;
    ASSERT_TRUE(publish());
    EXPECT_EQ(m_router.capture(), m_targets[1u].id);
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    EXPECT_EQ(m_router.targets()[1u].value, m_targets[1u].value);
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { 140.0f, 30.0f } }).pointerConsumed);
    ASSERT_TRUE(take(gesture));
    EXPECT_EQ(gesture.value, baseline.value);
    EXPECT_EQ(gesture.id.sequence, baseline.id.sequence);
    EXPECT_EQ(gesture.id.layoutGeneration, 1u);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.x, 20.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.x, 10.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.width, 180.0f);
    EXPECT_DOUBLE_EQ(gesture.maximum, 9.0);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 150.0f, 30.0f } }).pointerConsumed);
    ASSERT_TRUE(take(gesture));
    EXPECT_EQ(gesture.value, baseline.value);
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 130.0f, 30.0f } }).pointerConsumed);
    ASSERT_TRUE(take(gesture));
    EXPECT_EQ(gesture.value, m_targets[1u].value);
    EXPECT_EQ(gesture.id.layoutGeneration, 2u);
    EXPECT_GT(gesture.id.sequence, baseline.id.sequence);
    EXPECT_FLOAT_EQ(gesture.targetRectangle.x, 120.0f);
    EXPECT_FLOAT_EQ(gesture.referenceRectangle.x, 90.0f);
    EXPECT_DOUBLE_EQ(gesture.maximum, 19.0);
}

TEST_F(UiPointerGestureValueTests, SeveralCompletedPressesRetainTheirOwnPublishedValuesUntilConsumption){
    const Array<u64, 3u> values{ 17u, BitCast<u64>(-0.0), 0xfedcba9876543210u };
    for(usize index = 0u; index < values.size(); ++index){
        m_targets[1u].value = values[index];
        ASSERT_TRUE(publish());
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }).pointerConsumed);
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 35.0f, 30.0f } }).pointerConsumed);
    }
    u64 previousSequence = 0u;
    for(usize index = 0u; index < values.size(); ++index){
        PointerGesture gesture;
        ASSERT_TRUE(take(gesture));
        EXPECT_EQ(gesture.value, values[index]);
        EXPECT_EQ(gesture.id.layoutGeneration, index + 1u);
        EXPECT_EQ(gesture.state, PointerGestureState::Completed);
        EXPECT_GT(gesture.id.sequence, previousSequence);
        previousSequence = gesture.id.sequence;
    }
    PointerGesture gesture;
    EXPECT_FALSE(take(gesture));
}

TEST_F(UiPointerGestureValueTests, CoalescedMoveUsesItsUpdateSequenceAmongOrderedControlActions){
    ASSERT_TRUE(publish());
    focusHost();
    ASSERT_TRUE(m_router.queue({ .type = InputEventType::KeyDown, .key = InputKey::Right }));
    ASSERT_TRUE(m_router.queue({ .type = InputEventType::KeyUp, .key = InputKey::Right }));
    ASSERT_TRUE(m_router.queue({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }));
    ASSERT_TRUE(m_router.queue({ .type = InputEventType::PointerMove, .position = { 50.0f, 30.0f } }));
    ASSERT_TRUE(m_router.queue({ .type = InputEventType::KeyDown, .key = InputKey::Left }));
    ASSERT_TRUE(m_router.queue({ .type = InputEventType::KeyUp, .key = InputKey::Left }));
    ASSERT_TRUE(m_router.queue({ .type = InputEventType::PointerMove, .position = { 80.0f, 30.0f } }));
    const InputRoutingResult result = m_router.process();
    EXPECT_TRUE(result.pointerConsumed);
    EXPECT_TRUE(result.keyboardConsumed);
    ASSERT_EQ(m_router.controlActions().size(), 2u);
    ControlAction right;
    ControlAction left;
    PointerGesture gesture;
    ASSERT_TRUE(takeControl(right));
    ASSERT_TRUE(takeControl(left));
    ASSERT_TRUE(take(gesture));
    EXPECT_EQ(right.kind, ControlActionKind::Right);
    EXPECT_EQ(left.kind, ControlActionKind::Left);
    EXPECT_LT(right.id.sequence, gesture.id.sequence);
    EXPECT_LT(gesture.id.sequence, left.id.sequence);
    EXPECT_LT(left.id.sequence, gesture.updateSequence);
    EXPECT_EQ(gesture.value, BitCast<u64>(-0.0));
    EXPECT_FLOAT_EQ(gesture.position.x, 80.0f);
    EXPECT_FALSE(take(gesture));
    const u64 moveSequence = gesture.updateSequence;
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 80.0f, 30.0f } }).pointerConsumed);
    ASSERT_TRUE(take(gesture));
    EXPECT_GT(gesture.updateSequence, moveSequence);
    EXPECT_EQ(gesture.value, BitCast<u64>(-0.0));
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
}

TEST_F(UiPointerGestureValueTests, RejectedPublicationPreservesAcceptedPayloadAndActiveCapture){
    ASSERT_TRUE(publish());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }).pointerConsumed);
    const HitTarget accepted = m_targets[1u];
    m_targets[1u].value = 91u;
    m_targets[1u].rectangle.width = -1.0f;
    EXPECT_FALSE(publish());
    EXPECT_EQ(m_router.layoutGeneration(), 1u);
    EXPECT_EQ(m_router.capture(), accepted.id);
    EXPECT_EQ(m_router.targets()[1u].value, accepted.value);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 70.0f, 30.0f } }).pointerConsumed);
    PointerGesture gesture;
    ASSERT_TRUE(take(gesture));
    EXPECT_EQ(gesture.value, accepted.value);
    EXPECT_EQ(gesture.id.layoutGeneration, 1u);
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
}

TEST_F(UiPointerGestureValueTests, ReplacedControlTokenRetiresPayloadAndRestorationCannotReviveHeldPress){
    ASSERT_TRUE(publish());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }).pointerConsumed);
    const ControlToken original = m_targets[0u].control;
    for(auto& target : m_targets)
        ++target.control.contentRevision;
    ASSERT_TRUE(publish());
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_TRUE(m_router.primaryDown());
    PointerGesture gesture;
    gesture.value = 91u;
    EXPECT_FALSE(take(gesture));
    EXPECT_EQ(gesture.value, 91u);
    for(auto& target : m_targets)
        target.control = original;
    m_targets[1u].value = 101u;
    ASSERT_TRUE(publish());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { 70.0f, 30.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 70.0f, 30.0f } }).pointerConsumed);
    EXPECT_FALSE(take(gesture));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }).pointerConsumed);
    ASSERT_TRUE(take(gesture));
    EXPECT_EQ(gesture.value, 101u);
    EXPECT_EQ(gesture.id.layoutGeneration, 3u);
}

TEST_F(UiPointerGestureValueTests, DeclarationPolicyClipAndOmissionRetirePayloadBeforeTheirRestoration){
    const HitTarget original = m_targets[1u];
    for(usize variant = 0u; variant < 5u; ++variant){
        SCOPED_TRACE(variant);
        m_targets[1u] = original;
        m_targets[1u].value = variant + 17u;
        ASSERT_TRUE(publish());
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }).pointerConsumed);
        usize count = 2u;
        switch(variant){
        case 0u: ++m_targets[1u].declarationGeneration; break;
        case 1u: m_targets[1u].enabled = false; break;
        case 2u: m_targets[1u].clip = { 220.0f, 150.0f, 20.0f, 20.0f }; break;
        case 3u: m_targets[1u].pointerGesture = false; break;
        case 4u: count = 1u; break;
        }
        ASSERT_TRUE(publish(count));
        PointerGesture gesture;
        EXPECT_FALSE(take(gesture));
        m_targets[1u] = original;
        m_targets[1u].value = variant + 101u;
        ASSERT_TRUE(publish());
        EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { 70.0f, 30.0f } }).pointerConsumed);
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 70.0f, 30.0f } }).pointerConsumed);
        EXPECT_FALSE(take(gesture));
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }).pointerConsumed);
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 30.0f, 30.0f } }).pointerConsumed);
        ASSERT_TRUE(take(gesture));
        EXPECT_EQ(gesture.value, variant + 101u);
        EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    }
}

TEST_F(UiPointerGestureValueTests, NativeCaptureLossDiscardsActivePayloadAndKeepsCompletedPressSnapshot){
    m_targets[1u].value = 17u;
    ASSERT_TRUE(publish());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 35.0f, 30.0f } }).pointerConsumed);
    m_targets[1u].value = 91u;
    ASSERT_TRUE(publish());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PointerCaptureLost }).pointerConsumed);
    EXPECT_FALSE(m_router.capture().valid());
    EXPECT_FALSE(m_router.primaryDown());
    EXPECT_EQ(m_router.focus(), m_targets[0u].id);
    PointerGesture gesture;
    ASSERT_TRUE(take(gesture));
    EXPECT_EQ(gesture.value, 17u);
    EXPECT_EQ(gesture.state, PointerGestureState::Completed);
    const u64 previousSequence = gesture.id.sequence;
    EXPECT_FALSE(take(gesture));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = { 30.0f, 30.0f } }).pointerConsumed);
    ASSERT_TRUE(take(gesture));
    EXPECT_EQ(gesture.value, 91u);
    EXPECT_GT(gesture.id.sequence, previousSequence);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

