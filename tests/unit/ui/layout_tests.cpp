// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"

#include <impl/ecs_ui/toolkit/layout/tree.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_layout_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


static LayoutNodeDesc StretchContainer(const LayoutDirection::Enum direction){
    LayoutNodeDesc description;
    description.direction = direction;
    description.width = { LayoutSizePolicy::Stretch, 1.0f };
    description.height = { LayoutSizePolicy::Stretch, 1.0f };
    return description;
}

static LayoutNodeDesc FixedLeaf(const f32 width, const f32 height){
    LayoutNodeDesc description;
    description.width = { LayoutSizePolicy::Fixed, width };
    description.height = { LayoutSizePolicy::Fixed, height };
    return description;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiLayoutTests : public testing::Test{
public:
    UiLayoutTests()
        : m_arena(Name("tests/ui/layout"))
        , m_tree(m_arena)
    {}


protected:
    u32 add(const u32 parent, const LayoutNodeDesc& description){
        const auto index = m_tree.addNode(parent, description);
        EXPECT_TRUE(index);
        return index.value_or(s_LayoutNoParent);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    LayoutTree m_tree;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiLayoutTests, ExhaustedColumnSpaceCollapsesStretchAndClipsFixedOverflow){
    LayoutNodeDesc rootDescription = StretchContainer(LayoutDirection::Column);
    rootDescription.gap = 3.0f;
    const u32 root = add(s_LayoutNoParent, rootDescription);
    const u32 fixed = add(root, FixedLeaf(12.0f, 30.0f));
    const u32 stretched = add(root, StretchContainer(LayoutDirection::Leaf));
    ASSERT_TRUE(m_tree.arrange({ 0.0f, 0.0f, 20.0f, 17.0f }));
    ASSERT_EQ(m_tree.boxes().size(), 3u);
    NWB::UiWidgetTests::ExpectRect(m_tree.box(fixed)->rectangle, { 0.0f, 0.0f, 12.0f, 30.0f });
    NWB::UiWidgetTests::ExpectRect(m_tree.box(fixed)->hit, { 0.0f, 0.0f, 12.0f, 17.0f });
    NWB::UiWidgetTests::ExpectRect(m_tree.box(stretched)->rectangle, { 0.0f, 33.0f, 20.0f, 0.0f });
    EXPECT_FLOAT_EQ(m_tree.box(stretched)->hit.height, 0.0f);
}

TEST_F(UiLayoutTests, OverlaySizesChildrenIndependentlyAndClipsToPaddedContent){
    LayoutNodeDesc rootDescription = StretchContainer(LayoutDirection::Overlay);
    rootDescription.padding = { 2.0f, 3.0f, 4.0f, 5.0f };
    const u32 root = add(s_LayoutNoParent, rootDescription);
    const u32 fixed = add(root, FixedLeaf(70.0f, 80.0f));
    const u32 stretched = add(root, StretchContainer(LayoutDirection::Leaf));
    ASSERT_TRUE(m_tree.arrange({ 10.0f, 20.0f, 41.0f, 31.0f }));
    ASSERT_EQ(m_tree.boxes().size(), 3u);
    NWB::UiWidgetTests::ExpectRect(m_tree.box(fixed)->rectangle, { 12.0f, 23.0f, 70.0f, 80.0f });
    NWB::UiWidgetTests::ExpectRect(m_tree.box(fixed)->clip, { 12.0f, 23.0f, 35.0f, 23.0f });
    NWB::UiWidgetTests::ExpectRect(m_tree.box(stretched)->rectangle, { 12.0f, 23.0f, 35.0f, 23.0f });
}

TEST_F(UiLayoutTests, UnclippedContainerOverflowStillHonorsViewportAndOuterAncestors){
    const u32 root = add(s_LayoutNoParent, StretchContainer(LayoutDirection::Overlay));
    LayoutNodeDesc containerDescription = FixedLeaf(20.0f, 15.0f);
    containerDescription.direction = LayoutDirection::Overlay;
    containerDescription.clipChildren = false;
    const u32 container = add(root, containerDescription);
    const u32 leaf = add(container, FixedLeaf(80.0f, 70.0f));
    ASSERT_TRUE(m_tree.arrange({ 5.0f, 7.0f, 43.0f, 31.0f }));
    ASSERT_EQ(m_tree.boxes().size(), 3u);
    NWB::UiWidgetTests::ExpectRect(m_tree.box(container)->hit, { 5.0f, 7.0f, 20.0f, 15.0f });
    NWB::UiWidgetTests::ExpectRect(m_tree.box(leaf)->hit, { 5.0f, 7.0f, 43.0f, 31.0f });
}

TEST_F(UiLayoutTests, FixedAndContentRootSizingRespectsViewportClipping){
    LayoutNodeDesc rootDescription;
    rootDescription.direction = LayoutDirection::Overlay;
    rootDescription.width = { LayoutSizePolicy::Fixed, 40.0f };
    rootDescription.padding = { 2.0f, 3.0f, 2.0f, 3.0f };
    const u32 root = add(s_LayoutNoParent, rootDescription);
    add(root, FixedLeaf(20.0f, 18.0f));
    ASSERT_TRUE(m_tree.arrange({ 3.0f, 4.0f, 31.0f, 17.0f }));
    ASSERT_EQ(m_tree.boxes().size(), 2u);
    NWB::UiWidgetTests::ExpectRect(m_tree.box(root)->rectangle, { 3.0f, 4.0f, 40.0f, 24.0f });
    NWB::UiWidgetTests::ExpectRect(m_tree.box(root)->clip, { 3.0f, 4.0f, 31.0f, 17.0f });
}

TEST_F(UiLayoutTests, OddPhysicalDisplaySizesRemainLogicalAndDoNotRoundStretch){
    const u32 root = add(s_LayoutNoParent, StretchContainer(LayoutDirection::Row));
    const u32 first = add(root, StretchContainer(LayoutDirection::Leaf));
    const u32 second = add(root, StretchContainer(LayoutDirection::Leaf));
    const DisplayMetrics firstMetrics{ 901.0f / 1.5f, 607.0f / 1.5f, 1.5f, 1.5f };
    ASSERT_TRUE(m_tree.arrange({ 0.0f, 0.0f, firstMetrics.logicalWidth, firstMetrics.logicalHeight }));
    ASSERT_EQ(m_tree.boxes().size(), 3u);
    const Rect firstBefore = m_tree.box(first)->rectangle;
    const Rect secondBefore = m_tree.box(second)->rectangle;
    EXPECT_FLOAT_EQ(firstBefore.width, firstMetrics.logicalWidth * 0.5f);
    EXPECT_FLOAT_EQ(secondBefore.x + secondBefore.width, firstMetrics.logicalWidth);
    const DisplayMetrics secondMetrics{ firstMetrics.logicalWidth, firstMetrics.logicalHeight, 2.0f, 1.25f };
    ASSERT_TRUE(m_tree.arrange({ 0.0f, 0.0f, secondMetrics.logicalWidth, secondMetrics.logicalHeight }));
    NWB::UiWidgetTests::ExpectRect(m_tree.box(first)->rectangle, firstBefore);
    NWB::UiWidgetTests::ExpectRect(m_tree.box(second)->rectangle, secondBefore);
}

TEST_F(UiLayoutTests, OversizedPaddingAndEmptyViewportProduceFiniteEmptyContentAndHit){
    LayoutNodeDesc description = StretchContainer(LayoutDirection::Overlay);
    description.padding = { 100.0f, 100.0f, 100.0f, 100.0f };
    const u32 root = add(s_LayoutNoParent, description);
    const u32 leaf = add(root, StretchContainer(LayoutDirection::Leaf));
    ASSERT_TRUE(m_tree.arrange({ 7.0f, 11.0f, 5.0f, 3.0f }));
    ASSERT_EQ(m_tree.boxes().size(), 2u);
    NWB::UiWidgetTests::ExpectRect(m_tree.box(root)->content, { 12.0f, 14.0f, 0.0f, 0.0f });
    NWB::UiWidgetTests::ExpectRect(m_tree.box(leaf)->hit, { 12.0f, 14.0f, 0.0f, 0.0f });
    ASSERT_TRUE(m_tree.arrange({ 7.0f, 11.0f, 0.0f, 0.0f }));
    NWB::UiWidgetTests::ExpectRect(m_tree.box(root)->hit, { 7.0f, 11.0f, 0.0f, 0.0f });
}

TEST_F(UiLayoutTests, InvalidViewportOrOverflowCannotPublishPartialBoxes){
    add(s_LayoutNoParent, StretchContainer(LayoutDirection::Overlay));
    ASSERT_TRUE(m_tree.arrange({ 1.0f, 2.0f, 31.0f, 17.0f }));
    const LayoutBox* published = m_tree.box(0u);
    ASSERT_NE(published, nullptr);
    const Rect before = published->rectangle;
    EXPECT_FALSE(m_tree.arrange({ 0.0f, 0.0f, -1.0f, 10.0f }));
    EXPECT_FALSE(m_tree.arrange({ Limit<f32>::s_QuietNaN, 0.0f, 1.0f, 1.0f }));
    EXPECT_FALSE(m_tree.arrange({ 0.0f, 0.0f, Limit<f32>::s_Infinity, 1.0f }));
    EXPECT_EQ(m_tree.box(0u), published);
    NWB::UiWidgetTests::ExpectRect(m_tree.box(0u)->rectangle, before);
    m_tree.reset();
    const u32 root = add(s_LayoutNoParent, StretchContainer(LayoutDirection::Row));
    add(root, FixedLeaf(Limit<f32>::s_Max * 0.75f, 1.0f));
    add(root, FixedLeaf(Limit<f32>::s_Max * 0.75f, 1.0f));
    EXPECT_FALSE(m_tree.arrange({ 0.0f, 0.0f, 31.0f, 17.0f }));
    EXPECT_EQ(m_tree.boxes().size(), 1u);
    EXPECT_EQ(m_tree.box(0u), published);
    NWB::UiWidgetTests::ExpectRect(m_tree.box(0u)->rectangle, before);
    m_tree.reset();
    add(s_LayoutNoParent, FixedLeaf(Limit<f32>::s_Max * 0.75f, 10.0f));
    EXPECT_FALSE(m_tree.arrange({ Limit<f32>::s_Max, 0.0f, 0.0f, 0.0f }));
    EXPECT_EQ(m_tree.box(0u), published);
}

TEST_F(UiLayoutTests, InvalidConstructionPoisonsTheBuildUntilResetAndPreservesPublishedOutput){
    add(s_LayoutNoParent, StretchContainer(LayoutDirection::Overlay));
    ASSERT_TRUE(m_tree.arrange({ 0.0f, 0.0f, 21.0f, 13.0f }));
    const LayoutBox* published = m_tree.box(0u);
    EXPECT_FALSE(m_tree.addNode(s_LayoutNoParent, FixedLeaf(1.0f, 1.0f)));
    EXPECT_FALSE(m_tree.arrange({ 0.0f, 0.0f, 41.0f, 37.0f }));
    EXPECT_EQ(m_tree.box(0u), published);
    m_tree.reset();
    EXPECT_FALSE(m_tree.addNode(0u, FixedLeaf(1.0f, 1.0f)));
    EXPECT_FALSE(m_tree.addNode(s_LayoutNoParent, FixedLeaf(1.0f, 1.0f)));
    m_tree.reset();
    const u32 leaf = add(s_LayoutNoParent, FixedLeaf(1.0f, 1.0f));
    EXPECT_FALSE(m_tree.addNode(leaf, FixedLeaf(1.0f, 1.0f)));
    EXPECT_FALSE(m_tree.arrange({ 0.0f, 0.0f, 1.0f, 1.0f }));
    m_tree.reset();
    const u32 root = add(s_LayoutNoParent, StretchContainer(LayoutDirection::Overlay));
    EXPECT_FALSE(m_tree.addNode(root + 1u, FixedLeaf(1.0f, 1.0f)));
    EXPECT_EQ(m_tree.box(0u), published);
    m_tree.reset();
    add(s_LayoutNoParent, FixedLeaf(7.0f, 9.0f));
    ASSERT_TRUE(m_tree.arrange({ 3.0f, 4.0f, 21.0f, 13.0f }));
    NWB::UiWidgetTests::ExpectRect(m_tree.box(0u)->rectangle, { 3.0f, 4.0f, 7.0f, 9.0f });
}

TEST_F(UiLayoutTests, InvalidDescriptionsAndBoundedCapacityRejectNodeAdmission){
    LayoutNodeDesc invalid;
    LayoutNodeDesc invalidDescriptions[7u];
    invalidDescriptions[0u].width = { LayoutSizePolicy::Stretch, 0.0f };
    invalidDescriptions[1u].width = { LayoutSizePolicy::Fixed, -1.0f };
    invalidDescriptions[2u].height = { LayoutSizePolicy::Content, Limit<f32>::s_QuietNaN };
    invalidDescriptions[3u].padding = { 0.0f, -1.0f, 0.0f, 0.0f };
    invalidDescriptions[4u].gap = Limit<f32>::s_Infinity;
    invalidDescriptions[5u].intrinsicSize = { 0.0f, -1.0f };
    invalidDescriptions[6u].direction = static_cast<LayoutDirection::Enum>(255u);
    for(const LayoutNodeDesc& description : invalidDescriptions){
        m_tree.reset();
        EXPECT_FALSE(m_tree.addNode(s_LayoutNoParent, description));
        EXPECT_EQ(m_tree.nodeCount(), 0u);
        EXPECT_FALSE(m_tree.arrange({ 0.0f, 0.0f, 31.0f, 17.0f }));
    }
    LayoutTree bounded(m_arena, 2u);
    u32 root = s_LayoutNoParent;
    {
        const auto admittedNode = bounded.addNode(s_LayoutNoParent, StretchContainer(LayoutDirection::Overlay));
        ASSERT_TRUE(admittedNode);
        root = *admittedNode;
    }
    ASSERT_TRUE(bounded.addNode(root, FixedLeaf(1.0f, 1.0f)));
    ASSERT_TRUE(bounded.arrange({ 0.0f, 0.0f, 31.0f, 17.0f }));
    const LayoutBox* published = bounded.box(root);
    EXPECT_FALSE(bounded.addNode(root, FixedLeaf(1.0f, 1.0f)));
    EXPECT_FALSE(bounded.arrange({ 0.0f, 0.0f, 31.0f, 17.0f }));
    EXPECT_EQ(bounded.box(root), published);
    LayoutTree empty(m_arena, 0u);
    EXPECT_FALSE(empty.addNode(s_LayoutNoParent, invalid));
}

TEST_F(UiLayoutTests, MaximumDepthArrangesWithoutRecursionAndRejectsFurtherNodes){
    u32 parent = s_LayoutNoParent;
    for(u32 index = 0u; index < s_LayoutMaxNodes; ++index)
        parent = add(parent, StretchContainer(LayoutDirection::Overlay));
    ASSERT_EQ(m_tree.nodeCount(), s_LayoutMaxNodes);
    ASSERT_TRUE(m_tree.arrange({ 3.0f, 5.0f, 101.0f, 79.0f }));
    ASSERT_EQ(m_tree.boxes().size(), s_LayoutMaxNodes);
    NWB::UiWidgetTests::ExpectRect(m_tree.box(parent)->rectangle, { 3.0f, 5.0f, 101.0f, 79.0f });
    EXPECT_FALSE(m_tree.addNode(parent, FixedLeaf(1.0f, 1.0f)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

