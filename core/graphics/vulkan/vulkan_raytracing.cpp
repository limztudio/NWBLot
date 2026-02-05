// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// Helper functions for buffer addresses
//-----------------------------------------------------------------------------

static VkDeviceAddress getBufferDeviceAddress(IBuffer* _buffer, u64 offset = 0){
    if(!_buffer)
        return 0;
    
    Buffer* buffer = checked_cast<Buffer*>(_buffer);
    return buffer->deviceAddress + offset;
}

//-----------------------------------------------------------------------------
// AccelStruct - Bottom-level and Top-level acceleration structures
//-----------------------------------------------------------------------------

AccelStruct::AccelStruct(const VulkanContext& context)
    : m_Context(context)
{}

AccelStruct::~AccelStruct(){
    if(accelStruct){
        vkDestroyAccelerationStructureKHR(m_Context.device, accelStruct, m_Context.allocationCallbacks);
        accelStruct = VK_NULL_HANDLE;
    }
    
    buffer = nullptr; // RefCountPtr handles cleanup
}

Object AccelStruct::getNativeObject(ObjectType objectType){
    if(objectType == ObjectType::VK_AccelerationStructureKHR)
        return Object(accelStruct);
    return Object(nullptr);
}

//-----------------------------------------------------------------------------
// RayTracingPipeline - Ray tracing pipeline
//-----------------------------------------------------------------------------

RayTracingPipeline::RayTracingPipeline(const VulkanContext& context)
    : m_Context(context)
{}

RayTracingPipeline::~RayTracingPipeline(){
    if(pipeline){
        vkDestroyPipeline(m_Context.device, pipeline, m_Context.allocationCallbacks);
        pipeline = VK_NULL_HANDLE;
    }
}

Object RayTracingPipeline::getNativeObject(ObjectType objectType){
    if(objectType == ObjectType::VK_Pipeline)
        return Object(pipeline);
    return Object(nullptr);
}

//-----------------------------------------------------------------------------
// Device - Ray Tracing methods
//-----------------------------------------------------------------------------

RayTracingAccelStructHandle Device::createAccelStruct(const RayTracingAccelStructDesc& desc){
    if(!m_Context.extensions.KHR_acceleration_structure)
        return nullptr;
    
    AccelStruct* as = new AccelStruct(m_Context);
    as->desc = desc;
    
    // Determine acceleration structure type
    VkAccelerationStructureTypeKHR asType = desc.isTopLevel ? 
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR : 
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    
    // Create backing buffer for acceleration structure
    BufferDesc bufferDesc;
    bufferDesc.byteSize = desc.topLevelMaxInstances > 0 ? 
        desc.topLevelMaxInstances * sizeof(VkAccelerationStructureInstanceKHR) * 2 : // Estimate for TLAS
        1024 * 1024; // 1MB default for BLAS, will be resized based on actual requirements
    bufferDesc.isAccelStructStorage = true;
    bufferDesc.debugName = "AccelStructBuffer";
    
    as->buffer = createBuffer(bufferDesc);
    
    if(!as->buffer){
        delete as;
        return nullptr;
    }
    
    // Create acceleration structure
    VkAccelerationStructureCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    createInfo.buffer = checked_cast<Buffer*>(as->buffer.Get())->buffer;
    createInfo.offset = 0;
    createInfo.size = bufferDesc.byteSize;
    createInfo.type = asType;
    
    VkResult res = vkCreateAccelerationStructureKHR(m_Context.device, &createInfo, m_Context.allocationCallbacks, &as->accelStruct);
    
    if(res != VK_SUCCESS){
        delete as;
        return nullptr;
    }
    
    // Get device address
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    addressInfo.accelerationStructure = as->accelStruct;
    as->deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_Context.device, &addressInfo);
    
    return RayTracingAccelStructHandle::Create(as);
}

RayTracingOpacityMicromapHandle Device::createOpacityMicromap(const RayTracingOpacityMicromapDesc& desc){
    // Opacity micromaps require VK_EXT_opacity_micromap
    // Placeholder implementation
    return nullptr;
}

