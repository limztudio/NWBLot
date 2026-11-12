// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_context_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


namespace VulkanDetail{


class OptionalDeviceFeaturePolicy{
public:
    [[nodiscard]] static OptionalDeviceFeatureSet MakeRequested()noexcept;
    [[nodiscard]] static b8 SupportsRequested(const OptionalDeviceFeatureSet& requested, const OptionalDeviceFeatureSet& supported, DeviceExtensionFeature::Enum feature)noexcept;
    static void FinalizeEnablement(OptionalDeviceFeatureSet& enabled, const OptionalDeviceFeatureSet& supported)noexcept;
    static void AppendFeature(DeviceExtensionFeature::Enum feature, void*& pNext, OptionalDeviceFeatureSet& features, Array<b8, static_cast<usize>(DeviceExtensionFeature::Count)>& appended)noexcept;

private:
    [[nodiscard]] static void* FeatureStruct(OptionalDeviceFeatureSet& features, DeviceExtensionFeature::Enum feature)noexcept;
};


};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

