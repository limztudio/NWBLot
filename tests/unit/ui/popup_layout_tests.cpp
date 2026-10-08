// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/popup.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_popup_layout_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

static_assert(!IsConstructible_V<PopupState, const PopupState&>);
static_assert(!IsConstructible_V<PopupState, PopupState&&>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ExpectBounds(const Rect& actual, const Rect& expected){
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.width, expected.width);
    EXPECT_FLOAT_EQ(actual.height, expected.height);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiPopupLayoutTests, BottomEdgeFlipsAboveAndClampsHorizontalOverflow){
    PopupOptions options;
    options.anchor = { 730.0f, 560.0f, 50.0f, 24.0f };
    options.size = { 220.0f, 160.0f };
    PopupPlacement placement;
    const auto placementResult = PopupLayout::Place(options, { 800.0f, 600.0f, 1.0f, 1.0f });
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    ExpectBounds(placement.bounds, { 580.0f, 396.0f, 220.0f, 160.0f });
    EXPECT_EQ(placement.side, PopupPlacementSide::Above);
}

TEST(UiPopupLayoutTests, AbovePreferenceFlipsBelowAtTopEdge){
    PopupOptions options;
    options.anchor = { 40.0f, 12.0f, 60.0f, 30.0f };
    options.side = PopupPlacementSide::Above;
    options.size = { 140.0f, 90.0f };
    PopupPlacement placement;
    const auto placementResult = PopupLayout::Place(options, { 400.0f, 300.0f, 1.0f, 1.0f });
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    ExpectBounds(placement.bounds, { 40.0f, 46.0f, 140.0f, 90.0f });
    EXPECT_EQ(placement.side, PopupPlacementSide::Below);
}

TEST(UiPopupLayoutTests, RightPreferenceFlipsLeftAndClampsVerticalOverflow){
    PopupOptions options;
    options.anchor = { 370.0f, 260.0f, 20.0f, 30.0f };
    options.side = PopupPlacementSide::Right;
    options.size = { 140.0f, 90.0f };
    PopupPlacement placement;
    const auto placementResult = PopupLayout::Place(options, { 400.0f, 300.0f, 1.0f, 1.0f });
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    ExpectBounds(placement.bounds, { 226.0f, 210.0f, 140.0f, 90.0f });
    EXPECT_EQ(placement.side, PopupPlacementSide::Left);
}

TEST(UiPopupLayoutTests, LeftPreferenceFlipsRightAtLeftEdge){
    PopupOptions options;
    options.anchor = { 12.0f, 40.0f, 30.0f, 24.0f };
    options.side = PopupPlacementSide::Left;
    options.size = { 140.0f, 90.0f };
    PopupPlacement placement;
    const auto placementResult = PopupLayout::Place(options, { 400.0f, 300.0f, 1.0f, 1.0f });
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    ExpectBounds(placement.bounds, { 46.0f, 40.0f, 140.0f, 90.0f });
    EXPECT_EQ(placement.side, PopupPlacementSide::Right);
}

TEST(UiPopupLayoutTests, NeitherVerticalSideFitsUsesMoreRoomAndKeepsCompletePopupVisible){
    PopupOptions options;
    options.anchor = { 100.0f, 150.0f, 20.0f, 20.0f };
    options.size = { 80.0f, 200.0f };
    PopupPlacement placement;
    const auto placementResult = PopupLayout::Place(options, { 320.0f, 240.0f, 1.0f, 1.0f });
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    ExpectBounds(placement.bounds, { 100.0f, 0.0f, 80.0f, 200.0f });
    EXPECT_EQ(placement.side, PopupPlacementSide::Above);
}

TEST(UiPopupLayoutTests, EqualAvailableRoomPreservesTheRequestedSide){
    PopupOptions options;
    options.anchor = { 100.0f, 100.0f, 20.0f, 40.0f };
    options.side = PopupPlacementSide::Above;
    options.size = { 80.0f, 150.0f };
    options.gap = 0.0f;
    PopupPlacement placement;
    const auto placementResult = PopupLayout::Place(options, { 320.0f, 240.0f, 1.0f, 1.0f });
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    ExpectBounds(placement.bounds, { 100.0f, 0.0f, 80.0f, 150.0f });
    EXPECT_EQ(placement.side, PopupPlacementSide::Above);
}

TEST(UiPopupLayoutTests, OversizedPopupShrinksToTheViewportOnBothAxes){
    PopupOptions options;
    options.anchor = { 40.0f, 40.0f, 20.0f, 20.0f };
    options.size = { 1000.0f, 900.0f };
    PopupPlacement placement;
    const auto placementResult = PopupLayout::Place(options, { 100.0f, 80.0f, 2.0f, 1.25f });
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    ExpectBounds(placement.bounds, { 0.0f, 0.0f, 100.0f, 80.0f });
    ExpectBounds(placement.viewport, placement.bounds);
}