MemoryRequirements Device::getAccelStructMemoryRequirements(IRayTracingAccelStruct* _as){
    AccelStruct* as = checked_cast<AccelStruct*>(_as);
    
    MemoryRequirements requirements = {};
    
    if(as->buffer){
        requirements.size = as->buffer->getDescription().byteSize;
        requirements.alignment = 256; // AS alignment requirement
    }
    
    return requirements;
}

RayTracingClusterOperationSizeInfo Device::getClusterOperationSizeInfo(const RayTracingClusterOperationParams& params){
    // Cluster operations for NVIDIA's cluster-based AS optimization
    // Placeholder implementation
    RayTracingClusterOperationSizeInfo info = {};
    return info;
}

bool Device::bindAccelStructMemory(IRayTracingAccelStruct* as, IHeap* heap, u64 offset){
    // Memory is managed through the backing buffer
    return false;
}

RayTracingPipelineHandle Device::createRayTracingPipeline(const RayTracingPipelineDesc& desc){
    if(!m_Context.extensions.KHR_ray_tracing_pipeline)
        return nullptr;
    
    RayTracingPipeline* pso = new RayTracingPipeline(m_Context);
    pso->desc = desc;
    
    // Collect shader stages
    Vector<VkPipelineShaderStageCreateInfo> stages;
    Vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
    
    u32 shaderIndex = 0;
    
    // Raygen shaders
    for(const auto& shader : desc.shaders){
        if(!shader)
            continue;
        
        Shader* s = checked_cast<Shader*>(shader.Get());
        
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
        case ShaderType::ClosestHit:
            stageInfo.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            break;
        case ShaderType::AnyHit:
            stageInfo.stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            break;
        case ShaderType::Intersection:
            stageInfo.stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
            break;
        case ShaderType::Callable:
            stageInfo.stage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;
            break;
        default:
            continue;
        }
        
        stages.push_back(stageInfo);
        shaderIndex++;
    }
    
    // Create shader groups from hitGroups
    for(const auto& hitGroup : desc.hitGroups){
        VkRayTracingShaderGroupCreateInfoKHR group = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
        group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        group.generalShader = VK_SHADER_UNUSED_KHR;
        group.closestHitShader = hitGroup.closestHitShaderIndex >= 0 ? hitGroup.closestHitShaderIndex : VK_SHADER_UNUSED_KHR;
        group.anyHitShader = hitGroup.anyHitShaderIndex >= 0 ? hitGroup.anyHitShaderIndex : VK_SHADER_UNUSED_KHR;
        group.intersectionShader = hitGroup.intersectionShaderIndex >= 0 ? hitGroup.intersectionShaderIndex : VK_SHADER_UNUSED_KHR;
        groups.push_back(group);
    }
    
    // Get pipeline layout from binding layouts
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if(!desc.globalBindingLayouts.empty() && desc.globalBindingLayouts[0]){
        BindingLayout* layout = checked_cast<BindingLayout*>(desc.globalBindingLayouts[0].Get());
        pipelineLayout = layout->pipelineLayout;
        pso->pipelineLayout = pipelineLayout;
    }
    
    // Create ray tracing pipeline
    VkRayTracingPipelineCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
    createInfo.stageCount = static_cast<u32>(stages.size());
    createInfo.pStages = stages.data();
    createInfo.groupCount = static_cast<u32>(groups.size());
    createInfo.pGroups = groups.data();
    createInfo.maxPipelineRayRecursionDepth = desc.maxRecursionDepth;
    createInfo.layout = pipelineLayout;
    
    VkResult res = vkCreateRayTracingPipelinesKHR(m_Context.device, VK_NULL_HANDLE, m_Context.pipelineCache, 
                                                   1, &createInfo, m_Context.allocationCallbacks, &pso->pipeline);
    
    if(res != VK_SUCCESS){
        delete pso;
        return nullptr;
    }
    
    // Get shader group handles for SBT
    u32 handleSize = m_Context.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_Context.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    u32 baseAlignment = m_Context.rayTracingPipelineProperties.shaderGroupBaseAlignment;
    
    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);
    u32 groupCount = static_cast<u32>(groups.size());
    
    pso->shaderGroupHandles.resize(groupCount * handleSizeAligned);
    vkGetRayTracingShaderGroupHandlesKHR(m_Context.device, pso->pipeline, 0, groupCount, 
                                          pso->shaderGroupHandles.size(), pso->shaderGroupHandles.data());
    
    return RayTracingPipelineHandle::Create(pso);
}

