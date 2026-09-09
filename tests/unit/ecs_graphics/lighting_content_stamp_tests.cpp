// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/lighting_content_stamp.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_lighting_content_stamp_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


TEST(LightingContentStamp, DirectIntensityEditsInvalidateWithoutStructuralChanges){
    ECSRenderDetail::SceneShadingGpuData shading;
    ECSRenderDetail::SceneLightGpuData lights[2];
    const u64 original = ComputeSceneLightingContentHash(shading, lights, 1u);
    lights[0].colorIntensity.w = 0.f;
    EXPECT_NE(ComputeSceneLightingContentHash(shading, lights, 1u), original);
    lights[0].colorIntensity.w = 1.f;
    EXPECT_EQ(ComputeSceneLightingContentHash(shading, lights, 1u), original);
}

TEST(LightingContentStamp, OnlyTheSelectedLightPrefixContributes){
    ECSRenderDetail::SceneShadingGpuData shading;
    ECSRenderDetail::SceneLightGpuData lights[2];
    const u64 original = ComputeSceneLightingContentHash(shading, lights, 1u);
    lights[1].position.x = 3.f;
    lights[1].colorIntensity.w = 0.f;
    EXPECT_EQ(ComputeSceneLightingContentHash(shading, lights, 1u), original);
    EXPECT_NE(ComputeSceneLightingContentHash(shading, lights, 2u), original);
    EXPECT_EQ(ComputeSceneLightingContentHash(shading, nullptr, 0u), ComputeSceneLightingContentHash(shading, lights, 0u));
}

TEST(LightingContentStamp, PositionDirectionRangeAndSourceShapeAreSemanticInputs){
    ECSRenderDetail::SceneShadingGpuData shading;
    ECSRenderDetail::SceneLightGpuData light;
    const u64 original = ComputeSceneLightingContentHash(shading, &light, 1u);
    light.position.y = 2.f;
    EXPECT_NE(ComputeSceneLightingContentHash(shading, &light, 1u), original);
    light = {};
    light.direction.x = 1.f;
    EXPECT_NE(ComputeSceneLightingContentHash(shading, &light, 1u), original);
    light = {};
    light.params.x = 100.f;
    EXPECT_NE(ComputeSceneLightingContentHash(shading, &light, 1u), original);
    light = {};
    light.params2.y = 0.5f;
    EXPECT_NE(ComputeSceneLightingContentHash(shading, &light, 1u), original);
}

TEST(LightingContentStamp, LightSelectionOrderAndObserverStateArePreserved){
    ECSRenderDetail::SceneShadingGpuData shading;
    ECSRenderDetail::SceneLightGpuData lights[2];
    lights[0].colorIntensity.x = 0.2f;
    lights[1].colorIntensity.y = 0.7f;
    const u64 original = ComputeSceneLightingContentHash(shading, lights, 2u);
    const ECSRenderDetail::SceneLightGpuData reversed[2] = {lights[1], lights[0]};
    EXPECT_NE(ComputeSceneLightingContentHash(shading, reversed, 2u), original);
    shading.cameraPositionLightCount.x = 0.25f;
    EXPECT_NE(ComputeSceneLightingContentHash(shading, lights, 2u), original);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

