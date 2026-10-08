// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/slider.h>
#include <impl/ecs_ui/toolkit/widgets/slider_style.h>

#include <global/simplemath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_behavior_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;


static void ExpectResult(const SliderResult& actual, const SliderResult& expected){
    EXPECT_EQ(actual.valid, expected.valid);
    EXPECT_EQ(actual.valueChanged, expected.valueChanged);
    EXPECT_EQ(actual.focused, expected.focused);
    EXPECT_EQ(actual.dragging, expected.dragging);
}


class UiSliderBehaviorTests : public testing::Test{
protected:
    virtual void SetUp()override{
        ASSERT_TRUE(refresh());
    }

    [[nodiscard]] bool refresh(){
        const auto metrics = SliderLayout::Measure(m_options, {});
        if(!metrics)
            return false;
        const auto normalized = SliderBehavior::Normalize(m_options.minimum, m_options.maximum, m_state.value());
        if(!normalized)
            return false;
        const auto placement = SliderLayout::Place(m_bounds, m_bounds, *metrics, *normalized);
        if(!placement)
            return false;
        m_placement = *placement;
        return SliderBehavior::Admit(m_state, m_options, m_placement);
    }

    [[nodiscard]] bool set(const f64 value){
        return m_state.setValue(value) && refresh();
    }

    [[nodiscard]] ControlAction key(const ControlActionKind::Enum kind, const u64 sequence = 1u)const{
        ControlAction action;
        action.id = { { 41u }, 3u, 7u, sequence };
        action.control = m_state.controlToken();
        action.kind = kind;
        return action;
    }

    [[nodiscard]] PointerGesture gesture(const bool thumb = true)const{
        PointerGesture gesture;
        gesture.id = { { thumb ? 42u : 43u }, 3u, 7u, 11u };
        gesture.origin = { m_placement.thumb.x + m_placement.thumb.width * 0.5f, 36.0f };
        gesture.position = gesture.origin;
        gesture.targetRectangle = thumb ? m_placement.thumb : m_placement.track;
        gesture.referenceRectangle = thumb ? m_placement.travelBounds : m_placement.centerTravel;
        gesture.control = m_state.controlToken();
        gesture.updateSequence = 12u;
        gesture.value = BitCast<u64>(m_state.value());
        return gesture;
    }


protected:
    SliderState m_state;
    SliderOptions m_options;
    SliderPlacement m_placement;
    Rect m_bounds = { 10.0f, 20.0f, 240.0f, 32.0f };
    SliderResult m_result;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiSliderMathTests, OutOfRangeAndConstantSpansClampNormalization){
    Expected<f64> normalized = MakeUnexpected(Failure{});
    normalized = SliderBehavior::Normalize(10.0, 30.0, -100.0);
    ASSERT_TRUE(normalized);
    EXPECT_EQ(*normalized, 0.0);
    normalized = SliderBehavior::Normalize(10.0, 30.0, 100.0);
    ASSERT_TRUE(normalized);
    EXPECT_EQ(*normalized, 1.0);
    normalized = SliderBehavior::Normalize(10.0, 10.0, 100.0);
    ASSERT_TRUE(normalized);
    EXPECT_EQ(*normalized, 0.0);
}

TEST(UiSliderMathTests, OppositeExtremeBoundsNormalizeAndInterpolateWithoutAnOverflowingSpan){
    const f64 maximum = Limit<f64>::s_Max;
    Expected<f64> value = MakeUnexpected(Failure{});
    value = SliderBehavior::Normalize(-maximum, maximum, 0.0);
    ASSERT_TRUE(value);
    EXPECT_DOUBLE_EQ(*value, 0.5);
    value = SliderBehavior::Normalize(-maximum, maximum, maximum * 0.5);
    ASSERT_TRUE(value);
    EXPECT_DOUBLE_EQ(*value, 0.75);
    value = SliderBehavior::Interpolate(-maximum, maximum, 0.25);
    ASSERT_TRUE(value);
    EXPECT_TRUE(IsFinite(*value));
    EXPECT_DOUBLE_EQ(*value / maximum, -0.5);
    value = SliderBehavior::Interpolate(-maximum, maximum, 0.75);
    ASSERT_TRUE(value);
    EXPECT_DOUBLE_EQ(*value / maximum, 0.5);
}