//-----------------------------------------------------------------------------
// CommandList - Ray Tracing
//-----------------------------------------------------------------------------

RayTracingState& CommandList::getRayTracingState(){
    return stateTracker->rayTracingState;
}

void CommandList::setRayTracingState(const RayTracingState& state){
    stateTracker->rayTracingState = state;
    
    if(!state.shaderTable)
        return;
    
    const VulkanContext& vk = *m_Context;
    // Bind ray tracing pipeline from shader table
    // Full implementation would extract pipeline from shader table and bind descriptor sets
}

//-----------------------------------------------------------------------------
// CommandList - Ray Tracing commands
//-----------------------------------------------------------------------------

void CommandList::buildBottomLevelAccelStruct(IRayTracingAccelStruct* _as, const RayTracingGeometryDesc* pGeometries, usize numGeometries, RayTracingAccelStructBuildFlags buildFlags){
    if(!_as || !pGeometries || numGeometries == 0)
        return;
    
    const VulkanContext& vk = *m_Context;
    
    if(!vk.extensions.KHR_acceleration_structure)
        return;
    
    AccelStruct* as = checked_cast<AccelStruct*>(_as);
    
    // Build geometry array
    Vector<VkAccelerationStructureGeometryKHR> geometries;
    Vector<VkAccelerationStructureBuildRangeInfoKHR> rangeInfos;
    Vector<u32> primitiveCounts;
    
    geometries.reserve(numGeometries);
    rangeInfos.reserve(numGeometries);
    primitiveCounts.reserve(numGeometries);
    
    for(usize i = 0; i < numGeometries; i++){
        const auto& geomDesc = pGeometries[i];
        
        VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
        u32 primitiveCount = 0;
        
        if(geomDesc.geometryType == RayTracingGeometryType::Triangles){
            const auto& triangles = geomDesc.triangles;
            
            geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            geometry.geometry.triangles.vertexFormat = ConvertFormat(triangles.vertexFormat);
            geometry.geometry.triangles.vertexData.deviceAddress = getBufferDeviceAddress(triangles.vertexBuffer, triangles.vertexOffset);
            geometry.geometry.triangles.vertexStride = triangles.vertexStride;
            geometry.geometry.triangles.maxVertex = triangles.vertexCount > 0 ? triangles.vertexCount - 1 : 0;
            
            if(triangles.indexBuffer){
                geometry.geometry.triangles.indexType = triangles.indexFormat == Format::R16_UINT ? 
                    VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
                geometry.geometry.triangles.indexData.deviceAddress = getBufferDeviceAddress(triangles.indexBuffer, triangles.indexOffset);
                primitiveCount = triangles.indexCount / 3;
            }
            else{
                geometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
                primitiveCount = triangles.vertexCount / 3;
            }
            
            rangeInfo.primitiveCount = primitiveCount;
        }
        else if(geomDesc.geometryType == RayTracingGeometryType::AABBs){
            const auto& aabbs = geomDesc.aabbs;
            
            geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
            geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
            geometry.geometry.aabbs.data.deviceAddress = getBufferDeviceAddress(aabbs.buffer, aabbs.offset);
            geometry.geometry.aabbs.stride = aabbs.stride;
            
            primitiveCount = aabbs.count;
            rangeInfo.primitiveCount = primitiveCount;
        }
        
        // Set geometry flags
        geometry.flags = 0;
        if(geomDesc.flags & RayTracingGeometryFlags::Opaque)
            geometry.flags |= VK_GEOMETRY_OPAQUE_BIT_KHR;
        if(geomDesc.flags & RayTracingGeometryFlags::NoDuplicateAnyHitInvocation)
            geometry.flags |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
        
        geometries.push_back(geometry);
        rangeInfos.push_back(rangeInfo);
        primitiveCounts.push_back(primitiveCount);
    }
    
    // Set up build info
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
    
    // Query scratch buffer size
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, 
                                             &buildInfo, primitiveCounts.data(), &sizeInfo);
    
    // Allocate scratch buffer
    BufferDesc scratchDesc;
    scratchDesc.byteSize = sizeInfo.buildScratchSize;
    scratchDesc.structStride = 1;
    scratchDesc.debugName = "AS_BuildScratch";
    
    BufferHandle scratchBuffer = m_Device->createBuffer(scratchDesc);
    if(scratchBuffer){
        buildInfo.scratchData.deviceAddress = getBufferDeviceAddress(scratchBuffer.Get());
        
        // Build acceleration structure
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = rangeInfos.data();
        vkCmdBuildAccelerationStructuresKHR(currentCmdBuf->cmdBuf, 1, &buildInfo, &pRangeInfos);
        
        // Track scratch buffer until command completes
        currentCmdBuf->referencedStagingBuffers.push_back(scratchBuffer);
    }
    
    currentCmdBuf->referencedResources.push_back(_as);
}

