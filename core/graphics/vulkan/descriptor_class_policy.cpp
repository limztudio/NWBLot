// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "descriptor_class_policy.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


ResourceType::Enum DescriptorClassPolicy::ClassToResourceType(const GpuDescriptorClass::Enum descriptorClass)noexcept{
    switch(descriptorClass){
    case GpuDescriptorClass::SampledImage:  return ResourceType::Texture_SRV;
    case GpuDescriptorClass::StorageImage:  return ResourceType::Texture_UAV;
    case GpuDescriptorClass::SampledBuffer: return ResourceType::TypedBuffer_SRV;
    case GpuDescriptorClass::StorageBuffer: return ResourceType::StructuredBuffer_UAV;
    case GpuDescriptorClass::UniformBuffer: return ResourceType::ConstantBuffer;
    case GpuDescriptorClass::AccelStruct:   return ResourceType::RayTracingAccelStruct;
    case GpuDescriptorClass::Sampler:       return ResourceType::Sampler;
    case GpuDescriptorClass::SampledImage2DArray: return ResourceType::Texture_SRV;
    case GpuDescriptorClass::SampledImage3D: return ResourceType::Texture_SRV;
    case GpuDescriptorClass::SampledImage2DArrayUint: return ResourceType::Texture_SRV;
    case GpuDescriptorClass::SampledImageCube: return ResourceType::Texture_SRV;
    default:                                return ResourceType::None;
    }
}

b8 DescriptorClassPolicy::IsResourceTypeCompatible(const GpuDescriptorClass::Enum descriptorClass, const ResourceType::Enum resourceType)noexcept{
    switch(descriptorClass){
    case GpuDescriptorClass::SampledImage:
    case GpuDescriptorClass::SampledImage2DArray:
    case GpuDescriptorClass::SampledImage3D:
    case GpuDescriptorClass::SampledImage2DArrayUint:
    case GpuDescriptorClass::SampledImageCube:
        return resourceType == ResourceType::Texture_SRV;
    case GpuDescriptorClass::StorageImage:
        return resourceType == ResourceType::Texture_UAV;
    case GpuDescriptorClass::SampledBuffer:
        return resourceType == ResourceType::TypedBuffer_SRV;
    case GpuDescriptorClass::StorageBuffer:
        return resourceType == ResourceType::StructuredBuffer_SRV || resourceType == ResourceType::StructuredBuffer_UAV || resourceType == ResourceType::RawBuffer_SRV || resourceType == ResourceType::RawBuffer_UAV;
    case GpuDescriptorClass::UniformBuffer:
        return resourceType == ResourceType::ConstantBuffer;
    case GpuDescriptorClass::AccelStruct:
        return resourceType == ResourceType::RayTracingAccelStruct;
    case GpuDescriptorClass::Sampler:
        return resourceType == ResourceType::Sampler;
    default:
        return false;
    }
}

b8 DescriptorClassPolicy::IsSampledImageClass(const GpuDescriptorClass::Enum descriptorClass)noexcept{
    switch(descriptorClass){
    case GpuDescriptorClass::SampledImage:
    case GpuDescriptorClass::SampledImage2DArray:
    case GpuDescriptorClass::SampledImage3D:
    case GpuDescriptorClass::SampledImage2DArrayUint:
    case GpuDescriptorClass::SampledImageCube:
        return true;
    default:
        return false;
    }
}

TextureDimension::Enum DescriptorClassPolicy::GetSampledImageDimension(const GpuDescriptorClass::Enum descriptorClass)noexcept{
    switch(descriptorClass){
    case GpuDescriptorClass::SampledImage: return TextureDimension::Texture2D;
    case GpuDescriptorClass::SampledImage2DArray:
    case GpuDescriptorClass::SampledImage2DArrayUint: return TextureDimension::Texture2DArray;
    case GpuDescriptorClass::SampledImage3D: return TextureDimension::Texture3D;
    case GpuDescriptorClass::SampledImageCube: return TextureDimension::TextureCube;
    default: return TextureDimension::Unknown;
    }
}



NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

