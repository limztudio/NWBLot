// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/material/material_instance_mutable_cache.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/ecs_test_world.h>
#include <tests/unit/ecs_graphics/name_cases.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_instance_mutable_cache_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MaterialInstanceMutableCacheTests, RevisionChangesAndClearInvalidatePreparedBytes){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::ReportOnly);
    Tests::EcsTestWorld context;
    MaterialInstanceMutableCache cache(context.arena);
    MaterialSurfaceInfo surface(context.arena);
    surface.materialName = Name(NWB::Tests::EcsGraphicsCases::s_EcsGraphicsOverrideMaterial);
    surface.materialInterface = Name(NWB::Tests::EcsGraphicsCases::s_EcsGraphicsOverrideInterface);
    surface.typedLayoutHash = 7u;
    surface.mutableDefaultTypedBytes.resize(4u);
    MaterialTypedLayoutBlock block;
    block.blockName = Name(NWB::Tests::EcsGraphicsCases::s_EcsGraphicsOverrideBlock);
    block.blockClass = MaterialBlockClass::MaterialMutable;
    block.fieldCount = 1u;
    block.byteSize = 4u;
    surface.typedLayoutBlocks.push_back(block);
    MaterialTypedLayoutField field;
    field.fieldName = Name(NWB::Tests::EcsGraphicsCases::s_EcsGraphicsMutableValue);
    field.fieldType = MaterialLayoutFieldType::u32;
    surface.typedLayoutFields.push_back(field);
    MaterialInstanceComponent instance(surface.materialInterface);
    MaterialInstanceParameter parameter;
    parameter.parameterName = Name(NWB::Tests::EcsGraphicsCases::s_EcsGraphicsMutableParameter);
    parameter.blockName = block.blockName;
    parameter.fieldName = field.fieldName;
    parameter.fieldType = field.fieldType;
    parameter.value = UInt4U(0x01020304u, 0u, 0u, 0u);
    instance.overrides.push_back(parameter);
    const Core::ECS::EntityID entity(1u);

    ASSERT_FALSE(cache.find(entity, surface, &instance));
    auto first = cache.prepare(context.world, entity, surface, &instance);
    ASSERT_TRUE(first);
    ASSERT_EQ(first->size(), 4u);
    EXPECT_EQ((*first)[0u], 0x04u);
    EXPECT_TRUE(cache.find(entity, surface, &instance));
    ++instance.revision;
    instance.overrides[0u].value = UInt4U(0xAABBCCDDu, 0u, 0u, 0u);
    EXPECT_FALSE(cache.find(entity, surface, &instance));
    auto refreshed = cache.prepare(context.world, entity, surface, &instance);
    ASSERT_TRUE(refreshed);
    EXPECT_EQ((*refreshed)[0u], 0xDDu);
    EXPECT_EQ((*refreshed)[3u], 0xAAu);
    cache.clear();
    EXPECT_FALSE(cache.find(entity, surface, &instance));
    EXPECT_TRUE(cache.find(entity, surface, nullptr));
    EXPECT_EQ(logger.errorCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

