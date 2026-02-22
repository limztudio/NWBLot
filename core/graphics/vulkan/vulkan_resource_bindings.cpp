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
    : RefCounter<IBindingLayout>(*context.threadPool)
    , m_context(context)
    , m_descriptorSetLayouts(Alloc::CustomAllocator<VkDescriptorSetLayout>(*context.objectArena))
{}
BindingLayout::~BindingLayout(){
    if(m_pipelineLayout){
        vkDestroyPipelineLayout(m_context.device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    for(VkDescriptorSetLayout layout : m_descriptorSetLayouts){
        if(layout)
            vkDestroyDescriptorSetLayout(m_context.device, layout, nullptr);
    }
    m_descriptorSetLayouts.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DescriptorTable::DescriptorTable(const VulkanContext& context)
    : RefCounter<IDescriptorTable>(*context.threadPool)
    , m_context(context)
    , m_descriptorSets(Alloc::CustomAllocator<VkDescriptorSet>(*context.objectArena))
{}
DescriptorTable::~DescriptorTable(){
    if(m_descriptorPool != VK_NULL_HANDLE){
        vkDestroyDescriptorPool(m_context.device, m_descriptorPool, m_context.allocationCallbacks);
        m_descriptorPool = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BindingSet::BindingSet(const VulkanContext& context)
    : RefCounter<IBindingSet>(*context.threadPool)
    , m_context(context)
    , m_descriptorSets(Alloc::CustomAllocator<VkDescriptorSet>(*context.objectArena))
{}
BindingSet::~BindingSet(){
    m_descriptorTable.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BindingLayoutHandle Device::createBindingLayout(const BindingLayoutDesc& desc){
    VkResult res = VK_SUCCESS;

    Alloc::ScratchArena<> scratchArena;

    auto* layout = NewArenaObject<BindingLayout>(*m_context.objectArena, m_context);
    layout->m_desc = desc;

    Vector<VkDescriptorSetLayoutBinding, Alloc::ScratchAllocator<VkDescriptorSetLayoutBinding>> bindings{ Alloc::ScratchAllocator<VkDescriptorSetLayoutBinding>(scratchArena) };
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
    res = vkCreateDescriptorSetLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &setLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor set layout: {}"), ResultToString(res));
        DestroyArenaObject(*m_context.objectArena, layout);
        return nullptr;
    }
    layout->m_descriptorSetLayouts.push_back(setLayout);

    VkPushConstantRange pushConstantRange = {};
    bool hasPushConstants = pushConstantByteSize > 0;

    if(hasPushConstants){
        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.offset = 0;
        pushConstantRange.size = pushConstantByteSize;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutInfo.setLayoutCount = static_cast<u32>(layout->m_descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = layout->m_descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = hasPushConstants ? 1 : 0;
    pipelineLayoutInfo.pPushConstantRanges = hasPushConstants ? &pushConstantRange : nullptr;

    res = vkCreatePipelineLayout(m_context.device, &pipelineLayoutInfo, m_context.allocationCallbacks, &layout->m_pipelineLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create pipeline layout for binding layout: {}"), ResultToString(res));
        DestroyArenaObject(*m_context.objectArena, layout);
        return nullptr;
    }

    return BindingLayoutHandle(layout, BindingLayoutHandle::deleter_type(m_context.objectArena), AdoptRef);
}

BindingLayoutHandle Device::createBindlessLayout(const BindlessLayoutDesc& desc){
    VkResult res = VK_SUCCESS;

    Alloc::ScratchArena<> scratchArena;

    auto* layout = NewArenaObject<BindingLayout>(*m_context.objectArena, m_context);
    layout->m_isBindless = true;
    layout->m_bindlessDesc = desc;

    Vector<VkDescriptorSetLayoutBinding, Alloc::ScratchAllocator<VkDescriptorSetLayoutBinding>> bindings{ Alloc::ScratchAllocator<VkDescriptorSetLayoutBinding>(scratchArena) };
    bindings.reserve(desc.registerSpaces.size());
    Vector<VkDescriptorBindingFlags, Alloc::ScratchAllocator<VkDescriptorBindingFlags>> bindingFlags{ Alloc::ScratchAllocator<VkDescriptorBindingFlags>(scratchArena) };
    bindingFlags.reserve(desc.registerSpaces.size());

    for(const auto& item : desc.registerSpaces){
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = item.slot;
        binding.descriptorType = __hidden_vulkan::ConvertDescriptorType(item.type);
        binding.descriptorCount = desc.maxCapacity;
        binding.stageFlags = __hidden_vulkan::ConvertShaderStages(desc.visibility);
        binding.pImmutableSamplers = nullptr;
        bindings.push_back(binding);

        VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        bindingFlags.push_back(flags);
    }

    if(!bindingFlags.empty())
        bindingFlags.back() |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
    bindingFlagsInfo.bindingCount = static_cast<u32>(bindingFlags.size());
    bindingFlagsInfo.pBindingFlags = bindingFlags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    layoutInfo.pNext = &bindingFlagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    res = vkCreateDescriptorSetLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &setLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to create bindless descriptor set layout: {}"), ResultToString(res));
        DestroyArenaObject(*m_context.objectArena, layout);
        return nullptr;
    }
    layout->m_descriptorSetLayouts.push_back(setLayout);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pipelineLayoutInfo.setLayoutCount = static_cast<u32>(layout->m_descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = layout->m_descriptorSetLayouts.data();

    res = vkCreatePipelineLayout(m_context.device, &pipelineLayoutInfo, m_context.allocationCallbacks, &layout->m_pipelineLayout);

    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create pipeline layout for bindless layout: {}"), ResultToString(res));
        DestroyArenaObject(*m_context.objectArena, layout);
        return nullptr;
    }

    return BindingLayoutHandle(layout, BindingLayoutHandle::deleter_type(m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DescriptorTableHandle Device::createDescriptorTable(IBindingLayout* _layout){
    VkResult res = VK_SUCCESS;

    Alloc::ScratchArena<> scratchArena;

    auto* layout = checked_cast<BindingLayout*>(_layout);

    auto* table = NewArenaObject<DescriptorTable>(*m_context.objectArena, m_context);
    table->m_layout = layout;

    Vector<VkDescriptorPoolSize, Alloc::ScratchAllocator<VkDescriptorPoolSize>> poolSizes{ Alloc::ScratchAllocator<VkDescriptorPoolSize>(scratchArena) };
    poolSizes.reserve(5);

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
    poolInfo.maxSets = static_cast<u32>(layout->m_descriptorSetLayouts.size());
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    VkDescriptorPool pool = VK_NULL_HANDLE;
    res = vkCreateDescriptorPool(m_context.device, &poolInfo, m_context.allocationCallbacks, &pool);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor pool: {}"), ResultToString(res));
        DestroyArenaObject(*m_context.objectArena, table);
        return nullptr;
    }

    if(!layout->m_descriptorSetLayouts.empty()){
        VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = pool;
        allocInfo.descriptorSetCount = static_cast<u32>(layout->m_descriptorSetLayouts.size());
        allocInfo.pSetLayouts = layout->m_descriptorSetLayouts.data();

        table->m_descriptorSets.resize(layout->m_descriptorSetLayouts.size());
        res = vkAllocateDescriptorSets(m_context.device, &allocInfo, table->m_descriptorSets.data());

        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate descriptor sets: {}"), ResultToString(res));
            vkDestroyDescriptorPool(m_context.device, pool, m_context.allocationCallbacks);
            DestroyArenaObject(*m_context.objectArena, table);
            return nullptr;
        }
    }

    table->m_descriptorPool = pool;

    return DescriptorTableHandle(table, DescriptorTableHandle::deleter_type(m_context.objectArena), AdoptRef);
}

void Device::resizeDescriptorTable(IDescriptorTable* m_descriptorTable, u32 newSize, bool keepContents){
    VkResult res = VK_SUCCESS;

    auto* table = checked_cast<DescriptorTable*>(m_descriptorTable);

    if(!table->m_layout || newSize == 0)
        return;

    if(table->m_descriptorPool != VK_NULL_HANDLE){
        vkDestroyDescriptorPool(m_context.device, table->m_descriptorPool, m_context.allocationCallbacks);
        table->m_descriptorPool = VK_NULL_HANDLE;
    }
    table->m_descriptorSets.clear();

    Alloc::ScratchArena<> scratchArena;

    Vector<VkDescriptorPoolSize, Alloc::ScratchAllocator<VkDescriptorPoolSize>> poolSizes{ Alloc::ScratchAllocator<VkDescriptorPoolSize>(scratchArena) };
    poolSizes.reserve(5);
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

    res = vkCreateDescriptorPool(m_context.device, &poolInfo, m_context.allocationCallbacks, &table->m_descriptorPool);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor pool for resize: {}"), ResultToString(res));
        return;
    }

    if(!table->m_layout->m_descriptorSetLayouts.empty()){
        Vector<VkDescriptorSetLayout, Alloc::ScratchAllocator<VkDescriptorSetLayout>> layouts(newSize, table->m_layout->m_descriptorSetLayouts[0], Alloc::ScratchAllocator<VkDescriptorSetLayout>(scratchArena));

        VkDescriptorSetAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = table->m_descriptorPool;
        allocInfo.descriptorSetCount = newSize;
        allocInfo.pSetLayouts = layouts.data();

        table->m_descriptorSets.resize(newSize);
        res = vkAllocateDescriptorSets(m_context.device, &allocInfo, table->m_descriptorSets.data());
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate descriptor sets during resize: {}"), ResultToString(res));
            table->m_descriptorSets.clear();
            return;
        }
    }

    (void)keepContents;
}

bool Device::writeDescriptorTable(IDescriptorTable* m_descriptorTable, const BindingSetItem& item){
    auto* table = checked_cast<DescriptorTable*>(m_descriptorTable);

    if(table->m_descriptorSets.empty())
        return false;

    VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = table->m_descriptorSets[0];
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
        bufferInfo.buffer = buffer->m_buffer;
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
        imageInfo.sampler = sampler->m_sampler;
        write.pImageInfo = &imageInfo;
        break;
    }
    case ResourceType::RayTracingAccelStruct:{
        auto* as = checked_cast<AccelStruct*>(item.resourceHandle);
        asInfo.accelerationStructureCount = 1;
        asInfo.pAccelerationStructures = &as->m_accelStruct;
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

    auto* bindingSet = NewArenaObject<BindingSet>(*m_context.objectArena, m_context);
    bindingSet->m_desc = desc;

    DescriptorTableHandle tableHandle = createDescriptorTable(_layout);
    if(!tableHandle){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor table for binding set"));
        DestroyArenaObject(*m_context.objectArena, bindingSet);
        return nullptr;
    }

    bindingSet->m_descriptorTable = checked_cast<DescriptorTable*>(tableHandle.get());

    bindingSet->m_descriptorSets = bindingSet->m_descriptorTable->m_descriptorSets;
    bindingSet->m_layout = layout;

    Alloc::ScratchArena<> scratchArena(4096);

    Vector<VkWriteDescriptorSet, Alloc::ScratchAllocator<VkWriteDescriptorSet>> writes{ Alloc::ScratchAllocator<VkWriteDescriptorSet>(scratchArena) };
    Vector<VkDescriptorBufferInfo, Alloc::ScratchAllocator<VkDescriptorBufferInfo>> bufferInfos{ Alloc::ScratchAllocator<VkDescriptorBufferInfo>(scratchArena) };
    Vector<VkDescriptorImageInfo, Alloc::ScratchAllocator<VkDescriptorImageInfo>> imageInfos{ Alloc::ScratchAllocator<VkDescriptorImageInfo>(scratchArena) };
    Vector<VkWriteDescriptorSetAccelerationStructureKHR, Alloc::ScratchAllocator<VkWriteDescriptorSetAccelerationStructureKHR>> asInfos{ Alloc::ScratchAllocator<VkWriteDescriptorSetAccelerationStructureKHR>(scratchArena) };

    writes.reserve(desc.bindings.size());
    bufferInfos.reserve(desc.bindings.size());
    imageInfos.reserve(desc.bindings.size());
    asInfos.reserve(desc.bindings.size());

    for(const auto& item : desc.bindings){
        if(!item.resourceHandle)
            continue;

        VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = bindingSet->m_descriptorSets[0];
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
            bufInfo.buffer = buffer->m_buffer;
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
            imgInfo.sampler = sampler->m_sampler;
            imageInfos.push_back(imgInfo);
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
            break;
        }
        case ResourceType::TypedBuffer_SRV:
        case ResourceType::TypedBuffer_UAV:{
            auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
            VkDescriptorBufferInfo bufInfo = {};
            bufInfo.buffer = buffer->m_buffer;
            bufInfo.offset = item.range.byteOffset;
            bufInfo.range = item.range.byteSize > 0 ? item.range.byteSize : VK_WHOLE_SIZE;
            bufferInfos.push_back(bufInfo);
            write.pBufferInfo = &bufferInfos.back();
            writes.push_back(write);
            break;
        }
        case ResourceType::RayTracingAccelStruct:{
            auto* as = checked_cast<AccelStruct*>(item.resourceHandle);
            VkWriteDescriptorSetAccelerationStructureKHR asWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
            asWrite.accelerationStructureCount = 1;
            asWrite.pAccelerationStructures = &as->m_accelStruct;
            asInfos.push_back(asWrite);
            write.pNext = &asInfos.back();
            writes.push_back(write);
            break;
        }
        default:
            break;
        }
    }

    if(!writes.empty())
        vkUpdateDescriptorSets(m_context.device, static_cast<u32>(writes.size()), writes.data(), 0, nullptr);

    return BindingSetHandle(bindingSet, BindingSetHandle::deleter_type(m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

