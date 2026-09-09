// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/reflection/settings.h>

#include <global/limit.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_settings_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Impl;

TEST(ReflectionSettings, DefaultsAndZeroHardwareBudgetAreValid){
    ReflectionSettings settings;
    EXPECT_TRUE(ValidateReflectionSettings(settings));
    EXPECT_FALSE(settings.screenFeedbackEnabled);
    EXPECT_TRUE(settings.temporalEnabled);
    EXPECT_TRUE(settings.spatialFilterEnabled);
    EXPECT_EQ(settings.temporalMaxSamples, 16u);
    EXPECT_EQ(settings.spatialRadius, 2u);
    settings.screenFeedbackEnabled = true;
    settings.maxHardwareRaysPerFrame = 0u;
    EXPECT_TRUE(ValidateReflectionSettings(settings));
    settings.traceMode = ReflectionTraceMode::ScreenSpace;
    EXPECT_TRUE(ValidateReflectionSettings(settings));
}

TEST(ReflectionSettings, BoundsAllSceneQueriesPerAdmittedPath){
    ReflectionSettings settings;
    EXPECT_EQ(settings.maxOpticalQueries, 16u);
    settings.maxOpticalQueries = 0u;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.maxOpticalQueries = 1u;
    EXPECT_TRUE(ValidateReflectionSettings(settings));
    settings.maxOpticalQueries = 16u;
    EXPECT_TRUE(ValidateReflectionSettings(settings));
    settings.maxOpticalQueries = 17u;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
}

TEST(ReflectionSettings, BoundsTemporalReferenceCountAndSpatialRadius){
    ReflectionSettings settings;
    settings.temporalMaxSamples = 0u;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.temporalMaxSamples = 257u;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.temporalMaxSamples = 256u;
    EXPECT_TRUE(ValidateReflectionSettings(settings));
    settings.temporalMaxSamples = 1u;
    settings.spatialRadius = 4u;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.spatialRadius = 3u;
    EXPECT_TRUE(ValidateReflectionSettings(settings));
    settings.spatialRadius = 0u;
    settings.samplingSeed = Limit<u32>::s_Max;
    EXPECT_TRUE(ValidateReflectionSettings(settings));
}

TEST(ReflectionSettings, RejectsNonFiniteAndUnorderedDistanceControls){
    ReflectionSettings settings;
    settings.maxRayDistance = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.maxRayDistance = Limit<f32>::s_Infinity;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.maxRayDistance = 0.f;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.maxRayDistance = settings.distanceFadeStart;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.distanceFadeStart = -1.f;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.distanceFadeStart = 0.f;
    EXPECT_TRUE(ValidateReflectionSettings(settings));
}

TEST(ReflectionSettings, BoundsScreenTraversalAndFiniteConfidenceInputs){
    ReflectionSettings settings;
    settings.screenMaxSteps = 0u;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.screenMaxSteps = 257u;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.screenMaxSteps = 256u;
    EXPECT_TRUE(ValidateReflectionSettings(settings));
    settings.screenThickness = 0.f;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.screenThickness = Limit<f32>::s_Infinity;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings = ReflectionSettings{};
    settings.screenConfidenceThreshold = 0.f;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.screenConfidenceThreshold = 1.01f;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.screenConfidenceThreshold = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings = ReflectionSettings{};
    settings.screenEdgeFade = -0.1f;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.screenEdgeFade = 0.26f;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.screenEdgeFade = 0.f;
    EXPECT_TRUE(ValidateReflectionSettings(settings));
}

TEST(ReflectionSettings, RejectsInvalidMaterialThresholdsModesAndEnvironmentRadiance){
    ReflectionSettings settings;
    settings.roughnessCutoff = -0.1f;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.roughnessCutoff = 1.1f;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings.roughnessCutoff = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings = ReflectionSettings{};
    settings.traceMode = static_cast<ReflectionTraceMode::Enum>(255u);
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings = ReflectionSettings{};
    settings.debugView = static_cast<ReflectionDebugView::Enum>(255u);
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings = ReflectionSettings{};
    settings.environmentTop.x = -0.1f;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings = ReflectionSettings{};
    settings.environmentBottom.z = Limit<f32>::s_Infinity;
    EXPECT_FALSE(ValidateReflectionSettings(settings));
    settings = ReflectionSettings{};
    settings.environmentTop.x = 8.f;
    EXPECT_TRUE(ValidateReflectionSettings(settings));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

