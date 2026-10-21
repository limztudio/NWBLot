// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "list_builder_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/selectable_paint.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_builder_scrollbar_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;
using namespace NWB::UiListTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiListBuilderTests, ThumbUsesItsAcceptedTrackMaximumWhenCandidateHeightChanges){
    ASSERT_TRUE(accept(1u));
    const WidgetId thumb = UiTestSupport::RequireWidgetId(MakeWidgetId(host(), s_ScrollbarKey));
    ASSERT_NE(target(thumb), nullptr);
    const HitTarget accepted = *target(thumb);
    const Point origin = Center(accepted.rectangle);
    const Point destination{ origin.x, VectorGetX(VectorAdd(VectorReplicate(origin.y), VectorReplicate(10.0f))) };
    drag(origin, destination);
    ListOptions resized = Options();
    resized.height.value = 200.0f;
    ASSERT_TRUE(prepare(2u, resized));
    const SIMDVectorDouble travel = VectorDoubleSubtract(VectorDoubleReplicate(static_cast<f64>(accepted.gestureReference.height)), VectorDoubleReplicate(static_cast<f64>(accepted.rectangle.height)));
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleDivide(VectorDoubleReplicate(10.0), travel), VectorDoubleReplicate(accepted.gestureMaximum))));
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FLOAT_EQ(target(thumb)->gestureReference.height, 190.0f);
}

TEST_F(UiListBuilderTests, LatestThumbMotionFollowsAnInterveningWheelInNativeInputOrder){
    ASSERT_TRUE(accept(1u));
    const HitTarget accepted = *target(UiTestSupport::RequireWidgetId(MakeWidgetId(host(), s_ScrollbarKey)));
    const Point origin = Center(accepted.rectangle);
    const SIMDVectorSingle trackEnd = VectorAdd(VectorReplicate(accepted.gestureReference.y), VectorReplicate(accepted.gestureReference.height));
    const Point destination{ origin.x, VectorGetX(VectorNegativeMultiplySubtractExpression(VectorReplicate(accepted.rectangle.height), VectorReplicate(0.5f), trackEnd)) };
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = origin }).pointerConsumed);
    InputEvent event;
    event.type = InputEventType::PointerWheel;
    event.position = origin;
    event.scrollY = 1.0;
    EXPECT_TRUE(send(event).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = destination }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = destination }).pointerConsumed);
    ASSERT_TRUE(prepare(2u));
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), accepted.gestureMaximum);
    EXPECT_EQ(m_state.placement().endRow, 100000u);
}

TEST_F(UiListBuilderTests, EmptyDatasetKeepsABoundedPageCountWithoutReadingText){
    m_source.count = 0u;
    ListOptions options = Options();
    ASSERT_TRUE(accept(1u, options));
    ASSERT_NE(target(host()), nullptr);
    EXPECT_EQ(target(host())->pageRows, 1u);
    EXPECT_EQ(m_source.textCalls, 0u);
}

TEST_F(UiListBuilderTests, SmallDatasetBoundsPageSizeByRows){
    m_source.count = 2u;
    ListOptions options = Options();
    ASSERT_TRUE(accept(1u, options));
    ASSERT_NE(target(host()), nullptr);
    EXPECT_EQ(target(host())->pageRows, 2u);
    EXPECT_EQ(m_source.textCalls, 2u);
    EXPECT_EQ(m_state.placement().endRow, 2u);
}

