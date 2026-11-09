// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_model_renderer/model_renderer.adhd.h>

#include <tests/common/ecs_test_world.adhd.h>
#include <tests/unit/ecs_graphics/name_cases.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_renderer_reallocation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ModelRenderer, CopiesOwnerOverridesAfterComponentPoolMoves){
    NWB::Tests::EcsTestWorld context;
    auto owner = context.world.createEntity();
    auto& source = owner.addComponent<NWB::Impl::MaterialInstanceComponent>();
    source.materialInterface = Name(NWB::Tests::EcsGraphicsCases::s_EcsGraphicsOverrideInterface);
    source.revision = 37u;
    NWB::Impl::MaterialInstanceParameter parameter;
    parameter.parameterName = Name(NWB::Tests::EcsGraphicsCases::s_EcsGraphicsOverrideParameter);
    parameter.fieldType = NWB::Impl::MaterialLayoutFieldType::u32;
    parameter.value = UInt4U(42u, 0u, 0u, 0u);
    source.overrides.push_back(parameter);

    const NWB::Core::Assets::AssetRef<NWB::Impl::Material> material("tests/model_renderer/material");
    b8 sawRelocation = false;
    for(u32 index = 0u; index < 64u && !sawRelocation; ++index){
        const usize previous = reinterpret_cast<usize>(context.world.tryGetComponent<NWB::Impl::MaterialInstanceComponent>(owner.id()));
        auto child = context.world.createEntity();
        NWB::Impl::ApplyModelObjectRenderer(owner.id(), material, context.world, child);
        const auto* currentOwner = context.world.tryGetComponent<NWB::Impl::MaterialInstanceComponent>(owner.id());
        const auto* copied = context.world.tryGetComponent<NWB::Impl::MaterialInstanceComponent>(child.id());
        ASSERT_NE(currentOwner, nullptr);
        ASSERT_NE(copied, nullptr);
        ASSERT_EQ(copied->overrides.size(), 1u);
        EXPECT_EQ(copied->materialInterface, Name(NWB::Tests::EcsGraphicsCases::s_EcsGraphicsOverrideInterface));
        EXPECT_EQ(copied->revision, 37u);
        EXPECT_EQ(copied->overrides[0u].parameterName, Name(NWB::Tests::EcsGraphicsCases::s_EcsGraphicsOverrideParameter));
        EXPECT_EQ(copied->overrides[0u].fieldType, NWB::Impl::MaterialLayoutFieldType::u32);
        EXPECT_EQ(copied->overrides[0u].value.x, 42u);
        sawRelocation = previous != reinterpret_cast<usize>(currentOwner);
    }
    EXPECT_TRUE(sawRelocation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

