// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "descriptor_class_policy.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


ResourceType::Enum DescriptorClassPolicy::ClassToResourceType(const GpuDescriptorClass::Enum descriptorClass)noexcept{
    switch(descriptorClass){
    case GpuDescriptorClass::SampledImage:  return ResourceType::TextureSrv;
    case GpuDescriptorClass::StorageImage:  return ResourceType::TextureUav;
    case GpuDescriptorClass::SampledBuffer: return ResourceType::TypedBufferSrv;
    case GpuDescriptorClass::StorageBuffer: return ResourceType::StructuredBufferUav;
    case GpuDescriptorClass::UniformBuffer: return ResourceType::ConstantBuffer;
    case GpuDescriptorClass::AccelStruct:   return ResourceType::RayTracingAccelStruct;
    case GpuDescriptorClass::Sampler:       return ResourceType::Sampler;
    case GpuDescriptorClass::SampledImage2DArray: return ResourceType::TextureSrv;
    case GpuDescriptorClass::SampledImage3D: return ResourceType::TextureSrv;
    case GpuDescriptorClass::SampledImage2DArrayUint: return ResourceType::TextureSrv;
    case GpuDescriptorClass::SampledImageCube: return ResourceType::TextureSrv;
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
        return resourceType == ResourceType::TextureSrv;
    case GpuDescriptorClass::StorageImage:
        return resourceType == ResourceType::TextureUav;
    case GpuDescriptorClass::SampledBuffer:
        return resourceType == ResourceType::TypedBufferSrv;
    case GpuDescriptorClass::StorageBuffer:
        return resourceType == ResourceType::StructuredBufferSrv || resourceType == ResourceType::StructuredBufferUav || resourceType == ResourceType::RawBufferSrv || resourceType == ResourceType::RawBufferUav;
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

