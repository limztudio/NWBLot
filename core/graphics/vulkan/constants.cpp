// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Format conversion table


struct FormatMapping{
    VkFormat vkFormat;
    u32 bytesPerPixel;
    Format::Enum format;
    bool hasDepth;
    bool hasStencil;
    bool isCompressed;

    constexpr FormatMapping(
        const Format::Enum inFormat,
        const VkFormat inVkFormat,
        const u32 inBytesPerPixel,
        const bool inHasDepth,
        const bool inHasStencil,
        const bool inIsCompressed
    )
        : vkFormat(inVkFormat)
        , bytesPerPixel(inBytesPerPixel)
        , format(inFormat)
        , hasDepth(inHasDepth)
        , hasStencil(inHasStencil)
        , isCompressed(inIsCompressed)
    {}
};

static constexpr FormatMapping s_FormatMappings[] = {
    // RGBA formats
    { Format::RGBA32_FLOAT           , VK_FORMAT_R32G32B32A32_SFLOAT           , 16, false, false, false },
    { Format::RGBA32_UINT            , VK_FORMAT_R32G32B32A32_UINT             , 16, false, false, false },
    { Format::RGBA32_SINT            , VK_FORMAT_R32G32B32A32_SINT             , 16, false, false, false },

    { Format::RGB32_FLOAT            , VK_FORMAT_R32G32B32_SFLOAT              , 12, false, false, false },
    { Format::RGB32_UINT             , VK_FORMAT_R32G32B32_UINT                , 12, false, false, false },
    { Format::RGB32_SINT             , VK_FORMAT_R32G32B32_SINT                , 12, false, false, false },

    { Format::RGBA16_FLOAT           , VK_FORMAT_R16G16B16A16_SFLOAT           ,  8, false, false, false },
    { Format::RGBA16_UNORM           , VK_FORMAT_R16G16B16A16_UNORM            ,  8, false, false, false },
    { Format::RGBA16_SNORM           , VK_FORMAT_R16G16B16A16_SNORM            ,  8, false, false, false },
    { Format::RGBA16_UINT            , VK_FORMAT_R16G16B16A16_UINT             ,  8, false, false, false },
    { Format::RGBA16_SINT            , VK_FORMAT_R16G16B16A16_SINT             ,  8, false, false, false },

    { Format::RG32_FLOAT             , VK_FORMAT_R32G32_SFLOAT                 ,  8, false, false, false },
    { Format::RG32_UINT              , VK_FORMAT_R32G32_UINT                   ,  8, false, false, false },
    { Format::RG32_SINT              , VK_FORMAT_R32G32_SINT                   ,  8, false, false, false },

    { Format::R10G10B10A2_UNORM      , VK_FORMAT_A2B10G10R10_UNORM_PACK32      ,  4, false, false, false },
    { Format::R11G11B10_FLOAT        , VK_FORMAT_B10G11R11_UFLOAT_PACK32       ,  4, false, false, false },

    { Format::RGBA8_UNORM            , VK_FORMAT_R8G8B8A8_UNORM                ,  4, false, false, false },
    { Format::RGBA8_SNORM            , VK_FORMAT_R8G8B8A8_SNORM                ,  4, false, false, false },
    { Format::RGBA8_UINT             , VK_FORMAT_R8G8B8A8_UINT                 ,  4, false, false, false },
    { Format::RGBA8_SINT             , VK_FORMAT_R8G8B8A8_SINT                 ,  4, false, false, false },
    { Format::RGBA8_UNORM_SRGB       , VK_FORMAT_R8G8B8A8_SRGB                 ,  4, false, false, false },
    { Format::BGRA8_UNORM            , VK_FORMAT_B8G8R8A8_UNORM                ,  4, false, false, false },
    { Format::BGRA8_UNORM_SRGB       , VK_FORMAT_B8G8R8A8_SRGB                 ,  4, false, false, false },

    { Format::RG16_FLOAT             , VK_FORMAT_R16G16_SFLOAT                 ,  4, false, false, false },
    { Format::RG16_UNORM             , VK_FORMAT_R16G16_UNORM                  ,  4, false, false, false },
    { Format::RG16_SNORM             , VK_FORMAT_R16G16_SNORM                  ,  4, false, false, false },
    { Format::RG16_UINT              , VK_FORMAT_R16G16_UINT                   ,  4, false, false, false },
    { Format::RG16_SINT              , VK_FORMAT_R16G16_SINT                   ,  4, false, false, false },

    { Format::R32_FLOAT              , VK_FORMAT_R32_SFLOAT                    ,  4, false, false, false },
    { Format::R32_UINT               , VK_FORMAT_R32_UINT                      ,  4, false, false, false },
    { Format::R32_SINT               , VK_FORMAT_R32_SINT                      ,  4, false, false, false },

    { Format::RG8_UNORM              , VK_FORMAT_R8G8_UNORM                    ,  2, false, false, false },
    { Format::RG8_SNORM              , VK_FORMAT_R8G8_SNORM                    ,  2, false, false, false },
    { Format::RG8_UINT               , VK_FORMAT_R8G8_UINT                     ,  2, false, false, false },
    { Format::RG8_SINT               , VK_FORMAT_R8G8_SINT                     ,  2, false, false, false },

    { Format::R16_FLOAT              , VK_FORMAT_R16_SFLOAT                    ,  2, false, false, false },
    { Format::R16_UNORM              , VK_FORMAT_R16_UNORM                     ,  2, false, false, false },
    { Format::R16_SNORM              , VK_FORMAT_R16_SNORM                     ,  2, false, false, false },
    { Format::R16_UINT               , VK_FORMAT_R16_UINT                      ,  2, false, false, false },
    { Format::R16_SINT               , VK_FORMAT_R16_SINT                      ,  2, false, false, false },

    { Format::R8_UNORM               , VK_FORMAT_R8_UNORM                      ,  1, false, false, false },
    { Format::R8_SNORM               , VK_FORMAT_R8_SNORM                      ,  1, false, false, false },
    { Format::R8_UINT                , VK_FORMAT_R8_UINT                       ,  1, false, false, false },
    { Format::R8_SINT                , VK_FORMAT_R8_SINT                       ,  1, false, false, false },

    // Depth/Stencil formats
    { Format::D32                    , VK_FORMAT_D32_SFLOAT                    ,  4, true , false, false },
    { Format::D24S8                  , VK_FORMAT_D24_UNORM_S8_UINT             ,  4, true , true , false },
    { Format::D32S8                  , VK_FORMAT_D32_SFLOAT_S8_UINT            ,  5, true , true , false },
    { Format::D16                    , VK_FORMAT_D16_UNORM                     ,  2, true , false, false },

    // Compressed formats - BC (DXT)
    { Format::BC1_UNORM              , VK_FORMAT_BC1_RGBA_UNORM_BLOCK          ,  0, false, false, true  },
    { Format::BC1_UNORM_SRGB         , VK_FORMAT_BC1_RGBA_SRGB_BLOCK           ,  0, false, false, true  },
    { Format::BC2_UNORM              , VK_FORMAT_BC2_UNORM_BLOCK               ,  0, false, false, true  },
    { Format::BC2_UNORM_SRGB         , VK_FORMAT_BC2_SRGB_BLOCK                ,  0, false, false, true  },
    { Format::BC3_UNORM              , VK_FORMAT_BC3_UNORM_BLOCK               ,  0, false, false, true  },
    { Format::BC3_UNORM_SRGB         , VK_FORMAT_BC3_SRGB_BLOCK                ,  0, false, false, true  },
    { Format::BC4_UNORM              , VK_FORMAT_BC4_UNORM_BLOCK               ,  0, false, false, true  },
    { Format::BC4_SNORM              , VK_FORMAT_BC4_SNORM_BLOCK               ,  0, false, false, true  },
    { Format::BC5_UNORM              , VK_FORMAT_BC5_UNORM_BLOCK               ,  0, false, false, true  },
    { Format::BC5_SNORM              , VK_FORMAT_BC5_SNORM_BLOCK               ,  0, false, false, true  },
    { Format::BC6H_UFLOAT            , VK_FORMAT_BC6H_UFLOAT_BLOCK             ,  0, false, false, true  },
    { Format::BC6H_SFLOAT            , VK_FORMAT_BC6H_SFLOAT_BLOCK             ,  0, false, false, true  },
    { Format::BC7_UNORM              , VK_FORMAT_BC7_UNORM_BLOCK               ,  0, false, false, true  },
    { Format::BC7_UNORM_SRGB         , VK_FORMAT_BC7_SRGB_BLOCK                ,  0, false, false, true  },

    // Compressed formats - ASTC
    { Format::ASTC_4x4_UNORM         , VK_FORMAT_ASTC_4x4_UNORM_BLOCK          ,  0, false, false, true  },
    { Format::ASTC_4x4_UNORM_SRGB    , VK_FORMAT_ASTC_4x4_SRGB_BLOCK           ,  0, false, false, true  },
    { Format::ASTC_4x4_FLOAT         , VK_FORMAT_UNDEFINED                     ,  0, false, false, true  },
    { Format::ASTC_5x4_UNORM         , VK_FORMAT_ASTC_5x4_UNORM_BLOCK          ,  0, false, false, true  },
    { Format::ASTC_5x4_UNORM_SRGB    , VK_FORMAT_ASTC_5x4_SRGB_BLOCK           ,  0, false, false, true  },
    { Format::ASTC_5x4_FLOAT         , VK_FORMAT_UNDEFINED                     ,  0, false, false, true  },
    { Format::ASTC_5x5_UNORM         , VK_FORMAT_ASTC_5x5_UNORM_BLOCK          ,  0, false, false, true  },
    { Format::ASTC_5x5_UNORM_SRGB    , VK_FORMAT_ASTC_5x5_SRGB_BLOCK           ,  0, false, false, true  },
    { Format::ASTC_5x5_FLOAT         , VK_FORMAT_UNDEFINED                     ,  0, false, false, true  },
    { Format::ASTC_6x5_UNORM         , VK_FORMAT_ASTC_6x5_UNORM_BLOCK          ,  0, false, false, true  },
    { Format::ASTC_6x5_UNORM_SRGB    , VK_FORMAT_ASTC_6x5_SRGB_BLOCK           ,  0, false, false, true  },
    { Format::ASTC_6x5_FLOAT         , VK_FORMAT_UNDEFINED                     ,  0, false, false, true  },
    { Format::ASTC_6x6_UNORM         , VK_FORMAT_ASTC_6x6_UNORM_BLOCK          ,  0, false, false, true  },
    { Format::ASTC_6x6_UNORM_SRGB    , VK_FORMAT_ASTC_6x6_SRGB_BLOCK           ,  0, false, false, true  },
    { Format::ASTC_6x6_FLOAT         , VK_FORMAT_UNDEFINED                     ,  0, false, false, true  },
    { Format::ASTC_8x5_UNORM         , VK_FORMAT_ASTC_8x5_UNORM_BLOCK          ,  0, false, false, true  },
    { Format::ASTC_8x5_UNORM_SRGB    , VK_FORMAT_ASTC_8x5_SRGB_BLOCK           ,  0, false, false, true  },
    { Format::ASTC_8x5_FLOAT         , VK_FORMAT_UNDEFINED                     ,  0, false, false, true  },
    { Format::ASTC_8x6_UNORM         , VK_FORMAT_ASTC_8x6_UNORM_BLOCK          ,  0, false, false, true  },
    { Format::ASTC_8x6_UNORM_SRGB    , VK_FORMAT_ASTC_8x6_SRGB_BLOCK           ,  0, false, false, true  },
    { Format::ASTC_8x6_FLOAT         , VK_FORMAT_UNDEFINED                     ,  0, false, false, true  },
    { Format::ASTC_10x5_UNORM        , VK_FORMAT_ASTC_10x5_UNORM_BLOCK         ,  0, false, false, true  },
    { Format::ASTC_10x5_UNORM_SRGB   , VK_FORMAT_ASTC_10x5_SRGB_BLOCK          ,  0, false, false, true  },
    { Format::ASTC_10x5_FLOAT        , VK_FORMAT_UNDEFINED                     ,  0, false, false, true  },
    { Format::ASTC_10x6_UNORM        , VK_FORMAT_ASTC_10x6_UNORM_BLOCK         ,  0, false, false, true  },
    { Format::ASTC_10x6_UNORM_SRGB   , VK_FORMAT_ASTC_10x6_SRGB_BLOCK          ,  0, false, false, true  },
    { Format::ASTC_10x6_FLOAT        , VK_FORMAT_UNDEFINED                     ,  0, false, false, true  },
    { Format::ASTC_8x8_UNORM         , VK_FORMAT_ASTC_8x8_UNORM_BLOCK          ,  0, false, false, true  },
    { Format::ASTC_8x8_UNORM_SRGB    , VK_FORMAT_ASTC_8x8_SRGB_BLOCK           ,  0, false, false, true  },
    { Format::ASTC_8x8_FLOAT         , VK_FORMAT_UNDEFINED                     ,  0, false, false, true  },
    { Format::ASTC_10x8_UNORM        , VK_FORMAT_ASTC_10x8_UNORM_BLOCK         ,  0, false, false, true  },
    { Format::ASTC_10x8_UNORM_SRGB   , VK_FORMAT_ASTC_10x8_SRGB_BLOCK          ,  0, false, false, true  },
    { Format::ASTC_10x8_FLOAT        , VK_FORMAT_UNDEFINED                     ,  0, false, false, true  },
    { Format::ASTC_10x10_UNORM       , VK_FORMAT_ASTC_10x10_UNORM_BLOCK        ,  0, false, false, true  },
    { Format::ASTC_10x10_UNORM_SRGB  , VK_FORMAT_ASTC_10x10_SRGB_BLOCK         ,  0, false, false, true  },
    { Format::ASTC_10x10_FLOAT       , VK_FORMAT_UNDEFINED                     ,  0, false, false, true  },
    { Format::ASTC_12x10_UNORM       , VK_FORMAT_ASTC_12x10_UNORM_BLOCK        ,  0, false, false, true  },
    { Format::ASTC_12x10_UNORM_SRGB  , VK_FORMAT_ASTC_12x10_SRGB_BLOCK         ,  0, false, false, true  },
    { Format::ASTC_12x10_FLOAT       , VK_FORMAT_UNDEFINED                     ,  0, false, false, true  },
    { Format::ASTC_12x12_UNORM       , VK_FORMAT_ASTC_12x12_UNORM_BLOCK        ,  0, false, false, true  },
    { Format::ASTC_12x12_UNORM_SRGB  , VK_FORMAT_ASTC_12x12_SRGB_BLOCK         ,  0, false, false, true  },
    { Format::ASTC_12x12_FLOAT       , VK_FORMAT_UNDEFINED                     ,  0, false, false, true  },
};

