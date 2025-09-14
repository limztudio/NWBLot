// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include "../common.h"
#include "config.h"
#include "resources.h"

#include <core/alloc/assetPool.h>

#include "vk_mem_alloc.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u8 s_maxShaderStages = 5;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Buffer{
    VkBuffer buffer;
    VmaAllocation allocation;
    VkDeviceMemory deviceMemory;
    VkDeviceSize deviceSize;

    VkBufferUsageFlags typeFlags = 0;
    ResourceUsageType::Enum usage = ResourceUsageType::IMMUTABLE;
    u32 size = 0;
    u32 globalOffset = 0;

    Alloc::AssetHandle handle;
    Alloc::AssetHandle presentHandle;

    const char* name = nullptr;
};


struct Sampler{
    VkSampler sampler;

    VkFilter minFilter = VK_FILTER_NEAREST;
    VkFilter magFilter = VK_FILTER_NEAREST;
    VkSamplerMipmapMode mipFilter = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VkSamplerAddressMode addressU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode addressW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    const char* name = nullptr;
};


struct Texture{
    VkImage image;
    VkImageView imageView;
    VkFormat format;
    VkImageLayout layout;
    VmaAllocation allocation;

    u16 width = 1;
    u16 height = 1;
    u16 depth = 1;
    u8 mipLevels = 1;
    u8 flags = 0;

    Alloc::AssetHandle handle;
    TextureType::Enum type = TextureType::TEX2D;

    Sampler* sampler = nullptr;

    const char* name = nullptr;
};


struct ShaderState{
    VkPipelineShaderStageCreateInfo stageInfo[s_maxShaderStages];

    const char* name = nullptr;

    u32 activeShaderCount = 0;
    bool graphicsPipeline = false;

    spirVResult;
};


struct DescriptorBinding{
    VkDescriptorType type;
    u16 start = 0;
    u16 count = 0;
    u16 set = 0;

    const char* name = nullptr;
};


struct DescriptorSetLayout{
    VkDescriptorSetLayout descLayout;

    VkDescriptorSetLayoutBinding* binding = nullptr;
    DescriptorBinding* bindings = nullptr;
    u16 numBindings = 0;
    u16 setIndex = 0;

    Alloc::AssetHandle handle;
};


struct DescriptorSet{
    VkDescriptorSet descSet;
    
    Alloc::AssetHandle* resources = nullptr;
    Alloc::AssetHandle* samplers = nullptr;
    u16* bindings = nullptr;

    const DescriptorSetLayout* layout = nullptr;
    u32 numResources = 0;
};


struct Pipeline{
    VkPipeline pipeline;
    VkPipelineLayout pipeLayout;

    VkPipelineBindPoint bindPoint;

    Alloc::AssetHandle shaderState;


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

