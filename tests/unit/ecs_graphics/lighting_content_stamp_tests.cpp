// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_lighting_content_stamp_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


TEST(LightingContentStamp, OnlyTheSelectedLightPrefixContributes){
    SceneShadingGpuData shading;
    SceneLightGpuData lights[2];
    const u64 original = ComputeSceneLightingContentHash(shading, Span<const SceneLightGpuData>(lights, 1u));
    lights[1].position.x = 3.f;
    lights[1].colorIntensity.w = 0.f;
    EXPECT_EQ(ComputeSceneLightingContentHash(shading, Span<const SceneLightGpuData>(lights, 1u)), original);
    EXPECT_NE(ComputeSceneLightingContentHash(shading, Span<const SceneLightGpuData>(lights, s_ExpectedDualCount)), original);
    EXPECT_EQ(
        ComputeSceneLightingContentHash(shading, Span<const SceneLightGpuData>{}),
        ComputeSceneLightingContentHash(shading, Span<const SceneLightGpuData>(lights, 0u))
    );
}


TEST(LightingContentStamp, ReversingSelectedLightsChangesSemanticIdentity){
    SceneShadingGpuData shading;
    SceneLightGpuData lights[2];
    lights[0].colorIntensity.x = 0.2f;
    lights[1].colorIntensity.y = 0.7f;
    const u64 original = ComputeSceneLightingContentHash(shading, Span<const SceneLightGpuData>(lights, s_ExpectedDualCount));
    const SceneLightGpuData reversed[2] = {lights[1], lights[0]};
    EXPECT_NE(ComputeSceneLightingContentHash(shading, Span<const SceneLightGpuData>(reversed, s_ExpectedDualCount)), original);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

