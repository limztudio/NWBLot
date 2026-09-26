// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shadow/quality_settings.h>
#include <impl/ecs_render/shadow/transparent_sampling_history.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_quality_settings_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Impl;

TEST(ShadowQualitySettings, ReferenceDefaultPreservesTheThreeSampleBudget){
    const ShadowQualitySettings settings;
    EXPECT_EQ(settings.transparentSampling, TransparentShadowSampling::ReferenceThree);
    EXPECT_TRUE(ValidateShadowQualitySettings(settings));
    EXPECT_EQ(ResolveTransparentShadowSampleCount(settings, false), 3u);
    EXPECT_EQ(ResolveTransparentShadowSampleCount(settings, true), 3u);
}

TEST(ShadowQualitySettings, TemporalBudgetRequiresAcceptedFilteredHistory){
    ShadowQualitySettings settings;
    settings.transparentSampling = TransparentShadowSampling::TemporalOne;
    EXPECT_TRUE(ValidateShadowQualitySettings(settings));
    EXPECT_EQ(ResolveTransparentShadowSampleCount(settings, false), 3u);
    EXPECT_EQ(ResolveTransparentShadowSampleCount(settings, true), 1u);
    // A resize, reset, quality change or unavailable temporal filter restores the bootstrap budget.
    EXPECT_EQ(ResolveTransparentShadowSampleCount(settings, false), 3u);
}

TEST(ShadowQualitySettings, UnknownModesAreRejectedAndCannotRemoveAllSamples){
    ShadowQualitySettings settings;
    settings.transparentSampling = static_cast<TransparentShadowSampling::Enum>(2u);
    EXPECT_FALSE(ValidateShadowQualitySettings(settings));
    EXPECT_EQ(ResolveTransparentShadowSampleCount(settings, true), 3u);
    settings.transparentSampling = static_cast<TransparentShadowSampling::Enum>(255u);
    EXPECT_FALSE(ValidateShadowQualitySettings(settings));
    EXPECT_EQ(ResolveTransparentShadowSampleCount(settings, false), 3u);
}

TEST(ShadowQualitySettings, RgbHistoryRequiresAcceptedWritesForEveryRequestedSlot){
    TransparentShadowSamplingHistory history;
    const ShadowQualitySettings settings{ TransparentShadowSampling::TemporalOne };
    EXPECT_FALSE(history.covers(0u));
    EXPECT_EQ(ResolveTransparentShadowSampleCount(settings, history.covers(1u)), 3u);
    history.record(1u);
    EXPECT_FALSE(history.covers(1u));
    history.accept();
    EXPECT_EQ(ResolveTransparentShadowSampleCount(settings, history.covers(1u)), 1u);
    // Existing history for light zero does not initialize a newly assigned shadow slot.
    EXPECT_EQ(ResolveTransparentShadowSampleCount(settings, history.covers(3u)), 3u);
    history.record(3u);
    history.accept();
    EXPECT_EQ(ResolveTransparentShadowSampleCount(settings, history.covers(3u)), 1u);
}

TEST(ShadowQualitySettings, OpaqueOnlyAcceptedFramesInvalidateRgbQualification){
    TransparentShadowSamplingHistory history;
    history.record(3u);
    history.accept();
    ASSERT_TRUE(history.covers(3u));
    // The shared selector swaps on an opaque-only frame, without any RGB merge being recorded.
    history.accept();
    EXPECT_FALSE(history.covers(1u));
    history.record(3u);
    EXPECT_FALSE(history.covers(3u));
    history.accept();
    EXPECT_TRUE(history.covers(3u));
}

TEST(ShadowQualitySettings, FailedSubmissionsRetainAcceptedFrontButCannotPublishPendingWrites){
    TransparentShadowSamplingHistory history;
    history.record(1u);
    history.accept();
    history.record(3u);
    history.discardPending();
    EXPECT_TRUE(history.covers(1u));
    EXPECT_FALSE(history.covers(3u));
    history.accept();
    EXPECT_FALSE(history.covers(1u));
    history.record(3u);
    history.reset();
    history.accept();
    EXPECT_FALSE(history.covers(3u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

