// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/presentation_settings.h>

#include <global/limit.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_presentation_settings_tests{


using namespace NWB::Impl;

TEST(PresentationSettings, DefaultsAndExplicitLinearClampAreValid){
    PresentationSettings settings;
    EXPECT_TRUE(ValidatePresentationSettings(settings));
    EXPECT_EQ(settings.toneMap, PresentationToneMap::Reinhard);
    EXPECT_FLOAT_EQ(settings.exposure, 1.f);
    EXPECT_FLOAT_EQ(settings.shoulder, 0.65f);
    settings.toneMap = PresentationToneMap::LinearClamp;
    settings.exposure = 0.f;
    EXPECT_TRUE(ValidatePresentationSettings(settings));
}

TEST(PresentationSettings, RejectsNonFiniteOrNegativeExposure){
    PresentationSettings settings;
    settings.exposure = -1.f;
    EXPECT_FALSE(ValidatePresentationSettings(settings));
    settings.exposure = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(ValidatePresentationSettings(settings));
    settings.exposure = Limit<f32>::s_Infinity;
    EXPECT_FALSE(ValidatePresentationSettings(settings));
}

TEST(PresentationSettings, RejectsInvalidToneMapAndNonPositiveOrNonFiniteShoulder){
    PresentationSettings settings;
    settings.toneMap = static_cast<PresentationToneMap::Enum>(255u);
    EXPECT_FALSE(ValidatePresentationSettings(settings));
    settings = PresentationSettings{};
    settings.shoulder = 0.f;
    EXPECT_FALSE(ValidatePresentationSettings(settings));
    settings.shoulder = -0.65f;
    EXPECT_FALSE(ValidatePresentationSettings(settings));
    settings.shoulder = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(ValidatePresentationSettings(settings));
    settings.shoulder = Limit<f32>::s_Infinity;
    EXPECT_FALSE(ValidatePresentationSettings(settings));
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

