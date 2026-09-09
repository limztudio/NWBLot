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
    settings.maxHardwareRaysPerFrame = 0u;
    EXPECT_TRUE(ValidateReflectionSettings(settings));
    settings.traceMode = ReflectionTraceMode::ScreenSpace;
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

