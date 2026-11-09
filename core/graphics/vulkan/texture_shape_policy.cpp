// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "texture_shape_policy.h"
#include "texture_resource_detail.h"

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


namespace VulkanTextureDetail{


u32 TextureShapePolicy::MaxMipLevels(const TextureDesc& desc)noexcept{
    const u32 depth = desc.dimension == TextureDimension::Texture3D ? desc.depth : 1u;
    const u32 maxExtent = Max(Max(desc.width, desc.height), depth);
    return maxExtent > 1u ? FloorLog2(maxExtent) + 1u : 1u;
}

b8 TextureShapePolicy::IsValid(const TextureDesc& desc)noexcept{
    if(desc.width == 0u || desc.height == 0u || desc.depth == 0u || desc.mipLevels == 0u || desc.arraySize == 0u)
        return false;

    b8 dimensionValid = false;
    switch(desc.dimension){
    case TextureDimension::Texture1D:
        dimensionValid = desc.height == 1u && desc.depth == 1u && desc.arraySize == 1u;
        break;
    case TextureDimension::Texture1DArray:
        dimensionValid = desc.height == 1u && desc.depth == 1u;
        break;
    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DMS:
        dimensionValid = desc.depth == 1u && desc.arraySize == 1u;
        break;
    case TextureDimension::Texture2DArray:
    case TextureDimension::Texture2DMSArray:
        dimensionValid = desc.depth == 1u;
        break;
    case TextureDimension::TextureCube:
        dimensionValid = desc.depth == 1u && desc.width == desc.height && desc.arraySize == s_TextureCubeLayerCount;
        break;
    case TextureDimension::TextureCubeArray:
        dimensionValid = desc.depth == 1u && desc.width == desc.height && desc.arraySize >= s_TextureCubeLayerCount && (desc.arraySize % s_TextureCubeLayerCount) == 0u;
        break;
    case TextureDimension::Texture3D:
        dimensionValid = desc.arraySize == 1u;
        break;
    default:
        return false;
    }

    return dimensionValid && desc.mipLevels <= TextureShapePolicy::MaxMipLevels(desc);
}

b8 TextureShapePolicy::Validate(const TextureDesc& desc, TStringView operationName){
    if(desc.width == 0 || desc.height == 0 || desc.depth == 0 || desc.mipLevels == 0 || desc.arraySize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: dimensions, mip count, and array size must be nonzero"), operationName);
        return false;
    }
    switch(desc.dimension){
    case TextureDimension::Unknown:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: texture dimension is unknown"), operationName);
        return false;
    case TextureDimension::Texture1D:
    case TextureDimension::Texture1DArray:
    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DArray:
    case TextureDimension::TextureCube:
    case TextureDimension::TextureCubeArray:
    case TextureDimension::Texture2DMS:
    case TextureDimension::Texture2DMSArray:
    case TextureDimension::Texture3D:
        break;
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: texture dimension is invalid"), operationName);
        return false;
    }
    if((desc.dimension == TextureDimension::Texture1D || desc.dimension == TextureDimension::Texture2D || desc.dimension == TextureDimension::Texture2DMS) && desc.arraySize != 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: non-array texture array size must be 1"), operationName);
        return false;
    }
    if((desc.dimension == TextureDimension::Texture1D || desc.dimension == TextureDimension::Texture1DArray) && (desc.height != 1 || desc.depth != 1)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: 1D texture height and depth must be 1"), operationName);
        return false;
    }
    if(desc.dimension != TextureDimension::Texture3D && desc.depth != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: non-3D texture depth must be 1"), operationName);
        return false;
    }
    if(desc.dimension == TextureDimension::Texture3D && desc.arraySize != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: 3D texture array size must be 1"), operationName);
        return false;
    }

    const u32 maxMipLevels = TextureShapePolicy::MaxMipLevels(desc);
    if(desc.mipLevels > maxMipLevels){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: mip levels {} exceed maximum {} for texture dimensions {}x{}x{}"), operationName, desc.mipLevels, maxMipLevels, desc.width, desc.height, desc.depth);
        return false;
    }

    if(desc.dimension == TextureDimension::TextureCube || desc.dimension == TextureDimension::TextureCubeArray){
        if(desc.width != desc.height){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: cube textures must have equal width and height"), operationName);
            return false;
        }
        if(desc.dimension == TextureDimension::TextureCube && desc.arraySize != VulkanTextureDetail::s_TextureCubeLayerCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: cube textures must have exactly 6 array layers"), operationName);
            return false;
        }
        if(desc.dimension == TextureDimension::TextureCubeArray && (desc.arraySize < VulkanTextureDetail::s_TextureCubeLayerCount || (desc.arraySize % VulkanTextureDetail::s_TextureCubeLayerCount) != 0)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: cube texture arrays must have a positive multiple of 6 array layers"), operationName);
            return false;
        }
    }

    return true;
}


};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

