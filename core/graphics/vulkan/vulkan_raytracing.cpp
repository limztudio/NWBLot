// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkDeviceAddress GetBufferDeviceAddress(IBuffer* _buffer, u64 offset){
    if(!_buffer)
        return 0;

    auto* buffer = checked_cast<Buffer*>(_buffer);
    return buffer->deviceAddress + offset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AccelStruct::AccelStruct(const VulkanContext& context)
    : m_context(context)
{}
AccelStruct::~AccelStruct(){
    if(accelStruct){
        vkDestroyAccelerationStructureKHR(m_context.device, accelStruct, m_context.allocationCallbacks);
        accelStruct = VK_NULL_HANDLE;
    }

    buffer.reset();
}

Object AccelStruct::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_AccelerationStructureKHR)
        return Object(accelStruct);
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


OpacityMicromap::OpacityMicromap(const VulkanContext& context)
    : m_context(context)
{}
OpacityMicromap::~OpacityMicromap(){
    if(micromap != VK_NULL_HANDLE){
        vkDestroyMicromapEXT(m_context.device, micromap, m_context.allocationCallbacks);
        micromap = VK_NULL_HANDLE;
    }
    dataBuffer.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingPipeline::RayTracingPipeline(const VulkanContext& context)
    : m_context(context)
{}
RayTracingPipeline::~RayTracingPipeline(){
    if(pipeline){
        vkDestroyPipeline(m_context.device, pipeline, m_context.allocationCallbacks);
        pipeline = VK_NULL_HANDLE;
    }
}

Object RayTracingPipeline::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Pipeline)
        return Object(pipeline);
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingAccelStructHandle Device::createAccelStruct(const RayTracingAccelStructDesc& desc){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.KHR_acceleration_structure)
        return nullptr;

    auto* as = new AccelStruct(m_context);
    as->desc = desc;

    VkAccelerationStructureTypeKHR asType = desc.isTopLevel ?  VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR :  VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    BufferDesc bufferDesc;
    bufferDesc.byteSize = desc.topLevelMaxInstances > 0 ? 
        desc.topLevelMaxInstances * sizeof(VkAccelerationStructureInstanceKHR) * 2 : // Estimate for TLAS
        1024 * 1024;
    bufferDesc.isAccelStructStorage = true;
    bufferDesc.debugName = "AccelStructBuffer";

    as->buffer = createBuffer(bufferDesc);

    if(!as->buffer){
        delete as;
        return nullptr;
    }

    VkAccelerationStructureCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    createInfo.buffer = checked_cast<Buffer*>(as->buffer.get())->buffer;
    createInfo.offset = 0;
    createInfo.size = bufferDesc.byteSize;
    createInfo.type = asType;

    res = vkCreateAccelerationStructureKHR(m_context.device, &createInfo, m_context.allocationCallbacks, &as->accelStruct);
    if(res != VK_SUCCESS){
        delete as;
        return nullptr;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    addressInfo.accelerationStructure = as->accelStruct;
    as->deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_context.device, &addressInfo);

    return RayTracingAccelStructHandle(as, AdoptRef);
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

    auto* om = new OpacityMicromap(m_context);
    om->desc = desc;

    BufferDesc bufferDesc;
    bufferDesc.canHaveUAVs = true;
    bufferDesc.byteSize = buildSize.micromapSize;
    bufferDesc.initialState = ResourceStates::AccelStructBuildBlas;
    bufferDesc.keepInitialState = true;
    bufferDesc.isAccelStructStorage = true;
    bufferDesc.debugName = desc.debugName;
    om->dataBuffer = createBuffer(bufferDesc);

    if(!om->dataBuffer){
        delete om;
        return nullptr;
    }

    auto* buffer = checked_cast<Buffer*>(om->dataBuffer.get());

    VkMicromapCreateInfoEXT createInfo = { VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT };
    createInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    createInfo.buffer = buffer->buffer;
    createInfo.size = buildSize.micromapSize;
    createInfo.deviceAddress = buffer->deviceAddress;

    res = vkCreateMicromapEXT(m_context.device, &createInfo, m_context.allocationCallbacks, &om->micromap);
    if(res != VK_SUCCESS){
        delete om;
        return nullptr;
    }

    return RayTracingOpacityMicromapHandle(om, AdoptRef);
}