TEST(UiSliderMathTests, SameSignExtremeBoundsUseABoundedDifference){
    const f64 maximum = Limit<f64>::s_Max;
    for(const bool negative : { false, true }){
        const f64 minimum = negative ? -maximum : maximum * 0.75;
        const f64 upper = negative ? -maximum * 0.75 : maximum;
        Expected<f64> value = MakeUnexpected(Failure{});
        value = SliderBehavior::Interpolate(minimum, upper, 0.5);
        ASSERT_TRUE(value);
        EXPECT_TRUE(IsFinite(*value));
        EXPECT_GE(*value, minimum);
        EXPECT_LE(*value, upper);
        Expected<f64> normalized = MakeUnexpected(Failure{});
        normalized = SliderBehavior::Normalize(minimum, upper, *value);
        ASSERT_TRUE(normalized);
        EXPECT_NEAR(*normalized, 0.5, 1.0e-15);
    }
}

TEST(UiSliderMathTests, TinyAndAdjacentSpansStayInTheOrdinaryNormalizationPath){
    const f64 tiny = BitCast<f64>(1ull);
    Expected<f64> normalized = MakeUnexpected(Failure{});
    normalized = SliderBehavior::Normalize(-tiny, tiny, 0.0);
    ASSERT_TRUE(normalized);
    EXPECT_DOUBLE_EQ(*normalized, 0.5);
    normalized = SliderBehavior::Normalize(tiny, BitCast<f64>(2ull), tiny);
    ASSERT_TRUE(normalized);
    EXPECT_DOUBLE_EQ(*normalized, 0.0);
    const f64 lower = BitCast<f64>(0x3ff0000000000000ull);
    const f64 upper = BitCast<f64>(0x3ff0000000000001ull);
    normalized = SliderBehavior::Normalize(lower, upper, upper);
    ASSERT_TRUE(normalized);
    EXPECT_DOUBLE_EQ(*normalized, 1.0);
    Expected<f64> value = MakeUnexpected(Failure{});
    value = SliderBehavior::Interpolate(lower, upper, 1.0);
    ASSERT_TRUE(value);
    EXPECT_EQ(BitCast<u64>(*value), BitCast<u64>(upper));
}

TEST(UiSliderMathTests, InterpolationPreservesExactEndpointAndSignedZeroBits){
    Expected<f64> value = MakeUnexpected(Failure{});
    value = SliderBehavior::Interpolate(-0.0, 1.0, 0.0);
    ASSERT_TRUE(value);
    EXPECT_EQ(BitCast<u64>(*value), 0x8000000000000000ull);
    value = SliderBehavior::Interpolate(-1.0, 0.0, 1.0);
    ASSERT_TRUE(value);
    EXPECT_EQ(BitCast<u64>(*value), 0u);
    value = SliderBehavior::Interpolate(-0.0, 0.0, 0.0);
    ASSERT_TRUE(value);
    EXPECT_EQ(BitCast<u64>(*value), 0x8000000000000000ull);
    value = SliderBehavior::Interpolate(-0.0, 0.0, 1.0);
    ASSERT_TRUE(value);
    EXPECT_EQ(BitCast<u64>(*value), 0u);
}

TEST(UiSliderMathTests, InvalidRangeValueOrNormalizedInputRejectsTheQuery){
    for(u32 field = 0u; field < 7u; ++field){
        f64 minimum = 0.0;
        f64 maximum = 1.0;
        f64 query = 0.5;
        switch(field){
        case 0u: minimum = 2.0; break;
        case 1u: minimum = Limit<f64>::s_QuietNaN; break;
        case 2u: maximum = Limit<f64>::s_Infinity; break;
        case 3u: query = Limit<f64>::s_Infinity; break;
        case 4u: query = Limit<f64>::s_QuietNaN; break;
        case 5u: query = -0.1; break;
        default: query = 1.1; break;
        }
        if(field < 5u){
            EXPECT_FALSE(SliderBehavior::Normalize(minimum, maximum, query));
        }
        EXPECT_FALSE(SliderBehavior::Interpolate(minimum, maximum, query));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiSliderBehaviorTests, AdmissionKeepsAnExternalOutOfRangeValueUntilAcceptedInput){
    ASSERT_TRUE(set(20.0));
    EXPECT_EQ(m_state.value(), 20.0);
    EXPECT_FLOAT_EQ(m_placement.thumb.x, 222.0f);
    ASSERT_TRUE(SliderBehavior::Apply(m_state, m_options, key(ControlActionKind::Left), m_result));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.99);
    EXPECT_TRUE(m_result.valueChanged);
}