static constexpr usize s_NumFormatMappings = LengthOf(s_FormatMappings);


VkFormat ConvertFormat(Format::Enum format){
    for(usize i = 0; i < s_NumFormatMappings; ++i){
        if(s_FormatMappings[i].format == format)
            return s_FormatMappings[i].vkFormat;
    }

    return VK_FORMAT_UNDEFINED;
}

VkAccessFlags2 GetVkAccessFlags(ResourceStates::Mask states){
    VkAccessFlags2 flags = 0;

    if(states & ResourceStates::VertexBuffer)
        flags |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    if(states & ResourceStates::IndexBuffer)
        flags |= VK_ACCESS_2_INDEX_READ_BIT;
    if(states & ResourceStates::ConstantBuffer)
        flags |= VK_ACCESS_2_UNIFORM_READ_BIT;
    if(states & ResourceStates::IndirectArgument)
        flags |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    if(states & ResourceStates::ShaderResource)
        flags |= VK_ACCESS_2_SHADER_READ_BIT;
    if(states & ResourceStates::UnorderedAccess)
        flags |= VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    if(states & ResourceStates::RenderTarget)
        flags |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    if(states & ResourceStates::DepthWrite)
        flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if(states & ResourceStates::DepthRead)
        flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    if(states & ResourceStates::CopyDest)
        flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    if(states & ResourceStates::CopySource)
        flags |= VK_ACCESS_2_TRANSFER_READ_BIT;
    if(states & ResourceStates::Present)
        flags |= 0; // No access, just layout
    if(states & ResourceStates::AccelStructRead)
        flags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    if(states & (ResourceStates::AccelStructWrite | ResourceStates::AccelStructBuildBlas))
        flags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    if(states & ResourceStates::AccelStructBuildInput)
        flags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    return flags;
}

