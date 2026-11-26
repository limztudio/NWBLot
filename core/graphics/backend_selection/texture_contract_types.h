// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/api.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsBackend{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureClearValueKind{
    enum Enum : u8{
        Float,
        UInt,
        Int,
        DepthStencil,
    };
};

namespace TextureQueueRequirement{
    enum Enum : u8{
        Transfer,
        ComputeOrGraphics,
        Graphics,
    };
};

struct TextureClearContract{
    TextureSubresourceSet subresources;
    TextureQueueRequirement::Enum queueRequirement = TextureQueueRequirement::Transfer;
};

struct TextureCopyContract{
    TextureSlice sourceSlice;
    TextureSlice destinationSlice;
    TextureQueueRequirement::Enum queueRequirement = TextureQueueRequirement::Transfer;
};

[[nodiscard]] inline b8 TextureQueueRequirementSatisfied(const TextureQueueRequirement::Enum requirement, const GpuQueueCapability::Mask declaredCapabilities, const GpuQueueCapability::Mask physicalCapabilities)noexcept{
    const u8 declaredBits = static_cast<u8>(declaredCapabilities);
    const u8 physicalBits = static_cast<u8>(physicalCapabilities);
    switch(requirement){
    case TextureQueueRequirement::Transfer:
        return (declaredBits & physicalBits & static_cast<u8>(GpuQueueCapability::Transfer)) != 0u;
    case TextureQueueRequirement::ComputeOrGraphics:
        return (declaredBits & physicalBits & static_cast<u8>(GpuQueueCapability::Compute | GpuQueueCapability::Graphics)) != 0u;
    case TextureQueueRequirement::Graphics:
        return (declaredBits & physicalBits & static_cast<u8>(GpuQueueCapability::Graphics)) != 0u;
    default:
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

