// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"

#include <global/atomic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_PipelineCacheVirtualPath = "vulkan/pipeline_cache.bin";
// Pipeline-cache validation rejects data from different GPUs or drivers.
inline constexpr AStringView s_PipelineCacheVolumeName = "runtime_pipeline_cache";
inline constexpr usize s_PipelineCacheHeaderVersionOneSize = 32u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PipelineCacheDataValidation{
    enum Enum : u8{
        Usable,
        Malformed,
        Incompatible,
    };
};

[[nodiscard]] PipelineCacheDataValidation::Enum ValidatePipelineCacheData(BinaryByteView cacheData, const VkPhysicalDeviceProperties& properties)noexcept;

struct AstcHdrFeatureNegotiation{
    b8 apiSupportsVulkan13 = false;
    b8 vulkan13FeatureSupported = false;
    b8 extensionEnabled = false;
    b8 extensionFeatureSupported = false;
};

[[nodiscard]] inline constexpr b8 ShouldEnableAstcHdrFeature(const AstcHdrFeatureNegotiation& negotiation)noexcept{
    return negotiation.apiSupportsVulkan13 ? negotiation.vulkan13FeatureSupported : negotiation.extensionEnabled && negotiation.extensionFeatureSupported;
}

struct CompressedTextureFeatureState{
    b8 bcEnabled = false;
    b8 astcLdrEnabled = false;
    b8 astcHdrEnabled = false;
};

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
        u32 generation = m_nextGeneration.load(MemoryOrder::relaxed);
        while(generation != 0u && generation <= static_cast<u32>(Limit<u16>::s_Max)){
            if(m_nextGeneration.compare_exchange_weak(generation, generation + 1u, MemoryOrder::relaxed, MemoryOrder::relaxed))
                return static_cast<u16>(generation);
        }
        return MakeUnexpected(Failure{});
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

