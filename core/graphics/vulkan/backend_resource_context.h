// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "backend_forward.h"
#include "command_buffer_resource_references.h"
#include "heap_binding_contract.h"
#include "host_readback_sync.h"
#include "native_resource_provenance.h"
#include "native_queue_state.h"
#include "submitted_command_buffer_owner_lookup.h"

#include <core/common/log.h>
#include <core/graphics/vulkan/texture_transfer_types.adhd.h>
#include <core/task/cpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingMeasure;
class GpuTimingSubmissionTicket;
class GpuRecordedGraph;
class GpuTaskScheduler;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct VulkanAllocation;
using VulkanAllocationHandle = VulkanAllocation*;
struct VulkanAllocatorStorage;
using VulkanAllocatorHandle = VulkanAllocatorStorage*;

struct VulkanContext;
class Buffer;
class CommandList;
class Heap;
class MeshletPipeline;
class Texture;
class StagingTexture;
using PipelineRenderingFormatVector = Vector<VkFormat, Alloc::ScratchArena>;
using PipelineColorBlendAttachmentVector = Vector<VkPipelineColorBlendAttachmentState, Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PipelineStencilFaceMode{
    static constexpr u8 s_PipelineStencilFaceModeDepthOnlyBase = VulkanDefaults::s_VulkanDefaultPipelineStencilFaceModeDepthOnlyBase;
    enum Enum : u8{
        DepthOnly = s_PipelineStencilFaceModeDepthOnlyBase,
        IncludeStencilFaces,
    };
};

namespace IndirectDrawIndexMode{
    static constexpr u8 s_IndirectDrawIndexModeNonIndexedBase = VulkanDefaults::s_VulkanDefaultIndirectDrawIndexModeNonIndexedBase;
    enum Enum : u8{
        NonIndexed = s_IndirectDrawIndexModeNonIndexedBase,
        Indexed,
    };
};

namespace BufferImageCopyRequiredSize{
    static constexpr u8 s_BufferImageCopyRequiredSizeTouchedBytesBase = VulkanDefaults::s_VulkanDefaultBufferImageCopyRequiredSizeTouchedBytesBase;
    enum Enum : u8{
        TouchedBytes = s_BufferImageCopyRequiredSizeTouchedBytesBase,
        PaddedSlices,
    };
};

namespace BufferImageCopyPitchFields{
    static constexpr u8 s_BufferImageCopyPitchFieldsOmitImplicitBase = VulkanDefaults::s_VulkanDefaultBufferImageCopyPitchFieldsOmitImplicitBase;
    enum Enum : u8{
        OmitImplicit = s_BufferImageCopyPitchFieldsOmitImplicitBase,
        EmitExplicit,
    };
};

using StagingTextureMipLayoutVector = Vector<StagingTextureMipLayout, Alloc::GlobalArena>;
using StagingTextureQueueFamilyVector = Vector<u32, Alloc::GlobalArena>;

struct PipelineRenderingInfo{
    VkPipelineRenderingCreateInfo info = {};
    PipelineRenderingFormatVector colorFormats;


public:
    explicit PipelineRenderingInfo(Alloc::ScratchArena& scratchArena)noexcept
        : colorFormats(scratchArena)
    {}
};

