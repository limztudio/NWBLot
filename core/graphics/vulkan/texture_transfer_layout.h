// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_resource_context.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


namespace VulkanDetail{


class TextureTransferLayout{
public:
    struct StagingLayout{
        u64 arrayByteSize = 0u;
        StagingTextureMipLayoutVector mipLayouts;
    };

    static Expected<BufferImageCopyLayout> BuildBufferImageCopyLayout(
        const VkExtent3D& extent,
        const TextureFormatBlockLayout& formatLayout,
        u64 rowPitch,
        u64 depthPitch,
        BufferImageCopyRequiredSize::Enum requiredSizeMode,
        BufferImageCopyPitchFields::Enum pitchFields,
        TStringView operationName = {}
    );
    static Expected<StagingLayout> BuildStagingLayout(Alloc::GlobalArena& arena, const TextureDesc& desc, const TextureFormatBlockLayout& formatLayout, u32 bufferOffsetAlignment);
    static Expected<TextureSlice> ResolveSlice(const TextureDesc& desc, const TextureSlice& slice, const TextureFormatBlockLayout& formatLayout)noexcept;
    static Expected<StagingTextureRange> BuildStagingRange(const TextureSlice& resolvedSlice, const StagingTextureMipLayout& mipLayout, const TextureFormatBlockLayout& formatLayout, u64 arrayByteSize, u64 totalByteSize, u32 requiredOffsetAlignment, b8 requireHostPointerRange)noexcept;
};


};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