TEST_F(UiSliderBehaviorTests, PolicyAndStableGeometryChangesRetirePreviouslyCopiedInput){
    const ControlAction stale = key(ControlActionKind::Right);
    const u64 input = m_state.inputGeneration();
    const u64 first = m_state.admissionGeneration();
    m_options.keyStep = 0.1;
    ASSERT_TRUE(refresh());
    EXPECT_GT(m_state.admissionGeneration(), first);
    EXPECT_EQ(m_state.inputGeneration(), input);
    const SliderSnapshot before = m_state.snapshot();
    const SliderResult result = m_result;
    EXPECT_FALSE(SliderBehavior::Apply(m_state, m_options, stale, m_result));
    EXPECT_TRUE(m_state.matches(before));
    ExpectResult(m_result, result);
    const u64 policy = m_state.admissionGeneration();
    m_bounds.width = 280.0f;
    ASSERT_TRUE(refresh());
    EXPECT_GT(m_state.admissionGeneration(), policy);
    SliderPlacement clipped = m_placement;
    clipped.clip.width = 100.0f;
    const u64 geometry = m_state.admissionGeneration();
    ASSERT_TRUE(SliderBehavior::Admit(m_state, m_options, clipped));
    EXPECT_GT(m_state.admissionGeneration(), geometry);
}

TEST_F(UiSliderBehaviorTests, ExactSignedZeroPolicyChangesAdvanceAdmissionWithoutChangingTheValue){
    const SliderSnapshot before = m_state.snapshot();
    m_options.minimum = -0.0;
    ASSERT_TRUE(refresh());
    EXPECT_GT(m_state.admissionGeneration(), before.admissionGeneration);
    EXPECT_EQ(BitCast<u64>(m_state.value()), before.valueBits);
    const u64 range = m_state.admissionGeneration();
    m_options.keyStep = -0.0;
    ASSERT_TRUE(refresh());
    EXPECT_GT(m_state.admissionGeneration(), range);
}

TEST_F(UiSliderBehaviorTests, PublicIdenticalAndAwayBackIntentsRejectOldCopiedActionsAtomically){
    ASSERT_TRUE(set(0.25));
    const ControlAction stale = key(ControlActionKind::Right);
    ASSERT_TRUE(m_state.setValue(0.25));
    SliderSnapshot before = m_state.snapshot();
    EXPECT_FALSE(SliderBehavior::Apply(m_state, m_options, stale, m_result));
    EXPECT_TRUE(m_state.matches(before));
    EXPECT_FALSE(m_result.valid);
    ASSERT_TRUE(m_state.setValue(0.5));
    ASSERT_TRUE(m_state.setValue(0.25));
    before = m_state.snapshot();
    EXPECT_FALSE(SliderBehavior::Apply(m_state, m_options, stale, m_result));
    EXPECT_TRUE(m_state.matches(before));
    EXPECT_FALSE(m_result.valid);
}

TEST_F(UiSliderBehaviorTests, AutomaticStepsRemainFiniteAcrossOppositeExtremeBounds){
    m_options.minimum = -Limit<f64>::s_Max;
    m_options.maximum = Limit<f64>::s_Max;
    ASSERT_TRUE(set(0.0));
    ASSERT_TRUE(SliderBehavior::Apply(m_state, m_options, key(ControlActionKind::Right), m_result));
    EXPECT_TRUE(IsFinite(m_state.value()));
    EXPECT_GT(m_state.value(), 0.0);
    EXPECT_NEAR(m_state.value() / Limit<f64>::s_Max, 0.02, 1.0e-15);
    ASSERT_TRUE(SliderBehavior::Apply(m_state, m_options, key(ControlActionKind::PageUp), m_result));
    EXPECT_TRUE(IsFinite(m_state.value()));
    EXPECT_NEAR(m_state.value() / Limit<f64>::s_Max, 0.22, 1.0e-15);
}

TEST_F(UiSliderBehaviorTests, AutomaticSubnormalStepUsesTheRepresentableSpanWhenDivisionUnderflows){
    m_options.maximum = BitCast<f64>(1ull);
    ASSERT_TRUE(refresh());
    ASSERT_TRUE(SliderBehavior::Apply(m_state, m_options, key(ControlActionKind::Right), m_result));
    EXPECT_EQ(BitCast<u64>(m_state.value()), 1u);
    ASSERT_TRUE(SliderBehavior::Apply(m_state, m_options, key(ControlActionKind::Left), m_result));
    EXPECT_EQ(BitCast<u64>(m_state.value()), 0u);
}