VkPipelineStageFlags2 GetVkPipelineStageFlags(ResourceStates::Mask states, bool rayTracingStageAvailable){
    VkPipelineStageFlags2 flags = 0;

    if(states & ResourceStates::VertexBuffer)
        flags |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    if(states & ResourceStates::IndexBuffer)
        flags |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    if(states & (ResourceStates::ConstantBuffer | ResourceStates::ShaderResource | ResourceStates::UnorderedAccess)){
        // Shader-readable resources can be bound by any shader pipeline. ALL_GRAPHICS covers the graphics stages (vertex..fragment, plus mesh/task) and COMPUTE_SHADER the compute pipeline;
        // ray-tracing shaders are a separate pipeline that ALL_GRAPHICS omits, so add that stage explicitly when the ray-tracing pipeline is enabled (the bit is illegal to specify otherwise).
        // Without this, a UAV/SRV/CB written or read by a ray-tracing shader is left unsynchronized against later graphics/compute access.
        flags |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        if(rayTracingStageAvailable)
            flags |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    }
    if(states & ResourceStates::IndirectArgument)
        flags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    if(states & ResourceStates::RenderTarget)
        flags |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    if(states & (ResourceStates::DepthWrite | ResourceStates::DepthRead))
        flags |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    if(states & (ResourceStates::CopyDest | ResourceStates::CopySource))
        flags |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    if(states & (ResourceStates::AccelStructRead | ResourceStates::AccelStructWrite | ResourceStates::AccelStructBuildInput | ResourceStates::AccelStructBuildBlas))
        flags |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    if(flags == 0)
        flags = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    return flags;
}

