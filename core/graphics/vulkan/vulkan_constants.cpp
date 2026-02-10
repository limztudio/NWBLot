// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Format conversion table


struct FormatMapping{
    Format::Enum format;
    VkFormat vkFormat;
    u32 bytesPerPixel;
    bool hasDepth;
    bool hasStencil;
    bool isCompressed;
};

static constexpr FormatMapping g_FormatMappings[] = {
    // RGBA formats
    { Format::RGBA32_FLOAT,       VK_FORMAT_R32G32B32A32_SFLOAT,  16, false, false, false },
    { Format::RGBA32_UINT,        VK_FORMAT_R32G32B32A32_UINT,    16, false, false, false },
    { Format::RGBA32_SINT,        VK_FORMAT_R32G32B32A32_SINT,    16, false, false, false },
    
    { Format::RGB32_FLOAT,        VK_FORMAT_R32G32B32_SFLOAT,     12, false, false, false },
    { Format::RGB32_UINT,         VK_FORMAT_R32G32B32_UINT,       12, false, false, false },
    { Format::RGB32_SINT,         VK_FORMAT_R32G32B32_SINT,       12, false, false, false },
    
    { Format::RGBA16_FLOAT,       VK_FORMAT_R16G16B16A16_SFLOAT,  8,  false, false, false },
    { Format::RGBA16_UNORM,       VK_FORMAT_R16G16B16A16_UNORM,   8,  false, false, false },
    { Format::RGBA16_SNORM,       VK_FORMAT_R16G16B16A16_SNORM,   8,  false, false, false },
    { Format::RGBA16_UINT,        VK_FORMAT_R16G16B16A16_UINT,    8,  false, false, false },
    { Format::RGBA16_SINT,        VK_FORMAT_R16G16B16A16_SINT,    8,  false, false, false },
    
    { Format::RG32_FLOAT,         VK_FORMAT_R32G32_SFLOAT,        8,  false, false, false },
    { Format::RG32_UINT,          VK_FORMAT_R32G32_UINT,          8,  false, false, false },
    { Format::RG32_SINT,          VK_FORMAT_R32G32_SINT,          8,  false, false, false },
    
    { Format::R10G10B10A2_UNORM,  VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4, false, false, false },
    { Format::R11G11B10_FLOAT,    VK_FORMAT_B10G11R11_UFLOAT_PACK32,  4, false, false, false },
    
    { Format::RGBA8_UNORM,        VK_FORMAT_R8G8B8A8_UNORM,       4,  false, false, false },
    { Format::RGBA8_SNORM,        VK_FORMAT_R8G8B8A8_SNORM,       4,  false, false, false },
    { Format::RGBA8_UINT,         VK_FORMAT_R8G8B8A8_UINT,        4,  false, false, false },
    { Format::RGBA8_SINT,         VK_FORMAT_R8G8B8A8_SINT,        4,  false, false, false },
    { Format::RGBA8_UNORM_SRGB,   VK_FORMAT_R8G8B8A8_SRGB,        4,  false, false, false },
    { Format::BGRA8_UNORM,        VK_FORMAT_B8G8R8A8_UNORM,       4,  false, false, false },
    { Format::BGRA8_UNORM_SRGB,   VK_FORMAT_B8G8R8A8_SRGB,        4,  false, false, false },
    
    { Format::RG16_FLOAT,         VK_FORMAT_R16G16_SFLOAT,        4,  false, false, false },
    { Format::RG16_UNORM,         VK_FORMAT_R16G16_UNORM,         4,  false, false, false },
    { Format::RG16_SNORM,         VK_FORMAT_R16G16_SNORM,         4,  false, false, false },
    { Format::RG16_UINT,          VK_FORMAT_R16G16_UINT,          4,  false, false, false },
    { Format::RG16_SINT,          VK_FORMAT_R16G16_SINT,          4,  false, false, false },
    
    { Format::R32_FLOAT,          VK_FORMAT_R32_SFLOAT,           4,  false, false, false },
    { Format::R32_UINT,           VK_FORMAT_R32_UINT,             4,  false, false, false },
    { Format::R32_SINT,           VK_FORMAT_R32_SINT,             4,  false, false, false },
    
    { Format::RG8_UNORM,          VK_FORMAT_R8G8_UNORM,           2,  false, false, false },
    { Format::RG8_SNORM,          VK_FORMAT_R8G8_SNORM,           2,  false, false, false },
    { Format::RG8_UINT,           VK_FORMAT_R8G8_UINT,            2,  false, false, false },
    { Format::RG8_SINT,           VK_FORMAT_R8G8_SINT,            2,  false, false, false },
    
    { Format::R16_FLOAT,          VK_FORMAT_R16_SFLOAT,           2,  false, false, false },
    { Format::R16_UNORM,          VK_FORMAT_R16_UNORM,            2,  false, false, false },
    { Format::R16_SNORM,          VK_FORMAT_R16_SNORM,            2,  false, false, false },
    { Format::R16_UINT,           VK_FORMAT_R16_UINT,             2,  false, false, false },
    { Format::R16_SINT,           VK_FORMAT_R16_SINT,             2,  false, false, false },
    
    { Format::R8_UNORM,           VK_FORMAT_R8_UNORM,             1,  false, false, false },
    { Format::R8_SNORM,           VK_FORMAT_R8_SNORM,             1,  false, false, false },
    { Format::R8_UINT,            VK_FORMAT_R8_UINT,              1,  false, false, false },
    { Format::R8_SINT,            VK_FORMAT_R8_SINT,              1,  false, false, false },
    
    // Depth/Stencil formats
    { Format::D32,                VK_FORMAT_D32_SFLOAT,           4,  true,  false, false },
    { Format::D24S8,              VK_FORMAT_D24_UNORM_S8_UINT,    4,  true,  true,  false },
    { Format::D32S8,              VK_FORMAT_D32_SFLOAT_S8_UINT,   5,  true,  true,  false },
    { Format::D16,                VK_FORMAT_D16_UNORM,            2,  true,  false, false },
    
    // Compressed formats - BC (DXT)
    { Format::BC1_UNORM,          VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 0,  false, false, true },
    { Format::BC1_UNORM_SRGB,     VK_FORMAT_BC1_RGBA_SRGB_BLOCK,  0,  false, false, true },
    { Format::BC2_UNORM,          VK_FORMAT_BC2_UNORM_BLOCK,      0,  false, false, true },
    { Format::BC2_UNORM_SRGB,     VK_FORMAT_BC2_SRGB_BLOCK,       0,  false, false, true },
    { Format::BC3_UNORM,          VK_FORMAT_BC3_UNORM_BLOCK,      0,  false, false, true },
    { Format::BC3_UNORM_SRGB,     VK_FORMAT_BC3_SRGB_BLOCK,       0,  false, false, true },
    { Format::BC4_UNORM,          VK_FORMAT_BC4_UNORM_BLOCK,      0,  false, false, true },
    { Format::BC4_SNORM,          VK_FORMAT_BC4_SNORM_BLOCK,      0,  false, false, true },
    { Format::BC5_UNORM,          VK_FORMAT_BC5_UNORM_BLOCK,      0,  false, false, true },
    { Format::BC5_SNORM,          VK_FORMAT_BC5_SNORM_BLOCK,      0,  false, false, true },
    { Format::BC6H_UFLOAT,        VK_FORMAT_BC6H_UFLOAT_BLOCK,    0,  false, false, true },
    { Format::BC6H_SFLOAT,        VK_FORMAT_BC6H_SFLOAT_BLOCK,    0,  false, false, true },
    { Format::BC7_UNORM,          VK_FORMAT_BC7_UNORM_BLOCK,      0,  false, false, true },
    { Format::BC7_UNORM_SRGB,     VK_FORMAT_BC7_SRGB_BLOCK,       0,  false, false, true },
};

