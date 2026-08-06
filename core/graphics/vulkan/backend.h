// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <core/common/log.h>


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


namespace AccelStructCompactionMode{
    enum Enum : u8{
        Disabled = 0u,
        Allowed = 1u,
    };
};

namespace PipelineStencilFaceMode{
    enum Enum : u8{
        DepthOnly = 0u,
        IncludeStencilFaces = 1u,
    };
};

namespace IndirectDrawIndexMode{
    enum Enum : u8{
        NonIndexed = 0u,
        Indexed = 1u,
    };
};

namespace BufferImageCopyRequiredSize{
    enum Enum : u8{
        TouchedBytes = 0u,
        PaddedSlices = 1u,
    };
};

namespace BufferImageCopyPitchFields{
    enum Enum : u8{
        OmitImplicit = 0u,
        EmitExplicit = 1u,
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
){
    pipelineInfo.pViewportState = &fixedState.viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &fixedState.multisampling;
    pipelineInfo.pDepthStencilState = &fixedState.depthStencil;
    pipelineInfo.pColorBlendState = &fixedState.colorBlending;
    pipelineInfo.pDynamicState = &fixedState.dynamicState;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkAccessFlags2 GetVkAccessFlags(ResourceStates::Mask state);
VkPipelineStageFlags2 GetVkPipelineStageFlags(ResourceStates::Mask state, bool rayTracingStageAvailable);
VkImageLayout GetVkImageLayout(ResourceStates::Mask state);
VkFormat ConvertFormat(Format::Enum format);
VkSampleCountFlagBits GetSampleCountFlagBits(u32 sampleCount);
extern VkDeviceAddress GetBufferDeviceAddress(Buffer* bufferResource, u64 offset = 0);
bool IsSupportedSampleCount(u32 sampleCount);
bool ValidateTextureShape(const TextureDesc& desc, const tchar* operationName);
VkImageAspectFlags GetImageAspectMask(const FormatInfo& formatInfo);
bool GetTextureFormatBlockLayout(const FormatInfo& formatInfo, TextureFormatBlockLayout& outLayout);
bool ValidateBufferImageCopyAspectMask(VkImageAspectFlags aspectMask, const tchar* operationName);
VkExtent3D GetTextureMipExtent(const TextureDesc& desc, MipLevel mipLevel);
bool BuildBufferImageCopyLayout(
    const VkExtent3D& extent,
    const TextureFormatBlockLayout& formatLayout,
    u64 rowPitch,
    u64 depthPitch,
    BufferImageCopyRequiredSize::Enum requiredSizeMode,
    BufferImageCopyPitchFields::Enum pitchFields,
    const tchar* operationName,
    BufferImageCopyLayout& outLayout
);
VkImageSubresourceLayers BuildImageSubresourceLayers(
    VkImageAspectFlags aspectMask,
    MipLevel mipLevel,
    ArraySlice arraySlice,
    ArraySlice layerCount = 1u
);
VkImageSubresourceRange BuildImageSubresourceRange(const TextureSubresourceSet& subresources, VkImageAspectFlags aspectMask);
bool BuildTextureImageViewCreateInfo(
    Texture& texture,
    const TextureSubresourceSet& resolvedSubresources,
    TextureDimension::Enum dimension,
    Format::Enum format,
    const tchar* operationName,
    bool assertFailure,
    VkImageViewCreateInfo& outViewInfo
);
bool BuildImageViewCreateInfo(Texture& texture, const DescriptorWriteItem& item, VkImageViewCreateInfo& outViewInfo);
u64 ComputeStagingTextureOffset(
    const TextureSlice& resolvedSlice,
    const StagingTextureMipLayout& mipLayout,
    const TextureFormatBlockLayout& formatLayout,
    u64 arrayByteSize,
    usize* outRowPitch = nullptr,
    u32* outBufferRowLength = nullptr,
    u32* outBufferImageHeight = nullptr,
    u64* outRangeSize = nullptr
);
bool IsTextureSliceInBounds(const TextureDesc& desc, const TextureSlice& slice, const TextureFormatBlockLayout& formatLayout, TextureSlice* outResolved = nullptr);
bool IsBufferRangeInBounds(const BufferDesc& desc, u64 offsetBytes, u64 sizeBytes);

template<typename... Pointers>
inline bool DebugValidateNotNull(const tchar* operationName, const tchar* message, Pointers... pointers){
#if defined(NWB_DEBUG)
    if((... || (pointers == nullptr))){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
        return false;
    }
#else
    static_cast<void>(operationName);
    static_cast<void>(message);
    ((void)pointers, ...);
#endif

    return true;
}

inline bool DebugValidateBufferRange(
    const BufferDesc& desc,
    const u64 offsetBytes,
    const u64 sizeBytes,
    const tchar* operationName,
    const tchar* rangeName
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

inline bool DebugResolveTextureSlice(
    const TextureDesc& desc,
    const TextureSlice& slice,
    const TextureFormatBlockLayout& formatLayout,
    const tchar* operationName,
    const tchar* message,
    TextureSlice& outResolved
){
#if defined(NWB_DEBUG)
    if(!IsTextureSliceInBounds(desc, slice, formatLayout, &outResolved)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
        return false;
    }
#else
    static_cast<void>(formatLayout);
    static_cast<void>(operationName);
    static_cast<void>(message);
    outResolved = slice.resolve(desc);
#endif

    return true;
}

inline bool DebugValidateTextureSliceExtentsMatch(
    const TextureSlice& first,
    const TextureSlice& second,
    const tchar* operationName,
    const tchar* message
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

inline bool DebugValidateTextureSubresourceRange(const TextureSubresourceSet& subresources, const tchar* operationName){
#if defined(NWB_DEBUG)
    if(subresources.numMipLevels == 0 || subresources.numArraySlices == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: invalid subresource range"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: invalid subresource range"), operationName);
        return false;
    }
#else
    static_cast<void>(subresources);
    static_cast<void>(operationName);
#endif

    return true;
}

inline bool DebugValidateBufferImageCopyAspect(VkImageAspectFlags aspectMask, const tchar* operationName){
#if defined(NWB_DEBUG)
    if(!ValidateBufferImageCopyAspectMask(aspectMask, operationName)){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: combined depth/stencil buffer-image copies are not supported"), operationName);
        return false;
    }
#else
    static_cast<void>(aspectMask);
    static_cast<void>(operationName);
#endif

    return true;
}

bool BufferRangesOverlap(u64 firstOffsetBytes, u64 firstSizeBytes, u64 secondOffsetBytes, u64 secondSizeBytes);
u32 GetPushConstantByteSize(const BindingLayoutDesc& desc);
bool ValidatePushConstantByteSize(const VulkanContext& context, u32 byteSize, const tchar* operationName);
bool CreatePipelineLayout(const VulkanContext& context, const VkDescriptorSetLayout* setLayouts, u32 setLayoutCount, u32 pushConstantByteSize, VkPipelineLayout& outLayout, const tchar* operationName);
void DestroyPipelineAndOwnedLayout(VkDevice device, const VkAllocationCallbacks* allocationCallbacks, VkPipeline& pipeline, VkPipelineLayout& pipelineLayout, bool& ownsPipelineLayout);
VkBuildAccelerationStructureFlagsKHR ConvertAccelStructBuildFlags(RayTracingAccelStructBuildFlags::Mask buildFlags, AccelStructCompactionMode::Enum compactionMode);
bool BuildGraphicsPipelineFixedState(
    const FramebufferInfo& fbinfo,
    const RenderState& renderState,
    PipelineStencilFaceMode::Enum stencilFaceMode,
    const VkDynamicState* dynamicStates,
    u32 dynamicStateCount,
    const tchar* operationName,
    GraphicsPipelineFixedState& outState
);
bool BuildClusterOperationInputInfo(
    const RayTracingClusterOperationParams& params,
    VkClusterAccelerationStructureInputInfoNV& outInputInfo,
    VkClusterAccelerationStructureMoveObjectsInputNV& outMoveInput,
    VkClusterAccelerationStructureTriangleClusterInputNV& outClusterInput,
    VkClusterAccelerationStructureClustersBottomLevelInputNV& outBlasInput,
    const tchar* operationName
);
VkDescriptorType ConvertDescriptorType(ResourceType::Enum type);
VkShaderStageFlags ConvertShaderStages(ShaderType::Mask stages);
// Descriptor-buffer offset alignment clamped to a 32-bit value (1 when zero/oversized) for byte-offset math.
u32 GetDescriptorBufferOffsetAlignmentBytes(const VulkanContext& context);
VkComponentTypeKHR ConvertCoopVecDataType(CooperativeVectorDataType::Enum type);
CooperativeVectorDataType::Enum ConvertCoopVecDataType(VkComponentTypeKHR type);
VkCooperativeVectorMatrixLayoutNV ConvertCoopVecMatrixLayout(CooperativeVectorMatrixLayout::Enum layout);
bool BuildPipelineRenderingInfo(const FramebufferInfo& fbinfo, const tchar* operationName, VkPipelineRenderingCreateInfo& outRenderingInfo, PipelineRenderingFormatVector& outColorFormats);

template<typename T>
constexpr T MakeVkStruct(VkStructureType sType){
    T output{};
    output.sType = sType;
    return output;
}

constexpr VkCullModeFlags ConvertCullMode(RasterCullMode::Enum cullMode){
    switch(cullMode){
    case RasterCullMode::Back:  return VK_CULL_MODE_BACK_BIT;
    case RasterCullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case RasterCullMode::None:  return VK_CULL_MODE_NONE;
    default: return VK_CULL_MODE_BACK_BIT;
    }
}

constexpr VkPolygonMode ConvertFillMode(RasterFillMode::Enum fillMode){
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
){
    auto rasterizer = MakeVkStruct<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rasterizer.depthClampEnable = depthClampEnable;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = polygonMode;
    rasterizer.cullMode = ConvertCullMode(rasterState.cullMode);
    rasterizer.frontFace = rasterState.frontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = rasterState.depthBias != 0 ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = static_cast<f32>(rasterState.depthBias);
    rasterizer.depthBiasClamp = rasterState.depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = rasterState.slopeScaledDepthBias;
    rasterizer.lineWidth = s_DefaultRasterLineWidth;
    return rasterizer;
}

constexpr VkCompareOp ConvertCompareOp(ComparisonFunc::Enum compareFunc){
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

constexpr VkStencilOp ConvertStencilOp(StencilOp::Enum stencilOp){
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

constexpr VkStencilOpState ConvertStencilOpState(const DepthStencilState& dsState, const DepthStencilState::StencilOpDesc& stencilDesc){
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

constexpr VkBlendFactor ConvertBlendFactor(BlendFactor::Enum blendFactor){
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

constexpr VkBlendOp ConvertBlendOp(BlendOp::Enum blendOp){
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
    for(usize i = 0; i < colorFormatCount; ++i)
        outBlendAttachments.push_back(ConvertBlendState(blendState.targets[i]));

    auto colorBlending = MakeVkStruct<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = static_cast<u32>(outBlendAttachments.size());
    colorBlending.pAttachments = outBlendAttachments.data();
    return colorBlending;
}

bool ConfigurePipelineMultisampleState(const u32 sampleCount, const bool alphaToCoverageEnable, VkPipelineMultisampleStateCreateInfo& outState, const tchar* operationName);
void ConfigurePipelineDepthStencilState(const DepthStencilState& state, PipelineStencilFaceMode::Enum stencilFaceMode, VkPipelineDepthStencilStateCreateInfo& outState);
VkSamplerCreateInfo BuildSamplerCreateInfo(const SamplerDesc& desc);

inline void CopyHostMemory(
    Alloc::ThreadPool& workerPool,
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

    if(workerPool.isParallelEnabled() && size >= effectiveParallelThreshold){
        auto* dstBytes = static_cast<u8*>(dst);
        auto* srcBytes = static_cast<const u8*>(src);
        const usize chunkCount = DivideUp(size, effectiveChunkSize);
        workerPool.parallelFor(static_cast<usize>(0), chunkCount, [&](usize chunkIndex){
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


class Device;
class Queue;
class TrackedCommandBuffer;
class DescriptorBufferManager;

class Buffer;
class Texture;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core Vulkan objects and capabilities


struct VulkanContext{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkAllocationCallbacks* allocationCallbacks = nullptr;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;

    // Physical families used for Graphics/AsyncCompute resource sharing.
    i32 graphicsQueueFamilyIndex = s_InvalidQueueFamilyIndex;
    i32 asyncComputeQueueFamilyIndex = s_InvalidQueueFamilyIndex;
    bool asyncComputeLaneEnabled = false;

    Alloc::GlobalArena& objectArena;
    GraphicsAllocator& allocator;
    Alloc::ThreadPool& threadPool;

    VkPhysicalDeviceProperties physicalDeviceProperties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties{};

    // Lazily created descriptor-buffer gap-set layout for explicit heap sets.
    VkDescriptorSetLayout emptyDescriptorBufferSetLayout = VK_NULL_HANDLE;

    struct Extensions{
        bool KHR_synchronization2 = false;
        bool KHR_ray_tracing_pipeline = false;
        bool KHR_ray_query = false;
        bool KHR_acceleration_structure = false;
        bool buffer_device_address = false;
        bool EXT_descriptor_buffer = false;
        bool EXT_debug_utils = false;
        bool KHR_swapchain = false;
        bool KHR_dynamic_rendering = false;
        bool EXT_opacity_micromap = false;
        bool NV_cooperative_vector = false;
        bool NV_cluster_acceleration_structure = false;
        bool NV_device_diagnostic_checkpoints = false;
        bool EXT_device_fault = false;
        bool EXT_texture_compression_astc_hdr = false;
        bool AMD_buffer_marker = false;
        bool EXT_mesh_shader = false;
        bool KHR_fragment_shading_rate = false;
        bool EXT_ray_tracing_invocation_reorder = false;
        bool NV_ray_tracing_invocation_reorder = false;
        bool NV_ray_tracing_linear_swept_spheres = false;
    } extensions;

    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelStructProperties{};
    // Descriptor-buffer limits used for layout and offsets.
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProperties{};
    VkPhysicalDeviceCooperativeVectorPropertiesNV coopVecProperties{};
    VkPhysicalDeviceCooperativeVectorFeaturesNV coopVecFeatures{};
    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
    VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV rayTracingLinearSweptSpheresFeatures{};
    VkPhysicalDeviceClusterAccelerationStructurePropertiesNV nvClusterAccelerationStructureProperties{};
    // Core subgroup properties (the engine requires Vulkan 1.3).
    VkPhysicalDeviceSubgroupProperties subgroupProperties{};
    DescriptorBufferManager* descriptorBufferManager = nullptr;


    explicit VulkanContext(GraphicsAllocator& allocatorRef, Alloc::ThreadPool& threadPoolRef)
        : objectArena(allocatorRef.getObjectArena())
        , allocator(allocatorRef)
        , threadPool(threadPoolRef)
    {}
    VulkanContext(
        GraphicsAllocator& allocatorRef,
        Alloc::ThreadPool& threadPoolRef,
        VkInstance inst,
        VkPhysicalDevice physDev,
        VkDevice dev,
        VkAllocationCallbacks* allocCb)
        : instance(inst)
        , physicalDevice(physDev)
        , device(dev)
        , allocationCallbacks(allocCb)
        , objectArena(allocatorRef.getObjectArena())
        , allocator(allocatorRef)
        , threadPool(threadPoolRef)
    {}
};

// Temporary Vulkan queue-sharing view; familyIndices follow the source lifetime.
struct QueueFamilySharingInfo{
    VkSharingMode mode = VK_SHARING_MODE_EXCLUSIVE;
    Array<u32, 2u> familyIndices = {};
    u32 familyIndexCount = 0u;

    [[nodiscard]] const u32* data()const{ return familyIndexCount > 0u ? familyIndices.data() : nullptr; }
};

inline bool UsesConcurrentGraphicsAsyncComputeSharing(
    const ResourceQueueSharing::Mask sharing,
    const VulkanContext& context
){
    const u32 sharingBits = static_cast<u32>(sharing);
    const u32 requiredBits = static_cast<u32>(ResourceQueueSharing::GraphicsAndAsyncCompute);
    return
        context.asyncComputeLaneEnabled
        && context.graphicsQueueFamilyIndex != s_InvalidQueueFamilyIndex
        && context.asyncComputeQueueFamilyIndex != s_InvalidQueueFamilyIndex
        && context.graphicsQueueFamilyIndex != context.asyncComputeQueueFamilyIndex
        && (sharingBits & requiredBits) == requiredBits
    ;
}

inline QueueFamilySharingInfo ResolveQueueFamilySharing(
    const ResourceQueueSharing::Mask sharing,
    const VulkanContext& context
){
    QueueFamilySharingInfo result;
    if(!UsesConcurrentGraphicsAsyncComputeSharing(sharing, context))
        return result;

    result.mode = VK_SHARING_MODE_CONCURRENT;
    result.familyIndices[0] = static_cast<u32>(context.graphicsQueueFamilyIndex);
    result.familyIndices[1] = static_cast<u32>(context.asyncComputeQueueFamilyIndex);
    result.familyIndexCount = 2u;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command buffer with resource tracking


class TrackedCommandBuffer final : public RefCounter<GraphicsResource>, NoCopy{
    friend class CommandList;
    friend class Queue;


public:
    TrackedCommandBuffer(const VulkanContext& context, u32 queueFamilyIndex);
    ~TrackedCommandBuffer();


private:
    void clearTrackedReferences();


private:
    VkCommandBuffer m_cmdBuf = VK_NULL_HANDLE;
    VkCommandPool m_cmdPool = VK_NULL_HANDLE;

    Vector<Handle<GraphicsResource>, Alloc::GlobalArena> m_referencedResources;
    Vector<BufferHandle, Alloc::GlobalArena> m_referencedStagingBuffers;
    Vector<VkAccelerationStructureKHR, Alloc::GlobalArena> m_referencedAccelStructHandles;

    u64 m_recordingID = 0;
    u64 m_submissionID = 0;

    const VulkanContext& m_context;
};
typedef Handle<TrackedCommandBuffer> TrackedCommandBufferPtr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command queue wrapper with timeline semaphore tracking


class Queue final : NoCopy{
    friend class Device;


public:
    Queue(const VulkanContext& context, Device& device, CommandQueue::Enum queueID, VkQueue queue, u32 queueFamilyIndex);
    ~Queue();


public:
    [[nodiscard]] TrackedCommandBufferPtr createCommandBuffer();
    [[nodiscard]] TrackedCommandBufferPtr getOrCreateCommandBuffer();

    void addWaitSemaphore(VkSemaphore semaphore, u64 value);
    void addSignalSemaphore(VkSemaphore semaphore, u64 value);

    struct SubmissionWait{
        VkSemaphore semaphore = VK_NULL_HANDLE;
        u64 value = 0u;
    };

    u64 submit(
        CommandList* const* ppCmd,
        usize numCmd,
        const SubmissionWait* localWaits = nullptr,
        usize localWaitCount = 0u,
        bool* outSubmissionAccepted = nullptr
    );
    void updateLastFinishedID();

    void waitForIdle();


private:
    void clearPendingSemaphores();
    void recycleCommandBuffer(TrackedCommandBufferPtr&& cmdBuf);


private:
    VkSemaphore m_trackingSemaphore = VK_NULL_HANDLE;

    const VulkanContext& m_context;
    Device& m_device;

    VkQueue m_queue;
    CommandQueue::Enum m_queueID;
    u32 m_queueFamilyIndex;

    Futex m_mutex;
    Vector<VkSemaphore, Alloc::GlobalArena> m_waitSemaphores;
    Vector<u64, Alloc::GlobalArena> m_waitSemaphoreValues;
    Vector<VkSemaphore, Alloc::GlobalArena> m_signalSemaphores;
    Vector<u64, Alloc::GlobalArena> m_signalSemaphoreValues;

    u64 m_lastRecordingID = 0;
    u64 m_lastSubmittedID = 0;
    u64 m_lastFinishedID = 0;

    List<TrackedCommandBufferPtr, Alloc::GlobalArena> m_commandBuffersInFlight;
    List<TrackedCommandBufferPtr, Alloc::GlobalArena> m_commandBuffersPool;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles memory allocation


class VulkanAllocator final : NoCopy{
public:
    explicit VulkanAllocator(const VulkanContext& context);
    ~VulkanAllocator();


public:
    [[nodiscard]] bool initialize();

    VkResult createBuffer(Buffer& buffer, const VkBufferCreateInfo& bufferInfo);
    void destroyBuffer(Buffer& buffer);
    VkResult mapBufferMemory(Buffer& buffer, void** outData);
    void unmapBufferMemory(Buffer& buffer);
    VkResult invalidateBufferMemory(Buffer& buffer);

    VkResult createTexture(Texture& texture, const VkImageCreateInfo& imageInfo);
    void destroyTexture(Texture& texture);

    VkResult createStagingTexture(StagingTexture& texture, const VkBufferCreateInfo& bufferInfo, CpuAccessMode::Enum cpuAccess);
    void destroyStagingTexture(StagingTexture& texture);
    VkResult mapStagingTextureMemory(StagingTexture& texture, void** outData);
    void unmapStagingTextureMemory(StagingTexture& texture);
    VkResult invalidateStagingTextureMemory(StagingTexture& texture, u64 offset, u64 size);
    VkResult allocateHeap(Heap& heap);
    void freeHeap(Heap& heap);
    VkResult bindHeapBufferMemory(Buffer& buffer, Heap& heap, u64 offset);
    VkResult bindHeapTextureMemory(Texture& texture, Heap& heap, u64 offset);
    VkResult createHostMappedBuffer(
        VkBuffer& buffer,
        VulkanAllocationHandle& allocation,
        void*& mappedMemory,
        const VkBufferCreateInfo& bufferInfo
    );
    void destroyHostMappedBuffer(VkBuffer& buffer, VulkanAllocationHandle& allocation, void*& mappedMemory);


private:
    const VulkanContext& m_context;
    VulkanAllocatorHandle m_allocator = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device memory for placed resources


class Heap final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class VulkanAllocator;
    friend class Queue;


public:
    Heap(const VulkanContext& context, VulkanAllocator& allocator);
    ~Heap();


public:
    [[nodiscard]] const HeapDesc& getDescription()const{ return m_desc; }
    Object getNativeHandle(ObjectType objectType);


private:
    HeapDesc m_desc;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VulkanAllocationHandle m_allocation = nullptr;
    VkDeviceSize m_memoryOffset = 0;
    u32 m_memoryTypeIndex = UINT32_MAX;

    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles staging buffer uploads


class UploadManager final : NoCopy{
private:
    struct BufferChunk final : public RefCounter<GraphicsResource>{
        BufferHandle buffer;
        TrackedCommandBuffer* owner;
        CommandQueue::Enum queueID;
        u64 size;
        u64 allocated;
        u64 version;


        BufferChunk(Alloc::ThreadPool& pool, BufferHandle buf, TrackedCommandBuffer* chunkOwner, CommandQueue::Enum queue, u64 sz);
        ~BufferChunk();
    };
    using BufferChunkPtr = RefCountPtr<BufferChunk>;
    using BufferChunkList = List<BufferChunkPtr, Alloc::GlobalArena>;
    using ChunkRecyclePredicate = bool (*)(TrackedCommandBuffer* owner, const void* context);


public:
    UploadManager(Device& pParent, u64 defaultChunkSize, u64 memoryLimit, bool isScratchBuffer);
    ~UploadManager();


public:
    void clear();
    bool suballocateBuffer(
        u64 size,
        Buffer** pBuffer,
        u64* pOffset,
        void** pCpuVA,
        TrackedCommandBuffer* owner,
        CommandQueue::Enum queueID,
        u64 completedVersion,
        u32 alignment = s_DefaultUploadSuballocationAlignment
    );
    void submitChunks(CommandQueue::Enum queueID, u64 submittedVersion, TrackedCommandBuffer* const* submittedOwners, usize submittedOwnerCount);
    void discardChunks(CommandQueue::Enum queueID, TrackedCommandBuffer* owner, u64 reusableVersion);


private:
    void trimChunkPoolLocked(const u64* completedVersions);
    BufferChunkList::iterator recycleActiveChunkLocked(BufferChunkList& activeChunks, BufferChunkList::iterator it, u64 version, bool resetAllocated);
    void recycleMatchingActiveChunks(u32 queueIndex, u64 version, bool resetAllocated, const u64* completedVersions, ChunkRecyclePredicate predicate, const void* predicateContext);

    Device& m_device;
    u64 m_defaultChunkSize;
    u64 m_memoryLimit;
    bool m_isScratchBuffer;
    Futex m_mutex;
    u64 m_chunkPoolBytes = 0;

    BufferChunkList m_chunkPool;
    BufferChunkList m_activeChunks[static_cast<u32>(CommandQueue::kCount)];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Buffer


class Buffer final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class StateTracker;
    friend class VulkanAllocator;
    friend class UploadManager;
    friend class ShaderTable;

    friend VkDeviceAddress VulkanDetail::GetBufferDeviceAddress(Buffer* bufferResource, u64 offset);


private:
    struct BufferViewEntry{
        Format::Enum format = Format::UNKNOWN;
        u64 byteOffset = 0;
        u64 byteSize = 0;
        VkBufferView view = VK_NULL_HANDLE;
    };


public:
    // For volatile buffers - version tracking per frame
    struct VolatileBufferState{
        i32 latestVersion = 0;
        i32 minVersion = 0;
        i32 maxVersion = 0;
        bool initialized = false;
    };


public:
    Buffer(const VulkanContext& context, VulkanAllocator& allocator);
    ~Buffer();


public:
    [[nodiscard]] const BufferDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] GpuVirtualAddress getGpuVirtualAddress()const{ return m_deviceAddress; }


private:
    [[nodiscard]] VkBufferView getView(Format::Enum format, u64 byteOffset, u64 byteSize);

private:
    BufferDesc m_desc;

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VulkanAllocationHandle m_allocation = nullptr;
    u64 m_deviceAddress = 0;
    void* m_mappedMemory = nullptr;

    Vector<u64, Alloc::GlobalArena> m_versionTracking;
    Vector<BufferViewEntry, Alloc::GlobalArena> m_bufferViews;
    VolatileBufferState m_volatileState;
    Futex m_bufferViewsMutex;

    bool m_persistentlyMapped = false;
    bool m_requiresInvalidate = false;
    bool m_managed = true; // if true, owns the VkBuffer or VMA allocation

    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


inline UploadManager::BufferChunk::BufferChunk(
    Alloc::ThreadPool& pool,
    BufferHandle buf,
    TrackedCommandBuffer* chunkOwner,
    CommandQueue::Enum queue,
    u64 sz
)
    : RefCounter<GraphicsResource>(pool)
    , buffer(Move(buf))
    , owner(chunkOwner)
    , queueID(queue)
    , size(sz)
    , allocated(0)
    , version(0)
{}
inline UploadManager::BufferChunk::~BufferChunk() = default;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Texture


struct TextureViewKey{
    TextureSubresourceSet subresources;
    TextureDimension::Enum dimension = TextureDimension::Unknown;
    Format::Enum format = Format::UNKNOWN;
};

inline bool operator==(const TextureViewKey& lhs, const TextureViewKey& rhs)noexcept{
    return lhs.subresources == rhs.subresources && lhs.dimension == rhs.dimension && lhs.format == rhs.format;
}

struct TextureViewKeyHasher{
    usize operator()(const TextureViewKey& value)const noexcept{
        usize seed = 0;
        ::HashCombine(seed, value.subresources);
        ::HashCombine(seed, static_cast<u32>(value.dimension));
        ::HashCombine(seed, static_cast<u32>(value.format));
        return seed;
    }
};


class Texture final : public RefCounter<GraphicsResource>, NoCopy{
    friend bool VulkanDetail::BuildTextureImageViewCreateInfo(
        Texture& texture,
        const TextureSubresourceSet& resolvedSubresources,
        TextureDimension::Enum dimension,
        Format::Enum format,
        const tchar* operationName,
        bool assertFailure,
        VkImageViewCreateInfo& outViewInfo
    );
    friend bool VulkanDetail::BuildImageViewCreateInfo(
        Texture& texture,
        const DescriptorWriteItem& item,
        VkImageViewCreateInfo& outViewInfo
    );

    friend class BackendContext;
    friend class Device;
    friend class CommandList;
    friend class StateTracker;
    friend class VulkanAllocator;
    friend class Queue;


public:
    Texture(const VulkanContext& context, VulkanAllocator& allocator);
    ~Texture();


public:
    [[nodiscard]] const TextureDesc& getDescription()const{ return m_desc; }
    Object getNativeHandle(ObjectType objectType);
    Object getNativeView(ObjectType objectType, Format::Enum format, TextureSubresourceSet subresources, TextureDimension::Enum dimension, bool);

    [[nodiscard]] VkImageView getView(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format);


private:
    TextureDesc m_desc;
    VulkanDetail::TextureFormatBlockLayout m_formatLayout;
    VkImageAspectFlags m_aspectMask = 0;

    VkImage m_image = VK_NULL_HANDLE;
    VulkanAllocationHandle m_allocation = nullptr;
    VkImageCreateInfo m_imageInfo{};

    HashMap<TextureViewKey, VkImageView, TextureViewKeyHasher, EqualTo<TextureViewKey>, Alloc::GlobalArena> m_views;

    bool m_managed = true; // if true, owns the VkImage or VMA allocation
    bool m_keepInitialStateKnown = false;

    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Staging Texture


class StagingTexture final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class VulkanAllocator;


public:
    StagingTexture(const VulkanContext& context, VulkanAllocator& allocator);
    ~StagingTexture();


public:
    [[nodiscard]] const TextureDesc& getDescription()const{ return m_desc; }


private:
    TextureDesc m_desc;
    VulkanDetail::TextureFormatBlockLayout m_formatLayout;
    VkImageAspectFlags m_aspectMask = 0;
    u64 m_arrayByteSize = 0;
    VulkanDetail::StagingTextureMipLayoutVector m_mipLayouts;

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VulkanAllocationHandle m_allocation = nullptr;
    void* m_mappedMemory = nullptr;
    bool m_persistentlyMapped = false;
    bool m_requiresInvalidate = false;
    CpuAccessMode::Enum m_cpuAccess{};

    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampler


class Sampler final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class DescriptorBufferManager;


public:
    Sampler(const VulkanContext& context);
    ~Sampler();


public:
    [[nodiscard]] const SamplerDesc& getDescription()const{ return m_desc; }


private:
    SamplerDesc m_desc;
    VkSampler m_sampler = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader


class Shader final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class ShaderLibrary;


public:
    Shader(const VulkanContext& context);
    ~Shader();


public:
    [[nodiscard]] const ShaderDesc& getDescription()const{ return m_desc; }
    void getBytecode(const void** ppBytecode, usize* pSize)const{
        *ppBytecode = m_spirvWords.data();
        *pSize = m_spirvWords.size() * sizeof(u32);
    }


private:
    [[nodiscard]] VkSpecializationInfo makeSpecializationInfo()const;


private:
    ShaderDesc m_desc;
    VkShaderModule m_shaderModule = VK_NULL_HANDLE;

    Vector<u32, Alloc::GlobalArena> m_spirvWords;
    GraphicsString m_entryPointName;

    Vector<VkSpecializationMapEntry, Alloc::GlobalArena> m_specializationEntries;
    Vector<u32, Alloc::GlobalArena> m_specializationData;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader Library


struct ShaderLibraryKey{
    explicit ShaderLibraryKey(GraphicsArena& arena)
        : entryName(arena)
    {}
    ShaderLibraryKey(GraphicsArena& arena, const AStringView inEntryName, const ShaderType::Mask inShaderType)
        : entryName(inEntryName, arena)
        , shaderType(inShaderType)
    {}

    GraphicsString entryName;
    ShaderType::Mask shaderType = ShaderType::None;
};

inline bool operator==(const ShaderLibraryKey& lhs, const ShaderLibraryKey& rhs)noexcept{
    return lhs.entryName == rhs.entryName && lhs.shaderType == rhs.shaderType;
}

struct ShaderLibraryKeyHasher{
    usize operator()(const ShaderLibraryKey& value)const noexcept{
        usize seed = Hasher<GraphicsString>{}(value.entryName);
        ::HashCombine(seed, static_cast<u32>(value.shaderType));
        return seed;
    }
};


class ShaderLibrary final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;


public:
    ShaderLibrary(const VulkanContext& context);
    ~ShaderLibrary();


public:
    void getBytecode(const void** ppBytecode, usize* pSize)const;
    ShaderHandle getShader(AStringView entryName, ShaderType::Mask shaderType);


private:
    Vector<u32, Alloc::GlobalArena> m_spirvWords;
    HashMap<ShaderLibraryKey, Handle<Shader>, ShaderLibraryKeyHasher, EqualTo<ShaderLibraryKey>, GraphicsArena> m_shaders;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input Layout


class InputLayout final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    InputLayout(const VulkanContext& context);
    ~InputLayout() = default;


public:
    [[nodiscard]] const VertexAttributeDesc* getAttributeDescription(u32 index)const{
        if(index >= m_attributes.size())
            return nullptr;
        return &m_attributes[index];
    }

    [[nodiscard]] u32 getNumAttributes()const{ return static_cast<u32>(m_attributes.size()); }


private:
    Vector<VertexAttributeDesc, Alloc::GlobalArena> m_attributes;
    Vector<VkVertexInputBindingDescription, Alloc::GlobalArena> m_bindings;
    Vector<VkVertexInputAttributeDescription, Alloc::GlobalArena> m_vkAttributes;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Framebuffer


class Framebuffer final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    Framebuffer(const VulkanContext& context);
    ~Framebuffer();


public:
    [[nodiscard]] const FramebufferDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] const FramebufferInfoEx& getFramebufferInfo()const{ return m_framebufferInfo; }


private:
    FramebufferDesc m_desc;
    FramebufferInfoEx m_framebufferInfo;

    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;

    Vector<TextureHandle, Alloc::GlobalArena> m_resources;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pipeline binding


using PipelineShaderStageVector = Vector<VkPipelineShaderStageCreateInfo, Alloc::ScratchArena>;
using PipelineSpecializationInfoVector = Vector<VkSpecializationInfo, Alloc::ScratchArena>;

struct PipelineBindingState{
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    bool m_ownsPipelineLayout = false;
    u32 m_pushConstantByteSize = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void DestroyPipelineResource(const VulkanContext& context, PipelineBindingState& state, VkPipeline& pipeline){
    DestroyPipelineAndOwnedLayout(
        context.device,
        context.allocationCallbacks,
        pipeline,
        state.m_pipelineLayout,
        state.m_ownsPipelineLayout
    );
}

inline Object GetPipelineNativeHandle(const VkPipeline pipeline, const ObjectType objectType){
    if(objectType == ObjectTypes::VK_Pipeline)
        return Object(pipeline);
    return Object(nullptr);
}

inline void AttachPipelineBindingState(
    VkComputePipelineCreateInfo& pipelineInfo,
    const PipelineBindingState& bindingState,
    const void* next = nullptr
){
    pipelineInfo.pNext = next;
    pipelineInfo.layout = bindingState.m_pipelineLayout;
    pipelineInfo.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
}

inline void AttachPipelineBindingState(
    VkGraphicsPipelineCreateInfo& pipelineInfo,
    const PipelineBindingState& bindingState,
    const void* next = nullptr
){
    pipelineInfo.pNext = next;
    pipelineInfo.layout = bindingState.m_pipelineLayout;
    pipelineInfo.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Descriptor-buffer manager: host-mapped resource/sampler segments bind by byte offset.

namespace DescriptorBufferSegmentKind{
    enum Enum : u8{
        None = 0,
        Resource,
        Sampler,
    };
};

struct DescriptorBufferSegment{
    DescriptorBufferSegmentKind::Enum kind = DescriptorBufferSegmentKind::None;
    u32 offsetBytes = 0;
    u32 sizeBytes = 0;
    // Prevents stale handles from writing a recycled byte range.
    u64 allocationSerial = 0;

    [[nodiscard]] bool valid()const{
        return (kind == DescriptorBufferSegmentKind::Resource || kind == DescriptorBufferSegmentKind::Sampler)
            && sizeBytes > 0
            && allocationSerial != 0u
        ;
    }
};

class DescriptorBufferManager final : NoCopy{
private:
    // Mergeable free byte range.
    struct FreeRange{
        u32 offsetBytes = 0;
        u32 sizeBytes = 0;
    };

    // Host-mapped descriptor segment with synchronized free ranges and live-allocation tracking.
    struct SegmentStorage{
        VkBuffer buffer = VK_NULL_HANDLE;
        VulkanAllocationHandle allocation = nullptr;
        void* mappedMemory = nullptr;
        VkDeviceAddress deviceAddress = 0;
        u32 capacityBytes = 0;
        u32 writableOffsetBytes = 0;
        // Preserve serial across reinitialization so stale handles stay invalid.
        u64 nextAllocationSerial = 1u;
        VkDescriptorBufferBindingInfoEXT bindingInfo{};
        Futex mutex;
        Vector<FreeRange, Alloc::GlobalArena> freeRanges;
        Vector<DescriptorBufferSegment, Alloc::GlobalArena> liveAllocations;


        explicit SegmentStorage(Alloc::GlobalArena& arena)
            : freeRanges(arena)
            , liveAllocations(arena)
        {}
    };


public:
    DescriptorBufferManager(const VulkanContext& context, VulkanAllocator& allocator);
    ~DescriptorBufferManager();


public:
    bool initialize();
    void shutdown();

    [[nodiscard]] bool isEnabled()const{ return m_enabled; }

    // Exact driver descriptor size; 0 when descriptor buffers are disabled.
    [[nodiscard]] u32 getDescriptorSize(VkDescriptorType descriptorType)const;
    [[nodiscard]] u32 getOffsetAlignmentBytes()const;

    // Stable after initialize().
    [[nodiscard]] const VkDescriptorBufferBindingInfoEXT& getResourceBindingInfo()const{ return m_resourceSegment.bindingInfo; }
    [[nodiscard]] const VkDescriptorBufferBindingInfoEXT& getSamplerBindingInfo()const{ return m_samplerSegment.bindingInfo; }
    // Resource and sampler buffer indices in bind order.
    [[nodiscard]] u32 getResourceBufferIndex()const{ return 0; }
    [[nodiscard]] u32 getSamplerBufferIndex()const{ return 1; }

    // Allocates aligned, zeroed descriptor bytes from free ranges or the bump pointer.
    [[nodiscard]] DescriptorBufferSegment allocate(DescriptorBufferSegmentKind::Enum kind, u32 sizeBytes, u32 alignmentBytes);
    void free(const DescriptorBufferSegment& segment);

    // Validates allocation identity, then encodes one descriptor.
    bool writeDescriptor(const DescriptorWriteItem& item, const DescriptorBufferSegment& allocation, u32 dstOffsetBytes, VkDescriptorType descriptorType);


private:
    bool initializeSegment(SegmentStorage& segment, const ACompactString& debugName, u32 capacityBytes);
    void shutdownSegment(SegmentStorage& segment);


private:
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
    bool m_enabled = false;
    SegmentStorage m_resourceSegment;
    SegmentStorage m_samplerSegment;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Global Descriptor Heap


// Descriptor-buffer-only bindless heap at sets 8/9; required at device creation.
class GpuDescriptorHeap final : NoCopy{
    friend class Device;
    friend class CommandList;


public:
    explicit GpuDescriptorHeap(Device& device);
    ~GpuDescriptorHeap();


public:
    bool initialize(const GpuDescriptorHeapDesc& desc);
    void shutdown();

    [[nodiscard]] bool isInitialized()const{ return m_initialized; }

    // Returns invalid (logged) when the class namespace is exhausted.
    [[nodiscard]] GpuDescriptorHandle allocate(GpuDescriptorClass::Enum descriptorClass);

    // Quarantines freed slots for frames in flight.
    void free(GpuDescriptorHandle handle);

    // Caller must not overwrite a slot read by in-flight work.
    bool write(GpuDescriptorHandle handle, const DescriptorWriteItem& item);

    // Bind after state setup; optionally includes a per-generation TLAS block at set 10.
    void bindCompute(
        CommandList& commandList,
        const ComputePipeline& pipeline,
        GpuDescriptorHandle accelStructHandle = GpuDescriptorHandle::invalid()
    );

    // Graphics equivalents of bindCompute.
    void bindGraphics(CommandList& commandList, const GraphicsPipeline& pipeline);
    void bindGraphics(CommandList& commandList, const MeshletPipeline& pipeline);

    // Ray-tracing equivalent of bindCompute; optionally binds a TLAS block at set 10.
    void bindRayTracing(
        CommandList& commandList,
        const RayTracingPipeline& pipeline,
        GpuDescriptorHandle accelStructHandle = GpuDescriptorHandle::invalid()
    );

    // Advances deferred-free slot retirement.
    void advanceFrame();

    [[nodiscard]] u32 getResourceCapacity()const{ return m_resourceSlots.capacity; }
    [[nodiscard]] u32 getSamplerCapacity()const{ return m_samplerSlots.capacity; }
    [[nodiscard]] u32 getResourceSetIndex()const{ return m_desc.bindlessHeapAbi.resourceSetIndex; }
    [[nodiscard]] u32 getSamplerSetIndex()const{ return m_desc.bindlessHeapAbi.samplerSetIndex; }
    [[nodiscard]] u32 getAccelStructSetIndex()const{ return m_desc.bindlessHeapAbi.accelStructSetIndex; }
    // Resource and sampler layouts at sets 8 and 9.
    [[nodiscard]] const BindingLayoutHandle& getResourceLayout()const{ return m_resourceLayout; }
    [[nodiscard]] const BindingLayoutHandle& getSamplerLayout()const{ return m_samplerLayout; }
    // Per-generation one-descriptor TLAS layout at set 10.
    [[nodiscard]] const BindingLayoutHandle& getAccelStructLayout()const{ return m_accelStructLayout; }
    [[nodiscard]] bool hasAccelStructLayout()const{ return m_accelStructLayout != nullptr; }
    // SPIR-V binding number for a descriptor class.
    [[nodiscard]] u32 getRegisterSlot(GpuDescriptorClass::Enum descriptorClass)const;
    // Persistent segment blocks bound at heap sets.
    [[nodiscard]] const DescriptorBufferSegment& getResourceBufferBlock()const{ return m_resourceBufferBlock; }
    [[nodiscard]] const DescriptorBufferSegment& getSamplerBufferBlock()const{ return m_samplerBufferBlock; }
    [[nodiscard]] DescriptorBufferSegment getAccelStructBufferBlock(GpuDescriptorHandle handle)const;


private:
    // Fresh indices plus a recycled free list per namespace.
    struct SlotAllocator{
        u32 capacity = 0;
        u32 nextFresh = 0;
        Vector<u32, Alloc::GlobalArena> freeList;
        // Rejects double frees and retired-handle writes.
        Vector<u8, Alloc::GlobalArena> liveSlots;

        explicit SlotAllocator(Alloc::GlobalArena& arena)
            : freeList(arena)
            , liveSlots(arena)
        {}
    };
    struct RetiredSlot{
        GpuDescriptorHandle handle;
        u64 retireAtFrame = 0;
    };

    [[nodiscard]] SlotAllocator& allocatorForClass(GpuDescriptorClass::Enum descriptorClass);
    void releaseAccelStructDescriptorBlock(u32 slot);
    void releaseRetainedDescriptorResource(GpuDescriptorHandle handle);

    // Allocates persistent resource/sampler blocks; TLAS blocks are per handle.
    bool initializeDescriptorBufferBlocks(u32 offsetAlignmentBytes);
    // Encodes one descriptor at the class/slot offset.
    bool writeDescriptorBuffer(const DescriptorWriteItem& writeItem, GpuDescriptorClass::Enum descriptorClass);


private:
    Device& m_device;
    const VulkanContext& m_context;

    GpuDescriptorHeapDesc m_desc;

    BindingLayoutHandle m_resourceLayout;
    BindingLayoutHandle m_samplerLayout;
    BindingLayoutHandle m_accelStructLayout;

    DescriptorBufferSegment m_resourceBufferBlock{};
    DescriptorBufferSegment m_samplerBufferBlock{};
    // TLAS blocks are immutable per generation until deferred free.
    Vector<DescriptorBufferSegment, Alloc::GlobalArena> m_accelStructBufferBlocks;
    Vector<RayTracingAccelStructHandle, Alloc::GlobalArena> m_accelStructResources;
    // Resource keep-alives protect descriptors used by in-flight work.
    Vector<Handle<GraphicsResource>, Alloc::GlobalArena> m_resourceDescriptorResources;
    Vector<Handle<GraphicsResource>, Alloc::GlobalArena> m_samplerDescriptorResources;
    u32 m_accelStructBufferBindingOffset = 0u;
    // Binding byte offsets within a set block.
    u32 m_classBufferOffset[GpuDescriptorClass::kCount] = {};

    SlotAllocator m_resourceSlots;
    SlotAllocator m_samplerSlots;
    SlotAllocator m_accelStructSlots;

    Vector<RetiredSlot, Alloc::GlobalArena> m_retired;
    u64 m_frameCounter = 0;

    mutable Futex m_mutex;
    bool m_initialized = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Graphics Pipeline


class GraphicsPipeline final : public RefCounter<GraphicsResource>, public PipelineBindingState, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    GraphicsPipeline(const VulkanContext& context);
    ~GraphicsPipeline();


public:
    [[nodiscard]] const GraphicsPipelineDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] const FramebufferInfo& getFramebufferInfo()const{ return m_framebufferInfo; }
    Object getNativeHandle(ObjectType objectType);


private:
    GraphicsPipelineDesc m_desc;
    FramebufferInfo m_framebufferInfo;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compute Pipeline


class ComputePipeline final : public RefCounter<GraphicsResource>, public PipelineBindingState, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    ComputePipeline(const VulkanContext& context);
    ~ComputePipeline();


public:
    [[nodiscard]] const ComputePipelineDesc& getDescription()const{ return m_desc; }
    Object getNativeHandle(ObjectType objectType);


private:
    ComputePipelineDesc m_desc;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Meshlet Pipeline


class MeshletPipeline final : public RefCounter<GraphicsResource>, public PipelineBindingState, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    MeshletPipeline(const VulkanContext& context);
    ~MeshletPipeline();


public:
    [[nodiscard]] const MeshletPipelineDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] const FramebufferInfo& getFramebufferInfo()const{ return m_framebufferInfo; }
    Object getNativeHandle(ObjectType objectType);


private:
    MeshletPipelineDesc m_desc;
    FramebufferInfo m_framebufferInfo;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing Pipeline


class RayTracingPipeline final : public RefCounter<GraphicsResource>, public PipelineBindingState, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class ShaderTable;


public:
    RayTracingPipeline(const VulkanContext& context, Device& device);
    ~RayTracingPipeline();


public:
    [[nodiscard]] const RayTracingPipelineDesc& getDescription()const{ return m_desc; }
    RayTracingShaderTableHandle createShaderTable();
    Object getNativeHandle(ObjectType objectType);


private:
    RayTracingPipelineDesc m_desc;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    // vkGetRayTracingShaderGroupHandlesKHR returns tightly packed handles; SBT records add alignment later.
    Vector<u8, Alloc::GlobalArena> m_shaderGroupHandles;

    const VulkanContext& m_context;
    Device& m_device;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader Table


class ShaderTable final : public RefCounter<GraphicsResource>, NoCopy{
    friend class CommandList;
    friend class RayTracingPipeline;


public:
    ShaderTable(const VulkanContext& context, Device& device);
    ~ShaderTable();


public:
    void setRayGenerationShader(AStringView exportName);
    u32 addMissShader(AStringView exportName);
    u32 addHitGroup(AStringView exportName);
    u32 addCallableShader(AStringView exportName);
    void clearMissShaders();
    void clearHitShaders();
    void clearCallableShaders();
    RayTracingPipeline* getPipeline(){ return m_pipeline.get(); }
    Object getNativeHandle(ObjectType objectType);


private:
    void allocateSBTBuffer(BufferHandle& outBuffer, u64 sbtSize);
    u32 appendShaderRecord(
        AStringView exportName,
        BufferHandle& buffer,
        u64& offset,
        u32& count,
        const tchar* operationName,
        const tchar* recordName,
        const tchar* exportKind
    );
    u32 findGroupIndex(AStringView exportName)const;


private:
    Handle<RayTracingPipeline> m_pipeline;

    BufferHandle m_raygenBuffer;
    BufferHandle m_missBuffer;
    BufferHandle m_hitBuffer;
    BufferHandle m_callableBuffer;

    u64 m_raygenOffset = 0;
    u64 m_missOffset = 0;
    u64 m_hitOffset = 0;
    u64 m_callableOffset = 0;

    const VulkanContext& m_context;
    Device& m_device;

    u32 m_missCount = 0;
    u32 m_hitCount = 0;
    u32 m_callableCount = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Binding Layout


class BindingLayout final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    BindingLayout(const VulkanContext& context);
    ~BindingLayout();


public:
    [[nodiscard]] const BindingLayoutDesc* getDescription()const{ return m_isBindless ? nullptr : &m_desc; }
    [[nodiscard]] const BindlessLayoutDesc* getBindlessDesc()const{ return m_isBindless ? &m_bindlessDesc : nullptr; }
    Object getNativeHandle(ObjectType){ return Object(m_pipelineLayout); }

public:
    [[nodiscard]] const BindingLayoutDesc& getBindingLayoutDesc()const{ return m_desc; }
    // Descriptor-buffer metadata; layouts must be pure resource or sampler sets.
    [[nodiscard]] bool isDescriptorBufferCompatible()const{ return m_descriptorBufferCompatible; }
    [[nodiscard]] u32 getDescriptorBufferSetSizeBytes()const{ return m_descriptorBufferSetSizeBytes; }
    [[nodiscard]] DescriptorBufferSegmentKind::Enum getDescriptorBufferSegmentKind()const{ return m_descriptorBufferSegmentKind; }
    [[nodiscard]] const HashMap<u32, u32, Hasher<u32>, EqualTo<u32>, Alloc::GlobalArena>& getDescriptorBufferBindingOffsets()const{ return m_descriptorBufferBindingOffsets; }


private:
    BindingLayoutDesc m_desc;
    BindlessLayoutDesc m_bindlessDesc;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    Vector<VkDescriptorSetLayout, Alloc::GlobalArena> m_descriptorSetLayouts;
    // Driver-created set size, segment, and binding offsets.
    u32 m_descriptorBufferSetSizeBytes = 0;
    DescriptorBufferSegmentKind::Enum m_descriptorBufferSegmentKind = DescriptorBufferSegmentKind::None;
    HashMap<u32, u32, Hasher<u32>, EqualTo<u32>, Alloc::GlobalArena> m_descriptorBufferBindingOffsets;

    const VulkanContext& m_context;
    u32 m_pushConstantByteSize = 0;
    bool m_isBindless = false;
    bool m_descriptorBufferCompatible = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Acceleration Structure


class AccelStruct final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    AccelStruct(const VulkanContext& context);
    ~AccelStruct();


public:
    [[nodiscard]] const RayTracingAccelStructDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] bool isCompacted()const{ return m_compacted; }
    [[nodiscard]] u64 getDeviceAddress()const{ return m_deviceAddress; }
    // Exposed for explicit scheduling handoffs.
    [[nodiscard]] Buffer* getBackingBuffer()const{ return m_buffer.get(); }
    Object getNativeHandle(ObjectType objectType);


private:
    RayTracingAccelStructDesc m_desc;
    VkAccelerationStructureKHR m_accelStruct = VK_NULL_HANDLE;
    BufferHandle m_buffer;
    u64 m_deviceAddress = 0;
    VkQueryPool m_compactionQueryPool = VK_NULL_HANDLE;

    const VulkanContext& m_context;
    u32 m_compactionQueryIndex = 0;
    bool m_compacted = false;
    bool m_built = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Opacity Micromap


class OpacityMicromap final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    OpacityMicromap(const VulkanContext& context);
    ~OpacityMicromap();


public:
    [[nodiscard]] const RayTracingOpacityMicromapDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] bool isCompacted()const{ return m_compacted; }
    [[nodiscard]] u64 getDeviceAddress()const{ return m_deviceAddress; }


private:
    RayTracingOpacityMicromapDesc m_desc;
    BufferHandle m_dataBuffer;
    VkMicromapEXT m_micromap = VK_NULL_HANDLE;
    u64 m_deviceAddress = 0;
    bool m_compacted = false;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// State Tracker


struct TextureSubresourceStateKey{
    Texture* texture = nullptr;
    MipLevel mipLevel = 0;
    ArraySlice arraySlice = 0;
};

struct TextureSubresourceStateKeyHasher{
    [[nodiscard]] usize operator()(const TextureSubresourceStateKey& value)const noexcept{
        usize seed = 0;
        ::HashCombine(seed, value.texture);
        ::HashCombine(seed, value.mipLevel);
        ::HashCombine(seed, value.arraySlice);
        return seed;
    }
};

struct TextureSubresourceStateKeyEqualTo{
    [[nodiscard]] bool operator()(const TextureSubresourceStateKey& lhs, const TextureSubresourceStateKey& rhs)const noexcept{
        return lhs.texture == rhs.texture && lhs.mipLevel == rhs.mipLevel && lhs.arraySlice == rhs.arraySlice;
    }
};

class StateTracker final : NoCopy{
    friend class CommandList;


public:
    StateTracker(const VulkanContext& context);
    ~StateTracker();


public:
    void reset();
    void setPermanentTextureState(Texture& texture, ResourceStates::Mask state);
    void setPermanentBufferState(Buffer& buffer, ResourceStates::Mask state);

    [[nodiscard]] bool isPermanentTexture(Texture& texture)const;
    [[nodiscard]] bool isPermanentBuffer(Buffer& buffer)const;
    [[nodiscard]] ResourceStates::Mask getTextureState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel)const;
    [[nodiscard]] ResourceStates::Mask getBufferState(Buffer* buffer)const;

    void beginTrackingTexture(Texture* texture, TextureSubresourceSet subresources, ResourceStates::Mask state);
    void beginTrackingBuffer(Buffer* buffer, ResourceStates::Mask state);
    void appendKeepInitialStateBarriers(
        Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& imageBarriers,
        Vector<VkBufferMemoryBarrier2, Alloc::GlobalArena>& bufferBarriers
    );

    [[nodiscard]] bool isUavBarrierEnabledForTexture(Texture& texture)const;
    [[nodiscard]] bool isUavBarrierEnabledForBuffer(Buffer& buffer)const;
    void setEnableUavBarriersForTexture(Texture& texture, bool enableBarriers);
    void setEnableUavBarriersForBuffer(Buffer& buffer, bool enableBarriers);


private:
    [[nodiscard]] bool getTransientTextureState(Texture& texture, ArraySlice arraySlice, MipLevel mipLevel, ResourceStates::Mask& outState)const;
    [[nodiscard]] bool getResolvedTransientTextureState(Texture& texture, ArraySlice arraySlice, MipLevel mipLevel, ResourceStates::Mask& outState)const;
    [[nodiscard]] bool getTransientBufferState(Buffer& buffer, ResourceStates::Mask& outState)const;

    void beginTrackingTransientTexture(Texture& texture, TextureSubresourceSet subresources, ResourceStates::Mask state);
    void beginTrackingResolvedTransientTexture(Texture& texture, const TextureSubresourceSet& resolvedSubresources, ResourceStates::Mask state);
    void beginTrackingTransientBuffer(Buffer& buffer, ResourceStates::Mask state);


private:
    HashMap<Texture*, ResourceStates::Mask, Hasher<Texture*>, EqualTo<Texture*>, Alloc::GlobalArena> m_permanentTextureStates;
    HashMap<Buffer*, ResourceStates::Mask, Hasher<Buffer*>, EqualTo<Buffer*>, Alloc::GlobalArena> m_permanentBufferStates;
    HashMap<TextureSubresourceStateKey, ResourceStates::Mask, TextureSubresourceStateKeyHasher, TextureSubresourceStateKeyEqualTo, Alloc::GlobalArena> m_textureStates;
    HashMap<Buffer*, ResourceStates::Mask, Hasher<Buffer*>, EqualTo<Buffer*>, Alloc::GlobalArena> m_bufferStates;
    HashMap<Texture*, bool, Hasher<Texture*>, EqualTo<Texture*>, Alloc::GlobalArena> m_textureUavBarriers;
    HashMap<Buffer*, bool, Hasher<Buffer*>, EqualTo<Buffer*>, Alloc::GlobalArena> m_bufferUavBarriers;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command List


struct RenderPassParameters{
    Color colorClearValues[s_MaxRenderTargets]{};
    f32 depthClearValue = s_DepthClearValue;
    bool clearColorTargets = false;
    u8 colorClearMask = static_cast<u8>((1u << s_MaxRenderTargets) - 1u);
    bool clearDepthTarget = false;
    bool clearStencilTarget = false;
    u8 stencilClearValue = 0;


    [[nodiscard]] bool clearColorTarget(u32 index)const{ return (colorClearMask & (1u << index)) != 0; }
};

class CommandList final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class Queue;


public:
    CommandList(Device& device, const CommandListParameters& params);
    ~CommandList();


public:
    // Optional initial-state handoff; its producer must execute first.
    void open(const CommandListResourceStateHandoff* initialStates = nullptr);
    // Final state snapshot follows keepInitialState restoration.
    void close(CommandListResourceStateHandoff* finalStates = nullptr);
    // False when no submit-ready command buffer is owned.
    [[nodiscard]] bool hasCommandBuffer()const{ return m_currentCmdBuf != nullptr; }
    void clearState();
    void endRenderPass();

    void setResourceStatesForFramebuffer(Framebuffer& framebuffer);
    void commitBarriers();

    void setTextureState(Texture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits);
    void setBufferState(Buffer* buffer, ResourceStates::Mask stateBits);
    void setAccelStructState(RayTracingAccelStruct* as, ResourceStates::Mask stateBits);
    // Exports an exclusive resource to an ordered consumer lane.
    void releaseTextureOwnership(Texture* texture, TextureSubresourceSet subresources, RenderLane::Enum destinationLane);
    void releaseBufferOwnership(Buffer* buffer, RenderLane::Enum destinationLane);

    void setPermanentTextureState(Texture* texture, ResourceStates::Mask stateBits);
    void setPermanentBufferState(Buffer* buffer, ResourceStates::Mask stateBits);

    void clearTextureFloat(Texture* texture, TextureSubresourceSet subresources, const Color& clearColor);
    void clearTextureRectFloat(Texture* texture, TextureSubresourceSet subresources, const Rect& rect, const Color& clearColor);
    void clearTextureBoxFloat(Texture* texture, TextureSubresourceSet subresources, const Box& box, const Color& clearColor);
    void clearDepthStencilTexture(Texture* texture, TextureSubresourceSet subresources, bool clearDepth, f32 depth, bool clearStencil, u8 stencil);
    void clearDepthStencilTextureRect(Texture* texture, TextureSubresourceSet subresources, const Rect& rect, bool clearDepth, f32 depth, bool clearStencil, u8 stencil);
    void clearDepthStencilTextureBox(Texture* texture, TextureSubresourceSet subresources, const Box& box, bool clearDepth, f32 depth, bool clearStencil, u8 stencil);
    void clearTextureUInt(Texture* texture, TextureSubresourceSet subresources, u32 clearColor);
    void clearTextureUInt(Texture* texture, TextureSubresourceSet subresources, const UIntColor& clearColor);
    void clearTextureRectUInt(Texture* texture, TextureSubresourceSet subresources, const Rect& rect, u32 clearColor);
    void clearTextureRectUInt(Texture* texture, TextureSubresourceSet subresources, const Rect& rect, const UIntColor& clearColor);
    void clearTextureBoxUInt(Texture* texture, TextureSubresourceSet subresources, const Box& box, u32 clearColor);
    void clearTextureBoxUInt(Texture* texture, TextureSubresourceSet subresources, const Box& box, const UIntColor& clearColor);
    void clearTextureInt(Texture* texture, TextureSubresourceSet subresources, i32 clearColor);
    void clearTextureInt(Texture* texture, TextureSubresourceSet subresources, const IntColor& clearColor);
    void clearTextureRectInt(Texture* texture, TextureSubresourceSet subresources, const Rect& rect, i32 clearColor);
    void clearTextureRectInt(Texture* texture, TextureSubresourceSet subresources, const Rect& rect, const IntColor& clearColor);
    void clearTextureBoxInt(Texture* texture, TextureSubresourceSet subresources, const Box& box, i32 clearColor);
    void clearTextureBoxInt(Texture* texture, TextureSubresourceSet subresources, const Box& box, const IntColor& clearColor);

    void copyTexture(Texture* dest, const TextureSlice& destSlice, Texture* src, const TextureSlice& srcSlice);
    void copyTexture(StagingTexture* dest, const TextureSlice& destSlice, Texture* src, const TextureSlice& srcSlice);
    void copyTexture(Texture* dest, const TextureSlice& destSlice, StagingTexture* src, const TextureSlice& srcSlice);
    void writeBuffer(Buffer* buffer, const void* data, usize dataSize, u64 destOffsetBytes = 0);
    void clearBufferUInt(Buffer* buffer, u32 clearValue);
    void copyBuffer(Buffer* dest, u64 destOffsetBytes, Buffer* src, u64 srcOffsetBytes, u64 dataSizeBytes);
    void writeTexture(Texture* dest, u32 arraySlice, u32 mipLevel, const void* data, usize rowPitch, usize depthPitch = 0);
    void resolveTexture(Texture* dest, const TextureSubresourceSet& dstSubresources, Texture* src, const TextureSubresourceSet& srcSubresources);

    void setPushConstants(const void* data, usize byteSize);

    void setGraphicsState(const GraphicsState& state);
    void draw(const DrawArguments& args);
    void drawIndexed(const DrawArguments& args);
    void drawIndirect(u32 offsetBytes, u32 drawCount = 1);
    void drawIndexedIndirect(u32 offsetBytes, u32 drawCount = 1);

    void setComputeState(const ComputeState& state);
    void dispatch(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1);
    void dispatchIndirect(u32 offsetBytes);

    // Binds heap blocks at sets 8/9 and optional TLAS at set 10; pipeline must be descriptor-buffer compatible.
    void bindDescriptorBufferHeap(
        GpuDescriptorHeap& heap,
        VkPipelineBindPoint bindPoint,
        VkPipelineLayout pipelineLayout,
        GpuDescriptorHandle accelStructHandle = GpuDescriptorHandle::invalid()
    );

    void setMeshletState(const MeshletState& state);
    void dispatchMesh(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1);

    void setRayTracingState(const RayTracingState& state);
    void dispatchRays(const RayTracingDispatchRaysArguments& args);
    void buildBottomLevelAccelStruct(RayTracingAccelStruct* as, const RayTracingGeometryDesc* pGeometries, usize numGeometries, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None);
    void compactBottomLevelAccelStructs();
    void buildTopLevelAccelStruct(RayTracingAccelStruct* as, const RayTracingInstanceDesc* pInstances, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None);
    void buildOpacityMicromap(RayTracingOpacityMicromap* omm, const RayTracingOpacityMicromapDesc& desc);
    void buildTopLevelAccelStructFromBuffer(RayTracingAccelStruct* as, Buffer* instanceBuffer, u64 instanceBufferOffset, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None);
    void executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& desc);
    void convertCoopVecMatrices(CooperativeVectorConvertMatrixLayoutDesc const* convertDescs, usize numDescs);

    void resetTimerQuery(TimerQuery* query);
    [[nodiscard]] bool canResetTimerQueryHere()const;
    void beginTimerQuery(TimerQuery* query);
    void endTimerQuery(TimerQuery* query);
    void beginMarker(const AStringView name);
    void endMarker();

    void setEnableUavBarriersForTexture(Texture* texture, bool enableBarriers);
    void setEnableUavBarriersForBuffer(Buffer* buffer, bool enableBarriers);
    void beginTrackingTextureState(Texture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits);
    void beginTrackingBufferState(Buffer* buffer, ResourceStates::Mask stateBits);
    ResourceStates::Mask getTextureSubresourceState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel);
    ResourceStates::Mask getBufferState(Buffer* buffer);

    Device& getDevice(){ return m_device; }
    const CommandListParameters& getDescription(){ return m_desc; }

private:
    void setResourceStatesForGraphicsBuffers(const GraphicsState& state);
    [[nodiscard]] bool importResourceStateHandoff(const CommandListResourceStateHandoff& states);
    void exportResourceStateHandoff(CommandListResourceStateHandoff& states)const;
    void appendPendingOwnershipReleaseBarriers();
    void retainResource(GraphicsResource* resource);
    void retainStagingBuffer(Buffer& buffer);
    void ensureDescriptorBuffersBound();
    // Bind empty set 0 before global heap sets; no local descriptor transport.
    void bindDescriptorBufferEmptySet(VkPipelineBindPoint bindPoint, VkPipelineLayout pipelineLayout);
    void setViewportState(const ViewportState& viewport);

    bool beginDynamicRendering(Framebuffer* framebuffer, const RenderPassParameters& params);
    void endDynamicRendering();
    bool ensureGraphicsRenderPass(Framebuffer* framebuffer);
    void endActiveRenderPass();
    void executePipelineBarrier(const VkDependencyInfo& depInfo);
    bool validateIndirectBuffer(Buffer* buffer, u64 offsetBytes, u64 commandSizeBytes, u32 commandCount, const tchar* commandName)const;
    bool prepareDrawIndirect(u32 offsetBytes, u32 drawCount, u64 commandSizeBytes, const tchar* operationLabel, const tchar* commandName, VulkanDetail::IndirectDrawIndexMode::Enum indexMode, Buffer*& outIndirectBuffer)const;
    void clearColorTexture(Texture* textureResource, TextureSubresourceSet subresources, const tchar* valueName, const VkClearColorValue& clearValue, bool integerValue, bool signedIntegerValue);
    void clearColorTextureBox(Texture* textureResource, TextureSubresourceSet subresources, const Box& box, const tchar* valueName, const VkClearColorValue& clearValue, bool integerValue, bool signedIntegerValue);
    bool clearActiveRenderPassColorTextureRect(Texture& texture, const TextureSubresourceSet& resolvedSubresources, const Rect& rect, const VkClearColorValue& clearValue, const tchar* valueName);
    bool clearActiveRenderPassDepthStencilTextureRect(Texture& texture, const TextureSubresourceSet& resolvedSubresources, const Rect& rect, bool clearDepth, f32 depth, bool clearStencil, u8 stencil);
    bool prepareStagingTextureCopy(
        StagingTexture& stagingResource,
        const TextureSlice& stagingSlice,
        Texture& textureResource,
        const TextureSlice& textureSlice,
        const tchar* operationName,
        const tchar* singleSampleRequirement,
        VkBufferImageCopy& outRegion
    )const;
    bool prepareUploadStaging(usize dataSize, const tchar* operationName, Buffer*& outStagingBuffer, u64& outStagingOffset, void*& outCpuVA);
    bool prepareUploadStaging(const void* data, usize dataSize, const tchar* operationName, Buffer*& outStagingBuffer, u64& outStagingOffset);
    bool buildTopLevelAccelStructFromInstanceData(
        RayTracingAccelStruct& as,
        VkDeviceAddress instanceDataAddress,
        usize numInstances,
        RayTracingAccelStructBuildFlags::Mask buildFlags,
        const tchar* operationName
    );
    [[nodiscard]] bool attachAccelStructBuildScratchBuffer(VkAccelerationStructureBuildGeometryInfoKHR& buildInfo, u64 buildScratchSize, const char* debugName, const tchar* operationName);
    void discardUnsubmittedUploadChunks();


private:
    CommandListParameters m_desc;
    TrackedCommandBufferPtr m_currentCmdBuf;
    StateTracker m_stateTracker;
    bool m_enableAutomaticBarriers = true;
    bool m_renderPassActive = false;
    bool m_descriptorBuffersBound = false;
    Framebuffer* m_renderPassFramebuffer = nullptr;

    GraphicsState m_currentGraphicsState;
    ComputeState m_currentComputeState;
    MeshletState m_currentMeshletState;
    RayTracingState m_currentRayTracingState;

    Device& m_device;
    const VulkanContext& m_context;
    GpuCrashMarkerTracker m_gpuCrashMarkerTracker;

    Vector<VkImageMemoryBarrier2, Alloc::GlobalArena> m_pendingImageBarriers;
    Vector<VkBufferMemoryBarrier2, Alloc::GlobalArena> m_pendingBufferBarriers;
    HashMap<TextureSubresourceStateKey, CommandQueue::Enum, TextureSubresourceStateKeyHasher, TextureSubresourceStateKeyEqualTo, Alloc::GlobalArena> m_textureOwnershipReleaseDestinations;
    HashMap<Buffer*, CommandQueue::Enum, Hasher<Buffer*>, EqualTo<Buffer*>, Alloc::GlobalArena> m_bufferOwnershipReleaseDestinations;

    Vector<Handle<AccelStruct>, Alloc::GlobalArena> m_pendingCompactions;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Event Query


class EventQuery final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;


public:
    EventQuery(const VulkanContext& context);
    ~EventQuery();


private:
    VkFence m_fence = VK_NULL_HANDLE;
    bool m_started = false;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timer Query


class TimerQuery final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    TimerQuery(const VulkanContext& context);
    ~TimerQuery();


private:
    VkQueryPool m_queryPool = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device Implementation


class Device final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Buffer;
    friend class CommandList;
    friend class Queue;
    friend class Texture;
    friend class UploadManager;
    friend class GpuDescriptorHeap;


private:
    // AMD breadcrumb ring maps the last GPU marker after device loss.
    struct AmdBreadcrumbSlotRecord{
        u32 sequence = 0u;
        usize markerHash = 0u;
    };
    struct AmdBreadcrumbBuffer{
        VkBuffer buffer = VK_NULL_HANDLE;
        VulkanAllocationHandle allocation = {};
        void* mappedMemory = nullptr;
        Atomic<u32> nextSequence = 0u;
        // Prevents concurrent recording from tearing a marker record.
        Futex slotMutex;
        Array<AmdBreadcrumbSlotRecord, s_MaxAmdBreadcrumbSlots> slotRecords = {};
    };


public:
    // Reserves a marker slot; invalid when AMD breadcrumbs are unavailable.
    struct AmdBreadcrumbWrite{
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0u;
        u32 marker = 0u;
        bool valid = false;
    };


public:
    explicit Device(const DeviceDesc& desc);
    ~Device();


public:
    [[nodiscard]] HeapHandle createHeap(const HeapDesc& d);
    [[nodiscard]] TextureHandle createTexture(const TextureDesc& d);
    [[nodiscard]] MemoryRequirements getTextureMemoryRequirements(Texture* texture);
    bool bindTextureMemory(Texture* texture, Heap* heap, u64 offset);
    [[nodiscard]] TextureHandle createHandleForNativeTexture(ObjectType objectType, Object texture, const TextureDesc& desc);
    [[nodiscard]] StagingTextureHandle createStagingTexture(const TextureDesc& d, CpuAccessMode::Enum cpuAccess);
    void* mapStagingTexture(StagingTexture* tex, const TextureSlice& slice, CpuAccessMode::Enum, usize* outRowPitch);
    void unmapStagingTexture(StagingTexture* tex);
    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& d);
    void* mapBuffer(Buffer* buffer, CpuAccessMode::Enum);
    void unmapBuffer(Buffer* buffer);
    [[nodiscard]] MemoryRequirements getBufferMemoryRequirements(Buffer* buffer);
    bool bindBufferMemory(Buffer* buffer, Heap* heap, u64 offset);
    [[nodiscard]] BufferHandle createHandleForNativeBuffer(ObjectType objectType, Object buffer, const BufferDesc& desc);
    [[nodiscard]] ShaderHandle createShader(const ShaderDesc& d, const void* binary, usize binarySize);
    [[nodiscard]] ShaderHandle createShaderSpecialization(Shader* baseShader, const ShaderSpecialization* constants, u32 numConstants);
    [[nodiscard]] ShaderLibraryHandle createShaderLibrary(const void* binary, usize binarySize);
    [[nodiscard]] SamplerHandle createSampler(const SamplerDesc& d);
    [[nodiscard]] InputLayoutHandle createInputLayout(const VertexAttributeDesc* d, u32 attributeCount, Shader*);
    [[nodiscard]] EventQueryHandle createEventQuery();
    void setEventQuery(EventQuery* query, CommandQueue::Enum queue);
    bool pollEventQuery(EventQuery* query);
    void waitEventQuery(EventQuery* query);
    [[nodiscard]] TimerQueryHandle createTimerQuery();
    bool pollTimerQuery(TimerQuery* query);
    [[nodiscard]] bool getTimerQueryResult(TimerQuery* query, TimerQueryResult& outResult);
    f32 getTimerQueryTime(TimerQuery* query);
    void resetTimerQuery(TimerQuery* query);
    [[nodiscard]] FramebufferHandle createFramebuffer(const FramebufferDesc& desc);
    [[nodiscard]] GraphicsPipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc, FramebufferInfo const& fbinfo);
    [[nodiscard]] ComputePipelineHandle createComputePipeline(const ComputePipelineDesc& desc);
    [[nodiscard]] MeshletPipelineHandle createMeshletPipeline(const MeshletPipelineDesc& desc, FramebufferInfo const& fbinfo);
    [[nodiscard]] RayTracingPipelineHandle createRayTracingPipeline(const RayTracingPipelineDesc& desc);
    [[nodiscard]] BindingLayoutHandle createBindingLayout(const BindingLayoutDesc& desc);
    [[nodiscard]] BindingLayoutHandle createBindlessLayout(const BindlessLayoutDesc& desc);
    [[nodiscard]] RayTracingOpacityMicromapHandle createOpacityMicromap(const RayTracingOpacityMicromapDesc& desc);
    [[nodiscard]] RayTracingAccelStructHandle createAccelStruct(const RayTracingAccelStructDesc& desc);
    [[nodiscard]] MemoryRequirements getAccelStructMemoryRequirements(RayTracingAccelStruct* as);
    [[nodiscard]] RayTracingClusterOperationSizeInfo getClusterOperationSizeInfo(const RayTracingClusterOperationParams& params);
    bool bindAccelStructMemory(RayTracingAccelStruct* as, Heap* heap, u64 offset);
    [[nodiscard]] CommandListHandle createCommandList(const CommandListParameters& params = CommandListParameters());
    // outCommandListsSubmitted is true only for a new command-buffer submission.
    u64 executeCommandLists(
        CommandList* const* pCommandLists,
        usize numCommandLists,
        CommandQueue::Enum executionQueue = CommandQueue::Graphics,
        bool* outCommandListsSubmitted = nullptr
    );
    // Cross-lane dependencies are immutable submission-local token edges.
    [[nodiscard]] QueueSubmissionToken executeCommandLists(
        CommandList* const* pCommandLists,
        usize numCommandLists,
        CommandQueue::Enum executionQueue,
        const QueueSubmissionDesc& submitDesc
    );
    [[nodiscard]] QueueSubmissionToken executeCommandLists(
        CommandList* const* pCommandLists,
        usize numCommandLists,
        RenderLane::Enum executionLane,
        const QueueSubmissionDesc& submitDesc
    );
    void queueWaitForCommandList(CommandQueue::Enum waitQueue, CommandQueue::Enum executionQueue, u64 instance);
    [[nodiscard]] CommandQueue::Enum resolveRenderLane(RenderLane::Enum lane)const;
    [[nodiscard]] bool isRenderLaneDedicated(RenderLane::Enum lane)const;
    // Device loss requires full device recreation.
    [[nodiscard]] bool isDeviceLost()const noexcept{ return m_deviceLost.load(MemoryOrder::acquire); }
    // Cross-queue timing requires Graphics and Compute timestamp support.
    [[nodiscard]] bool supportsGraphicsAndComputeTimestamps()const{
        return m_context.physicalDeviceProperties.limits.timestampComputeAndGraphics == VK_TRUE;
    }
    [[nodiscard]] u32 getQueueFamilyIndex(CommandQueue::Enum queue)const;
    [[nodiscard]] bool usesConcurrentQueueSharing(ResourceQueueSharing::Mask sharing)const{
        return UsesConcurrentGraphicsAsyncComputeSharing(sharing, m_context);
    }
    bool waitForIdle();
    void runGarbageCollection();
    bool queryFeatureSupport(Feature::Enum feature, void* = nullptr, usize = 0);
    [[nodiscard]] FormatSupport::Mask queryFormatSupport(Format::Enum format);
    [[nodiscard]] CooperativeVectorDeviceFeatures queryCoopVecFeatures();
    usize getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns);
    [[nodiscard]] Object getNativeQueue(ObjectType objectType, CommandQueue::Enum queue);
    bool isGpuCrashDiagnosticsEnabled(){ return m_gpuCrashDiagnosticsEnabled && m_context.extensions.NV_device_diagnostic_checkpoints; }
    bool isAmdBreadcrumbEnabled(){ return m_gpuCrashDiagnosticsEnabled && m_context.extensions.AMD_buffer_marker && m_amdBreadcrumb.buffer != VK_NULL_HANDLE; }
    // NV and AMD marker paths share one command-list tracker.
    bool isAnyGpuMarkerEnabled(){ return isGpuCrashDiagnosticsEnabled() || isAmdBreadcrumbEnabled(); }
    [[nodiscard]] GpuCrashTracker& getGpuCrashTracker(){ return m_gpuCrashTracker; }
    void captureGpuCrash(AStringView context)noexcept;
#if defined(NWB_GPU_FAULT_INJECTION)
    // Test-only device-loss fault injection.
    void debugTriggerGpuFault(u64 faultDeviceAddress);
#endif

    [[nodiscard]] AmdBreadcrumbWrite reserveAmdBreadcrumb(usize markerHash);

    void queueWaitForSemaphore(CommandQueue::Enum waitQueue, VkSemaphore semaphore, u64 value);
    void queueSignalSemaphore(CommandQueue::Enum executionQueue, VkSemaphore semaphore, u64 value);
    [[nodiscard]] u64 queueGetCompletedInstance(CommandQueue::Enum queue);

public:
    [[nodiscard]] Queue* getQueue(CommandQueue::Enum queueType);
#if !defined(NWB_FINAL) || defined(NWB_ENABLE_TEST_FEATURE_OVERRIDES)
    // Test-only validated-submit rejection seam exercises production cleanup.
    void rejectNextSubmissionForTesting(CommandQueue::Enum queue);
    void clearSubmissionRejectionsForTesting();
#endif
    [[nodiscard]] GpuDescriptorHeap& getDescriptorHeap(){ return m_gpuDescriptorHeap; }
    // Writes descriptor-buffer entries, including TLAS handles.
    [[nodiscard]] DescriptorBufferManager& getDescriptorBufferManager(){ return m_descriptorBufferManager; }


private:
#if !defined(NWB_FINAL) || defined(NWB_ENABLE_TEST_FEATURE_OVERRIDES)
    [[nodiscard]] bool consumeSubmissionRejectionForTesting(CommandQueue::Enum queue);
#endif
    // Probed once at device initialization so compressed texture selection does not rely on
    // a later optimistic format-property query.
    void probeCompressedTextureFormats();
    [[nodiscard]] FormatSupport::Mask queryFormatSupportUncached(Format::Enum format)const;
    [[nodiscard]] bool canCreateSampledTextureFormat(Format::Enum format)const;
    [[nodiscard]] bool loadPipelineCacheData(GraphicsBytes& outData);
    void savePipelineCacheData();
    [[nodiscard]] bool createPipelineLayoutForBindingLayouts(
        const BindingLayoutVector& bindingLayouts,
        const tchar* operationName,
        VkPipelineLayout& outPipelineLayout,
        u32& outPushConstantByteSize,
        bool& outOwnsPipelineLayout,
        Alloc::ScratchArena& scratchArena
    )const;
    // Lazily creates the descriptor-buffer gap-set layout for explicit heap sets.
    [[nodiscard]] VkDescriptorSetLayout getOrCreateEmptyDescriptorBufferSetLayout()const;
#if defined(NWB_DEBUG)
    [[nodiscard]] bool validateHeapMemoryBinding(
        const Heap& heap,
        const VkMemoryRequirements& memoryRequirements,
        u64 offset,
        const tchar* operationName,
        const tchar* resourceName
    )const;
#endif
    [[nodiscard]] bool configurePipelineBindings(
        const BindingLayoutVector& bindingLayouts,
        const tchar* operationName,
        PipelineBindingState& outBindings,
        Alloc::ScratchArena& scratchArena
    )const;
    template<typename PipelineT>
    [[nodiscard]] bool configurePipelineBindingsOrDestroy(
        const BindingLayoutVector& bindingLayouts,
        const tchar* operationName,
        PipelineT* pipeline,
        Alloc::ScratchArena& scratchArena
    )const{
        if(configurePipelineBindings(bindingLayouts, operationName, *pipeline, scratchArena))
            return true;

        DestroyArenaObject(m_context.objectArena, pipeline);
        return false;
    }
    template<typename PipelineT>
    [[nodiscard]] bool buildGraphicsPipelineFixedStateOrDestroy(
        const FramebufferInfo& fbinfo,
        const RenderState& renderState,
        const VulkanDetail::PipelineStencilFaceMode::Enum stencilFaceMode,
        const VkDynamicState* dynamicStates,
        const u32 dynamicStateCount,
        const tchar* operationName,
        PipelineT* pipeline,
        VulkanDetail::GraphicsPipelineFixedState& outState
    )const{
        if(VulkanDetail::BuildGraphicsPipelineFixedState(
            fbinfo,
            renderState,
            stencilFaceMode,
            dynamicStates,
            dynamicStateCount,
            operationName,
            outState
        ))
            return true;

        DestroyArenaObject(m_context.objectArena, pipeline);
        return false;
    }
    template<typename PipelineT>
    [[nodiscard]] bool createPipelineOrDestroy(
        const tchar* operationName,
        PipelineT* pipeline,
        const VkComputePipelineCreateInfo& pipelineInfo
    )const{
        const VkResult res = vkCreateComputePipelines(m_context.device, m_context.pipelineCache, 1, &pipelineInfo, m_context.allocationCallbacks, &pipeline->m_pipeline);
        if(res == VK_SUCCESS)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: {}"), operationName, ResultToString(res));
        DestroyArenaObject(m_context.objectArena, pipeline);
        return false;
    }
    template<typename PipelineT>
    [[nodiscard]] bool createPipelineOrDestroy(
        const tchar* operationName,
        PipelineT* pipeline,
        const VkGraphicsPipelineCreateInfo& pipelineInfo
    )const{
        const VkResult res = vkCreateGraphicsPipelines(m_context.device, m_context.pipelineCache, 1, &pipelineInfo, m_context.allocationCallbacks, &pipeline->m_pipeline);
        if(res == VK_SUCCESS)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: {}"), operationName, ResultToString(res));
        DestroyArenaObject(m_context.objectArena, pipeline);
        return false;
    }
    void appendPipelineShaderStage(
        Shader* shader,
        VkShaderStageFlagBits stage,
        PipelineSpecializationInfoVector& specializationInfos,
        PipelineShaderStageVector& shaderStages
    )const;


private:
    // First: queues unregister command lists during destruction.
    bool m_gpuCrashDiagnosticsEnabled = false;
    // Distinguishes submit rejection from device loss.
    Atomic<bool> m_deviceLost = false;
    Atomic<bool> m_gpuCrashCaptured = false;
    GpuCrashTracker m_gpuCrashTracker;
    // Pre-reserved crash-capture arena.
    Alloc::PersistentArena m_gpuCrashReportArena;
    Alloc::PersistentArena m_gpuCrashVendorBinaryArena;

    AmdBreadcrumbBuffer m_amdBreadcrumb;

    VulkanContext m_context;
    VulkanAllocator m_allocator;
    DescriptorBufferManager m_descriptorBufferManager;
    GpuDescriptorHeap m_gpuDescriptorHeap;
    Path m_pipelineCacheDirectory;
    GraphicsString m_pipelineCacheVolumeName;
    Optional<Queue> m_queues[static_cast<u32>(CommandQueue::kCount)];
    // Only block-compressed entries are populated; all are resolved before the device is exposed.
    Array<FormatSupport::Mask, static_cast<usize>(Format::kCount)> m_compressedFormatSupport = {};

#if !defined(NWB_FINAL) || defined(NWB_ENABLE_TEST_FEATURE_OVERRIDES)
    Array<Atomic<u32>, static_cast<u32>(CommandQueue::kCount)> m_submissionRejectionsForTesting = {};
#endif

    UploadManager m_uploadManager;
    UploadManager m_scratchManager;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

