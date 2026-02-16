// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr VkDescriptorType ConvertDescriptorType(ResourceType::Enum type){
    switch(type){
    case ResourceType::Texture_SRV:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case ResourceType::Texture_UAV:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case ResourceType::TypedBuffer_SRV:
        return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    case ResourceType::TypedBuffer_UAV:
        return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    case ResourceType::StructuredBuffer_SRV:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case ResourceType::StructuredBuffer_UAV:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case ResourceType::ConstantBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case ResourceType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case ResourceType::RawBuffer_SRV:
    case ResourceType::RawBuffer_UAV:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case ResourceType::RayTracingAccelStruct:
        return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    default:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}

constexpr VkShaderStageFlags ConvertShaderStages(ShaderType::Mask stages){
    VkShaderStageFlags flags = 0;
    
    if(stages & ShaderType::Vertex)
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if(stages & ShaderType::Hull)
        flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if(stages & ShaderType::Domain)
        flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    if(stages & ShaderType::Geometry)
        flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if(stages & ShaderType::Pixel)
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if(stages & ShaderType::Compute)
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    if(stages & ShaderType::Amplification)
        flags |= VK_SHADER_STAGE_TASK_BIT_EXT;
    if(stages & ShaderType::Mesh)
        flags |= VK_SHADER_STAGE_MESH_BIT_EXT;
    if(stages & ShaderType::AllRayTracing)
        flags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR | 
                 VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    
    if(flags == 0)
        flags = VK_SHADER_STAGE_ALL;
    
    return flags;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BindingLayout::BindingLayout(const VulkanContext& context)
    : m_context(context)
{}

BindingLayout::~BindingLayout(){
    if(pipelineLayout){
        vkDestroyPipelineLayout(m_context.device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    
    for(VkDescriptorSetLayout layout : descriptorSetLayouts){
        if(layout)
            vkDestroyDescriptorSetLayout(m_context.device, layout, nullptr);
    }
    descriptorSetLayouts.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DescriptorTable::DescriptorTable(const VulkanContext& context)
    : m_context(context)
{}

DescriptorTable::~DescriptorTable(){
    if(descriptorPool != VK_NULL_HANDLE){
        vkDestroyDescriptorPool(m_context.device, descriptorPool, m_context.allocationCallbacks);
        descriptorPool = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BindingSet::BindingSet(const VulkanContext& context)
    : m_context(context)
{}
BindingSet::~BindingSet(){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BindingLayoutHandle Device::createBindingLayout(const BindingLayoutDesc& desc){
    auto* layout = new BindingLayout(m_context);
    layout->desc = desc;
    
    Vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.bindings.size());
    
    u32 pushConstantByteSize = 0;
    for(const auto& item : desc.bindings){
        if(item.type == ResourceType::PushConstants){
            pushConstantByteSize = item.size;
            continue;
        }
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = item.slot;
        binding.descriptorType = __hidden_vulkan::ConvertDescriptorType(item.type);
        binding.descriptorCount = item.getArraySize();
        binding.stageFlags = __hidden_vulkan::ConvertShaderStages(desc.visibility);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);
    }
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkResult res = vkCreateDescriptorSetLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &setLayout);
    
    if(res == VK_SUCCESS){
        layout->descriptorSetLayouts.push_back(setLayout);
    }
    
    VkPushConstantRange pushConstantRange = {};
    bool hasPushConstants = pushConstantByteSize > 0;
    
    if(hasPushConstants){
        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.offset = 0;
        pushConstantRange.size = pushConstantByteSize;
    }
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutInfo.setLayoutCount = static_cast<u32>(layout->descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = layout->descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = hasPushConstants ? 1 : 0;
    pipelineLayoutInfo.pPushConstantRanges = hasPushConstants ? &pushConstantRange : nullptr;
    
    res = vkCreatePipelineLayout(m_context.device, &pipelineLayoutInfo, m_context.allocationCallbacks, &layout->pipelineLayout);
    
    if(res != VK_SUCCESS){
        delete layout;
        return nullptr;
    }
    
    return BindingLayoutHandle(layout, AdoptRef);
}

BindingLayoutHandle Device::createBindlessLayout(const BindlessLayoutDesc& desc){
    auto* layout = new BindingLayout(m_context);
    layout->isBindless = true;
    layout->bindlessDesc = desc;
    
    Vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.registerSpaces.size());
    Vector<VkDescriptorBindingFlags> bindingFlags;
    bindingFlags.reserve(desc.registerSpaces.size());
    
    for(const auto& item : desc.registerSpaces){
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = item.slot;
        binding.descriptorType = __hidden_vulkan::ConvertDescriptorType(item.type);
        binding.descriptorCount = desc.maxCapacity;
        binding.stageFlags = __hidden_vulkan::ConvertShaderStages(desc.visibility);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);
        
        VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        bindingFlags.push_back(flags);
    }
    
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
    bindingFlagsInfo.bindingCount = static_cast<u32>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutInfo.pNext = &bindingFlagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkResult res = vkCreateDescriptorSetLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &setLayout);
    
    if(res == VK_SUCCESS)
        layout->descriptorSetLayouts.push_back(setLayout);
    else
        NWB_LOGGER_WARNING(NWB_TEXT("Failed to create bindless descriptor set layout: {}"), ResultToString(res));
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutInfo.setLayoutCount = static_cast<u32>(layout->descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = layout->descriptorSetLayouts.data();
    
    res = vkCreatePipelineLayout(m_context.device, &pipelineLayoutInfo, m_context.allocationCallbacks, &layout->pipelineLayout);
    
    if(res != VK_SUCCESS){
        delete layout;
        return nullptr;
    }
    
    return BindingLayoutHandle(layout, AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DescriptorTableHandle Device::createDescriptorTable(IBindingLayout* _layout){
    auto* layout = checked_cast<BindingLayout*>(_layout);
    
    auto* table = new DescriptorTable(m_context);
    table->layout = layout;
    
    Vector<VkDescriptorPoolSize> poolSizes;
    
    VkDescriptorPoolSize uniformSize = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16 };
    VkDescriptorPoolSize storageSize = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16 };
    VkDescriptorPoolSize samplerSize = { VK_DESCRIPTOR_TYPE_SAMPLER, 16 };
    VkDescriptorPoolSize sampledImageSize = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64 };
    VkDescriptorPoolSize storageImageSize = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16 };
    
    poolSizes.push_back(uniformSize);
    poolSizes.push_back(storageSize);
    poolSizes.push_back(samplerSize);
    poolSizes.push_back(sampledImageSize);
    poolSizes.push_back(storageImageSize);
    
    VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = static_cast<u32>(layout->descriptorSetLayouts.size());
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkResult res = vkCreateDescriptorPool(m_context.device, &poolInfo, m_context.allocationCallbacks, &pool);
    
    if(res != VK_SUCCESS){
        delete table;
        return nullptr;
    }
    
    if(!layout->descriptorSetLayouts.empty()){
        VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = pool;
        allocInfo.descriptorSetCount = static_cast<u32>(layout->descriptorSetLayouts.size());
        allocInfo.pSetLayouts = layout->descriptorSetLayouts.data();
        
        table->descriptorSets.resize(layout->descriptorSetLayouts.size());
        res = vkAllocateDescriptorSets(m_context.device, &allocInfo, table->descriptorSets.data());
        
        if(res != VK_SUCCESS){
            vkDestroyDescriptorPool(m_context.device, pool, m_context.allocationCallbacks);
            delete table;
            return nullptr;
        }
    }
    
    table->descriptorPool = pool;
    
    return DescriptorTableHandle(table, AdoptRef);
}

void Device::resizeDescriptorTable(IDescriptorTable* descriptorTable, u32 newSize, bool keepContents){
    auto* table = checked_cast<DescriptorTable*>(descriptorTable);
    
    if(!table->layout || newSize == 0)
        return;
    
    if(table->descriptorPool != VK_NULL_HANDLE){
        vkDestroyDescriptorPool(m_context.device, table->descriptorPool, m_context.allocationCallbacks);
        table->descriptorPool = VK_NULL_HANDLE;
    }
    table->descriptorSets.clear();
    
    Vector<VkDescriptorPoolSize> poolSizes;
    VkDescriptorPoolSize uniformSize = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, newSize };
    VkDescriptorPoolSize storageSize = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, newSize };
    VkDescriptorPoolSize samplerSize = { VK_DESCRIPTOR_TYPE_SAMPLER, newSize };
    VkDescriptorPoolSize sampledImageSize = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, newSize };
    VkDescriptorPoolSize storageImageSize = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, newSize };
    poolSizes.push_back(uniformSize);
    poolSizes.push_back(storageSize);
    poolSizes.push_back(samplerSize);
    poolSizes.push_back(sampledImageSize);
    poolSizes.push_back(storageImageSize);
    
    VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = newSize;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    
    VkResult res = vkCreateDescriptorPool(m_context.device, &poolInfo, m_context.allocationCallbacks, &table->descriptorPool);
    if(res != VK_SUCCESS)
        return;
    
    if(!table->layout->descriptorSetLayouts.empty()){
        Vector<VkDescriptorSetLayout> layouts(newSize, table->layout->descriptorSetLayouts[0]);
        
        VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = table->descriptorPool;
        allocInfo.descriptorSetCount = newSize;
        allocInfo.pSetLayouts = layouts.data();
        
        table->descriptorSets.resize(newSize);
        vkAllocateDescriptorSets(m_context.device, &allocInfo, table->descriptorSets.data());
    }
    
    (void)keepContents;
}