static constexpr usize g_NumFormatMappings = LengthOf(g_FormatMappings);


constexpr VkFormat ConvertFormat(Format::Enum format){
    for(usize i = 0; i < g_NumFormatMappings; ++i){
        if(g_FormatMappings[i].format == format)
            return g_FormatMappings[i].vkFormat;
    }
    
    // Unsupported format
    return VK_FORMAT_UNDEFINED;
}

constexpr VkAccessFlags2 GetVkAccessFlags(ResourceStates::Mask states){
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
    if(states & ResourceStates::StreamOut)
        flags |= VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
    if(states & ResourceStates::CopyDest)
        flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    if(states & ResourceStates::CopySource)
        flags |= VK_ACCESS_2_TRANSFER_READ_BIT;
    if(states & ResourceStates::ResolveDest)
        flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    if(states & ResourceStates::ResolveSource)
        flags |= VK_ACCESS_2_TRANSFER_READ_BIT;
    if(states & ResourceStates::Present)
        flags |= 0; // No access, just layout
    if(states & ResourceStates::AccelStructRead)
        flags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    if(states & ResourceStates::AccelStructWrite)
        flags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    if(states & ResourceStates::AccelStructBuildInput)
        flags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    if(states & ResourceStates::ShadingRateSurface)
        flags |= VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;
    if(states & ResourceStates::OpacityMicromapRead)
        flags |= VK_ACCESS_2_SHADER_READ_BIT;
    
    return flags;
}

constexpr VkPipelineStageFlags2 GetVkPipelineStageFlags(ResourceStates::Mask states){
    VkPipelineStageFlags2 flags = 0;
    
    if(states & ResourceStates::VertexBuffer)
        flags |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    if(states & ResourceStates::IndexBuffer)
        flags |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    if(states & (ResourceStates::ConstantBuffer | ResourceStates::ShaderResource | ResourceStates::UnorderedAccess))
        flags |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    if(states & ResourceStates::IndirectArgument)
        flags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    if(states & ResourceStates::RenderTarget)
        flags |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    if(states & (ResourceStates::DepthWrite | ResourceStates::DepthRead))
        flags |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    if(states & ResourceStates::StreamOut)
        flags |= VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT;
    if(states & (ResourceStates::CopyDest | ResourceStates::CopySource | ResourceStates::ResolveDest | ResourceStates::ResolveSource))
        flags |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    if(states & (ResourceStates::AccelStructRead | ResourceStates::AccelStructWrite | ResourceStates::AccelStructBuildInput))
        flags |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
    if(states & ResourceStates::ShadingRateSurface)
        flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
    
    if(flags == 0)
        flags = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    
    return flags;
}

constexpr VkImageLayout GetVkImageLayout(ResourceStates::Mask states){
    // Return the most appropriate layout for the given state
    // Priority: specific states first, general states last
    
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
    if(states & ResourceStates::ResolveDest)
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if(states & ResourceStates::ResolveSource)
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    if(states & ResourceStates::Present)
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    if(states & ResourceStates::ShadingRateSurface)
        return VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
    
    return VK_IMAGE_LAYOUT_GENERAL;
}

constexpr VkSampleCountFlagBits GetSampleCountFlagBits(u32 sampleCount){
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


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkFormat ConvertFormat(Format::Enum format){
    return __hidden_vulkan::ConvertFormat(format);
}

const char* ResultToString(VkResult result){
    switch(result){
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_EVENT_SET: return "VK_EVENT_SET";
    case VK_EVENT_RESET: return "VK_EVENT_RESET";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
    case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
    case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT: return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
    default: return "UNKNOWN_VK_RESULT";
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

