// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "backend_forward.h"

#include <core/task/cpu/scheduler.h>
#include "command_buffer_resource_references.h"
#include "heap_binding_contract.h"
#include "host_readback_sync.h"
#include "native_buffer_provenance.h"
#include "native_queue_state.h"
#include "native_texture_provenance.h"
#include "submitted_command_buffer_owner_lookup.h"

#include <core/common/log.h>


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
    static constexpr u8 s_PipelineStencilFaceModeDepthOnlyBase = 0u;
    enum Enum : u8{
        DepthOnly = s_PipelineStencilFaceModeDepthOnlyBase,
        IncludeStencilFaces,
    };
};

namespace IndirectDrawIndexMode{
    static constexpr u8 s_IndirectDrawIndexModeNonIndexedBase = 0u;
    enum Enum : u8{
        NonIndexed = s_IndirectDrawIndexModeNonIndexedBase,
        Indexed,
    };
};

namespace BufferImageCopyRequiredSize{
    static constexpr u8 s_BufferImageCopyRequiredSizeTouchedBytesBase = 0u;
    enum Enum : u8{
        TouchedBytes = s_BufferImageCopyRequiredSizeTouchedBytesBase,
        PaddedSlices,
    };
};

namespace BufferImageCopyPitchFields{
    static constexpr u8 s_BufferImageCopyPitchFieldsOmitImplicitBase = 0u;
    enum Enum : u8{
        OmitImplicit = s_BufferImageCopyPitchFieldsOmitImplicitBase,
        EmitExplicit,
    };
};

struct TextureFormatBlockLayout{
    u32 blockWidth = 0;
    u32 blockHeight = 0;
    u32 bytesPerBlock = 0;
};

struct BufferImageCopyLayout{
    u64 requiredSize = 0;
    u32 bufferRowLength = 0;
    u32 bufferImageHeight = 0;
};

struct StagingTextureMipLayout{
    u64 byteOffset = 0;
    u64 rowPitch = 0;
    u64 slicePitch = 0;
    u32 bufferRowLength = 0;
    u32 bufferImageHeight = 0;
};
using StagingTextureMipLayoutVector = Vector<StagingTextureMipLayout, Alloc::GlobalArena>;

struct StagingTextureRange{
    u64 byteOffset = 0;
    u64 byteSize = 0;
    u64 rowPitch = 0;
    u32 bufferRowLength = 0;
    u32 bufferImageHeight = 0;
};
using StagingTextureQueueFamilyVector = Vector<u32, Alloc::GlobalArena>;

struct PipelineRenderingInfo{
    VkPipelineRenderingCreateInfo info = {};
    PipelineRenderingFormatVector colorFormats;

    explicit PipelineRenderingInfo(Alloc::ScratchArena& scratchArena)
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

    explicit GraphicsPipelineFixedState(Alloc::ScratchArena& scratchArena)
        : blendAttachments(scratchArena)
        , colorFormats(scratchArena)
    {}
};