TEST_F(UiListBuilderTests, PopupListsAutofocusAndRestoreEachCoveredListHost){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    EXPECT_EQ(m_context.input().focus(), host());
    PopupState lowerPopup;
    PopupState upperPopup;
    ListState lowerState;
    ListState upperState;
    lowerState.select(2u);
    upperState.select(3u);
    lowerPopup.open();
    ASSERT_TRUE(preparePopups(2u, lowerPopup, lowerState));
    ASSERT_TRUE(m_context.commitFrame(2u));
    const WidgetId lowerHost = id(s_ListKey, s_PopupKey);
    EXPECT_EQ(m_context.input().focus(), lowerHost);
    upperPopup.open();
    ASSERT_TRUE(preparePopups(3u, lowerPopup, lowerState, &upperPopup, &upperState));
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_EQ(m_context.input().focus(), id(s_ListKey, s_UpperPopupKey));
    press(Core::Key::Down);
    ASSERT_TRUE(preparePopups(4u, lowerPopup, lowerState, &upperPopup, &upperState));
    EXPECT_EQ(upperState.selectedKey(), 4u);
    EXPECT_EQ(lowerState.selectedKey(), 2u);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    ASSERT_TRUE(m_context.commitFrame(4u));
    upperPopup.close();
    ASSERT_TRUE(preparePopups(5u, lowerPopup, lowerState, &upperPopup, &upperState));
    ASSERT_TRUE(m_context.commitFrame(5u));
    EXPECT_EQ(m_context.input().focus(), lowerHost);
    press(Core::Key::Down);
    ASSERT_TRUE(preparePopups(6u, lowerPopup, lowerState, &upperPopup, &upperState));
    EXPECT_EQ(lowerState.selectedKey(), 3u);
    ASSERT_TRUE(m_context.commitFrame(6u));
    lowerPopup.close();
    ASSERT_TRUE(preparePopups(7u, lowerPopup, lowerState));
    ASSERT_TRUE(m_context.commitFrame(7u));
    EXPECT_EQ(m_context.input().focus(), host());
    press(Core::Key::Down);
    ASSERT_TRUE(prepare(8u));
    EXPECT_EQ(m_state.selectedKey(), 2u);
}

TEST_F(UiListBuilderTests, TwoCompletedThumbDragsAreConsumedInOneDeclarationUsingTheirOwnBaseline){
    ASSERT_TRUE(accept(1u));
    const WidgetId thumb = UiTestSupport::RequireWidgetId(MakeWidgetId(host(), s_ScrollbarKey));
    ASSERT_NE(target(thumb), nullptr);
    const HitTarget accepted = *target(thumb);
    const Point origin = Center(accepted.rectangle);
    const SIMDVectorSingle destinationY = VectorAdd(VectorReplicate(origin.y), VectorSet(10.0f, 20.0f, 0.0f, 0.0f));
    drag(origin, { origin.x, VectorGetX(destinationY) });
    drag(origin, { origin.x, VectorGetY(destinationY) });
    ASSERT_TRUE(prepare(2u));
    const SIMDVectorDouble travel = VectorDoubleSubtract(VectorDoubleReplicate(static_cast<f64>(accepted.gestureReference.height)), VectorDoubleReplicate(static_cast<f64>(accepted.rectangle.height)));
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleDivide(VectorDoubleReplicate(20.0), travel), VectorDoubleReplicate(accepted.gestureMaximum))));
    EXPECT_FALSE(m_context.input().consumePointerGesture(thumb, accepted.declarationGeneration));
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), host());
}

TEST_F(UiListBuilderTests, FullHeightMinimumThumbFocusesItsHostWithoutStartingAnUnusableGesture){
    ListOptions options = Options();
    options.height.value = 20.0f;
    ASSERT_TRUE(accept(1u, options));
    const WidgetId thumb = UiTestSupport::RequireWidgetId(MakeWidgetId(host(), s_ScrollbarKey));
    ASSERT_NE(target(thumb), nullptr);
    const HitTarget accepted = *target(thumb);
    EXPECT_FLOAT_EQ(m_state.placement().track.height, m_state.placement().thumb.height);
    EXPECT_FALSE(accepted.pointerGesture);
    click(Center(accepted.rectangle));
    EXPECT_EQ(m_context.input().focus(), host());
    EXPECT_FALSE(m_context.input().consumePointerGesture(thumb, accepted.declarationGeneration));
    ASSERT_TRUE(prepare(2u, options));
    EXPECT_TRUE(m_result.valid);
    EXPECT_TRUE(m_result.focused);
    EXPECT_FALSE(m_result.activated);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 0.0);
    EXPECT_FALSE(m_context.failed());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

