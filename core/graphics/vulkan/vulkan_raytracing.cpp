// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkDeviceAddress GetBufferDeviceAddress(IBuffer* _buffer, u64 offset){
    if(!_buffer)
        return 0;

    auto* buffer = checked_cast<Buffer*>(_buffer);
    return buffer->m_deviceAddress + offset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AccelStruct::AccelStruct(const VulkanContext& context)
    : RefCounter<IRayTracingAccelStruct>(*context.threadPool)
    , m_context(context)
{}
AccelStruct::~AccelStruct(){
    if(m_compactionQueryPool != VK_NULL_HANDLE){
        vkDestroyQueryPool(m_context.device, m_compactionQueryPool, m_context.allocationCallbacks);
        m_compactionQueryPool = VK_NULL_HANDLE;
    }

    if(m_accelStruct){
        vkDestroyAccelerationStructureKHR(m_context.device, m_accelStruct, m_context.allocationCallbacks);
        m_accelStruct = VK_NULL_HANDLE;
    }

    m_buffer.reset();
}

Object AccelStruct::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_AccelerationStructureKHR)
        return Object(m_accelStruct);
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


OpacityMicromap::OpacityMicromap(const VulkanContext& context)
    : RefCounter<IRayTracingOpacityMicromap>(*context.threadPool)
    , m_context(context)
{}
OpacityMicromap::~OpacityMicromap(){
    if(m_micromap != VK_NULL_HANDLE){
        vkDestroyMicromapEXT(m_context.device, m_micromap, m_context.allocationCallbacks);
        m_micromap = VK_NULL_HANDLE;
    }
    m_dataBuffer.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingPipeline::RayTracingPipeline(const VulkanContext& context, Device& device)
    : RefCounter<IRayTracingPipeline>(*context.threadPool)
    , m_context(context)
    , m_shaderGroupHandles(Alloc::CustomAllocator<u8>(*context.objectArena))
    , m_device(device)
{}
RayTracingPipeline::~RayTracingPipeline(){
    if(m_pipeline){
        vkDestroyPipeline(m_context.device, m_pipeline, m_context.allocationCallbacks);
        m_pipeline = VK_NULL_HANDLE;
    }

    if(m_ownsPipelineLayout && m_pipelineLayout != VK_NULL_HANDLE){
        vkDestroyPipelineLayout(m_context.device, m_pipelineLayout, m_context.allocationCallbacks);
        m_pipelineLayout = VK_NULL_HANDLE;
        m_ownsPipelineLayout = false;
    }
}

Object RayTracingPipeline::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Pipeline)
        return Object(m_pipeline);
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingAccelStructHandle Device::createAccelStruct(const RayTracingAccelStructDesc& desc){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.KHR_acceleration_structure)
        return nullptr;

    auto* as = NewArenaObject<AccelStruct>(*m_context.objectArena, m_context);
    as->m_desc = desc;

    VkAccelerationStructureTypeKHR asType = desc.isTopLevel ?  VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR :  VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    BufferDesc bufferDesc;
    bufferDesc.byteSize = desc.topLevelMaxInstances > 0 ? 
        desc.topLevelMaxInstances * sizeof(VkAccelerationStructureInstanceKHR) * s_DefaultTlasBufferSizeMultiplier : // Estimate for TLAS
        s_DefaultTopLevelASBufferSize;
    bufferDesc.isAccelStructStorage = true;
    bufferDesc.debugName = "AccelStructBuffer";

    as->m_buffer = createBuffer(bufferDesc);

    if(!as->m_buffer){
        DestroyArenaObject(*m_context.objectArena, as);
        return nullptr;
    }

    VkAccelerationStructureCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    createInfo.buffer = checked_cast<Buffer*>(as->m_buffer.get())->m_buffer;
    createInfo.offset = 0;
    createInfo.size = bufferDesc.byteSize;
    createInfo.type = asType;

    res = vkCreateAccelerationStructureKHR(m_context.device, &createInfo, m_context.allocationCallbacks, &as->m_accelStruct);
    if(res != VK_SUCCESS){
        DestroyArenaObject(*m_context.objectArena, as);
        return nullptr;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    addressInfo.accelerationStructure = as->m_accelStruct;
    as->m_deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_context.device, &addressInfo);

    return RayTracingAccelStructHandle(as, RayTracingAccelStructHandle::deleter_type(m_context.objectArena), AdoptRef);
}

RayTracingOpacityMicromapHandle Device::createOpacityMicromap(const RayTracingOpacityMicromapDesc& desc){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.EXT_opacity_micromap)
        return nullptr;

    VkBuildMicromapFlagBitsEXT buildFlags = static_cast<VkBuildMicromapFlagBitsEXT>(0);
    if(desc.flags & RayTracingOpacityMicromapBuildFlags::FastTrace)
        buildFlags = VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
    else if(desc.flags & RayTracingOpacityMicromapBuildFlags::FastBuild)
        buildFlags = VK_BUILD_MICROMAP_PREFER_FAST_BUILD_BIT_EXT;

    VkMicromapBuildInfoEXT buildInfo = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT };
    buildInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    buildInfo.flags = buildFlags;
    buildInfo.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    buildInfo.usageCountsCount = static_cast<u32>(desc.counts.size());
    buildInfo.pUsageCounts = reinterpret_cast<const VkMicromapUsageEXT*>(desc.counts.data());

    VkMicromapBuildSizesInfoEXT buildSize = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT };
    vkGetMicromapBuildSizesEXT(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &buildSize);

    auto* om = NewArenaObject<OpacityMicromap>(*m_context.objectArena, m_context);
    om->m_desc = desc;

    BufferDesc bufferDesc;
    bufferDesc.canHaveUAVs = true;
    bufferDesc.byteSize = buildSize.micromapSize;
    bufferDesc.initialState = ResourceStates::AccelStructBuildBlas;
    bufferDesc.keepInitialState = true;
    bufferDesc.isAccelStructStorage = true;
    bufferDesc.debugName = desc.debugName;
    om->m_dataBuffer = createBuffer(bufferDesc);

    if(!om->m_dataBuffer){
        DestroyArenaObject(*m_context.objectArena, om);
        return nullptr;
    }

    auto* buffer = checked_cast<Buffer*>(om->m_dataBuffer.get());

    VkMicromapCreateInfoEXT createInfo = { VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT };
    createInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    createInfo.buffer = buffer->m_buffer;
    createInfo.size = buildSize.micromapSize;
    createInfo.deviceAddress = buffer->m_deviceAddress;

    res = vkCreateMicromapEXT(m_context.device, &createInfo, m_context.allocationCallbacks, &om->m_micromap);
    if(res != VK_SUCCESS){
        DestroyArenaObject(*m_context.objectArena, om);
        return nullptr;
    }

    return RayTracingOpacityMicromapHandle(om, RayTracingOpacityMicromapHandle::deleter_type(m_context.objectArena), AdoptRef);
}

