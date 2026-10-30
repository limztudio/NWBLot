// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/software_transparent_sampling.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>

#include <global/simd/vector.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_software_transparent_sampling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SoftwareTransparentSampling, OnlyAcceptedCompleteStaticScenePermitsLocalBudgetSelection){
    SoftwareTransparentSamplingHistory history;
    const RayTracingSceneContentStamp scene{ 10u, 20u, true };
    const SceneLightGpuData light;
    history.prepareScene(scene);
    history.prepareLighting(Span<const SceneLightGpuData>(&light, 1u));
    EXPECT_FALSE(history.usable());
    history.accept();
    EXPECT_TRUE(history.usable());
    history.prepareScene(scene);
    EXPECT_FALSE(history.usable());
    history.prepareLighting(Span<const SceneLightGpuData>(&light, 1u));
    EXPECT_TRUE(history.usable());
    history.discard();
    EXPECT_FALSE(history.usable());
}

TEST(SoftwareTransparentSampling, MovingCasterWithStationaryReceiverCannotReuseTheReducedBudget){
    SoftwareTransparentSamplingHistory history;
    const SceneLightGpuData light;
    history.prepareScene({ 10u, 20u, true });
    history.prepareLighting(Span<const SceneLightGpuData>(&light, 1u));
    history.accept();
    // No camera/receiver input changes: the caster transform is part of the independently gathered scene identity.
    history.prepareScene({ 11u, 20u, true });
    history.prepareLighting(Span<const SceneLightGpuData>(&light, 1u));
    EXPECT_FALSE(history.usable());
    history.accept();
    EXPECT_TRUE(history.usable());
    history.prepareScene({ 11u, 21u, true });
    history.prepareLighting(Span<const SceneLightGpuData>(&light, 1u));
    EXPECT_FALSE(history.usable());
    history.prepareScene({ 11u, 20u, false });
    history.prepareLighting(Span<const SceneLightGpuData>(&light, 1u));
    history.accept();
    EXPECT_FALSE(history.usable());
}

TEST(SoftwareTransparentSampling, EveryLightInputAndSlotOrderInvalidatesTrust){
    const RayTracingSceneContentStamp scene{ 10u, 20u, true };
    SceneLightGpuData original[2];
    original[1].direction.x = 0.25f;
    constexpr Float4 SceneLightGpuData::* s_Fields[] = {
        &SceneLightGpuData::position, &SceneLightGpuData::direction,
        &SceneLightGpuData::colorIntensity, &SceneLightGpuData::params,
        &SceneLightGpuData::params2,
    };
    for(const auto field : s_Fields){
        SoftwareTransparentSamplingHistory history;
        history.prepareScene(scene);
        history.prepareLighting(Span<const SceneLightGpuData>(original, s_ExpectedDualCount));
        history.accept();
        SceneLightGpuData changed[] = { original[0], original[1] };
        Float4& value = changed[0].*field;
        value.x = VectorGetX(VectorAdd(VectorReplicate(value.x), VectorReplicate(0.125f)));
        history.prepareLighting(Span<const SceneLightGpuData>(changed, s_ExpectedDualCount));
        EXPECT_FALSE(history.usable());
    }
    SoftwareTransparentSamplingHistory history;
    history.prepareScene(scene);
    history.prepareLighting(Span<const SceneLightGpuData>(original, s_ExpectedDualCount));
    history.accept();
    SceneLightGpuData reversed[] = { original[1], original[0] };
    history.prepareLighting(Span<const SceneLightGpuData>(reversed, s_ExpectedDualCount));
    EXPECT_FALSE(history.usable());
    history.prepareLighting(Span<const SceneLightGpuData>(original, 1u));
    EXPECT_FALSE(history.usable());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

