// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/material/material_instance_override_applier.adhd.h>

#include <tests/common/capturing_logger.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_instance_override_applier_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MaterialInstanceOverrideApplierTests, RejectsTruncatedStorageWithoutWritingAndAcceptsExactFit){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::ReportOnly);
    const Name interfaceName("test/interface");
    const Name blockName("runtime");
    const Name fieldName("roughness");
    MaterialTypedLayoutBlock block;
    block.blockName = blockName;
    block.blockClass = MaterialBlockClass::MaterialMutable;
    block.fieldCount = 1u;
    block.byteSize = 8u;
    MaterialTypedLayoutField field;
    field.fieldName = fieldName;
    field.fieldType = MaterialLayoutFieldType::u32;
    field.offset = 4u;
    MaterialInstanceParameter parameter;
    parameter.parameterName = Name("runtime.roughness");
    parameter.blockName = blockName;
    parameter.fieldName = fieldName;
    parameter.fieldType = field.fieldType;
    parameter.value = UInt4U(0x12345678u, 0u, 0u, 0u);
    MaterialInstanceComponent instance(interfaceName);
    instance.overrides.push_back(parameter);
    Array<u8, 8> bytes{};
    for(u8& value : bytes)
        value = 0xABu;
    Span<const MaterialTypedLayoutBlock> blocks(&block, 1u);
    Span<const MaterialTypedLayoutField> fields(&field, 1u);
    Span<u8> truncated(bytes.data(), bytes.size() - 1u);
    const MaterialInstanceOverrideApplier applier;

    EXPECT_FALSE(applier.apply(1u, Name("test/material"), interfaceName, blocks, fields, instance, truncated));
    for(const u8 value : bytes)
        EXPECT_EQ(value, 0xABu);
    EXPECT_EQ(logger.errorCount(), 1u);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("exceeds mutable storage")));

    Span<u8> exact(bytes.data(), bytes.size());
    ASSERT_TRUE(applier.apply(1u, Name("test/material"), interfaceName, blocks, fields, instance, exact));
    EXPECT_EQ(bytes[3u], 0xABu);
    EXPECT_EQ(bytes[4u], 0x78u);
    EXPECT_EQ(bytes[5u], 0x56u);
    EXPECT_EQ(bytes[6u], 0x34u);
    EXPECT_EQ(bytes[7u], 0x12u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