MemoryRequirements Device::getAccelStructMemoryRequirements(IRayTracingAccelStruct* _as){
    auto* as = checked_cast<AccelStruct*>(_as);

    MemoryRequirements requirements = {};

    if(as->m_buffer){
        requirements.size = as->m_buffer->getDescription().byteSize;
        requirements.alignment = s_AccelerationStructureAlignment; // AS alignment requirement
    }

    return requirements;
}

RayTracingClusterOperationSizeInfo Device::getClusterOperationSizeInfo(const RayTracingClusterOperationParams& params){
    if(!m_context.extensions.NV_cluster_acceleration_structure){
        return RayTracingClusterOperationSizeInfo{};
    }

    RayTracingClusterOperationSizeInfo info;

    VkClusterAccelerationStructureOpTypeNV opType;
    switch(params.type){
    case RayTracingClusterOperationType::Move:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_MOVE_OBJECTS_NV;
        break;
    case RayTracingClusterOperationType::ClasBuild:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV;
        break;
    case RayTracingClusterOperationType::ClasBuildTemplates:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV;
        break;
    case RayTracingClusterOperationType::ClasInstantiateTemplates:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV;
        break;
    case RayTracingClusterOperationType::BlasBuild:
        opType = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV;
        break;
    default:
        return info;
    }

    VkClusterAccelerationStructureOpModeNV opMode;
    switch(params.mode){
    case RayTracingClusterOperationMode::ImplicitDestinations:
        opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV;
        break;
    case RayTracingClusterOperationMode::ExplicitDestinations:
        opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV;
        break;
    case RayTracingClusterOperationMode::GetSizes:
        opMode = VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_COMPUTE_SIZES_NV;
        break;
    default:
        return info;
    }

    VkBuildAccelerationStructureFlagsKHR opFlags = 0;
    if(params.flags & RayTracingClusterOperationFlags::FastTrace)
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if(!(params.flags & RayTracingClusterOperationFlags::FastTrace) && (params.flags & RayTracingClusterOperationFlags::FastBuild))
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    if(params.flags & RayTracingClusterOperationFlags::AllowOMM)
        opFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_OPACITY_MICROMAP_UPDATE_EXT;

    VkClusterAccelerationStructureInputInfoNV inputInfo = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_INPUT_INFO_NV };
    inputInfo.maxAccelerationStructureCount = params.maxArgCount;
    inputInfo.flags = opFlags;
    inputInfo.opType = opType;
    inputInfo.opMode = opMode;

    VkClusterAccelerationStructureMoveObjectsInputNV moveInput = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_MOVE_OBJECTS_INPUT_NV };
    VkClusterAccelerationStructureTriangleClusterInputNV clusterInput = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_TRIANGLE_CLUSTER_INPUT_NV };
    VkClusterAccelerationStructureClustersBottomLevelInputNV blasInput = { VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_CLUSTERS_BOTTOM_LEVEL_INPUT_NV };

    switch(params.type){
    case RayTracingClusterOperationType::Move:{
        VkClusterAccelerationStructureTypeNV moveType;
        switch(params.move.type){
        case RayTracingClusterOperationMoveType::BottomLevel:  moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_CLUSTERS_BOTTOM_LEVEL_NV; break;
        case RayTracingClusterOperationMoveType::ClusterLevel: moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_NV; break;
        case RayTracingClusterOperationMoveType::Template:     moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_TRIANGLE_CLUSTER_TEMPLATE_NV; break;
        default: moveType = VK_CLUSTER_ACCELERATION_STRUCTURE_TYPE_CLUSTERS_BOTTOM_LEVEL_NV; break;
        }
        moveInput.type = moveType;
        moveInput.noMoveOverlap = (params.flags & RayTracingClusterOperationFlags::NoOverlap) ? VK_TRUE : VK_FALSE;
        moveInput.maxMovedBytes = params.move.maxBytes;
        inputInfo.opInput.pMoveObjects = &moveInput;
        break;
    }
    case RayTracingClusterOperationType::ClasBuild:
    case RayTracingClusterOperationType::ClasBuildTemplates:
    case RayTracingClusterOperationType::ClasInstantiateTemplates:{
        clusterInput.vertexFormat = __hidden_vulkan::ConvertFormat(params.clas.vertexFormat);
        clusterInput.maxGeometryIndexValue = params.clas.maxGeometryIndex;
        clusterInput.maxClusterUniqueGeometryCount = params.clas.maxUniqueGeometryCount;
        clusterInput.maxClusterTriangleCount = params.clas.maxTriangleCount;
        clusterInput.maxClusterVertexCount = params.clas.maxVertexCount;
        clusterInput.maxTotalTriangleCount = params.clas.maxTotalTriangleCount;
        clusterInput.maxTotalVertexCount = params.clas.maxTotalVertexCount;
        clusterInput.minPositionTruncateBitCount = params.clas.minPositionTruncateBitCount;
        inputInfo.opInput.pTriangleClusters = &clusterInput;
        break;
    }
    case RayTracingClusterOperationType::BlasBuild:{
        blasInput.maxClusterCountPerAccelerationStructure = params.blas.maxClasPerBlasCount;
        blasInput.maxTotalClusterCount = params.blas.maxTotalClasCount;
        inputInfo.opInput.pClustersBottomLevel = &blasInput;
        break;
    }
    default:
        break;
    }

    VkAccelerationStructureBuildSizesInfoKHR vkSizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetClusterAccelerationStructureBuildSizesNV(m_context.device, &inputInfo, &vkSizeInfo);

    info.resultMaxSizeInBytes = vkSizeInfo.accelerationStructureSize;
    info.scratchSizeInBytes = vkSizeInfo.buildScratchSize;

    return info;
}

