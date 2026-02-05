// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// BindingLayout - Pipeline resource binding layout
//-----------------------------------------------------------------------------

BindingLayout::BindingLayout(const VulkanContext& context)
    : m_Context(context)
{}

BindingLayout::~BindingLayout(){
    const VulkanContext& vk = *m_Context;
    
    if(pipelineLayout){
        vk.vkDestroyPipelineLayout(vk.device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    
    for(VkDescriptorSetLayout layout : descriptorSetLayouts){
        if(layout)
            vk.vkDestroyDescriptorSetLayout(vk.device, layout, nullptr);
    }
    descriptorSetLayouts.clear();
}

//-----------------------------------------------------------------------------
// DescriptorTable - Descriptor set wrapper
//-----------------------------------------------------------------------------

DescriptorTable::DescriptorTable(const VulkanContext& context)
    : m_Context(context)
{}

DescriptorTable::~DescriptorTable(){
    // Descriptor sets are freed automatically when pool is destroyed
    // No explicit cleanup needed here
}

//-----------------------------------------------------------------------------
// BindingSet - Concrete binding set for command list
//-----------------------------------------------------------------------------

BindingSet::BindingSet(const VulkanContext& context)
    : m_Context(context)
{}

BindingSet::~BindingSet(){
    // Binding sets reference descriptor tables, no explicit cleanup
}

//-----------------------------------------------------------------------------
// Device - Binding Layout creation
//-----------------------------------------------------------------------------

BindingLayoutHandle Device::createBindingLayout(const BindingLayoutDesc& desc){
    // TODO: Implement binding layout creation
    // 1. Parse BindingLayoutDesc to extract binding items
    // 2. Create VkDescriptorSetLayout for each descriptor set
    // 3. Create VkPipelineLayout combining all descriptor set layouts
    // 4. Handle push constants if present
    
    BindingLayout* layout = new BindingLayout(m_Context);
    layout->desc = desc;
    
    const VulkanContext& vk = m_Context;
    
    // Create descriptor set layout bindings from desc
    Vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.bindings.size());
    
    for(const auto& item : desc.bindings){
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = item.slot;
        binding.descriptorType = convertDescriptorType(item.type);
        binding.descriptorCount = item.size > 0 ? item.size : 1;
        binding.stageFlags = convertShaderStages(item.shaderVisibility);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);
    }
    
    VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkResult res = vkCreateDescriptorSetLayout(vk.device, &layoutInfo, vk.allocationCallbacks, &setLayout);
    
    if(res == VK_SUCCESS){
        layout->descriptorSetLayouts.push_back(setLayout);
    }
    
    // Create pipeline layout
    VkPushConstantRange pushConstantRange = {};
    bool hasPushConstants = desc.pushConstantByteSize > 0;
    
    if(hasPushConstants){
        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.offset = 0;
        pushConstantRange.size = desc.pushConstantByteSize;
    }
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutInfo.setLayoutCount = static_cast<u32>(layout->descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = layout->descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = hasPushConstants ? 1 : 0;
    pipelineLayoutInfo.pPushConstantRanges = hasPushConstants ? &pushConstantRange : nullptr;
    
    res = vkCreatePipelineLayout(vk.device, &pipelineLayoutInfo, vk.allocationCallbacks, &layout->pipelineLayout);
    
    if(res != VK_SUCCESS){
        delete layout;
        return nullptr;
    }
    
    return BindingLayoutHandle::Create(layout);
}

BindingLayoutHandle Device::createBindlessLayout(const BindlessLayoutDesc& desc){
    // Create a bindless layout with UPDATE_AFTER_BIND flag for descriptor indexing
    BindingLayout* layout = new BindingLayout(m_Context);
    
    const VulkanContext& vk = m_Context;
    
    // Create descriptor set layout bindings
    Vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.bindings.size());
    Vector<VkDescriptorBindingFlags> bindingFlags;
    bindingFlags.reserve(desc.bindings.size());
    
    for(const auto& item : desc.bindings){
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = item.slot;
        binding.descriptorType = convertDescriptorType(item.type);
        binding.descriptorCount = item.maxBindingSize; // Large array
        binding.stageFlags = convertShaderStages(item.visibility);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);
        
        VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | 
                                          VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                          VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
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
    VkResult res = vkCreateDescriptorSetLayout(vk.device, &layoutInfo, vk.allocationCallbacks, &setLayout);
    
    if(res == VK_SUCCESS){
        layout->descriptorSetLayouts.push_back(setLayout);
    }
    
    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutInfo.setLayoutCount = static_cast<u32>(layout->descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = layout->descriptorSetLayouts.data();
    
    res = vkCreatePipelineLayout(vk.device, &pipelineLayoutInfo, vk.allocationCallbacks, &layout->pipelineLayout);
    
    if(res != VK_SUCCESS){
        delete layout;
        return nullptr;
    }
    
    return BindingLayoutHandle::Create(layout);
}

//-----------------------------------------------------------------------------
// Device - Descriptor Table creation
//-----------------------------------------------------------------------------

DescriptorTableHandle Device::createDescriptorTable(IBindingLayout* _layout){
    // Allocate descriptor sets from pool
    BindingLayout* layout = checked_cast<BindingLayout*>(_layout);
    
    DescriptorTable* table = new DescriptorTable(m_Context);
    table->layout = layout;
    
    const VulkanContext& vk = m_Context;
    
    // Get or create descriptor pool
    // For now, create a dedicated pool for this table
    Vector<VkDescriptorPoolSize> poolSizes;
    
    // Estimate pool sizes based on layout
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
    VkResult res = vkCreateDescriptorPool(vk.device, &poolInfo, vk.allocationCallbacks, &pool);
    
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
        res = vkAllocateDescriptorSets(vk.device, &allocInfo, table->descriptorSets.data());
        
        if(res != VK_SUCCESS){
            vkDestroyDescriptorPool(vk.device, pool, vk.allocationCallbacks);
            delete table;
            return nullptr;
        }
    }
    
    return DescriptorTableHandle::Create(table);
}