TEST(UiPopupLayoutTests, OffscreenAnchorAndLargeGapStillProduceBoundedGeometry){
    PopupOptions options;
    options.anchor = { 600.0f, -80.0f, 20.0f, 20.0f };
    options.size = { 100.0f, 100.0f };
    options.side = PopupPlacementSide::Right;
    PopupPlacement placement;
    const auto placementResult = PopupLayout::Place(options, { 320.0f, 240.0f, 1.0f, 1.0f });
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    ExpectBounds(placement.bounds, { 220.0f, 0.0f, 100.0f, 100.0f });
    EXPECT_EQ(placement.side, PopupPlacementSide::Left);
    options.gap = Limit<f32>::s_Max;
    const auto placementResult2 = PopupLayout::Place(options, { 320.0f, 240.0f, 1.0f, 1.0f });
    ASSERT_TRUE(placementResult2);
    placement = *placementResult2;
    ExpectBounds(placement.bounds, { 220.0f, 0.0f, 100.0f, 100.0f });
    EXPECT_EQ(placement.side, PopupPlacementSide::Right);
}

TEST(UiPopupLayoutTests, EmptyAnchorCanRepresentPointPlacement){
    PopupOptions options;
    options.anchor = { 124.0f, 30.0f, 0.0f, 0.0f };
    options.size = { 80.0f, 60.0f };
    PopupPlacement placement;
    const auto placementResult = PopupLayout::Place(options, { 320.0f, 240.0f, 1.0f, 1.0f });
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    ExpectBounds(placement.bounds, { 124.0f, 34.0f, 80.0f, 60.0f });
    EXPECT_EQ(placement.side, PopupPlacementSide::Below);
}

TEST(UiPopupLayoutTests, AsymmetricDpiChangesDoNotRescaleLogicalPlacement){
    PopupOptions options;
    options.anchor = { 610.0f, 410.0f, 50.0f, 30.0f };
    PopupPlacement first;
    PopupPlacement second;
    const auto firstResult = PopupLayout::Place(options, { 680.0f, 480.0f, 1.0f, 1.0f });
    ASSERT_TRUE(firstResult);
    first = *firstResult;
    const auto secondResult = PopupLayout::Place(options, { 680.0f, 480.0f, 2.5f, 1.25f });
    ASSERT_TRUE(secondResult);
    second = *secondResult;
    ExpectBounds(second.bounds, first.bounds);
    ExpectBounds(second.viewport, first.viewport);
    EXPECT_EQ(second.side, first.side);
}

TEST(UiPopupLayoutTests, InvalidInputsRejectPlacement){
    const PopupOptions validOptions;
    const DisplayMetrics validDisplay{ 320.0f, 240.0f, 1.0f, 1.0f };
    const auto reject = [](const PopupOptions& options, const DisplayMetrics& display){
        EXPECT_FALSE(PopupLayout::Place(options, display));
    };
    PopupOptions options = validOptions;
    options.anchor.x = Limit<f32>::s_QuietNaN;
    reject(options, validDisplay);
    options = validOptions;
    options.anchor.height = -1.0f;
    reject(options, validDisplay);
    options = validOptions;
    options.anchor = { Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 0.0f };
    reject(options, validDisplay);
    options = validOptions;
    options.size.x = 0.0f;
    reject(options, validDisplay);
    options = validOptions;
    options.size.y = Limit<f32>::s_Infinity;
    reject(options, validDisplay);
    options = validOptions;
    options.side = static_cast<PopupPlacementSide::Enum>(255u);
    reject(options, validDisplay);
    options = validOptions;
    options.gap = -1.0f;
    reject(options, validDisplay);
    options = validOptions;
    options.gap = Limit<f32>::s_QuietNaN;
    reject(options, validDisplay);
    DisplayMetrics display = validDisplay;
    display.logicalWidth = 0.0f;
    reject(validOptions, display);
    display = validDisplay;
    display.logicalHeight = Limit<f32>::s_Infinity;
    reject(validOptions, display);
    display = validDisplay;
    display.pixelScaleX = -1.0f;
    reject(validOptions, display);
    display = validDisplay;
    display.pixelScaleY = Limit<f32>::s_QuietNaN;
    reject(validOptions, display);
}

TEST(UiPopupStateTests, OpenEpochAdvancesOnlyOnClosedToOpenTransitions){
    PopupState state;
    EXPECT_EQ(state.openGeneration(), 0u);
    state.close();
    EXPECT_EQ(state.openGeneration(), 0u);
    state.open();
    EXPECT_TRUE(state.isOpen());
    EXPECT_EQ(state.openGeneration(), 1u);
    state.open();
    EXPECT_EQ(state.openGeneration(), 1u);
    state.close();
    state.close();
    EXPECT_EQ(state.openGeneration(), 1u);
    state.open();
    EXPECT_TRUE(state.isOpen());
    EXPECT_EQ(state.openGeneration(), 2u);
}

TEST(UiPopupStateTests, IndependentAndRecreatedStatesNeverShareInstanceIdentity){
    u64 retiredIdentity = 0u;
    {
        PopupState retired;
        retiredIdentity = retired.instanceGeneration();
        retired.open();
    }
    PopupState first;
    PopupState second;
    EXPECT_NE(first.instanceGeneration(), retiredIdentity);
    EXPECT_NE(second.instanceGeneration(), retiredIdentity);
    EXPECT_NE(first.instanceGeneration(), second.instanceGeneration());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

