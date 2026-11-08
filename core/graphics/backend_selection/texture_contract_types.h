// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/api.h>

#include <core/graphics/backend_selection/texture_contract_requirements.adhd.h>


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

namespace TextureClearQueueRequirement{
    enum Enum : u8{
        Transfer,
        ComputeOrGraphics,
        Graphics,
    };
};

[[nodiscard]] inline b8 TextureClearQueueRequirementSatisfied(const TextureClearQueueRequirement::Enum requirement, const GpuQueueCapability::Mask declaredCapabilities, const GpuQueueCapability::Mask physicalCapabilities)noexcept{
    static_assert(static_cast<u8>(GpuQueueCapability::Transfer) == NWB::Core::GraphicsBackend::s_QueueCapabilityTransferBit);
    static_assert(static_cast<u8>(GpuQueueCapability::Compute) == NWB::Core::GraphicsBackend::s_QueueCapabilityComputeBit);
    static_assert(static_cast<u8>(GpuQueueCapability::Graphics) == NWB::Core::GraphicsBackend::s_QueueCapabilityGraphicsBit);
    return NWB::Core::GraphicsBackend::TextureClearQueueRequirementSatisfied(static_cast<u8>(requirement), static_cast<u8>(declaredCapabilities), static_cast<u8>(physicalCapabilities));
}

struct TextureClearContract{
    TextureSubresourceSet subresources;
    TextureClearQueueRequirement::Enum queueRequirement = TextureClearQueueRequirement::Transfer;
};

namespace TextureCopyQueueRequirement{
    enum Enum : u8{
        Transfer,
        ComputeOrGraphics,
        Graphics,
    };
};

struct TextureCopyContract{
    TextureSlice sourceSlice;
    TextureSlice destinationSlice;
    TextureCopyQueueRequirement::Enum queueRequirement = TextureCopyQueueRequirement::Transfer;
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

