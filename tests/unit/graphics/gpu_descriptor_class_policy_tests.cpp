// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/vulkan/descriptor_class_policy.h>
#include <core/graphics/vulkan/descriptor_image_policy.h>
#include <tests/common/capturing_logger.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_descriptor_class_policy_tests{
using namespace NWB;

TEST(GpuDescriptorClassPolicy, StorageAliasesAndSampledImageDimensionsStayDistinct){
    using Policy = Core::GraphicsBackend::DescriptorClassPolicy;
    EXPECT_EQ(Policy::ClassToResourceType(Core::GpuDescriptorClass::StorageBuffer), Core::ResourceType::StructuredBuffer_UAV);
    EXPECT_TRUE(Policy::IsResourceTypeCompatible(Core::GpuDescriptorClass::StorageBuffer, Core::ResourceType::StructuredBuffer_SRV));
    EXPECT_TRUE(Policy::IsResourceTypeCompatible(Core::GpuDescriptorClass::StorageBuffer, Core::ResourceType::RawBuffer_UAV));
    EXPECT_FALSE(Policy::IsResourceTypeCompatible(Core::GpuDescriptorClass::UniformBuffer, Core::ResourceType::RawBuffer_UAV));
    EXPECT_TRUE(Policy::IsSampledImageClass(Core::GpuDescriptorClass::SampledImage2DArrayUint));
    EXPECT_EQ(Policy::GetSampledImageDimension(Core::GpuDescriptorClass::SampledImage2DArrayUint), Core::TextureDimension::Texture2DArray);
    EXPECT_EQ(Policy::GetSampledImageDimension(Core::GpuDescriptorClass::SampledImageCube), Core::TextureDimension::TextureCube);
    EXPECT_FALSE(Policy::IsSampledImageClass(Core::GpuDescriptorClass::StorageImage));
    EXPECT_EQ(Policy::GetSampledImageDimension(Core::GpuDescriptorClass::StorageImage), Core::TextureDimension::Unknown);
}


TEST(GpuDescriptorImagePolicy, Storage3dRejectsIntegerFormatWhileTwoDimensionalStorageAcceptsIt){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard guard(logger, Core::Common::LoggerBreakPolicy::ReportOnly);
    Core::TextureDesc texture;
    texture.dimension = Core::TextureDimension::Texture3D;
    texture.format = Core::Format::R32_UINT;
    auto item = Core::DescriptorWriteItem::None();
    using Policy = Core::GraphicsBackend::DescriptorImagePolicy;
    EXPECT_FALSE(Policy::Validate(Core::GpuDescriptorClass::StorageImage, item, texture));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("rejected format")));
    item.dimension = Core::TextureDimension::Texture2D;
    EXPECT_TRUE(Policy::Validate(Core::GpuDescriptorClass::StorageImage, item, texture));
    item.dimension = Core::TextureDimension::Unknown;
    item.format = Core::Format::R32_FLOAT;
    EXPECT_TRUE(Policy::Validate(Core::GpuDescriptorClass::StorageImage, item, texture));
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
