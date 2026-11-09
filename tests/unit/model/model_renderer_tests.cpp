// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/ecs_test_world.h>
#include <tests/unit/model/name_cases.adhd.h>

#include <impl/ecs_model_renderer/model_renderer.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_renderer_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ModelRenderer, OwnerOverridesSurviveMaterialComponentPoolGrowth){
    NWB::Tests::EcsTestWorld context;
    auto owner = context.world.createEntity();
    auto& renderer = owner.addComponent<NWB::Impl::RendererComponent>();
    const Name identity = NWB::Tests::ModelCases::s_ModelFixtureNameObject;
    renderer.material = NWB::Core::Assets::AssetRef<NWB::Impl::Material>(identity);
    auto& source = owner.addComponent<NWB::Impl::MaterialInstanceComponent>(identity);
    source.revision = 7u;
    source.overrides.emplace_back().parameterName = identity;
    b8 relocated = false;
    for(u32 index = 0u; index < 256u && !relocated; ++index){
        const auto* const before = context.world.tryGetComponent<NWB::Impl::MaterialInstanceComponent>(owner.id());
        ASSERT_NE(before, nullptr);
        const usize beforeAddress = reinterpret_cast<usize>(before);
        auto child = context.world.createEntity();
        NWB::Impl::ApplyModelObjectRenderer(owner.id(), NWB::Core::Assets::AssetRef<NWB::Impl::Material>{}, context.world, child);

        const auto* const after = context.world.tryGetComponent<NWB::Impl::MaterialInstanceComponent>(owner.id());
        const auto* const copied = context.world.tryGetComponent<NWB::Impl::MaterialInstanceComponent>(child.id());
        ASSERT_NE(after, nullptr);
        ASSERT_NE(copied, nullptr);
        relocated = beforeAddress != reinterpret_cast<usize>(after);
        ASSERT_EQ(after->overrides.size(), 1u);
        ASSERT_EQ(copied->overrides.size(), 1u);
        EXPECT_EQ(copied->materialInterface, identity);
        EXPECT_EQ(copied->revision, 7u);
        EXPECT_EQ(copied->overrides.front().parameterName, identity);
    }
    EXPECT_TRUE(relocated);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