VkImageLayout GetVkImageLayout(ResourceStates::Mask states){
    if(states & ResourceStates::RenderTarget)
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    if(states & ResourceStates::DepthWrite)
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if(states & ResourceStates::DepthRead)
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    if(states & ResourceStates::UnorderedAccess)
        return VK_IMAGE_LAYOUT_GENERAL;
    if(states & ResourceStates::ShaderResource)
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if(states & ResourceStates::CopyDest)
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if(states & ResourceStates::CopySource)
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    if(states & ResourceStates::Present)
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    return VK_IMAGE_LAYOUT_GENERAL;
}

VkSampleCountFlagBits GetSampleCountFlagBits(u32 sampleCount){
    switch(sampleCount){
    case 1:  return VK_SAMPLE_COUNT_1_BIT;
    case 2:  return VK_SAMPLE_COUNT_2_BIT;
    case 4:  return VK_SAMPLE_COUNT_4_BIT;
    case 8:  return VK_SAMPLE_COUNT_8_BIT;
    case 16: return VK_SAMPLE_COUNT_16_BIT;
    case 32: return VK_SAMPLE_COUNT_32_BIT;
    case 64: return VK_SAMPLE_COUNT_64_BIT;
    default: return VK_SAMPLE_COUNT_1_BIT;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkFormat ConvertFormat(Format::Enum format){
    return VulkanDetail::ConvertFormat(format);
}

const tchar* ResultToString(VkResult result){
    switch(result){
    case VK_SUCCESS: return NWB_TEXT("VK_SUCCESS");
    case VK_NOT_READY: return NWB_TEXT("VK_NOT_READY");
    case VK_TIMEOUT: return NWB_TEXT("VK_TIMEOUT");
    case VK_EVENT_SET: return NWB_TEXT("VK_EVENT_SET");
    case VK_EVENT_RESET: return NWB_TEXT("VK_EVENT_RESET");
    case VK_INCOMPLETE: return NWB_TEXT("VK_INCOMPLETE");
    case VK_ERROR_OUT_OF_HOST_MEMORY: return NWB_TEXT("VK_ERROR_OUT_OF_HOST_MEMORY");
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return NWB_TEXT("VK_ERROR_OUT_OF_DEVICE_MEMORY");
    case VK_ERROR_INITIALIZATION_FAILED: return NWB_TEXT("VK_ERROR_INITIALIZATION_FAILED");
    case VK_ERROR_DEVICE_LOST: return NWB_TEXT("VK_ERROR_DEVICE_LOST");
    case VK_ERROR_MEMORY_MAP_FAILED: return NWB_TEXT("VK_ERROR_MEMORY_MAP_FAILED");
    case VK_ERROR_LAYER_NOT_PRESENT: return NWB_TEXT("VK_ERROR_LAYER_NOT_PRESENT");
    case VK_ERROR_EXTENSION_NOT_PRESENT: return NWB_TEXT("VK_ERROR_EXTENSION_NOT_PRESENT");
    case VK_ERROR_FEATURE_NOT_PRESENT: return NWB_TEXT("VK_ERROR_FEATURE_NOT_PRESENT");
    case VK_ERROR_INCOMPATIBLE_DRIVER: return NWB_TEXT("VK_ERROR_INCOMPATIBLE_DRIVER");
    case VK_ERROR_TOO_MANY_OBJECTS: return NWB_TEXT("VK_ERROR_TOO_MANY_OBJECTS");
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return NWB_TEXT("VK_ERROR_FORMAT_NOT_SUPPORTED");
    case VK_ERROR_FRAGMENTED_POOL: return NWB_TEXT("VK_ERROR_FRAGMENTED_POOL");
    case VK_ERROR_UNKNOWN: return NWB_TEXT("VK_ERROR_UNKNOWN");
    case VK_ERROR_OUT_OF_POOL_MEMORY: return NWB_TEXT("VK_ERROR_OUT_OF_POOL_MEMORY");
    case VK_ERROR_INVALID_EXTERNAL_HANDLE: return NWB_TEXT("VK_ERROR_INVALID_EXTERNAL_HANDLE");
    case VK_ERROR_FRAGMENTATION: return NWB_TEXT("VK_ERROR_FRAGMENTATION");
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return NWB_TEXT("VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS");
    case VK_ERROR_SURFACE_LOST_KHR: return NWB_TEXT("VK_ERROR_SURFACE_LOST_KHR");
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return NWB_TEXT("VK_ERROR_NATIVE_WINDOW_IN_USE_KHR");
    case VK_SUBOPTIMAL_KHR: return NWB_TEXT("VK_SUBOPTIMAL_KHR");
    case VK_ERROR_OUT_OF_DATE_KHR: return NWB_TEXT("VK_ERROR_OUT_OF_DATE_KHR");
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return NWB_TEXT("VK_ERROR_INCOMPATIBLE_DISPLAY_KHR");
    case VK_ERROR_VALIDATION_FAILED_EXT: return NWB_TEXT("VK_ERROR_VALIDATION_FAILED_EXT");
    case VK_ERROR_INVALID_SHADER_NV: return NWB_TEXT("VK_ERROR_INVALID_SHADER_NV");
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT: return NWB_TEXT("VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT");
    default: return NWB_TEXT("UNKNOWN_VK_RESULT");
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

