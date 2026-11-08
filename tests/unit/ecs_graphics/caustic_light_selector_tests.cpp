// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/caustic/caustic_light_selector.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_caustic_light_selector_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CausticLightSelectorTests, MissingInputsPreserveSlotsAndPointLightsCannotWin){
    Array<SceneLightGpuData, 3> lights{};
    lights[0].params.y = 0.0f;
    lights[1].params.y = 1.0f;
    lights[2].params.y = 2.0f;
    for(auto& light : lights)
        light.params.w = -1.0f;
    const Array<f32, 3> importance{ 10.0f, 100.0f, 5.0f };
    MutableSceneLightGpuDataSpan lightSpan(lights.data(), lights.size());
    const Span<const f32> allImportance(importance.data(), importance.size());
    const Span<const f32> shortImportance(importance.data(), importance.size() - 1u);
    const CausticLightSelector selector;

    EXPECT_EQ(selector.assignSlots(allImportance, 0u, lightSpan), 0u);
    EXPECT_EQ(selector.assignSlots(shortImportance, 1u, lightSpan), 0u);
    for(const auto& light : lights)
        EXPECT_EQ(light.params.w, -1.0f);
    EXPECT_EQ(selector.assignSlots(allImportance, 1u, lightSpan), 2u);
    EXPECT_EQ(lights[0].params.w, 0.0f);
    EXPECT_EQ(lights[1].params.w, -1.0f);
    EXPECT_EQ(lights[2].params.w, 1.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
