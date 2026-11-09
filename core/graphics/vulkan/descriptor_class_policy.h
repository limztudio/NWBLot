// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


class DescriptorClassPolicy final{
public:
    [[nodiscard]] static ResourceType::Enum ClassToResourceType(GpuDescriptorClass::Enum descriptorClass)noexcept;
    [[nodiscard]] static b8 IsResourceTypeCompatible(GpuDescriptorClass::Enum descriptorClass, ResourceType::Enum resourceType)noexcept;
    [[nodiscard]] static b8 IsSampledImageClass(GpuDescriptorClass::Enum descriptorClass)noexcept;
    [[nodiscard]] static TextureDimension::Enum GetSampledImageDimension(GpuDescriptorClass::Enum descriptorClass)noexcept;
};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

