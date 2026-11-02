// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shared/frame_types.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_lighting_content_stamp_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


TEST(LightingContentStamp, OnlyTheSelectedLightPrefixContributes){
    SceneShadingGpuData shading;
    Array<SceneLightGpuData, 2> lights{};
    const u64 original = ComputeSceneLightingContentHash(shading, Span<const SceneLightGpuData>(lights.data(), 1u));
    lights[1].position.x = 3.f;
    lights[1].colorIntensity.w = 0.f;
    EXPECT_EQ(ComputeSceneLightingContentHash(shading, Span<const SceneLightGpuData>(lights.data(), 1u)), original);
    EXPECT_NE(ComputeSceneLightingContentHash(shading, Span<const SceneLightGpuData>(lights.data(), 2u)), original);
    EXPECT_EQ(
        ComputeSceneLightingContentHash(shading, Span<const SceneLightGpuData>{}),
        ComputeSceneLightingContentHash(shading, Span<const SceneLightGpuData>(lights.data(), 0u))
    );
}


TEST(LightingContentStamp, ReversingSelectedLightsChangesSemanticIdentity){
    SceneShadingGpuData shading;
    Array<SceneLightGpuData, 2> lights{};
    lights[0].colorIntensity.x = 0.2f;
    lights[1].colorIntensity.y = 0.7f;
    const u64 original = ComputeSceneLightingContentHash(shading, Span<const SceneLightGpuData>(lights.data(), 2u));
    const Array<SceneLightGpuData, 2> reversed = {lights[1], lights[0]};
    EXPECT_NE(ComputeSceneLightingContentHash(shading, Span<const SceneLightGpuData>(reversed.data(), 2u)), original);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

