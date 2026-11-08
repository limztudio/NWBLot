// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/scene_shading_upload_tracker.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_shading_upload_tests{


using namespace NWB;
using namespace NWB::Impl;


TEST(SceneShadingUploads, EmptyLightTransitionInvalidatesThePreviousLightMirror){
    SceneShadingUploadTracker tracker;
    Array<u8, 5120u> lightBytes{};
    Array<u8, 16u> shadingBytes{};
    u32 lightCount = 0u;
    b8 lightValid = false;
    b8 shadingValid = false;
    SceneShadingGpuData shading{};
    Array<SceneLightGpuData, 1u> light{};
    const Span<const SceneLightGpuData> empty{};
    const Span<const SceneLightGpuData> one(light.data(), light.size());

    EXPECT_FALSE(tracker.lightUploadRequired(lightValid, lightCount, lightBytes, empty));
    EXPECT_TRUE(tracker.shadingUploadRequired(shadingValid, shadingBytes, shading));
    tracker.confirm(empty, false, shading, true, lightBytes, lightCount, lightValid, shadingBytes, shadingValid);
    EXPECT_TRUE(lightValid);
    EXPECT_EQ(lightCount, 0u);
    EXPECT_FALSE(tracker.shadingUploadRequired(shadingValid, shadingBytes, shading));

    ASSERT_TRUE(tracker.lightUploadRequired(lightValid, lightCount, lightBytes, one));
    tracker.confirm(one, true, shading, false, lightBytes, lightCount, lightValid, shadingBytes, shadingValid);
    EXPECT_FALSE(tracker.lightUploadRequired(lightValid, lightCount, lightBytes, one));
    light[0].position.x = 1.0f;
    EXPECT_TRUE(tracker.lightUploadRequired(lightValid, lightCount, lightBytes, one));
    tracker.confirm(empty, false, shading, false, lightBytes, lightCount, lightValid, shadingBytes, shadingValid);
    EXPECT_EQ(lightCount, 0u);
    EXPECT_TRUE(tracker.lightUploadRequired(lightValid, lightCount, lightBytes, one));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