bool Device::writeDescriptorTable(IDescriptorTable* descriptorTable, const BindingSetItem& item){
    auto* table = checked_cast<DescriptorTable*>(descriptorTable);
    
    if(table->descriptorSets.empty())
        return false;
    
    VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = table->descriptorSets[0];
    write.dstBinding = item.slot;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = __hidden_vulkan::ConvertDescriptorType(item.type);
    
    VkDescriptorBufferInfo bufferInfo = {};
    VkDescriptorImageInfo imageInfo = {};
    VkWriteDescriptorSetAccelerationStructureKHR asInfo = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
    
    switch(item.type){
    case ResourceType::ConstantBuffer:
    case ResourceType::StructuredBuffer_SRV:
    case ResourceType::StructuredBuffer_UAV:
    case ResourceType::RawBuffer_SRV:
    case ResourceType::RawBuffer_UAV:{
        auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
        bufferInfo.buffer = buffer->buffer;
        bufferInfo.offset = item.range.byteOffset;
        bufferInfo.range = item.range.byteSize > 0 ? item.range.byteSize : VK_WHOLE_SIZE;
        write.pBufferInfo = &bufferInfo;
        break;
    }
    case ResourceType::Texture_SRV:
    case ResourceType::Texture_UAV:{
        auto* texture = checked_cast<Texture*>(item.resourceHandle);
        imageInfo.imageView = texture->getView(item.subresources, item.dimension, item.format, false);
        imageInfo.imageLayout = item.type == ResourceType::Texture_UAV ? 
                                VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        write.pImageInfo = &imageInfo;
        break;
    }
    case ResourceType::Sampler:{
        auto* sampler = checked_cast<Sampler*>(item.resourceHandle);
        imageInfo.sampler = sampler->sampler;
        write.pImageInfo = &imageInfo;
        break;
    }
    case ResourceType::RayTracingAccelStruct:{
        auto* as = checked_cast<AccelStruct*>(item.resourceHandle);
        asInfo.accelerationStructureCount = 1;
        asInfo.pAccelerationStructures = &as->accelStruct;
        write.pNext = &asInfo;
        break;
    }
    default:
        return false;
    }
    
    vkUpdateDescriptorSets(m_context.device, 1, &write, 0, nullptr);
    return true;
}