bool Device::bindAccelStructMemory(IRayTracingAccelStruct* _as, IHeap* heap, u64 offset){
    if(!_as || !heap)
        return false;

    auto* as = checked_cast<AccelStruct*>(_as);

    if(as->m_buffer)
        return bindBufferMemory(as->m_buffer.get(), heap, offset);

    return false;
}

RayTracingPipelineHandle Device::createRayTracingPipeline(const RayTracingPipelineDesc& desc){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.KHR_ray_tracing_pipeline)
        return nullptr;

    auto* pso = NewArenaObject<RayTracingPipeline>(*m_context.objectArena, m_context, *this);
    pso->m_desc = desc;

    Alloc::ScratchArena<> scratchArena(s_RayTracingScratchArenaBytes);

    Vector<VkPipelineShaderStageCreateInfo, Alloc::ScratchAllocator<VkPipelineShaderStageCreateInfo>> stages{ Alloc::ScratchAllocator<VkPipelineShaderStageCreateInfo>(scratchArena) };
    Vector<VkRayTracingShaderGroupCreateInfoKHR, Alloc::ScratchAllocator<VkRayTracingShaderGroupCreateInfoKHR>> groups{ Alloc::ScratchAllocator<VkRayTracingShaderGroupCreateInfoKHR>(scratchArena) };

    stages.reserve(desc.shaders.size() + desc.hitGroups.size() * s_RayTracingHitGroupShaderStageCount);
    groups.reserve(desc.shaders.size() + desc.hitGroups.size());

    for(const auto& shaderDesc : desc.shaders){
        if(!shaderDesc.shader)
            continue;

        auto* s = checked_cast<Shader*>(shaderDesc.shader.get());

        VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageInfo.module = s->m_shaderModule;
        stageInfo.pName = s->m_desc.entryName.c_str();

        switch(s->m_desc.shaderType){
        case ShaderType::RayGeneration:
            stageInfo.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            break;
        case ShaderType::Miss:
            stageInfo.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
            break;
        case ShaderType::Callable:
            stageInfo.stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;
            break;
        default:
            continue;
        }

        VkRayTracingShaderGroupCreateInfoKHR group = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
        group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        group.generalShader = static_cast<u32>(stages.size());
        group.closestHitShader = VK_SHADER_UNUSED_KHR;
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;

        stages.push_back(stageInfo);
        groups.push_back(group);
    }

    for(const auto& hitGroup : desc.hitGroups){
        VkRayTracingShaderGroupCreateInfoKHR group = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
        group.type = hitGroup.isProceduralPrimitive ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR : VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        group.generalShader = VK_SHADER_UNUSED_KHR;
        group.closestHitShader = VK_SHADER_UNUSED_KHR;
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;

        if(hitGroup.closestHitShader){
            auto* s = checked_cast<Shader*>(hitGroup.closestHitShader.get());
            VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageInfo.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            stageInfo.module = s->m_shaderModule;
            stageInfo.pName = s->m_desc.entryName.c_str();
            group.closestHitShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        if(hitGroup.anyHitShader){
            auto* s = checked_cast<Shader*>(hitGroup.anyHitShader.get());
            VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageInfo.stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            stageInfo.module = s->m_shaderModule;
            stageInfo.pName = s->m_desc.entryName.c_str();
            group.anyHitShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        if(hitGroup.intersectionShader){
            auto* s = checked_cast<Shader*>(hitGroup.intersectionShader.get());
            VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageInfo.stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
            stageInfo.module = s->m_shaderModule;
            stageInfo.pName = s->m_desc.entryName.c_str();
            group.intersectionShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        groups.push_back(group);
    }

    if(stages.empty() || groups.empty()){
        DestroyArenaObject(*m_context.objectArena, pso);
        return nullptr;
    }

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if(desc.globalBindingLayouts.size() == 1){
        auto* layout = checked_cast<BindingLayout*>(desc.globalBindingLayouts[0].get());
        if(layout)
            pipelineLayout = layout->m_pipelineLayout;
    }
    else if(desc.globalBindingLayouts.size() > 1){
        Vector<VkDescriptorSetLayout, Alloc::ScratchAllocator<VkDescriptorSetLayout>> allDescriptorSetLayouts{ Alloc::ScratchAllocator<VkDescriptorSetLayout>(scratchArena) };
        u32 pushConstantByteSize = 0;
        for(u32 i = 0; i < static_cast<u32>(desc.globalBindingLayouts.size()); ++i){
            auto* bl = checked_cast<BindingLayout*>(desc.globalBindingLayouts[i].get());
            if(!bl)
                continue;
            for(const auto& item : bl->m_desc.bindings){
                if(item.type == ResourceType::PushConstants)
                    pushConstantByteSize = Max<u32>(pushConstantByteSize, item.size);
            }
            for(auto& dsl : bl->m_descriptorSetLayouts)
                allDescriptorSetLayouts.push_back(dsl);
        }

        if(!allDescriptorSetLayouts.empty()){
            VkPushConstantRange pushConstantRange = {};
            if(pushConstantByteSize > 0){
                pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
                pushConstantRange.offset = 0;
                pushConstantRange.size = pushConstantByteSize;
            }

            VkPipelineLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            layoutInfo.setLayoutCount = static_cast<u32>(allDescriptorSetLayouts.size());
            layoutInfo.pSetLayouts = allDescriptorSetLayouts.data();
            layoutInfo.pushConstantRangeCount = pushConstantByteSize > 0 ? 1u : 0u;
            layoutInfo.pPushConstantRanges = pushConstantByteSize > 0 ? &pushConstantRange : nullptr;
            res = vkCreatePipelineLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &pipelineLayout);
            if(res != VK_SUCCESS){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create pipeline layout for ray tracing pipeline: {}"), ResultToString(res));
                DestroyArenaObject(*m_context.objectArena, pso);
                return nullptr;
            }
            pso->m_ownsPipelineLayout = true;
        }
    }
    pso->m_pipelineLayout = pipelineLayout;

    VkRayTracingPipelineCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
    createInfo.stageCount = static_cast<u32>(stages.size());
    createInfo.pStages = stages.data();
    createInfo.groupCount = static_cast<u32>(groups.size());
    createInfo.pGroups = groups.data();
    createInfo.maxPipelineRayRecursionDepth = desc.maxRecursionDepth;
    createInfo.layout = pipelineLayout;

    res = vkCreateRayTracingPipelinesKHR(m_context.device, VK_NULL_HANDLE, m_context.pipelineCache, 1, &createInfo, m_context.allocationCallbacks, &pso->m_pipeline);
    if(res != VK_SUCCESS){
        DestroyArenaObject(*m_context.objectArena, pso);
        return nullptr;
    }

    u32 handleSize = m_context.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_context.rayTracingPipelineProperties.shaderGroupHandleAlignment;

    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);
    u32 groupCount = static_cast<u32>(groups.size());

    pso->m_shaderGroupHandles.resize(groupCount * handleSizeAligned);
    res = vkGetRayTracingShaderGroupHandlesKHR(
        m_context.device,
        pso->m_pipeline,
        0,
        groupCount,
        pso->m_shaderGroupHandles.size(),
        pso->m_shaderGroupHandles.data()
    );
    if(res != VK_SUCCESS){
        DestroyArenaObject(*m_context.objectArena, pso);
        return nullptr;
    }

    return RayTracingPipelineHandle(pso, RayTracingPipelineHandle::deleter_type(m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderTable::ShaderTable(const VulkanContext& context, Device& device)
    : RefCounter<IRayTracingShaderTable>(*context.threadPool)
    , m_context(context)
    , m_device(device)
{}
ShaderTable::~ShaderTable() = default;

RayTracingShaderTableHandle RayTracingPipeline::createShaderTable(){
    auto* sbt = NewArenaObject<ShaderTable>(*m_context.objectArena, m_context, m_device);
    sbt->m_pipeline = this;
    return RayTracingShaderTableHandle(sbt, RayTracingShaderTableHandle::deleter_type(m_context.objectArena), AdoptRef);
}

u32 ShaderTable::findGroupIndex(const Name& exportName)const{
    if(!m_pipeline)
        return UINT32_MAX;

    u32 groupIndex = 0;
    for(const auto& shaderDesc : m_pipeline->m_desc.shaders){
        if(shaderDesc.shader && shaderDesc.shader->getDescription().entryName == exportName)
            return groupIndex;
        groupIndex++;
    }
    for(const auto& hitGroup : m_pipeline->m_desc.hitGroups){
        if(hitGroup.exportName == exportName)
            return groupIndex;
        groupIndex++;
    }
    return UINT32_MAX;
}

void ShaderTable::allocateSBTBuffer(BufferHandle& outBuffer, u64 sbtSize){
    BufferDesc bufferDesc;
    bufferDesc.byteSize = sbtSize;
    bufferDesc.debugName = "SBT_Buffer";
    bufferDesc.isShaderBindingTable = true;
    bufferDesc.cpuAccess = CpuAccessMode::Write;

    outBuffer = m_device.createBuffer(bufferDesc);
}

void ShaderTable::setRayGenerationShader(const Name& exportName, IBindingSet* /*bindings*/){
    if(!m_pipeline)
        return;

    u32 handleSize = m_context.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_context.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    u32 baseAlignment = m_context.rayTracingPipelineProperties.shaderGroupBaseAlignment;
    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);
    u64 sbtSize = (handleSizeAligned + baseAlignment - 1) & ~(static_cast<u64>(baseAlignment) - 1);

    allocateSBTBuffer(m_raygenBuffer, sbtSize);
    if(!m_raygenBuffer)
        return;

    m_raygenOffset = 0;

    u32 groupIndex = findGroupIndex(exportName);
    if(groupIndex == UINT32_MAX){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Ray generation export not found in pipeline"));
        return;
    }

    void* mapped = m_device.mapBuffer(m_raygenBuffer.get(), CpuAccessMode::Write);
    if(!mapped){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map ray generation SBT buffer"));
        return;
    }

    NWB_MEMCPY(mapped, handleSizeAligned, m_pipeline->m_shaderGroupHandles.data() + groupIndex * handleSizeAligned, handleSize);
    m_device.unmapBuffer(m_raygenBuffer.get());
}

u32 ShaderTable::addMissShader(const Name& exportName, IBindingSet* /*bindings*/){
    if(!m_pipeline)
        return m_missCount++;

    u32 handleSize = m_context.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_context.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    u32 baseAlignment = m_context.rayTracingPipelineProperties.shaderGroupBaseAlignment;
    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

    u32 newCount = m_missCount + 1;
    u64 sbtSize = (static_cast<u64>(newCount) * handleSizeAligned + baseAlignment - 1) & ~(static_cast<u64>(baseAlignment) - 1);

    BufferHandle newBuffer;
    allocateSBTBuffer(newBuffer, sbtSize);
    if(!newBuffer){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to allocate miss SBT buffer"));
        return m_missCount;
    }

    void* mapped = m_device.mapBuffer(newBuffer.get(), CpuAccessMode::Write);
    if(!mapped){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map miss SBT buffer"));
        return m_missCount;
    }

    if(m_missBuffer && m_missCount > 0){
        void* oldMapped = m_device.mapBuffer(m_missBuffer.get(), CpuAccessMode::Read);
        if(!oldMapped){
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map previous miss SBT buffer"));
            m_device.unmapBuffer(newBuffer.get());
            return m_missCount;
        }
        const usize copySize = static_cast<usize>(m_missCount) * handleSizeAligned;
        __hidden_vulkan::CopyHostMemory(taskPool(), mapped, oldMapped, copySize);
        m_device.unmapBuffer(m_missBuffer.get());
    }

    u32 groupIndex = findGroupIndex(exportName);
    if(groupIndex == UINT32_MAX){
        m_device.unmapBuffer(newBuffer.get());
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Miss shader export not found in pipeline"));
        return m_missCount;
    }

    auto* dst = static_cast<u8*>(mapped) + m_missCount * handleSizeAligned;
    NWB_MEMCPY(dst, handleSizeAligned, m_pipeline->m_shaderGroupHandles.data() + groupIndex * handleSizeAligned, handleSize);
    m_device.unmapBuffer(newBuffer.get());

    m_missBuffer = newBuffer;
    m_missOffset = 0;
    return m_missCount++;
}

u32 ShaderTable::addHitGroup(const Name& exportName, IBindingSet* /*bindings*/){
    if(!m_pipeline)
        return m_hitCount++;

    u32 handleSize = m_context.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_context.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    u32 baseAlignment = m_context.rayTracingPipelineProperties.shaderGroupBaseAlignment;
    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

    u32 newCount = m_hitCount + 1;
    u64 sbtSize = (static_cast<u64>(newCount) * handleSizeAligned + baseAlignment - 1) & ~(static_cast<u64>(baseAlignment) - 1);

    BufferHandle newBuffer;
    allocateSBTBuffer(newBuffer, sbtSize);
    if(!newBuffer){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to allocate hit SBT buffer"));
        return m_hitCount;
    }

    void* mapped = m_device.mapBuffer(newBuffer.get(), CpuAccessMode::Write);
    if(!mapped){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map hit SBT buffer"));
        return m_hitCount;
    }

    if(m_hitBuffer && m_hitCount > 0){
        void* oldMapped = m_device.mapBuffer(m_hitBuffer.get(), CpuAccessMode::Read);
        if(!oldMapped){
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map previous hit SBT buffer"));
            m_device.unmapBuffer(newBuffer.get());
            return m_hitCount;
        }
        const usize copySize = static_cast<usize>(m_hitCount) * handleSizeAligned;
        __hidden_vulkan::CopyHostMemory(taskPool(), mapped, oldMapped, copySize);
        m_device.unmapBuffer(m_hitBuffer.get());
    }

    u32 groupIndex = findGroupIndex(exportName);
    if(groupIndex == UINT32_MAX){
        m_device.unmapBuffer(newBuffer.get());
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Hit group export not found in pipeline"));
        return m_hitCount;
    }

    auto* dst = static_cast<u8*>(mapped) + m_hitCount * handleSizeAligned;
    NWB_MEMCPY(dst, handleSizeAligned, m_pipeline->m_shaderGroupHandles.data() + groupIndex * handleSizeAligned, handleSize);
    m_device.unmapBuffer(newBuffer.get());

    m_hitBuffer = newBuffer;
    m_hitOffset = 0;
    return m_hitCount++;
}

u32 ShaderTable::addCallableShader(const Name& exportName, IBindingSet* /*bindings*/){
    if(!m_pipeline)
        return m_callableCount++;

    u32 handleSize = m_context.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_context.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    u32 baseAlignment = m_context.rayTracingPipelineProperties.shaderGroupBaseAlignment;
    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

    u32 newCount = m_callableCount + 1;
    u64 sbtSize = (static_cast<u64>(newCount) * handleSizeAligned + baseAlignment - 1) & ~(static_cast<u64>(baseAlignment) - 1);

    BufferHandle newBuffer;
    allocateSBTBuffer(newBuffer, sbtSize);
    if(!newBuffer){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to allocate callable SBT buffer"));
        return m_callableCount;
    }

    void* mapped = m_device.mapBuffer(newBuffer.get(), CpuAccessMode::Write);
    if(!mapped){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map callable SBT buffer"));
        return m_callableCount;
    }

    if(m_callableBuffer && m_callableCount > 0){
        void* oldMapped = m_device.mapBuffer(m_callableBuffer.get(), CpuAccessMode::Read);
        if(!oldMapped){
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map previous callable SBT buffer"));
            m_device.unmapBuffer(newBuffer.get());
            return m_callableCount;
        }
        const usize copySize = static_cast<usize>(m_callableCount) * handleSizeAligned;
        __hidden_vulkan::CopyHostMemory(taskPool(), mapped, oldMapped, copySize);
        m_device.unmapBuffer(m_callableBuffer.get());
    }

    u32 groupIndex = findGroupIndex(exportName);
    if(groupIndex == UINT32_MAX){
        m_device.unmapBuffer(newBuffer.get());
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Callable shader export not found in pipeline"));
        return m_callableCount;
    }

    auto* dst = static_cast<u8*>(mapped) + m_callableCount * handleSizeAligned;
    NWB_MEMCPY(dst, handleSizeAligned, m_pipeline->m_shaderGroupHandles.data() + groupIndex * handleSizeAligned, handleSize);
    m_device.unmapBuffer(newBuffer.get());

    m_callableBuffer = newBuffer;
    m_callableOffset = 0;
    return m_callableCount++;
}

void ShaderTable::clearMissShaders(){ m_missCount = 0; m_missBuffer = nullptr; }
void ShaderTable::clearHitShaders(){ m_hitCount = 0; m_hitBuffer = nullptr; }
void ShaderTable::clearCallableShaders(){ m_callableCount = 0; m_callableBuffer = nullptr; }

Object ShaderTable::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Buffer && m_raygenBuffer){
        auto* buf = checked_cast<Buffer*>(m_raygenBuffer.get());
        return Object(buf->m_buffer);
    }
    return Object{nullptr};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setRayTracingState(const RayTracingState& state){
    m_currentRayTracingState = state;

    if(!state.shaderTable)
        return;

    auto* sbt = checked_cast<ShaderTable*>(state.shaderTable);
    RayTracingPipeline* pipeline = sbt->m_pipeline;

    if(!pipeline)
        return;

    vkCmdBindPipeline(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->m_pipeline);

    if(state.bindings.size() > 0 && pipeline->m_pipelineLayout != VK_NULL_HANDLE){
        for(usize i = 0; i < state.bindings.size(); ++i){
            if(state.bindings[i]){
                auto* bindingSet = checked_cast<BindingSet*>(state.bindings[i]);
                if(!bindingSet->m_descriptorSets.empty())
                    vkCmdBindDescriptorSets(m_currentCmdBuf->m_cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                        pipeline->m_pipelineLayout, static_cast<u32>(i),
                        static_cast<u32>(bindingSet->m_descriptorSets.size()),
                        bindingSet->m_descriptorSets.data(), 0, nullptr);
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::buildBottomLevelAccelStruct(IRayTracingAccelStruct* _as, const RayTracingGeometryDesc* pGeometries, usize numGeometries, RayTracingAccelStructBuildFlags::Mask buildFlags){
    if(!_as || !pGeometries || numGeometries == 0)
        return;

    if(!m_context.extensions.KHR_acceleration_structure)
        return;

    auto* as = checked_cast<AccelStruct*>(_as);

    Alloc::ScratchArena<> scratchArena(s_RayTracingScratchArenaBytes);

    Vector<VkAccelerationStructureGeometryKHR, Alloc::ScratchAllocator<VkAccelerationStructureGeometryKHR>> geometries{ Alloc::ScratchAllocator<VkAccelerationStructureGeometryKHR>(scratchArena) };
    Vector<VkAccelerationStructureBuildRangeInfoKHR, Alloc::ScratchAllocator<VkAccelerationStructureBuildRangeInfoKHR>> rangeInfos{ Alloc::ScratchAllocator<VkAccelerationStructureBuildRangeInfoKHR>(scratchArena) };
    Vector<uint32_t, Alloc::ScratchAllocator<uint32_t>> primitiveCounts{ Alloc::ScratchAllocator<uint32_t>(scratchArena) };

    geometries.resize(numGeometries);
    rangeInfos.resize(numGeometries);
    primitiveCounts.resize(numGeometries);

    auto buildGeometry = [&](usize i){
        const auto& geomDesc = pGeometries[i];

        VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
        u32 primitiveCount = 0;

        if(geomDesc.geometryType == RayTracingGeometryType::Triangles){
            const auto& triangles = geomDesc.geometryData.triangles;

            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexFormat = ConvertFormat(triangles.vertexFormat);
            geometry.geometry.triangles.vertexData.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(triangles.vertexBuffer, triangles.vertexOffset);
            geometry.geometry.triangles.vertexStride = triangles.vertexStride;
            geometry.geometry.triangles.maxVertex = triangles.vertexCount > 0 ? triangles.vertexCount - 1 : 0;

            if(triangles.indexBuffer){
                geometry.geometry.triangles.indexType = triangles.indexFormat == Format::R16_UINT ? 
                    VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
                geometry.geometry.triangles.indexData.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(triangles.indexBuffer, triangles.indexOffset);
                primitiveCount = triangles.indexCount / s_TrianglesPerPrimitive;
            }
            else{
                geometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
                primitiveCount = triangles.vertexCount / s_TrianglesPerPrimitive;
            }

            rangeInfo.primitiveCount = primitiveCount;
        }
        else if(geomDesc.geometryType == RayTracingGeometryType::AABBs){
            const auto& aabbs = geomDesc.geometryData.aabbs;

            geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
            geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
            geometry.geometry.aabbs.data.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(aabbs.buffer, aabbs.offset);
            geometry.geometry.aabbs.stride = aabbs.stride;

            primitiveCount = aabbs.count;
            rangeInfo.primitiveCount = primitiveCount;
        }

        geometry.flags = 0;
        if(geomDesc.flags & RayTracingGeometryFlags::Opaque)
            geometry.flags |= VK_GEOMETRY_OPAQUE_BIT_KHR;
        if(geomDesc.flags & RayTracingGeometryFlags::NoDuplicateAnyHitInvocation)
            geometry.flags |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;

        geometries[i] = geometry;
        rangeInfos[i] = rangeInfo;
        primitiveCounts[i] = static_cast<uint32_t>(primitiveCount);
    };

    if(taskPool().isParallelEnabled() && numGeometries >= s_ParallelGeometryThreshold)
        scheduleParallelFor(static_cast<usize>(0), numGeometries, s_GeometryGrainSize, buildGeometry);
    else{
        for(usize i = 0; i < numGeometries; ++i)
            buildGeometry(i);
    }

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = 0;

    if(buildFlags & RayTracingAccelStructBuildFlags::AllowUpdate)
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    if(buildFlags & RayTracingAccelStructBuildFlags::AllowCompaction)
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
    if(buildFlags & RayTracingAccelStructBuildFlags::PreferFastTrace)
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if(buildFlags & RayTracingAccelStructBuildFlags::PreferFastBuild)
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;

    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = as->m_accelStruct;
    buildInfo.geometryCount = static_cast<u32>(geometries.size());
    buildInfo.pGeometries = geometries.data();

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, primitiveCounts.data(), &sizeInfo);

    BufferDesc scratchDesc;
    scratchDesc.byteSize = sizeInfo.buildScratchSize;
    scratchDesc.structStride = 1;
    scratchDesc.debugName = "AS_BuildScratch";

    BufferHandle scratchBuffer;
    if(sizeInfo.buildScratchSize > 0){
        scratchBuffer = m_device.createBuffer(scratchDesc);
        if(!scratchBuffer){
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to allocate BLAS scratch buffer"));
            return;
        }
        buildInfo.scratchData.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(scratchBuffer.get());
        m_currentCmdBuf->m_referencedStagingBuffers.push_back(scratchBuffer);
    }

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = rangeInfos.data();
    vkCmdBuildAccelerationStructuresKHR(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo, &pRangeInfos);

    if(buildFlags & RayTracingAccelStructBuildFlags::AllowCompaction)
        m_pendingCompactions.push_back(as);

    m_currentCmdBuf->m_referencedResources.push_back(_as);
}

void CommandList::compactBottomLevelAccelStructs(){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.KHR_acceleration_structure)
        return;

    if(m_pendingCompactions.empty())
        return;

    for(auto& as : m_pendingCompactions){
        if(as->m_compactionQueryPool == VK_NULL_HANDLE || as->m_compacted)
            continue;

        u64 compactedSize = 0;
        res = vkGetQueryPoolResults(
            m_context.device,
            as->m_compactionQueryPool,
            as->m_compactionQueryIndex,
            1,
            sizeof(u64),
            &compactedSize,
            sizeof(u64),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
        );

        if(res != VK_SUCCESS || compactedSize == 0)
            continue;

        BufferDesc compactBufferDesc;
        compactBufferDesc.byteSize = compactedSize;
        compactBufferDesc.isAccelStructStorage = true;
        compactBufferDesc.debugName = as->m_desc.debugName;

        BufferHandle compactBuffer = m_device.createBuffer(compactBufferDesc);
        if(!compactBuffer)
            continue;

        VkAccelerationStructureCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        createInfo.buffer = checked_cast<Buffer*>(compactBuffer.get())->m_buffer;
        createInfo.size = compactedSize;
        createInfo.type = as->m_desc.isTopLevel
            ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
            : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
            ;

        VkAccelerationStructureKHR newAS = VK_NULL_HANDLE;
        res = vkCreateAccelerationStructureKHR(m_context.device, &createInfo, m_context.allocationCallbacks, &newAS);
        if(res != VK_SUCCESS)
            continue;

        VkCopyAccelerationStructureInfoKHR copyInfo = { VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };
        copyInfo.src  = as->m_accelStruct; // original (still the copy source)
        copyInfo.dst  = newAS;
        copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
        vkCmdCopyAccelerationStructureKHR(m_currentCmdBuf->m_cmdBuf, &copyInfo);

        m_currentCmdBuf->m_referencedAccelStructHandles.push_back(as->m_accelStruct);
        m_currentCmdBuf->m_referencedStagingBuffers.push_back(as->m_buffer);

        as->m_accelStruct = newAS;
        as->m_buffer = Move(compactBuffer);

        VkAccelerationStructureDeviceAddressInfoKHR addrInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
        addrInfo.accelerationStructure = as->m_accelStruct;
        as->m_deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_context.device, &addrInfo);

        vkDestroyQueryPool(m_context.device, as->m_compactionQueryPool, m_context.allocationCallbacks);
        as->m_compactionQueryPool  = VK_NULL_HANDLE;
        as->m_compactionQueryIndex = 0;
        as->m_compacted = true;
    }

    for(auto& as : m_pendingCompactions){
        if(as->m_compactionQueryPool != VK_NULL_HANDLE || as->m_compacted)
            continue;

        VkQueryPoolCreateInfo queryPoolInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        queryPoolInfo.queryType  = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        queryPoolInfo.queryCount = s_SingleQueryCount;

        VkQueryPool queryPool = VK_NULL_HANDLE;
        if(vkCreateQueryPool(m_context.device, &queryPoolInfo, m_context.allocationCallbacks, &queryPool) != VK_SUCCESS)
            continue;

        vkCmdResetQueryPool(m_currentCmdBuf->m_cmdBuf, queryPool, s_TimerQueryBeginIndex, s_SingleQueryCount);

        vkCmdWriteAccelerationStructuresPropertiesKHR(
            m_currentCmdBuf->m_cmdBuf,
            s_SingleQueryCount,
            &as->m_accelStruct,
            VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
            queryPool,
            s_TimerQueryBeginIndex
        );

        as->m_compactionQueryPool  = queryPool;
        as->m_compactionQueryIndex = 0;
    }

    {
        usize dst = 0;
        for(usize i = 0; i < m_pendingCompactions.size(); ++i)
            if(!m_pendingCompactions[i]->m_compacted)
                m_pendingCompactions[dst++] = Move(m_pendingCompactions[i]);
        m_pendingCompactions.resize(dst);
    }
}

void CommandList::buildTopLevelAccelStruct(IRayTracingAccelStruct* _as, const RayTracingInstanceDesc* pInstances, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags){
    if(!_as || !pInstances || numInstances == 0)
        return;

    if(!m_context.extensions.KHR_acceleration_structure)
        return;

    auto* as = checked_cast<AccelStruct*>(_as);

    u64 instanceBufferSize = numInstances * sizeof(VkAccelerationStructureInstanceKHR);
    BufferDesc instanceBufferDesc;
    instanceBufferDesc.byteSize = instanceBufferSize;
    instanceBufferDesc.cpuAccess = CpuAccessMode::Write;
    instanceBufferDesc.isAccelStructBuildInput = true;
    instanceBufferDesc.debugName = "TLAS_InstanceBuffer";

    BufferHandle instanceBuffer = m_device.createBuffer(instanceBufferDesc);
    if(!instanceBuffer)
        return;

    auto* mappedInstances = static_cast<VkAccelerationStructureInstanceKHR*>(m_device.mapBuffer(instanceBuffer.get(), CpuAccessMode::Write));
    if(!mappedInstances){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map TLAS instance buffer"));
        return;
    }

    auto buildVkInstance = [&](usize i){
        const auto& inst = pInstances[i];
        VkAccelerationStructureInstanceKHR& vkInst = mappedInstances[i];

        NWB_MEMCPY(&vkInst.transform, sizeof(VkTransformMatrixKHR), &inst.transform, sizeof(VkTransformMatrixKHR));

        vkInst.instanceCustomIndex = inst.instanceID & s_InstanceFieldMask24Bit;
        vkInst.mask = inst.instanceMask;
        vkInst.instanceShaderBindingTableRecordOffset = inst.instanceContributionToHitGroupIndex & s_InstanceFieldMask24Bit;
        vkInst.flags = 0;

        if(inst.flags & RayTracingInstanceFlags::TriangleCullDisable)
            vkInst.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        if(inst.flags & RayTracingInstanceFlags::TriangleFrontCounterclockwise)
            vkInst.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR;
        if(inst.flags & RayTracingInstanceFlags::ForceOpaque)
            vkInst.flags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
        if(inst.flags & RayTracingInstanceFlags::ForceNonOpaque)
            vkInst.flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;

        auto* blas = checked_cast<AccelStruct*>(inst.bottomLevelAS);
        vkInst.accelerationStructureReference = blas ? blas->m_deviceAddress : 0;
    };

    // TLAS instance conversion is CPU-only and scales with scene instance count.
    if(taskPool().isParallelEnabled() && numInstances >= s_ParallelTlasInstanceThreshold)
        scheduleParallelFor(static_cast<usize>(0), numInstances, s_TlasInstanceGrainSize, buildVkInstance);
    else{
        for(usize i = 0; i < numInstances; ++i)
            buildVkInstance(i);
    }

    m_device.unmapBuffer(instanceBuffer.get());

    VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(instanceBuffer.get());

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = 0;

    if(buildFlags & RayTracingAccelStructBuildFlags::AllowUpdate)
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    if(buildFlags & RayTracingAccelStructBuildFlags::PreferFastTrace)
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if(buildFlags & RayTracingAccelStructBuildFlags::PreferFastBuild)
        buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;

    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = as->m_accelStruct;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    auto primitiveCount = static_cast<uint32_t>(numInstances);
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

    BufferDesc scratchDesc;
    scratchDesc.byteSize = sizeInfo.buildScratchSize;
    scratchDesc.structStride = 1;
    scratchDesc.debugName = "TLAS_BuildScratch";

    BufferHandle scratchBuffer;
    if(sizeInfo.buildScratchSize > 0){
        scratchBuffer = m_device.createBuffer(scratchDesc);
        if(!scratchBuffer){
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to allocate TLAS scratch buffer"));
            return;
        }
        buildInfo.scratchData.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(scratchBuffer.get());
        m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(scratchBuffer));
    }

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
    rangeInfo.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
    vkCmdBuildAccelerationStructuresKHR(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo, &pRangeInfo);

    m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(instanceBuffer));
    m_currentCmdBuf->m_referencedResources.push_back(_as);
}

