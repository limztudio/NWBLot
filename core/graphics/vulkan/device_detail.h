// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"

#include <core/graphics/vulkan/device_feature_types.adhd.h>
#include <core/graphics/vulkan/pipeline_cache_types.adhd.h>

#include <global/atomic_identity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_PipelineCacheVirtualPath = NWB::Core::GraphicsBackend::VulkanDetail::s_VulkanCacheVirtualPath;
// Pipeline-cache validation rejects data from different GPUs or drivers.
inline constexpr AStringView s_PipelineCacheVolumeName = NWB::Core::GraphicsBackend::VulkanDetail::s_VulkanCacheVolumeName;
inline constexpr usize s_PipelineCacheHeaderVersionOneSize = NWB::Core::GraphicsBackend::VulkanDetail::s_VulkanCacheHeaderVersionOneSize;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PipelineCacheDataValidation{
    enum Enum : u8{
        Usable,
        Malformed,
        Incompatible,
    };
};

[[nodiscard]] PipelineCacheDataValidation::Enum ValidatePipelineCacheData(BinaryByteView cacheData, const VkPhysicalDeviceProperties& properties)noexcept;

[[nodiscard]] inline constexpr b8 ShouldEnableAstcHdrFeature(const AstcHdrFeatureNegotiation& negotiation)noexcept{
    return negotiation.apiSupportsVulkan13 ? negotiation.vulkan13FeatureSupported : negotiation.extensionEnabled && negotiation.extensionFeatureSupported;
}

[[nodiscard]] inline constexpr b8 IsCompressedTextureFormatFeatureEnabled(const Format::Enum format, const CompressedTextureFeatureState& featureState)noexcept{
    if(Format::IsBCCompressedFormat(format))
        return featureState.bcEnabled;
    if(Format::IsASTCHdrFormat(format))
        return featureState.astcHdrEnabled;
    if(Format::IsASTCCompressedFormat(format))
        return featureState.astcLdrEnabled;
    return true;
}


class DeviceGenerationAllocator final : NoCopy{
public:
    [[nodiscard]] Expected<u16> allocate()noexcept{
        return TryNextAtomicBoundedIdentity<u16>(m_nextGeneration);
    }


private:
    Atomic<u32> m_nextGeneration{ 1u };
};

[[nodiscard]] inline constexpr GpuQueueCapability::Mask DeviceMinimumQueueCapabilities(const CommandQueue::Enum queue)noexcept{
    switch(queue){
    case CommandQueue::Graphics:
        return static_cast<GpuQueueCapability::Mask>(static_cast<u8>(GpuQueueCapability::Graphics) | static_cast<u8>(GpuQueueCapability::Transfer));
    case CommandQueue::Compute:
        return static_cast<GpuQueueCapability::Mask>(static_cast<u8>(GpuQueueCapability::Compute) | static_cast<u8>(GpuQueueCapability::Transfer));
    case CommandQueue::Transfer:
        return GpuQueueCapability::Transfer;
    default:
        return GpuQueueCapability::None;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