void CommandList::compactBottomLevelAccelStruct(IRayTracingAccelStruct* _src, IRayTracingAccelStruct* _dest){
    if(!_src || !_dest)
        return;
    
    const VulkanContext& vk = *m_Context;
    
    if(!vk.extensions.KHR_acceleration_structure)
        return;
    
    AccelStruct* src = checked_cast<AccelStruct*>(_src);
    AccelStruct* dest = checked_cast<AccelStruct*>(_dest);
    
    VkCopyAccelerationStructureInfoKHR copyInfo = { VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };
    copyInfo.src = src->accelStruct;
    copyInfo.dst = dest->accelStruct;
    copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
    
    vkCmdCopyAccelerationStructureKHR(currentCmdBuf->cmdBuf, &copyInfo);
    
    currentCmdBuf->referencedResources.push_back(_src);
    currentCmdBuf->referencedResources.push_back(_dest);
}

void CommandList::buildTopLevelAccelStruct(IRayTracingAccelStruct* _as, const RayTracingInstanceDesc* pInstances, usize numInstances, RayTracingAccelStructBuildFlags buildFlags){
    if(!_as || !pInstances || numInstances == 0)
        return;
    
    const VulkanContext& vk = *m_Context;
    
    if(!vk.extensions.KHR_acceleration_structure)
        return;
    
    AccelStruct* as = checked_cast<AccelStruct*>(_as);
    
    // Create instance buffer
    u64 instanceBufferSize = numInstances * sizeof(VkAccelerationStructureInstanceKHR);
    BufferDesc instanceBufferDesc;
    instanceBufferDesc.byteSize = instanceBufferSize;
    instanceBufferDesc.cpuAccess = CpuAccessMode::Write;
    instanceBufferDesc.isAccelStructBuildInput = true;
    instanceBufferDesc.debugName = "TLAS_InstanceBuffer";
    
    BufferHandle instanceBuffer = m_Device->createBuffer(instanceBufferDesc);
    if(!instanceBuffer)
        return;
    
    // Fill instance data
    VkAccelerationStructureInstanceKHR* mappedInstances = 
        static_cast<VkAccelerationStructureInstanceKHR*>(m_Device->mapBuffer(instanceBuffer.Get(), CpuAccessMode::Write));
    
    if(mappedInstances){
        for(usize i = 0; i < numInstances; i++){
            const auto& inst = pInstances[i];
            VkAccelerationStructureInstanceKHR& vkInst = mappedInstances[i];
            
            // Copy transform (row-major 3x4)
            memcpy(&vkInst.transform, &inst.transform, sizeof(VkTransformMatrixKHR));
            
            vkInst.instanceCustomIndex = inst.instanceID & 0xFFFFFF;
            vkInst.mask = inst.instanceMask;
            vkInst.instanceShaderBindingTableRecordOffset = inst.instanceContributionToHitGroupIndex & 0xFFFFFF;
            vkInst.flags = 0;
            
            if(inst.flags & RayTracingInstanceFlags::TriangleFacingCullDisable)
                vkInst.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            if(inst.flags & RayTracingInstanceFlags::TriangleFrontCounterClockwise)
                vkInst.flags |= VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR;
            if(inst.flags & RayTracingInstanceFlags::ForceOpaque)
                vkInst.flags |= VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
            if(inst.flags & RayTracingInstanceFlags::ForceNonOpaque)
                vkInst.flags |= VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR;
            
            // Get BLAS device address
            AccelStruct* blas = checked_cast<AccelStruct*>(inst.bottomLevelAS);
            vkInst.accelerationStructureReference = blas ? blas->deviceAddress : 0;
        }
        
        m_Device->unmapBuffer(instanceBuffer.Get());
    }
    
    // Set up geometry for instances
    VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = getBufferDeviceAddress(instanceBuffer.Get());
    
    // Set up build info
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
    
    // Query scratch buffer size
    u32 primitiveCount = static_cast<u32>(numInstances);
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, 
                                             &buildInfo, &primitiveCount, &sizeInfo);
    
    // Allocate scratch buffer
    BufferDesc scratchDesc;
    scratchDesc.byteSize = sizeInfo.buildScratchSize;
    scratchDesc.structStride = 1;
    scratchDesc.debugName = "TLAS_BuildScratch";
    
    BufferHandle scratchBuffer = m_Device->createBuffer(scratchDesc);
    if(scratchBuffer){
        buildInfo.scratchData.deviceAddress = getBufferDeviceAddress(scratchBuffer.Get());
        
        // Build acceleration structure
        VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
        rangeInfo.primitiveCount = primitiveCount;
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
        
        vkCmdBuildAccelerationStructuresKHR(currentCmdBuf->cmdBuf, 1, &buildInfo, &pRangeInfo);
        
        // Track buffers
        currentCmdBuf->referencedStagingBuffers.push_back(scratchBuffer);
        currentCmdBuf->referencedStagingBuffers.push_back(instanceBuffer);
    }
    
    currentCmdBuf->referencedResources.push_back(_as);
}

