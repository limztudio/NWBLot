// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shadow/types.adhd.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>

#include <global/simd/vector.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_software_transparent_sampling_tests{


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
    Array<SceneLightGpuData, 2> original{};
    original[1].direction.x = 0.25f;
    constexpr Array<Float4 SceneLightGpuData::*, 5> s_Fields = {
        &SceneLightGpuData::position, &SceneLightGpuData::direction,
        &SceneLightGpuData::colorIntensity, &SceneLightGpuData::params,
        &SceneLightGpuData::params2,
    };
    for(const auto field : s_Fields){
        SoftwareTransparentSamplingHistory history;
        history.prepareScene(scene);
        history.prepareLighting(Span<const SceneLightGpuData>(original.data(), 2u));
        history.accept();
        Array<SceneLightGpuData, 2> changed = { original[0], original[1] };
        Float4& value = changed[0].*field;
        value.x = value.x + 0.125f;
        history.prepareLighting(Span<const SceneLightGpuData>(changed.data(), 2u));
        EXPECT_FALSE(history.usable());
    }
    SoftwareTransparentSamplingHistory history;
    history.prepareScene(scene);
    history.prepareLighting(Span<const SceneLightGpuData>(original.data(), 2u));
    history.accept();
    Array<SceneLightGpuData, 2> reversed = { original[1], original[0] };
    history.prepareLighting(Span<const SceneLightGpuData>(reversed.data(), 2u));
    EXPECT_FALSE(history.usable());
    history.prepareLighting(Span<const SceneLightGpuData>(original.data(), 1u));
    EXPECT_FALSE(history.usable());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