TEST_F(UiSliderBehaviorTests, ExplicitExtremeStepsAndPageIterationSaturateWithoutOverflow){
    m_options.minimum = -Limit<f64>::s_Max;
    m_options.maximum = Limit<f64>::s_Max;
    m_options.keyStep = Limit<f64>::s_Max;
    ASSERT_TRUE(refresh());
    ASSERT_TRUE(SliderBehavior::Apply(m_state, m_options, key(ControlActionKind::PageUp), m_result));
    EXPECT_EQ(BitCast<u64>(m_state.value()), BitCast<u64>(m_options.maximum));
    ASSERT_TRUE(SliderBehavior::Apply(m_state, m_options, key(ControlActionKind::PageDown), m_result));
    EXPECT_EQ(BitCast<u64>(m_state.value()), BitCast<u64>(m_options.minimum));
    ASSERT_TRUE(SliderBehavior::Apply(m_state, m_options, key(ControlActionKind::Right), m_result));
    EXPECT_EQ(m_state.value(), 0.0);
}

TEST_F(UiSliderBehaviorTests, DisabledAndConstantRangesAreValidWithoutApplyingInput){
    m_options.enabled = false;
    ASSERT_TRUE(set(0.25));
    SliderSnapshot before = m_state.snapshot();
    ASSERT_TRUE(SliderBehavior::Apply(m_state, m_options, key(ControlActionKind::Right), m_result));
    EXPECT_TRUE(m_state.matches(before));
    EXPECT_TRUE(m_result.valid);
    EXPECT_FALSE(m_result.valueChanged);
    m_options.enabled = true;
    m_options.minimum = 5.0;
    m_options.maximum = 5.0;
    ASSERT_TRUE(refresh());
    before = m_state.snapshot();
    ASSERT_TRUE(SliderBehavior::Seek(m_state, m_options, gesture(false), m_result));
    EXPECT_TRUE(m_state.matches(before));
    EXPECT_EQ(m_state.value(), 0.25);
    EXPECT_FALSE(m_result.dragging);
}

TEST_F(UiSliderBehaviorTests, SubmitActivationAndWheelLeaveTheValueAndSemanticSnapshotUnchanged){
    ASSERT_TRUE(set(-0.0));
    const SliderSnapshot before = m_state.snapshot();
    for(const auto kind : { ControlActionKind::Submit, ControlActionKind::Activate, ControlActionKind::Wheel }){
        ASSERT_TRUE(SliderBehavior::Apply(m_state, m_options, key(kind), m_result));
        EXPECT_TRUE(m_state.matches(before));
        EXPECT_TRUE(m_result.valid);
        EXPECT_FALSE(m_result.valueChanged);
    }
}

TEST_F(UiSliderBehaviorTests, InvalidPolicyTokenAndUnadmittedInputPreserveStateAndResult){
    const SliderSnapshot before = m_state.snapshot();
    const SliderResult result{ true, true, true, true };
    m_result = result;
    ControlAction action = key(ControlActionKind::Right);
    ++action.control.contentGeneration;
    EXPECT_FALSE(SliderBehavior::Apply(m_state, m_options, action, m_result));
    EXPECT_TRUE(m_state.matches(before));
    ExpectResult(m_result, result);
    SliderOptions invalid = m_options;
    invalid.keyStep = -1.0;
    EXPECT_FALSE(SliderBehavior::Apply(m_state, invalid, key(ControlActionKind::Right), m_result));
    EXPECT_TRUE(m_state.matches(before));
    ExpectResult(m_result, result);
    SliderState unadmitted;
    action.control = unadmitted.controlToken();
    const SliderSnapshot unknown = unadmitted.snapshot();
    EXPECT_FALSE(SliderBehavior::Apply(unadmitted, m_options, action, m_result));
    EXPECT_TRUE(unadmitted.matches(unknown));
    ExpectResult(m_result, result);
}