void CommandList::buildOpacityMicromap(IRayTracingOpacityMicromap* _omm, const RayTracingOpacityMicromapDesc& desc){
    // Opacity micromaps require VK_EXT_opacity_micromap
    // Placeholder implementation
}

void CommandList::dispatchRays(const RayTracingDispatchArguments& args){
    const VulkanContext& vk = *m_Context;
    
    if(!vk.extensions.KHR_ray_tracing_pipeline)
        return;
    
    // Get shader binding table regions from current ray tracing state
    RayTracingState& state = stateTracker->rayTracingState;
    
    if(!state.shaderTable)
        return;
    
    // Get SBT addresses from shader table
    ShaderTable* sbt = checked_cast<ShaderTable*>(state.shaderTable.Get());
    
    VkStridedDeviceAddressRegionKHR raygenRegion = {};
    VkStridedDeviceAddressRegionKHR missRegion = {};
    VkStridedDeviceAddressRegionKHR hitRegion = {};
    VkStridedDeviceAddressRegionKHR callableRegion = {};
    
    u32 handleSize = vk.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = vk.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    u32 baseAlignment = vk.rayTracingPipelineProperties.shaderGroupBaseAlignment;
    
    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);
    
    if(sbt->raygenBuffer){
        raygenRegion.deviceAddress = getBufferDeviceAddress(sbt->raygenBuffer.Get(), sbt->raygenOffset);
        raygenRegion.stride = handleSizeAligned;
        raygenRegion.size = handleSizeAligned;
    }
    
    if(sbt->missBuffer){
        missRegion.deviceAddress = getBufferDeviceAddress(sbt->missBuffer.Get(), sbt->missOffset);
        missRegion.stride = handleSizeAligned;
        missRegion.size = sbt->missCount * handleSizeAligned;
    }
    
    if(sbt->hitBuffer){
        hitRegion.deviceAddress = getBufferDeviceAddress(sbt->hitBuffer.Get(), sbt->hitOffset);
        hitRegion.stride = handleSizeAligned;
        hitRegion.size = sbt->hitCount * handleSizeAligned;
    }
    
    if(sbt->callableBuffer){
        callableRegion.deviceAddress = getBufferDeviceAddress(sbt->callableBuffer.Get(), sbt->callableOffset);
        callableRegion.stride = handleSizeAligned;
        callableRegion.size = sbt->callableCount * handleSizeAligned;
    }
    
    vkCmdTraceRaysKHR(currentCmdBuf->cmdBuf, 
                      &raygenRegion, &missRegion, &hitRegion, &callableRegion,
                      args.width, args.height, args.depth);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