void Device::resizeDescriptorTable(IDescriptorTable* descriptorTable, u32 newSize, bool keepContents){
    // Resize is typically used for bindless arrays
    // Implementation depends on whether we support variable descriptor counts
}

bool Device::writeDescriptorTable(IDescriptorTable* descriptorTable, const BindingSetItem& item){
    DescriptorTable* table = checked_cast<DescriptorTable*>(descriptorTable);
    const VulkanContext& vk = m_Context;
    
    if(table->descriptorSets.empty())
        return false;
    
    VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = table->descriptorSets[0];
    write.dstBinding = item.slot;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = convertDescriptorType(item.type);
    
    VkDescriptorBufferInfo bufferInfo = {};
    VkDescriptorImageInfo imageInfo = {};
    VkWriteDescriptorSetAccelerationStructureKHR asInfo = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
    
    switch(item.type){
    case ResourceType::ConstantBuffer:
    case ResourceType::StructuredBuffer_SRV:
    case ResourceType::StructuredBuffer_UAV:
    case ResourceType::RawBuffer_SRV:
    case ResourceType::RawBuffer_UAV:{
        Buffer* buffer = checked_cast<Buffer*>(item.resourceHandle);
        bufferInfo.buffer = buffer->buffer;
        bufferInfo.offset = item.range.byteOffset;
        bufferInfo.range = item.range.byteSize > 0 ? item.range.byteSize : VK_WHOLE_SIZE;
        write.pBufferInfo = &bufferInfo;
        break;
    }
    case ResourceType::Texture_SRV:
    case ResourceType::Texture_UAV:{
        Texture* texture = checked_cast<Texture*>(item.resourceHandle);
        imageInfo.imageView = texture->getView(item.subresources, item.dimension, item.format, false);
        imageInfo.imageLayout = item.type == ResourceType::Texture_UAV ? 
                                VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        write.pImageInfo = &imageInfo;
        break;
    }
    case ResourceType::Sampler:{
        Sampler* sampler = checked_cast<Sampler*>(item.resourceHandle);
        imageInfo.sampler = sampler->sampler;
        write.pImageInfo = &imageInfo;
        break;
    }
    case ResourceType::RayTracingAccelStruct:{
        AccelStruct* as = checked_cast<AccelStruct*>(item.resourceHandle);
        asInfo.accelerationStructureCount = 1;
        asInfo.pAccelerationStructures = &as->accelStruct;
        write.pNext = &asInfo;
        break;
    }
    default:
        return false;
    }
    
    vkUpdateDescriptorSets(vk.device, 1, &write, 0, nullptr);
    return true;
}