inline void AttachGraphicsPipelineFixedState(
    VkGraphicsPipelineCreateInfo& pipelineInfo,
    const VkPipelineRasterizationStateCreateInfo& rasterizer,
    const GraphicsPipelineFixedState& fixedState
)noexcept{
    pipelineInfo.pViewportState = &fixedState.viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &fixedState.multisampling;
    pipelineInfo.pDepthStencilState = &fixedState.depthStencil;
    pipelineInfo.pColorBlendState = &fixedState.colorBlending;
    pipelineInfo.pDynamicState = &fixedState.dynamicState;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Synchronization2 gives sampled images and read-only attachments one shared native layout.
inline constexpr VkImageLayout s_ReadOnlyImageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkAccessFlags2 GetVkAccessFlags(ResourceStates::Mask state)noexcept;
VkPipelineStageFlags2 GetVkPipelineStageFlags(ResourceStates::Mask state, bool rayTracingStageAvailable)noexcept;
VkImageLayout GetVkImageLayout(ResourceStates::Mask state)noexcept;
VkFormat ConvertFormat(Format::Enum format)noexcept;
VkSampleCountFlagBits GetSampleCountFlagBits(u32 sampleCount)noexcept;
extern VkDeviceAddress GetBufferDeviceAddress(Buffer* bufferResource, u64 offset = 0)noexcept;
bool IsSupportedSampleCount(u32 sampleCount)noexcept;
bool ValidateTextureShape(const TextureDesc& desc, TStringView operationName);
VkImageAspectFlags GetImageAspectMask(const FormatInfo& formatInfo)noexcept;
Expected<TextureFormatBlockLayout> GetTextureFormatBlockLayout(const FormatInfo& formatInfo)noexcept;
Expected<u32> TryComputeCommonAlignment(u32 firstAlignment, u32 secondAlignment)noexcept;
Expected<u32> TryComputeUploadSuballocationAlignment(u32 requiredAlignment)noexcept;
bool IsBufferImageCopyAspectMaskSupported(VkImageAspectFlags aspectMask)noexcept;
VkExtent3D GetTextureMipExtent(const TextureDesc& desc, MipLevel mipLevel)noexcept;
Expected<BufferImageCopyLayout> BuildBufferImageCopyLayout(
    const VkExtent3D& extent,
    const TextureFormatBlockLayout& formatLayout,
    u64 rowPitch,
    u64 depthPitch,
    BufferImageCopyRequiredSize::Enum requiredSizeMode,
    BufferImageCopyPitchFields::Enum pitchFields,
    TStringView operationName = {}
);
VkImageSubresourceLayers BuildImageSubresourceLayers(
    VkImageAspectFlags aspectMask,
    MipLevel mipLevel,
    ArraySlice arraySlice,
    ArraySlice layerCount = 1u
)noexcept;
VkImageSubresourceRange BuildImageSubresourceRange(const TextureSubresourceSet& subresources, VkImageAspectFlags aspectMask)noexcept;
Expected<VkImageViewCreateInfo> BuildTextureImageViewCreateInfo(
    Texture& texture,
    const TextureSubresourceSet& resolvedSubresources,
    TextureDimension::Enum dimension,
    Format::Enum format,
    TStringView operationName,
    bool assertFailure
);
Expected<StagingTextureRange> BuildStagingTextureRange(
    const TextureSlice& resolvedSlice,
    const StagingTextureMipLayout& mipLayout,
    const TextureFormatBlockLayout& formatLayout,
    u64 arrayByteSize,
    u64 totalByteSize,
    u32 requiredOffsetAlignment,
    bool requireHostPointerRange
)noexcept;
Expected<TextureSlice> ResolveTextureSlice(const TextureDesc& desc, const TextureSlice& slice, const TextureFormatBlockLayout& formatLayout)noexcept;
bool IsBufferRangeInBounds(const BufferDesc& desc, u64 offsetBytes, u64 sizeBytes)noexcept;

template<typename... Pointers>
[[nodiscard]] constexpr bool AreAllPointersValid(Pointers... pointers)noexcept{
    return (... && (pointers != nullptr));
}

[[nodiscard]] constexpr bool IsTextureSubresourceRangeValid(const TextureSubresourceSet& subresources)noexcept{
    return subresources.numMipLevels != 0u && subresources.numArraySlices != 0u;
}

[[nodiscard]] constexpr bool IsPushConstantByteSizeValid(const u32 byteSize, const u32 maximumByteSize)noexcept{
    return byteSize != 0u && (byteSize & s_BufferAlignmentMask) == 0u && byteSize <= maximumByteSize;
}

[[nodiscard]] constexpr bool AreDispatchGroupCountsValid(
    const u32 groupsX,
    const u32 groupsY,
    const u32 groupsZ,
    const u32* const maximumGroupCounts
)noexcept{
    return
        maximumGroupCounts
        && groupsX <= maximumGroupCounts[0u]
        && groupsY <= maximumGroupCounts[1u]
        && groupsZ <= maximumGroupCounts[2u]
    ;
}

inline bool DebugValidateBufferRange(
    const BufferDesc& desc,
    const u64 offsetBytes,
    const u64 sizeBytes,
    TStringView operationName,
    TStringView rangeName
){
#if defined(NWB_DEBUG)
    if(!IsBufferRangeInBounds(desc, offsetBytes, sizeBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} offset {} size {} is outside buffer size {}")
            , operationName
            , rangeName
            , offsetBytes
            , sizeBytes
            , desc.byteSize
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: {} range is outside the buffer"), operationName, rangeName);
        return false;
    }
#else
    static_cast<void>(desc);
    static_cast<void>(offsetBytes);
    static_cast<void>(sizeBytes);
    static_cast<void>(operationName);
    static_cast<void>(rangeName);
#endif

    return true;
}

inline Expected<TextureSlice> DebugResolveTextureSlice(
    const TextureDesc& desc,
    const TextureSlice& slice,
    const TextureFormatBlockLayout& formatLayout,
    TStringView operationName,
    TStringView message
){
#if defined(NWB_DEBUG)
    const auto resolved = ResolveTextureSlice(desc, slice, formatLayout);
    if(!resolved){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
        return MakeUnexpected(resolved.error());
    }
    return *resolved;
#else
    static_cast<void>(formatLayout);
    static_cast<void>(operationName);
    static_cast<void>(message);
    return slice.resolve(desc);
#endif
}

inline bool DebugValidateTextureSliceExtentsMatch(
    const TextureSlice& first,
    const TextureSlice& second,
    TStringView operationName,
    TStringView message
){
#if defined(NWB_DEBUG)
    if(first.width != second.width || first.height != second.height || first.depth != second.depth){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
        return false;
    }
#else
    static_cast<void>(first);
    static_cast<void>(second);
    static_cast<void>(operationName);
    static_cast<void>(message);
#endif

    return true;
}

bool BufferRangesOverlap(u64 firstOffsetBytes, u64 firstSizeBytes, u64 secondOffsetBytes, u64 secondSizeBytes)noexcept;
u32 GetPushConstantByteSize(const BindingLayoutDesc& desc)noexcept;
bool ValidatePushConstantByteSize(const VulkanContext& context, u32 byteSize, TStringView operationName);
Expected<VkPipelineLayout> CreatePipelineLayout(const VulkanContext& context, const VkDescriptorSetLayout* setLayouts, u32 setLayoutCount, u32 pushConstantByteSize, TStringView operationName);
void DestroyPipelineAndOwnedLayout(const VulkanContext& context, VkPipeline& pipeline, VkPipelineLayout& pipelineLayout, bool& ownsPipelineLayout);
[[nodiscard]] Expected<VkBuildAccelerationStructureFlagsKHR> ConvertAccelStructBuildFlags(
    RayTracingAccelStructBuildFlags::Mask buildFlags,
    TStringView operationName
);
Expected<GraphicsPipelineFixedState> BuildGraphicsPipelineFixedState(
    const FramebufferInfo& fbinfo,
    const RenderState& renderState,
    PipelineStencilFaceMode::Enum stencilFaceMode,
    const VkDynamicState* dynamicStates,
    u32 dynamicStateCount,
    TStringView operationName,
    Alloc::ScratchArena& scratchArena
);
struct ClusterOperationInput{
    VkClusterAccelerationStructureInputInfoNV info = {};
    VkClusterAccelerationStructureMoveObjectsInputNV move = {};
    VkClusterAccelerationStructureTriangleClusterInputNV cluster = {};
    VkClusterAccelerationStructureClustersBottomLevelInputNV blas = {};

    [[nodiscard]] VkClusterAccelerationStructureInputInfoNV nativeInfo()noexcept{
        auto native = info;
        switch(info.opType){
        case VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_MOVE_OBJECTS_NV:
            native.opInput.pMoveObjects = &move;
            break;
        case VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV:
            native.opInput.pClustersBottomLevel = &blas;
            break;
        default:
            native.opInput.pTriangleClusters = &cluster;
            break;
        }
        return native;
    }
};

Expected<ClusterOperationInput> BuildClusterOperationInputInfo(
    const RayTracingClusterOperationParams& params,
    TStringView operationName
);
VkDescriptorType ConvertDescriptorType(ResourceType::Enum type)noexcept;
VkShaderStageFlags ConvertShaderStages(ShaderType::Mask stages)noexcept;
// Descriptor-buffer offset alignment clamped to a 32-bit value (1 when zero/oversized) for byte-offset math.
u32 GetDescriptorBufferOffsetAlignmentBytes(const VulkanContext& context)noexcept;
VkComponentTypeKHR ConvertCoopVecDataType(CooperativeVectorDataType::Enum type);
CooperativeVectorDataType::Enum ConvertCoopVecDataType(VkComponentTypeKHR type);
VkCooperativeVectorMatrixLayoutNV ConvertCoopVecMatrixLayout(CooperativeVectorMatrixLayout::Enum layout);
Expected<PipelineRenderingInfo> BuildPipelineRenderingInfo(const FramebufferInfo& fbinfo, TStringView operationName, Alloc::ScratchArena& scratchArena);

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

inline VkPipelineRasterizationStateCreateInfo BuildPipelineRasterizationState(
    const RasterState& rasterState,
    const VkPolygonMode polygonMode,
    const VkBool32 depthClampEnable
)noexcept{
    auto rasterizer = MakeVkStruct<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rasterizer.depthClampEnable = depthClampEnable;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = polygonMode;
    rasterizer.cullMode = ConvertCullMode(rasterState.cullMode);
    rasterizer.frontFace = rasterState.frontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable =
        rasterState.depthBias != 0 || rasterState.slopeScaledDepthBias != 0.0f ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = static_cast<f32>(rasterState.depthBias);
    rasterizer.depthBiasClamp = rasterState.depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = rasterState.slopeScaledDepthBias;
    rasterizer.lineWidth = s_DefaultRasterLineWidth;
    return rasterizer;
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

constexpr VkPipelineColorBlendAttachmentState ConvertBlendState(const BlendState::RenderTarget& target){
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

inline VkPipelineColorBlendStateCreateInfo BuildPipelineColorBlendState(const FramebufferInfo& fbinfo, const BlendState& blendState, PipelineColorBlendAttachmentVector& outBlendAttachments){
    const usize colorFormatCount = fbinfo.colorFormats.size();
    outBlendAttachments.clear();
    outBlendAttachments.reserve(colorFormatCount);
    for(usize i = 0u; i < colorFormatCount; ++i)
        outBlendAttachments.push_back(ConvertBlendState(blendState.targets[i]));

    auto colorBlending = MakeVkStruct<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = static_cast<u32>(outBlendAttachments.size());
    colorBlending.pAttachments = outBlendAttachments.data();
    return colorBlending;
}

Expected<VkPipelineMultisampleStateCreateInfo> ConfigurePipelineMultisampleState(u32 sampleCount, bool alphaToCoverageEnable, TStringView operationName);
void ConfigurePipelineDepthStencilState(const DepthStencilState& state, PipelineStencilFaceMode::Enum stencilFaceMode, VkPipelineDepthStencilStateCreateInfo& outState)noexcept;
VkSamplerCreateInfo BuildSamplerCreateInfo(const SamplerDesc& desc)noexcept;

inline void CopyHostMemory(
    CpuTaskScheduler& cpuScheduler,
    void* dst,
    const void* src,
    usize size,
    usize parallelThreshold = s_CopyHostMemoryParallelThreshold,
    usize chunkSize = s_CopyHostMemoryChunkSize
){
    if(!dst || !src || size == 0)
        return;

    const usize effectiveParallelThreshold = parallelThreshold > 0 ? parallelThreshold : 1;
    const usize effectiveChunkSize = chunkSize > 0 ? chunkSize : 1;

    if(cpuScheduler.isParallelEnabled() && size >= effectiveParallelThreshold){
        auto* dstBytes = static_cast<u8*>(dst);
        auto* srcBytes = static_cast<const u8*>(src);
        const usize chunkCount = DivideUp(size, effectiveChunkSize);
        cpuScheduler.parallelFor(static_cast<usize>(0), chunkCount, [&](usize chunkIndex){
            const usize chunkOffset = chunkIndex * effectiveChunkSize;
            const usize chunkBytes = Min(effectiveChunkSize, size - chunkOffset);
            NWB_MEMCPY(dstBytes + chunkOffset, chunkBytes, srcBytes + chunkOffset, chunkBytes);
        });
        return;
    }

    NWB_MEMCPY(dst, size, src, size);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