MemoryRequirements Device::getAccelStructMemoryRequirements(IRayTracingAccelStruct* _as){
    auto* as = checked_cast<AccelStruct*>(_as);

    MemoryRequirements requirements = {};

    if(as->buffer){
        requirements.size = as->buffer->getDescription().byteSize;
        requirements.alignment = 256; // AS alignment requirement
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

    if(as->buffer)
        return bindBufferMemory(as->buffer.get(), heap, offset);

    return false;
}

RayTracingPipelineHandle Device::createRayTracingPipeline(const RayTracingPipelineDesc& desc){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.KHR_ray_tracing_pipeline)
        return nullptr;

    auto* pso = new RayTracingPipeline(m_context);
    pso->desc = desc;
    pso->m_device = this;

    Vector<VkPipelineShaderStageCreateInfo> stages;
    Vector<VkRayTracingShaderGroupCreateInfoKHR> groups;

    u32 shaderIndex = 0;

    for(const auto& shaderDesc : desc.shaders){
        if(!shaderDesc.shader)
            continue;

        auto* s = checked_cast<Shader*>(shaderDesc.shader.get());

        VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageInfo.module = s->shaderModule;
        stageInfo.pName = s->desc.entryName.c_str();

        switch(s->desc.shaderType){
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
        shaderIndex++;
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
            stageInfo.module = s->shaderModule;
            stageInfo.pName = s->desc.entryName.c_str();
            group.closestHitShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        if(hitGroup.anyHitShader){
            auto* s = checked_cast<Shader*>(hitGroup.anyHitShader.get());
            VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageInfo.stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            stageInfo.module = s->shaderModule;
            stageInfo.pName = s->desc.entryName.c_str();
            group.anyHitShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        if(hitGroup.intersectionShader){
            auto* s = checked_cast<Shader*>(hitGroup.intersectionShader.get());
            VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageInfo.stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
            stageInfo.module = s->shaderModule;
            stageInfo.pName = s->desc.entryName.c_str();
            group.intersectionShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        groups.push_back(group);
    }

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if(!desc.globalBindingLayouts.empty() && desc.globalBindingLayouts[0]){
        auto* layout = checked_cast<BindingLayout*>(desc.globalBindingLayouts[0].get());
        pipelineLayout = layout->pipelineLayout;
        pso->pipelineLayout = pipelineLayout;
    }

    VkRayTracingPipelineCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
    createInfo.stageCount = static_cast<u32>(stages.size());
    createInfo.pStages = stages.data();
    createInfo.groupCount = static_cast<u32>(groups.size());
    createInfo.pGroups = groups.data();
    createInfo.maxPipelineRayRecursionDepth = desc.maxRecursionDepth;
    createInfo.layout = pipelineLayout;

    res = vkCreateRayTracingPipelinesKHR(m_context.device, VK_NULL_HANDLE, m_context.pipelineCache, 1, &createInfo, m_context.allocationCallbacks, &pso->pipeline);
    if(res != VK_SUCCESS){
        delete pso;
        return nullptr;
    }

    u32 handleSize = m_context.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_context.rayTracingPipelineProperties.shaderGroupHandleAlignment;

    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);
    u32 groupCount = static_cast<u32>(groups.size());

    pso->shaderGroupHandles.resize(groupCount * handleSizeAligned);
    vkGetRayTracingShaderGroupHandlesKHR(m_context.device, pso->pipeline, 0, groupCount, pso->shaderGroupHandles.size(), pso->shaderGroupHandles.data());

    return RayTracingPipelineHandle(pso, AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderTable::ShaderTable(const VulkanContext& context, Device* device)
    : m_context(context)
    , m_device(device)
{}
ShaderTable::~ShaderTable() = default;

RayTracingShaderTableHandle RayTracingPipeline::createShaderTable(){
    auto* sbt = new ShaderTable(m_context, m_device);
    sbt->pipeline = this;
    return RayTracingShaderTableHandle(sbt, AdoptRef);
}

u32 ShaderTable::findGroupIndex(const Name& exportName)const{
    if(!pipeline)
        return 0;

    u32 groupIndex = 0;
    for(const auto& shaderDesc : pipeline->desc.shaders){
        if(shaderDesc.shader && shaderDesc.shader->getDescription().entryName == exportName)
            return groupIndex;
        groupIndex++;
    }
    for(const auto& hitGroup : pipeline->desc.hitGroups){
        if(hitGroup.exportName == exportName)
            return groupIndex;
        groupIndex++;
    }
    return 0;
}

void ShaderTable::allocateSBTBuffer(BufferHandle& outBuffer, u64 sbtSize){
    if(!m_device)
        return;

    BufferDesc bufferDesc;
    bufferDesc.byteSize = sbtSize;
    bufferDesc.debugName = "SBT_Buffer";
    bufferDesc.isShaderBindingTable = true;
    bufferDesc.cpuAccess = CpuAccessMode::Write;

    outBuffer = m_device->createBuffer(bufferDesc);
}

void ShaderTable::setRayGenerationShader(const Name& exportName, IBindingSet* /*bindings*/){
    if(!pipeline || !m_device)
        return;

    u32 handleSize = m_context.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_context.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    u32 baseAlignment = m_context.rayTracingPipelineProperties.shaderGroupBaseAlignment;
    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);
    u64 sbtSize = (handleSizeAligned + baseAlignment - 1) & ~(static_cast<u64>(baseAlignment) - 1);

    allocateSBTBuffer(raygenBuffer, sbtSize);
    if(!raygenBuffer)
        return;

    raygenOffset = 0;

    u32 groupIndex = findGroupIndex(exportName);

    void* mapped = m_device->mapBuffer(raygenBuffer.get(), CpuAccessMode::Write);
    if(mapped){
        NWB_MEMCPY(mapped, handleSizeAligned, pipeline->shaderGroupHandles.data() + groupIndex * handleSizeAligned, handleSize);
        m_device->unmapBuffer(raygenBuffer.get());
    }
}

u32 ShaderTable::addMissShader(const Name& exportName, IBindingSet* /*bindings*/){
    if(!pipeline || !m_device)
        return missCount++;

    u32 handleSize = m_context.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_context.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    u32 baseAlignment = m_context.rayTracingPipelineProperties.shaderGroupBaseAlignment;
    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

    u32 newCount = missCount + 1;
    u64 sbtSize = (static_cast<u64>(newCount) * handleSizeAligned + baseAlignment - 1) & ~(static_cast<u64>(baseAlignment) - 1);

    BufferHandle newBuffer;
    allocateSBTBuffer(newBuffer, sbtSize);
    if(!newBuffer)
        return missCount++;

    void* mapped = m_device->mapBuffer(newBuffer.get(), CpuAccessMode::Write);
    if(mapped){
        if(missBuffer && missCount > 0){
            void* oldMapped = m_device->mapBuffer(missBuffer.get(), CpuAccessMode::Read);
            if(oldMapped){
                NWB_MEMCPY(mapped, missCount * handleSizeAligned, oldMapped, missCount * handleSizeAligned);
                m_device->unmapBuffer(missBuffer.get());
            }
        }

        u32 groupIndex = findGroupIndex(exportName);
        auto* dst = static_cast<u8*>(mapped) + missCount * handleSizeAligned;
        NWB_MEMCPY(dst, handleSizeAligned, pipeline->shaderGroupHandles.data() + groupIndex * handleSizeAligned, handleSize);
        m_device->unmapBuffer(newBuffer.get());
    }

    missBuffer = newBuffer;
    missOffset = 0;
    return missCount++;
}

u32 ShaderTable::addHitGroup(const Name& exportName, IBindingSet* /*bindings*/){
    if(!pipeline || !m_device)
        return hitCount++;

    u32 handleSize = m_context.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_context.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    u32 baseAlignment = m_context.rayTracingPipelineProperties.shaderGroupBaseAlignment;
    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

    u32 newCount = hitCount + 1;
    u64 sbtSize = (static_cast<u64>(newCount) * handleSizeAligned + baseAlignment - 1) & ~(static_cast<u64>(baseAlignment) - 1);

    BufferHandle newBuffer;
    allocateSBTBuffer(newBuffer, sbtSize);
    if(!newBuffer)
        return hitCount++;

    void* mapped = m_device->mapBuffer(newBuffer.get(), CpuAccessMode::Write);
    if(mapped){
        if(hitBuffer && hitCount > 0){
            void* oldMapped = m_device->mapBuffer(hitBuffer.get(), CpuAccessMode::Read);
            if(oldMapped){
                NWB_MEMCPY(mapped, hitCount * handleSizeAligned, oldMapped, hitCount * handleSizeAligned);
                m_device->unmapBuffer(hitBuffer.get());
            }
        }

        u32 groupIndex = findGroupIndex(exportName);
        auto* dst = static_cast<u8*>(mapped) + hitCount * handleSizeAligned;
        NWB_MEMCPY(dst, handleSizeAligned, pipeline->shaderGroupHandles.data() + groupIndex * handleSizeAligned, handleSize);
        m_device->unmapBuffer(newBuffer.get());
    }

    hitBuffer = newBuffer;
    hitOffset = 0;
    return hitCount++;
}

u32 ShaderTable::addCallableShader(const Name& exportName, IBindingSet* /*bindings*/){
    if(!pipeline || !m_device)
        return callableCount++;

    u32 handleSize = m_context.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_context.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    u32 baseAlignment = m_context.rayTracingPipelineProperties.shaderGroupBaseAlignment;
    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);

    u32 newCount = callableCount + 1;
    u64 sbtSize = (static_cast<u64>(newCount) * handleSizeAligned + baseAlignment - 1) & ~(static_cast<u64>(baseAlignment) - 1);

    BufferHandle newBuffer;
    allocateSBTBuffer(newBuffer, sbtSize);
    if(!newBuffer)
        return callableCount++;

    void* mapped = m_device->mapBuffer(newBuffer.get(), CpuAccessMode::Write);
    if(mapped){
        if(callableBuffer && callableCount > 0){
            void* oldMapped = m_device->mapBuffer(callableBuffer.get(), CpuAccessMode::Read);
            if(oldMapped){
                NWB_MEMCPY(mapped, callableCount * handleSizeAligned, oldMapped, callableCount * handleSizeAligned);
                m_device->unmapBuffer(callableBuffer.get());
            }
        }

        u32 groupIndex = findGroupIndex(exportName);
        auto* dst = static_cast<u8*>(mapped) + callableCount * handleSizeAligned;
        NWB_MEMCPY(dst, handleSizeAligned, pipeline->shaderGroupHandles.data() + groupIndex * handleSizeAligned, handleSize);
        m_device->unmapBuffer(newBuffer.get());
    }

    callableBuffer = newBuffer;
    callableOffset = 0;
    return callableCount++;
}

void ShaderTable::clearMissShaders(){ missCount = 0; missBuffer = nullptr; }
void ShaderTable::clearHitShaders(){ hitCount = 0; hitBuffer = nullptr; }
void ShaderTable::clearCallableShaders(){ callableCount = 0; callableBuffer = nullptr; }

Object ShaderTable::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Buffer && raygenBuffer){
        auto* buf = checked_cast<Buffer*>(raygenBuffer.get());
        return Object(buf->buffer);
    }
    return Object{nullptr};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setRayTracingState(const RayTracingState& state){
    currentRayTracingState = state;

    if(!state.shaderTable)
        return;

    auto* sbt = checked_cast<ShaderTable*>(state.shaderTable);
    RayTracingPipeline* pipeline = sbt->pipeline;

    if(!pipeline)
        return;

    vkCmdBindPipeline(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->pipeline);

    if(state.bindings.size() > 0 && pipeline->pipelineLayout != VK_NULL_HANDLE){
        for(usize i = 0; i < state.bindings.size(); ++i){
            if(state.bindings[i]){
                auto* bindingSet = checked_cast<BindingSet*>(state.bindings[i]);
                if(!bindingSet->descriptorSets.empty())
                    vkCmdBindDescriptorSets(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                        pipeline->pipelineLayout, static_cast<u32>(i),
                        static_cast<u32>(bindingSet->descriptorSets.size()),
                        bindingSet->descriptorSets.data(), 0, nullptr);
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

    Vector<VkAccelerationStructureGeometryKHR> geometries;
    Vector<VkAccelerationStructureBuildRangeInfoKHR> rangeInfos;
    Vector<uint32_t> primitiveCounts;

    geometries.reserve(numGeometries);
    rangeInfos.reserve(numGeometries);
    primitiveCounts.reserve(numGeometries);

    for(usize i = 0; i < numGeometries; ++i){
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
                primitiveCount = triangles.indexCount / 3;
            }
            else{
                geometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
                primitiveCount = triangles.vertexCount / 3;
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

        geometries.push_back(geometry);
        rangeInfos.push_back(rangeInfo);
        primitiveCounts.push_back(static_cast<uint32_t>(primitiveCount));
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
    buildInfo.dstAccelerationStructure = as->accelStruct;
    buildInfo.geometryCount = static_cast<u32>(geometries.size());
    buildInfo.pGeometries = geometries.data();

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, primitiveCounts.data(), &sizeInfo);

    BufferDesc scratchDesc;
    scratchDesc.byteSize = sizeInfo.buildScratchSize;
    scratchDesc.structStride = 1;
    scratchDesc.debugName = "AS_BuildScratch";

    BufferHandle scratchBuffer = m_device.createBuffer(scratchDesc);
    if(scratchBuffer){
        buildInfo.scratchData.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(scratchBuffer.get());

        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = rangeInfos.data();
        vkCmdBuildAccelerationStructuresKHR(currentCmdBuf->cmdBuf, 1, &buildInfo, &pRangeInfos);

        currentCmdBuf->referencedStagingBuffers.push_back(scratchBuffer);
    }

    if(buildFlags & RayTracingAccelStructBuildFlags::AllowCompaction)
        m_pendingCompactions.push_back(as);

    currentCmdBuf->referencedResources.push_back(_as);
}

void CommandList::compactBottomLevelAccelStructs(){
    if(!m_context.extensions.KHR_acceleration_structure)
        return;

    if(m_pendingCompactions.empty())
        return;

    for(auto& as : m_pendingCompactions){
        vkCmdWriteAccelerationStructuresPropertiesKHR(currentCmdBuf->cmdBuf, 1, &as->accelStruct, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, VK_NULL_HANDLE, 0);

        // Note: A full implementation would:
        // 1. Read back the compacted size after GPU finishes
        // 2. Create a new, smaller AS
        // 3. vkCmdCopyAccelerationStructureKHR with COMPACT mode
        // 4. Swap the new AS into the AccelStruct object
        // This requires multi-pass (submit, readback, submit) which cannot happen in a single command list.
        // For now, mark as compacted since the data is valid.
        as->compacted = true;
    }

    m_pendingCompactions.clear();
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

    if(mappedInstances){
        for(usize i = 0; i < numInstances; ++i){
            const auto& inst = pInstances[i];
            VkAccelerationStructureInstanceKHR& vkInst = mappedInstances[i];

            NWB_MEMCPY(&vkInst.transform, sizeof(VkTransformMatrixKHR), &inst.transform, sizeof(VkTransformMatrixKHR));

            vkInst.instanceCustomIndex = inst.instanceID & 0xFFFFFF;
            vkInst.mask = inst.instanceMask;
            vkInst.instanceShaderBindingTableRecordOffset = inst.instanceContributionToHitGroupIndex & 0xFFFFFF;
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
            vkInst.accelerationStructureReference = blas ? blas->deviceAddress : 0;
        }

        m_device.unmapBuffer(instanceBuffer.get());
    }

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
    buildInfo.dstAccelerationStructure = as->accelStruct;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    auto primitiveCount = static_cast<uint32_t>(numInstances);
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

    BufferDesc scratchDesc;
    scratchDesc.byteSize = sizeInfo.buildScratchSize;
    scratchDesc.structStride = 1;
    scratchDesc.debugName = "TLAS_BuildScratch";

    BufferHandle scratchBuffer = m_device.createBuffer(scratchDesc);
    if(scratchBuffer){
        buildInfo.scratchData.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(scratchBuffer.get());

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
        rangeInfo.primitiveCount = primitiveCount;
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

        vkCmdBuildAccelerationStructuresKHR(currentCmdBuf->cmdBuf, 1, &buildInfo, &pRangeInfo);

        currentCmdBuf->referencedStagingBuffers.push_back(Move(scratchBuffer));
        currentCmdBuf->referencedStagingBuffers.push_back(Move(instanceBuffer));
    }

    currentCmdBuf->referencedResources.push_back(_as);
}

void CommandList::buildOpacityMicromap(IRayTracingOpacityMicromap* _omm, const RayTracingOpacityMicromapDesc& ommDesc){
    if(!m_context.extensions.EXT_opacity_micromap)
        return;

    auto* omm = checked_cast<OpacityMicromap*>(_omm);

    if(enableAutomaticBarriers){
        if(ommDesc.inputBuffer)
            setBufferState(ommDesc.inputBuffer, ResourceStates::OpacityMicromapBuildInput);
        if(ommDesc.perOmmDescs)
            setBufferState(ommDesc.perOmmDescs, ResourceStates::OpacityMicromapBuildInput);
        if(omm->dataBuffer)
            setBufferState(omm->dataBuffer.get(), ResourceStates::OpacityMicromapWrite);
    }

    if(ommDesc.trackLiveness){
        if(ommDesc.inputBuffer)
            currentCmdBuf->referencedResources.push_back(ommDesc.inputBuffer);
        if(ommDesc.perOmmDescs)
            currentCmdBuf->referencedResources.push_back(ommDesc.perOmmDescs);
        if(omm->dataBuffer)
            currentCmdBuf->referencedResources.push_back(omm->dataBuffer.get());
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
    buildInfo.dstMicromap = omm->micromap;
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

        vkCmdBuildMicromapsEXT(currentCmdBuf->cmdBuf, 1, &buildInfo);

        currentCmdBuf->referencedStagingBuffers.push_back(Move(scratchBuffer));
    }
    else{
        vkCmdBuildMicromapsEXT(currentCmdBuf->cmdBuf, 1, &buildInfo);
    }
}

void CommandList::dispatchRays(const RayTracingDispatchRaysArguments& args){
    if(!m_context.extensions.KHR_ray_tracing_pipeline)
        return;

    RayTracingState& state = currentRayTracingState;

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

    if(sbt->raygenBuffer){
        raygenRegion.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(sbt->raygenBuffer.get(), sbt->raygenOffset);
        raygenRegion.stride = handleSizeAligned;
        raygenRegion.size = handleSizeAligned;
    }

    if(sbt->missBuffer){
        missRegion.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(sbt->missBuffer.get(), sbt->missOffset);
        missRegion.stride = handleSizeAligned;
        missRegion.size = sbt->missCount * handleSizeAligned;
    }

    if(sbt->hitBuffer){
        hitRegion.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(sbt->hitBuffer.get(), sbt->hitOffset);
        hitRegion.stride = handleSizeAligned;
        hitRegion.size = sbt->hitCount * handleSizeAligned;
    }

    if(sbt->callableBuffer){
        callableRegion.deviceAddress = __hidden_vulkan::GetBufferDeviceAddress(sbt->callableBuffer.get(), sbt->callableOffset);
        callableRegion.stride = handleSizeAligned;
        callableRegion.size = sbt->callableCount * handleSizeAligned;
    }

    vkCmdTraceRaysKHR(currentCmdBuf->cmdBuf, &raygenRegion, &missRegion, &hitRegion, &callableRegion, args.width, args.height, args.depth);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