void CommandList::buildOpacityMicromap(IRayTracingOpacityMicromap* _omm, const RayTracingOpacityMicromapDesc& ommDesc){
    if(!m_context.extensions.EXT_opacity_micromap)
        return;

    auto* omm = checked_cast<OpacityMicromap*>(_omm);

    if(m_enableAutomaticBarriers){
        if(ommDesc.inputBuffer)
            setBufferState(ommDesc.inputBuffer, ResourceStates::OpacityMicromapBuildInput);
        if(ommDesc.perOmmDescs)
            setBufferState(ommDesc.perOmmDescs, ResourceStates::OpacityMicromapBuildInput);
        if(omm->m_dataBuffer)
            setBufferState(omm->m_dataBuffer.get(), ResourceStates::OpacityMicromapWrite);
    }

    if(ommDesc.trackLiveness){
        if(ommDesc.inputBuffer)
            m_currentCmdBuf->m_referencedResources.push_back(ommDesc.inputBuffer);
        if(ommDesc.perOmmDescs)
            m_currentCmdBuf->m_referencedResources.push_back(ommDesc.perOmmDescs);
        if(omm->m_dataBuffer)
            m_currentCmdBuf->m_referencedResources.push_back(omm->m_dataBuffer.get());
    }

    commitBarriers();

    VkBuildMicromapFlagBitsEXT buildFlags = static_cast<VkBuildMicromapFlagBitsEXT>(0);
    if(ommDesc.flags & RayTracingOpacityMicromapBuildFlags::FastTrace)
        buildFlags = VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
    else if(ommDesc.flags & RayTracingOpacityMicromapBuildFlags::FastBuild)
        buildFlags = VK_BUILD_MICROMAP_PREFER_FAST_BUILD_BIT_EXT;

    VkMicromapBuildInfoEXT buildInfo = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT };
    buildInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    buildInfo.flags = buildFlags;
    buildInfo.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    buildInfo.dstMicromap = omm->m_micromap;
    buildInfo.usageCountsCount = static_cast<u32>(ommDesc.counts.size());
    buildInfo.pUsageCounts = reinterpret_cast<const VkMicromapUsageEXT*>(ommDesc.counts.data());
    buildInfo.data.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(ommDesc.inputBuffer, ommDesc.inputBufferOffset);
    buildInfo.triangleArray.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(ommDesc.perOmmDescs, ommDesc.perOmmDescsOffset);
    buildInfo.triangleArrayStride = sizeof(VkMicromapTriangleEXT);

    VkMicromapBuildSizesInfoEXT buildSize = { VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT };
    vkGetMicromapBuildSizesEXT(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &buildSize);

    if(buildSize.buildScratchSize != 0){
        BufferDesc scratchDesc;
        scratchDesc.byteSize = buildSize.buildScratchSize;
        scratchDesc.structStride = 1;
        scratchDesc.debugName = "OMM_BuildScratch";
        scratchDesc.canHaveUAVs = true;

        BufferHandle scratchBuffer = m_device.createBuffer(scratchDesc);
        if(!scratchBuffer)
            return;

        buildInfo.scratchData.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(scratchBuffer.get());

        vkCmdBuildMicromapsEXT(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo);

        m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(scratchBuffer));
    }
    else{
        vkCmdBuildMicromapsEXT(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo);
    }
}