TEST_F(UiSliderBehaviorTests, InvalidAdmissionGeometryOrOptionsPreserveThePreviousBinding){
    const SliderSnapshot before = m_state.snapshot();
    SliderPlacement invalid = m_placement;
    invalid.track.width = Limit<f32>::s_Infinity;
    EXPECT_FALSE(SliderBehavior::Admit(m_state, m_options, invalid));
    EXPECT_TRUE(m_state.matches(before));
    SliderOptions policy = m_options;
    policy.minimum = 2.0;
    EXPECT_FALSE(SliderBehavior::Admit(m_state, policy, m_placement));
    EXPECT_TRUE(m_state.matches(before));
    invalid = m_placement;
    invalid.thumbExtent.x = invalid.travelBounds.width + 1.0f;
    EXPECT_FALSE(SliderBehavior::Admit(m_state, m_options, invalid));
    EXPECT_TRUE(m_state.matches(before));
}

TEST_F(UiSliderBehaviorTests, TrackSeekUsesAbsoluteAcceptedCenterTravelAndExactEndpoints){
    PointerGesture seek = gesture(false);
    seek.position.x = -Limit<f32>::s_Max;
    ASSERT_TRUE(SliderBehavior::Seek(m_state, m_options, seek, m_result));
    EXPECT_EQ(BitCast<u64>(m_state.value()), BitCast<u64>(m_options.minimum));
    seek.position.x = Limit<f32>::s_Max;
    seek.state = PointerGestureState::Completed;
    ASSERT_TRUE(SliderBehavior::Seek(m_state, m_options, seek, m_result));
    EXPECT_EQ(BitCast<u64>(m_state.value()), BitCast<u64>(m_options.maximum));
    EXPECT_FALSE(m_result.dragging);
}

TEST_F(UiSliderBehaviorTests, TrackSeekRejectsAChangedCopiedReferenceWithoutPublishingResult){
    PointerGesture seek = gesture(false);
    seek.referenceRectangle.x += 1.0f;
    const SliderSnapshot before = m_state.snapshot();
    const SliderResult result = m_result;
    EXPECT_FALSE(SliderBehavior::Seek(m_state, m_options, seek, m_result));
    EXPECT_TRUE(m_state.matches(before));
    ExpectResult(m_result, result);
}

TEST_F(UiSliderBehaviorTests, RepeatedThumbUpdatesUseOriginalCopiedBaselineWithoutAccumulatingDelta){
    ASSERT_TRUE(set(0.25));
    PointerGesture drag = gesture();
    drag.position.x += (drag.referenceRectangle.width - drag.targetRectangle.width) * 0.25f;
    ASSERT_TRUE(SliderBehavior::Drag(m_state, m_options, drag, m_result));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.5);
    drag.position.x += (drag.referenceRectangle.width - drag.targetRectangle.width) * 0.25f;
    ++drag.updateSequence;
    ASSERT_TRUE(SliderBehavior::Drag(m_state, m_options, drag, m_result));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.75);
}

TEST_F(UiSliderBehaviorTests, AStationaryThumbCompletionPreservesAnOrderedKeyChange){
    m_options.keyStep = 0.125;
    ASSERT_TRUE(set(0.25));
    PointerGesture drag = gesture();
    ASSERT_TRUE(SliderBehavior::Drag(m_state, m_options, drag, m_result));
    EXPECT_FALSE(m_result.valueChanged);
    ASSERT_TRUE(SliderBehavior::Apply(m_state, m_options, key(ControlActionKind::Right, 13u), m_result));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.375);
    drag.state = PointerGestureState::Completed;
    ++drag.updateSequence;
    ASSERT_TRUE(SliderBehavior::Drag(m_state, m_options, drag, m_result));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.375);
    EXPECT_FALSE(m_result.dragging);
    EXPECT_FALSE(m_state.snapshot().press.valid());
}

TEST_F(UiSliderBehaviorTests, ReturningAfterAnAppliedMoveRestoresTheExactSignedZeroBaseline){
    m_options.minimum = -1.0;
    ASSERT_TRUE(set(-0.0));
    PointerGesture drag = gesture();
    drag.position.x += (drag.referenceRectangle.width - drag.targetRectangle.width) * 0.25f;
    ASSERT_TRUE(SliderBehavior::Drag(m_state, m_options, drag, m_result));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.5);
    EXPECT_TRUE(m_state.snapshot().pressMoved);
    drag.position = drag.origin;
    drag.state = PointerGestureState::Completed;
    ++drag.updateSequence;
    ASSERT_TRUE(SliderBehavior::Drag(m_state, m_options, drag, m_result));
    EXPECT_EQ(BitCast<u64>(m_state.value()), 0x8000000000000000ull);
    EXPECT_FALSE(m_state.snapshot().pressMoved);
}