//-----------------------------------------------------------------------------
// Device - Binding Set creation (bind resources to descriptor table)
//-----------------------------------------------------------------------------

BindingSetHandle Device::createBindingSet(const BindingSetDesc& desc, IBindingLayout* _layout){
    BindingLayout* layout = checked_cast<BindingLayout*>(_layout);
    
    BindingSet* bindingSet = new BindingSet(m_Context);
    bindingSet->desc = desc;
    
    // Create descriptor table for this binding set
    DescriptorTableHandle tableHandle = createDescriptorTable(_layout);
    if(!tableHandle){
        delete bindingSet;
        return nullptr;
    }
    
    bindingSet->descriptorTable = checked_cast<DescriptorTable*>(tableHandle.Get());
    
    // Store the descriptor set for direct access
    if(!bindingSet->descriptorTable->descriptorSets.empty()){
        bindingSet->descriptorSet = bindingSet->descriptorTable->descriptorSets[0];
    }
    
    const VulkanContext& vk = m_Context;
    
    // Update descriptors with actual resources
    Vector<VkWriteDescriptorSet> writes;
    Vector<VkDescriptorBufferInfo> bufferInfos;
    Vector<VkDescriptorImageInfo> imageInfos;
    
    bufferInfos.reserve(desc.bindings.size());
    imageInfos.reserve(desc.bindings.size());
    
    for(const auto& item : desc.bindings){
        if(!item.resourceHandle)
            continue;
        
        VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = bindingSet->descriptorSet;
        write.dstBinding = item.slot;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = convertDescriptorType(item.type);
        
        switch(item.type){
        case ResourceType::ConstantBuffer:
        case ResourceType::StructuredBuffer_SRV:
        case ResourceType::StructuredBuffer_UAV:
        case ResourceType::RawBuffer_SRV:
        case ResourceType::RawBuffer_UAV:{
            Buffer* buffer = checked_cast<Buffer*>(item.resourceHandle);
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
            Texture* texture = checked_cast<Texture*>(item.resourceHandle);
            VkDescriptorImageInfo imgInfo = {};
            imgInfo.imageView = texture->getView(item.subresources, item.dimension, item.format, false);
            imgInfo.imageLayout = item.type == ResourceType::Texture_UAV ? 
                                  VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos.push_back(imgInfo);
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
            break;
        }
        case ResourceType::Sampler:{
            Sampler* sampler = checked_cast<Sampler*>(item.resourceHandle);
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
    
    if(!writes.empty()){
        vkUpdateDescriptorSets(vk.device, static_cast<u32>(writes.size()), writes.data(), 0, nullptr);
    }
    
    return BindingSetHandle::Create(bindingSet);
}

//-----------------------------------------------------------------------------
// Helper functions for descriptor conversion
//-----------------------------------------------------------------------------

static VkDescriptorType convertDescriptorType(ResourceType::Enum type){
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

static VkShaderStageFlags convertShaderStages(ShaderType::Mask stages){
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

//-----------------------------------------------------------------------------
// Descriptor Pool Management
//-----------------------------------------------------------------------------

// TODO: Implement descriptor pool management
// - Create pools with various descriptor type counts
// - Allocate from pools
// - Reset pools when full
// - Free pools when no longer needed


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