struct GraphicsPipelineFixedState{
    VkPipelineViewportStateCreateInfo viewportState = {};
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    PipelineColorBlendAttachmentVector blendAttachments;
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    VkPipelineRenderingCreateInfo renderingInfo = {};
    PipelineRenderingFormatVector colorFormats;


public:
    explicit GraphicsPipelineFixedState(Alloc::ScratchArena& scratchArena)noexcept
        : blendAttachments(scratchArena)
        , colorFormats(scratchArena)
    {}
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Synchronization2 gives sampled images and read-only attachments one shared native layout.
inline constexpr VkImageLayout s_ReadOnlyImageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkFormat ConvertFormat(Format::Enum format)noexcept;
VkAccessFlags2 GetVkAccessFlags(ResourceStates::Mask states)noexcept;
VkPipelineStageFlags2 GetVkPipelineStageFlags(ResourceStates::Mask states, b8 rayTracingStageAvailable)noexcept;
VkImageLayout GetVkImageLayout(ResourceStates::Mask states)noexcept;
VkSampleCountFlagBits GetSampleCountFlagBits(u32 sampleCount)noexcept;
VkComponentTypeKHR ConvertCoopVecDataType(CooperativeVectorDataType::Enum type);
CooperativeVectorDataType::Enum ConvertCoopVecDataType(VkComponentTypeKHR type);
VkCooperativeVectorMatrixLayoutNV ConvertCoopVecMatrixLayout(CooperativeVectorMatrixLayout::Enum layout);
extern VkDeviceAddress GetBufferDeviceAddress(Buffer* bufferResource, u64 offset = 0)noexcept;
b8 IsSupportedSampleCount(u32 sampleCount)noexcept;
VkImageAspectFlags GetImageAspectMask(const FormatInfo& formatInfo)noexcept;
Expected<TextureFormatBlockLayout> GetTextureFormatBlockLayout(const FormatInfo& formatInfo)noexcept;
Expected<u32> TryComputeUploadSuballocationAlignment(u32 requiredAlignment)noexcept;
b8 IsBufferImageCopyAspectMaskSupported(VkImageAspectFlags aspectMask)noexcept;
VkExtent3D GetTextureMipExtent(const TextureDesc& desc, MipLevel mipLevel)noexcept;
VkImageSubresourceLayers BuildImageSubresourceLayers(VkImageAspectFlags aspectMask, MipLevel mipLevel, ArraySlice arraySlice, ArraySlice layerCount = 1u)noexcept;
VkImageSubresourceRange BuildImageSubresourceRange(const TextureSubresourceSet& subresources, VkImageAspectFlags aspectMask)noexcept;
Expected<VkImageViewCreateInfo> BuildTextureImageViewCreateInfo(Texture& texture, const TextureSubresourceSet& resolvedSubresources, TextureDimension::Enum dimension, Format::Enum format, TStringView operationName, b8 assertFailure);
b8 IsBufferRangeInBounds(const BufferDesc& desc, u64 offsetBytes, u64 sizeBytes)noexcept;

[[nodiscard]] constexpr b8 IsTextureSubresourceRangeValid(const TextureSubresourceSet& subresources)noexcept{
    return subresources.numMipLevels != 0u && subresources.numArraySlices != 0u;
}

[[nodiscard]] constexpr b8 IsPushConstantByteSizeValid(const u32 byteSize, const u32 maximumByteSize)noexcept{
    return byteSize != 0u && (byteSize & s_BufferAlignmentMask) == 0u && byteSize <= maximumByteSize;
}

[[nodiscard]] constexpr b8 AreDispatchGroupCountsValid(const u32 groupsX, const u32 groupsY, const u32 groupsZ, const Array<u32, 3u>& maximumGroupCounts)noexcept{
    return groupsX <= maximumGroupCounts[0u] && groupsY <= maximumGroupCounts[1u] && groupsZ <= maximumGroupCounts[2u];
}

[[nodiscard]] Expected<VkBuildAccelerationStructureFlagsKHR> ConvertAccelStructBuildFlags(RayTracingAccelStructBuildFlags::Mask buildFlags, TStringView operationName);

template<typename T>
constexpr T MakeVkStruct(VkStructureType sType)noexcept(noexcept(T{}) && noexcept(static_cast<T*>(nullptr)->sType = sType) && IsNothrowMoveConstructible_V<T> && IsNothrowDestructible_V<T>){
    T output{};
    output.sType = sType;
    return output;
}

constexpr VkCullModeFlags ConvertCullMode(RasterCullMode::Enum cullMode)noexcept{
    switch(cullMode){
    case RasterCullMode::Back:  return VK_CULL_MODE_BACK_BIT;
    case RasterCullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case RasterCullMode::None:  return VK_CULL_MODE_NONE;
    default: return VK_CULL_MODE_BACK_BIT;
    }
}

constexpr VkPolygonMode ConvertFillMode(RasterFillMode::Enum fillMode)noexcept{
    switch(fillMode){
    case RasterFillMode::Solid:     return VK_POLYGON_MODE_FILL;
    case RasterFillMode::Wireframe: return VK_POLYGON_MODE_LINE;
    default: return VK_POLYGON_MODE_FILL;
    }
}


constexpr VkCompareOp ConvertCompareOp(ComparisonFunc::Enum compareFunc)noexcept{
    switch(compareFunc){
    case ComparisonFunc::Never:          return VK_COMPARE_OP_NEVER;
    case ComparisonFunc::Less:           return VK_COMPARE_OP_LESS;
    case ComparisonFunc::Equal:          return VK_COMPARE_OP_EQUAL;
    case ComparisonFunc::LessOrEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case ComparisonFunc::Greater:        return VK_COMPARE_OP_GREATER;
    case ComparisonFunc::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
    case ComparisonFunc::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case ComparisonFunc::Always:         return VK_COMPARE_OP_ALWAYS;
    default: return VK_COMPARE_OP_ALWAYS;
    }
}

constexpr VkStencilOp ConvertStencilOp(StencilOp::Enum stencilOp)noexcept{
    switch(stencilOp){
    case StencilOp::Keep:              return VK_STENCIL_OP_KEEP;
    case StencilOp::Zero:              return VK_STENCIL_OP_ZERO;
    case StencilOp::Replace:           return VK_STENCIL_OP_REPLACE;
    case StencilOp::IncrementAndClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case StencilOp::DecrementAndClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case StencilOp::Invert:            return VK_STENCIL_OP_INVERT;
    case StencilOp::IncrementAndWrap:  return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case StencilOp::DecrementAndWrap:  return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default: return VK_STENCIL_OP_KEEP;
    }
}

constexpr VkStencilOpState ConvertStencilOpState(const DepthStencilState& dsState, const DepthStencilState::StencilOpDesc& stencilDesc)noexcept{
    VkStencilOpState state = {};
    state.failOp = ConvertStencilOp(stencilDesc.failOp);
    state.passOp = ConvertStencilOp(stencilDesc.passOp);
    state.depthFailOp = ConvertStencilOp(stencilDesc.depthFailOp);
    state.compareOp = ConvertCompareOp(stencilDesc.stencilFunc);
    state.compareMask = dsState.stencilReadMask;
    state.writeMask = dsState.stencilWriteMask;
    state.reference = dsState.stencilRefValue;
    return state;
}

constexpr VkBlendFactor ConvertBlendFactor(BlendFactor::Enum blendFactor)noexcept{
    switch(blendFactor){
    case BlendFactor::Zero:             return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One:              return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SrcColor:         return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::InvSrcColor:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SrcAlpha:         return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha:         return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::InvDstAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case BlendFactor::DstColor:         return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::InvDstColor:      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::SrcAlphaSaturate: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case BlendFactor::ConstantColor:    return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case BlendFactor::InvConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case BlendFactor::Src1Color:        return VK_BLEND_FACTOR_SRC1_COLOR;
    case BlendFactor::InvSrc1Color:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
    case BlendFactor::Src1Alpha:        return VK_BLEND_FACTOR_SRC1_ALPHA;
    case BlendFactor::InvSrc1Alpha:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
    default: return VK_BLEND_FACTOR_ZERO;
    }
}

constexpr VkBlendOp ConvertBlendOp(BlendOp::Enum blendOp)noexcept{
    switch(blendOp){
    case BlendOp::Add:             return VK_BLEND_OP_ADD;
    case BlendOp::Subtract:        return VK_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::Min:             return VK_BLEND_OP_MIN;
    case BlendOp::Max:             return VK_BLEND_OP_MAX;
    default: return VK_BLEND_OP_ADD;
    }
}

constexpr VkPipelineColorBlendAttachmentState ConvertBlendState(const BlendState::RenderTarget& target)noexcept{
    VkPipelineColorBlendAttachmentState state = {};
    state.blendEnable = target.blendEnable ? VK_TRUE : VK_FALSE;
    state.srcColorBlendFactor = ConvertBlendFactor(target.srcBlend);
    state.dstColorBlendFactor = ConvertBlendFactor(target.destBlend);
    state.colorBlendOp = ConvertBlendOp(target.blendOp);
    state.srcAlphaBlendFactor = ConvertBlendFactor(target.srcBlendAlpha);
    state.dstAlphaBlendFactor = ConvertBlendFactor(target.destBlendAlpha);
    state.alphaBlendOp = ConvertBlendOp(target.blendOpAlpha);
    state.colorWriteMask = 0;
    if(target.colorWriteMask & ColorMask::Red)
        state.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
    if(target.colorWriteMask & ColorMask::Green)
        state.colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
    if(target.colorWriteMask & ColorMask::Blue)
        state.colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
    if(target.colorWriteMask & ColorMask::Alpha)
        state.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
    return state;
}

VkDescriptorType ConvertDescriptorType(ResourceType::Enum type)noexcept;
VkShaderStageFlags ConvertShaderStages(ShaderType::Mask stages)noexcept;
// Descriptor-buffer offset alignment clamped to a 32-bit value (1 when zero/oversized) for byte-offset math.
u32 GetDescriptorBufferOffsetAlignmentBytes(const VulkanContext& context)noexcept;
void DestroyPipelineAndOwnedLayout(const VulkanContext& context, VkPipeline& pipeline, VkPipelineLayout& pipelineLayout, b8& ownsPipelineLayout);
u32 GetPushConstantByteSize(const BindingLayoutDesc& desc)noexcept;
b8 ValidatePushConstantByteSize(const VulkanContext& context, u32 byteSize, TStringView operationName);
Expected<VkPipelineLayout> CreatePipelineLayout(const VulkanContext& context, Span<const VkDescriptorSetLayout> setLayouts, u32 pushConstantByteSize, TStringView operationName);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