TEST_F(UiSliderBehaviorTests, ANewStationaryPressDoesNotBorrowTheOldPressMovedFlag){
    ASSERT_TRUE(set(0.25));
    PointerGesture first = gesture();
    first.position.x += 52.0f;
    ASSERT_TRUE(SliderBehavior::Drag(m_state, m_options, first, m_result));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.5);
    PointerGesture second = first;
    ++second.id.sequence;
    ++second.updateSequence;
    second.position = second.origin;
    second.state = PointerGestureState::Completed;
    ASSERT_TRUE(SliderBehavior::Drag(m_state, m_options, second, m_result));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.5);
}

TEST_F(UiSliderBehaviorTests, AdmissionRetirementEndsThePressAndKeepsItsLastAppliedValue){
    ASSERT_TRUE(set(0.25));
    PointerGesture drag = gesture();
    drag.position.x += 52.0f;
    ASSERT_TRUE(SliderBehavior::Drag(m_state, m_options, drag, m_result));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.5);
    m_options.enabled = false;
    ASSERT_TRUE(refresh());
    EXPECT_DOUBLE_EQ(m_state.value(), 0.5);
    EXPECT_FALSE(m_state.snapshot().press.valid());
    EXPECT_FALSE(m_state.snapshot().pressMoved);
    const SliderSnapshot before = m_state.snapshot();
    EXPECT_FALSE(SliderBehavior::Drag(m_state, m_options, drag, m_result));
    EXPECT_TRUE(m_state.matches(before));
}

TEST_F(UiSliderBehaviorTests, ThumbDisplacementCastsBeforeSubtractingExtremeFloatCoordinates){
    PointerGesture drag = gesture();
    drag.origin.x = -Limit<f32>::s_Max;
    drag.position.x = Limit<f32>::s_Max;
    ASSERT_TRUE(SliderBehavior::Drag(m_state, m_options, drag, m_result));
    EXPECT_EQ(BitCast<u64>(m_state.value()), BitCast<u64>(m_options.maximum));
    drag.origin.x = Limit<f32>::s_Max;
    drag.position.x = -Limit<f32>::s_Max;
    ++drag.updateSequence;
    ASSERT_TRUE(SliderBehavior::Drag(m_state, m_options, drag, m_result));
    EXPECT_EQ(BitCast<u64>(m_state.value()), BitCast<u64>(m_options.minimum));
}

TEST_F(UiSliderBehaviorTests, InvalidThumbPayloadGeometryOrTokenIsRejectedAtomically){
    ASSERT_TRUE(set(-0.0));
    const SliderSnapshot before = m_state.snapshot();
    m_result = { true, true, true, true };
    const SliderResult result = m_result;
    for(u32 field = 0u; field < 7u; ++field){
        PointerGesture drag = gesture();
        switch(field){
        case 0u: drag.value = BitCast<u64>(Limit<f64>::s_QuietNaN); break;
        case 1u: ++drag.control.instanceGeneration; break;
        case 2u: drag.targetRectangle.width += 1.0f; break;
        case 3u: drag.referenceRectangle.x += 1.0f; break;
        case 4u: drag.position.x = Limit<f32>::s_Infinity; break;
        case 5u: drag.id.sequence = 0u; break;
        default: drag.updateSequence = 0u; break;
        }
        EXPECT_FALSE(SliderBehavior::Drag(m_state, m_options, drag, m_result));
        EXPECT_TRUE(m_state.matches(before));
        ExpectResult(m_result, result);
    }
}

TEST_F(UiSliderBehaviorTests, AClampedNonzeroMoveStillRestoresItsBaselineWhenReturningAfterAKeyChange){
    m_options.keyStep = 0.125;
    ASSERT_TRUE(set(1.0));
    PointerGesture drag = gesture();
    drag.position.x += 20.0f;
    ASSERT_TRUE(SliderBehavior::Drag(m_state, m_options, drag, m_result));
    EXPECT_FALSE(m_result.valueChanged);
    EXPECT_TRUE(m_state.snapshot().pressMoved);
    ASSERT_TRUE(SliderBehavior::Apply(m_state, m_options, key(ControlActionKind::Left), m_result));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.875);
    drag.position = drag.origin;
    drag.state = PointerGestureState::Completed;
    ASSERT_TRUE(SliderBehavior::Drag(m_state, m_options, drag, m_result));
    EXPECT_DOUBLE_EQ(m_state.value(), 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

