// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_context_claim_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

constexpr AStringView s_SliderKey = "slider";
constexpr AStringView s_ListKey = "list";


class UiContextClaimTests : public testing::Test{
public:
    UiContextClaimTests()
        : m_arena(Name("tests/ui/context_claim"))
        , m_context(m_arena)
    {}


protected:
    [[nodiscard]] bool begin(const u64 generation){
        return m_context.beginFrame(generation) && m_context.beginRoot({ 17u, 1u });
    }

    [[nodiscard]] bool commit(const u64 generation){
        return m_context.endRoot() && m_context.finishFrame() && m_context.commitFrame(generation);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    Context m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiContextClaimTests, DifferentKindsCanShareAnInstanceGenerationWhilePreservingTheFullGeneration){
    ASSERT_TRUE(begin(1u));
    const auto slider = m_context.declare(s_SliderKey, WidgetKind::Slider);
    const auto list = m_context.declare(s_ListKey, WidgetKind::VirtualList);
    ASSERT_TRUE(slider);
    ASSERT_TRUE(list);
    EXPECT_TRUE(m_context.claimState(**slider, Limit<u64>::s_Max));
    EXPECT_TRUE(m_context.claimState(**list, Limit<u64>::s_Max));
    EXPECT_TRUE(m_context.claimState(**slider, Limit<u32>::s_Max));
    EXPECT_TRUE(m_context.claimState(**list, Limit<u32>::s_Max));
    ASSERT_TRUE(commit(1u));
}

TEST_F(UiContextClaimTests, ASharedKindAndInstanceFailsAcrossDistinctRoots){
    ASSERT_TRUE(begin(1u));
    const auto first = m_context.declare(s_SliderKey, WidgetKind::Slider);
    ASSERT_TRUE(first);
    ASSERT_TRUE(m_context.claimState(**first, 91u));
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.beginRoot({ 18u, 1u }));
    const auto second = m_context.declare(s_SliderKey, WidgetKind::Slider);
    ASSERT_TRUE(second);
    EXPECT_NE((*first)->id, (*second)->id);
    EXPECT_FALSE(m_context.claimState(**second, 91u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(1u));
}

TEST_F(UiContextClaimTests, ASharedKindAndInstanceFailsAcrossDistinctScopes){
    ASSERT_TRUE(begin(1u));
    const auto first = m_context.declare(s_SliderKey, WidgetKind::Slider);
    ASSERT_TRUE(first);
    ASSERT_TRUE(m_context.claimState(**first, 91u));
    ASSERT_TRUE(m_context.pushScope("nested"));
    const auto second = m_context.declare(s_SliderKey, WidgetKind::Slider);
    ASSERT_TRUE(second);
    EXPECT_NE((*first)->id, (*second)->id);
    EXPECT_FALSE(m_context.claimState(**second, 91u));
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiContextClaimTests, AcceptedAndAbandonedFramesReleaseTheirClaimsBeforeTheNextFrame){
    ASSERT_TRUE(begin(1u));
    const auto first = m_context.declare(s_SliderKey, WidgetKind::Slider);
    ASSERT_TRUE(first);
    ASSERT_TRUE(m_context.claimState(**first, 91u));
    ASSERT_TRUE(commit(1u));

    ASSERT_TRUE(begin(2u));
    const auto second = m_context.declare(s_SliderKey, WidgetKind::Slider);
    ASSERT_TRUE(second);
    ASSERT_TRUE(m_context.claimState(**second, 91u));
    EXPECT_FALSE(m_context.claimState(**second, 91u));
    m_context.abandonFrame();

    ASSERT_TRUE(begin(3u));
    const auto third = m_context.declare(s_SliderKey, WidgetKind::Slider);
    ASSERT_TRUE(third);
    EXPECT_TRUE(m_context.claimState(**third, 91u));
    ASSERT_TRUE(commit(3u));
}

TEST_F(UiContextClaimTests, ZeroInstancesAndStaleDeclarationsStillRejectTheFrame){
    ASSERT_TRUE(begin(1u));
    const auto first = m_context.declare(s_SliderKey, WidgetKind::Slider);
    ASSERT_TRUE(first);
    const WidgetState stale = **first;
    EXPECT_FALSE(m_context.claimState(**first, 0u));
    EXPECT_TRUE(m_context.failed());
    m_context.abandonFrame();

    ASSERT_TRUE(begin(2u));
    const auto current = m_context.declare(s_SliderKey, WidgetKind::Slider);
    ASSERT_TRUE(current);
    ASSERT_TRUE(m_context.claimState(**current, 91u));
    EXPECT_FALSE(m_context.claimState(stale, 92u));
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiContextClaimTests, PromotedClaimsStillRejectPairsFromTheInlinePrefix){
    ASSERT_TRUE(begin(1u));
    const auto slider = m_context.declare(s_SliderKey, WidgetKind::Slider);
    const auto list = m_context.declare(s_ListKey, WidgetKind::VirtualList);
    ASSERT_TRUE(slider);
    ASSERT_TRUE(list);
    for(u64 instance = 1u; instance <= 32u; ++instance)
        ASSERT_TRUE(m_context.claimState(**slider, instance));
    ASSERT_TRUE(m_context.claimState(**list, 1u));
    EXPECT_FALSE(m_context.claimState(**slider, 1u));
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiContextClaimTests, DuplicateAtThePromotionBoundaryRejectsTheFrame){
    ASSERT_TRUE(begin(1u));
    const auto slider = m_context.declare(s_SliderKey, WidgetKind::Slider);
    ASSERT_TRUE(slider);
    for(u64 instance = 1u; instance <= 32u; ++instance)
        ASSERT_TRUE(m_context.claimState(**slider, instance));
    EXPECT_FALSE(m_context.claimState(**slider, 32u));
    EXPECT_TRUE(m_context.failed());
}

TEST_F(UiContextClaimTests, LargeSmallAndEmptyFramesDoNotLeakPreviousPairsIntoPromotion){
    for(u64 generation = 1u; generation <= 4u; ++generation){
        ASSERT_TRUE(begin(generation));
        const auto slider = m_context.declare(s_SliderKey, WidgetKind::Slider);
        const auto list = m_context.declare(s_ListKey, WidgetKind::VirtualList);
        ASSERT_TRUE(slider);
        ASSERT_TRUE(list);
        const u64 claims = generation == 1u ? 128u : generation == 2u ? 16u : generation == 3u ? 0u : 33u;
        for(u64 instance = 1u; instance <= claims; ++instance)
            ASSERT_TRUE(m_context.claimState(**slider, instance));
        if(generation == 4u){
            EXPECT_TRUE(m_context.claimState(**slider, 128u));
            EXPECT_TRUE(m_context.claimState(**list, 1u));
            EXPECT_FALSE(m_context.claimState(**slider, 1u));
            EXPECT_TRUE(m_context.failed());
        }
        else
            ASSERT_TRUE(commit(generation));
    }
}

TEST_F(UiContextClaimTests, CapacityCountsDistinctKindAndInstancePairs){
    ASSERT_TRUE(begin(1u));
    const auto slider = m_context.declare(s_SliderKey, WidgetKind::Slider);
    const auto list = m_context.declare(s_ListKey, WidgetKind::VirtualList);
    ASSERT_TRUE(slider);
    ASSERT_TRUE(list);
    for(usize index = 0u; index < s_InputMaxTargets / 2u; ++index){
        const u64 instance = static_cast<u64>(index + 1u);
        ASSERT_TRUE(m_context.claimState(**slider, instance));
        ASSERT_TRUE(m_context.claimState(**list, instance));
    }
    EXPECT_FALSE(m_context.claimState(**slider, Limit<u64>::s_Max));
    EXPECT_TRUE(m_context.failed());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