BindingSetHandle Device::createBindingSet(const BindingSetDesc& desc, IBindingLayout* _layout){
    auto* layout = checked_cast<BindingLayout*>(_layout);
    
    auto* bindingSet = new BindingSet(m_context);
    bindingSet->desc = desc;
    
    DescriptorTableHandle tableHandle = createDescriptorTable(_layout);
    if(!tableHandle){
        delete bindingSet;
        return nullptr;
    }
    
    bindingSet->descriptorTable = checked_cast<DescriptorTable*>(tableHandle.get());
    
    bindingSet->descriptorSets = bindingSet->descriptorTable->descriptorSets;
    bindingSet->layout = layout;
    
    Vector<VkWriteDescriptorSet> writes;
    Vector<VkDescriptorBufferInfo> bufferInfos;
    Vector<VkDescriptorImageInfo> imageInfos;
    
    bufferInfos.reserve(desc.bindings.size());
    imageInfos.reserve(desc.bindings.size());
    
    for(const auto& item : desc.bindings){
        if(!item.resourceHandle)
            continue;
        
        VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = bindingSet->descriptorSets[0];
        write.dstBinding = item.slot;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = __hidden_vulkan::ConvertDescriptorType(item.type);
        
        switch(item.type){
        case ResourceType::ConstantBuffer:
        case ResourceType::StructuredBuffer_SRV:
        case ResourceType::StructuredBuffer_UAV:
        case ResourceType::RawBuffer_SRV:
        case ResourceType::RawBuffer_UAV:{
            auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
            VkDescriptorBufferInfo bufInfo = {};
            bufInfo.buffer = buffer->buffer;
            bufInfo.offset = item.range.byteOffset;
            bufInfo.range = item.range.byteSize > 0 ? item.range.byteSize : VK_WHOLE_SIZE;
            bufferInfos.push_back(bufInfo);
            write.pBufferInfo = &bufferInfos.back();
            writes.push_back(write);
            break;
        }
        case ResourceType::Texture_SRV:
        case ResourceType::Texture_UAV:{
            auto* texture = checked_cast<Texture*>(item.resourceHandle);
            VkDescriptorImageInfo imgInfo = {};
            imgInfo.imageView = texture->getView(item.subresources, item.dimension, item.format, false);
            imgInfo.imageLayout = item.type == ResourceType::Texture_UAV ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos.push_back(imgInfo);
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
            break;
        }
        case ResourceType::Sampler:{
            auto* sampler = checked_cast<Sampler*>(item.resourceHandle);
            VkDescriptorImageInfo imgInfo = {};
            imgInfo.sampler = sampler->sampler;
            imageInfos.push_back(imgInfo);
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
            break;
        }
        default:
            break;
        }
    }
    
    if(!writes.empty())
        vkUpdateDescriptorSets(m_context.device, static_cast<u32>(writes.size()), writes.data(), 0, nullptr);
    
    return BindingSetHandle(bindingSet, AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