void CommandList::dispatchRays(const RayTracingDispatchRaysArguments& args){
    if(!m_context.extensions.KHR_ray_tracing_pipeline)
        return;

    RayTracingState& state = m_currentRayTracingState;

    if(!state.shaderTable)
        return;

    auto* sbt = checked_cast<ShaderTable*>(state.shaderTable);

    VkStridedDeviceAddressRegionKHR raygenRegion = {};
    VkStridedDeviceAddressRegionKHR missRegion = {};
    VkStridedDeviceAddressRegionKHR hitRegion = {};
    VkStridedDeviceAddressRegionKHR callableRegion = {};

    u32 handleSize = m_context.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_context.rayTracingPipelineProperties.shaderGroupHandleAlignment;

    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

    if(sbt->m_raygenBuffer){
        raygenRegion.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(sbt->m_raygenBuffer.get(), sbt->m_raygenOffset);
        raygenRegion.stride = handleSizeAligned;
        raygenRegion.size = handleSizeAligned;
    }

    if(sbt->m_missBuffer){
        missRegion.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(sbt->m_missBuffer.get(), sbt->m_missOffset);
        missRegion.stride = handleSizeAligned;
        missRegion.size = sbt->m_missCount * handleSizeAligned;
    }

    if(sbt->m_hitBuffer){
        hitRegion.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(sbt->m_hitBuffer.get(), sbt->m_hitOffset);
        hitRegion.stride = handleSizeAligned;
        hitRegion.size = sbt->m_hitCount * handleSizeAligned;
    }

    if(sbt->m_callableBuffer){
        callableRegion.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(sbt->m_callableBuffer.get(), sbt->m_callableOffset);
        callableRegion.stride = handleSizeAligned;
        callableRegion.size = sbt->m_callableCount * handleSizeAligned;
    }

    vkCmdTraceRaysKHR(m_currentCmdBuf->m_cmdBuf, &raygenRegion, &missRegion, &hitRegion, &callableRegion, args.width, args.height, args.depth);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

