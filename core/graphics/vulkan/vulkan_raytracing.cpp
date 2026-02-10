// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN

using __hidden_vulkan::checked_cast;
using namespace __hidden_vulkan;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// Helper functions for buffer addresses
//-----------------------------------------------------------------------------

namespace __hidden_vulkan{
VkDeviceAddress getBufferDeviceAddress(IBuffer* _buffer, u64 offset = 0){
    if(!_buffer)
        return 0;
    
    Buffer* buffer = checked_cast<Buffer*>(_buffer);
    return buffer->deviceAddress + offset;
}
} // namespace __hidden_vulkan

//-----------------------------------------------------------------------------
// AccelStruct - Bottom-level and Top-level acceleration structures
//-----------------------------------------------------------------------------

AccelStruct::AccelStruct(const VulkanContext& context)
    : m_context(context)
{}

AccelStruct::~AccelStruct(){
    if(accelStruct){
        vkDestroyAccelerationStructureKHR(m_context.device, accelStruct, m_context.allocationCallbacks);
        accelStruct = VK_NULL_HANDLE;
    }
    
    buffer = nullptr; // RefCountPtr handles cleanup
}

Object AccelStruct::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_AccelerationStructureKHR)
        return Object(accelStruct);
    return Object(nullptr);
}

//-----------------------------------------------------------------------------
// RayTracingPipeline - Ray tracing pipeline
//-----------------------------------------------------------------------------

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

//-----------------------------------------------------------------------------
// Device - Ray Tracing methods
//-----------------------------------------------------------------------------

RayTracingAccelStructHandle Device::createAccelStruct(const RayTracingAccelStructDesc& desc){
    if(!m_context.extensions.KHR_acceleration_structure)
        return nullptr;
    
    AccelStruct* as = new AccelStruct(m_context);
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
    createInfo.buffer = checked_cast<Buffer*>(as->buffer.get())->buffer;
    createInfo.offset = 0;
    createInfo.size = bufferDesc.byteSize;
    createInfo.type = asType;
    
    VkResult res = vkCreateAccelerationStructureKHR(m_context.device, &createInfo, m_context.allocationCallbacks, &as->accelStruct);
    
    if(res != VK_SUCCESS){
        delete as;
        return nullptr;
    }
    
    // Get device address
    VkAccelerationStructureDeviceAddressInfoKHR addressInfo = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    addressInfo.accelerationStructure = as->accelStruct;
    as->deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(m_context.device, &addressInfo);
    
    return RayTracingAccelStructHandle(as, AdoptRef);
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

bool Device::bindAccelStructMemory(IRayTracingAccelStruct* _as, IHeap* heap, u64 offset){
    if(!_as || !heap)
        return false;
    
    AccelStruct* as = checked_cast<AccelStruct*>(_as);
    Heap* h = checked_cast<Heap*>(heap);
    
    // The acceleration structure is backed by a buffer
    // Bind the backing buffer's memory to the heap
    if(as->buffer)
        return bindBufferMemory(as->buffer, heap, offset);
    
    return false;
}

RayTracingPipelineHandle Device::createRayTracingPipeline(const RayTracingPipelineDesc& desc){
    if(!m_context.extensions.KHR_ray_tracing_pipeline)
        return nullptr;
    
    RayTracingPipeline* pso = new RayTracingPipeline(m_context);
    pso->desc = desc;
    pso->m_device = this;
    
    // Collect shader stages
    Vector<VkPipelineShaderStageCreateInfo> stages;
    Vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
    
    u32 shaderIndex = 0;
    
    // General shaders (raygen, miss, callable)
    for(const auto& shaderDesc : desc.shaders){
        if(!shaderDesc.shader)
            continue;
        
        Shader* s = checked_cast<Shader*>(shaderDesc.shader.get());
        
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
        
        // Create general shader group for raygen/miss/callable
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
    
    // Create shader groups from hitGroups
    for(const auto& hitGroup : desc.hitGroups){
        VkRayTracingShaderGroupCreateInfoKHR group = { VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
        group.type = hitGroup.isProceduralPrimitive 
            ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR 
            : VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        group.generalShader = VK_SHADER_UNUSED_KHR;
        group.closestHitShader = VK_SHADER_UNUSED_KHR;
        group.anyHitShader = VK_SHADER_UNUSED_KHR;
        group.intersectionShader = VK_SHADER_UNUSED_KHR;
        
        if(hitGroup.closestHitShader){
            Shader* s = checked_cast<Shader*>(hitGroup.closestHitShader.get());
            VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageInfo.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            stageInfo.module = s->shaderModule;
            stageInfo.pName = s->desc.entryName.c_str();
            group.closestHitShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        if(hitGroup.anyHitShader){
            Shader* s = checked_cast<Shader*>(hitGroup.anyHitShader.get());
            VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageInfo.stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            stageInfo.module = s->shaderModule;
            stageInfo.pName = s->desc.entryName.c_str();
            group.anyHitShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        if(hitGroup.intersectionShader){
            Shader* s = checked_cast<Shader*>(hitGroup.intersectionShader.get());
            VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            stageInfo.stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
            stageInfo.module = s->shaderModule;
            stageInfo.pName = s->desc.entryName.c_str();
            group.intersectionShader = static_cast<u32>(stages.size());
            stages.push_back(stageInfo);
        }
        groups.push_back(group);
    }
    
    // Get pipeline layout from binding layouts
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if(!desc.globalBindingLayouts.empty() && desc.globalBindingLayouts[0]){
        BindingLayout* layout = checked_cast<BindingLayout*>(desc.globalBindingLayouts[0].get());
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
    
    VkResult res = vkCreateRayTracingPipelinesKHR(m_context.device, VK_NULL_HANDLE, m_context.pipelineCache, 
                                                   1, &createInfo, m_context.allocationCallbacks, &pso->pipeline);
    
    if(res != VK_SUCCESS){
        delete pso;
        return nullptr;
    }
    
    // Get shader group handles for SBT
    u32 handleSize = m_context.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_context.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    u32 baseAlignment = m_context.rayTracingPipelineProperties.shaderGroupBaseAlignment;
    
    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);
    u32 groupCount = static_cast<u32>(groups.size());
    
    pso->shaderGroupHandles.resize(groupCount * handleSizeAligned);
    vkGetRayTracingShaderGroupHandlesKHR(m_context.device, pso->pipeline, 0, groupCount, 
                                          pso->shaderGroupHandles.size(), pso->shaderGroupHandles.data());
    
    return RayTracingPipelineHandle(pso, AdoptRef);
}

//-----------------------------------------------------------------------------
// ShaderTable
//-----------------------------------------------------------------------------

ShaderTable::ShaderTable(const VulkanContext& context, Device* device)
    : m_context(context)
    , m_device(device)
{}

ShaderTable::~ShaderTable() = default;

RayTracingShaderTableHandle RayTracingPipeline::createShaderTable(){
    ShaderTable* sbt = new ShaderTable(m_context, m_device);
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
    
    // Find shader group index and copy handle
    u32 groupIndex = findGroupIndex(exportName);
    
    void* mapped = m_device->mapBuffer(raygenBuffer.get(), CpuAccessMode::Write);
    if(mapped){
        NWB_MEMCPY(mapped, handleSizeAligned, pipeline->shaderGroupHandles.data() + groupIndex * handleSizeAligned, handleSize);
        m_device->unmapBuffer(raygenBuffer.get());
    }
}

int ShaderTable::addMissShader(const Name& exportName, IBindingSet* /*bindings*/){
    if(!pipeline || !m_device)
        return missCount++;
    
    u32 handleSize = m_context.rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_context.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    u32 baseAlignment = m_context.rayTracingPipelineProperties.shaderGroupBaseAlignment;
    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);
    
    u32 newCount = missCount + 1;
    u64 sbtSize = (static_cast<u64>(newCount) * handleSizeAligned + baseAlignment - 1) & ~(static_cast<u64>(baseAlignment) - 1);
    
    // Reallocate miss buffer
    BufferHandle newBuffer;
    allocateSBTBuffer(newBuffer, sbtSize);
    if(!newBuffer)
        return missCount++;
    
    void* mapped = m_device->mapBuffer(newBuffer.get(), CpuAccessMode::Write);
    if(mapped){
        // Copy existing entries
        if(missBuffer && missCount > 0){
            void* oldMapped = m_device->mapBuffer(missBuffer.get(), CpuAccessMode::Read);
            if(oldMapped){
                NWB_MEMCPY(mapped, missCount * handleSizeAligned, oldMapped, missCount * handleSizeAligned);
                m_device->unmapBuffer(missBuffer.get());
            }
        }
        
        // Copy new handle
        u32 groupIndex = findGroupIndex(exportName);
        u8* dst = static_cast<u8*>(mapped) + missCount * handleSizeAligned;
        NWB_MEMCPY(dst, handleSizeAligned, pipeline->shaderGroupHandles.data() + groupIndex * handleSizeAligned, handleSize);
        m_device->unmapBuffer(newBuffer.get());
    }
    
    missBuffer = newBuffer;
    missOffset = 0;
    return missCount++;
}

int ShaderTable::addHitGroup(const Name& exportName, IBindingSet* /*bindings*/){
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
        u8* dst = static_cast<u8*>(mapped) + hitCount * handleSizeAligned;
        NWB_MEMCPY(dst, handleSizeAligned, pipeline->shaderGroupHandles.data() + groupIndex * handleSizeAligned, handleSize);
        m_device->unmapBuffer(newBuffer.get());
    }
    
    hitBuffer = newBuffer;
    hitOffset = 0;
    return hitCount++;
}

int ShaderTable::addCallableShader(const Name& exportName, IBindingSet* /*bindings*/){
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
        u8* dst = static_cast<u8*>(mapped) + callableCount * handleSizeAligned;
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
        Buffer* buf = checked_cast<Buffer*>(raygenBuffer.get());
        return Object(buf->buffer);
    }
    return Object();
}

//-----------------------------------------------------------------------------
// CommandList - Ray Tracing
//-----------------------------------------------------------------------------

void CommandList::setRayTracingState(const RayTracingState& state){
    currentRayTracingState = state;
    
    if(!state.shaderTable)
        return;
    
    ShaderTable* sbt = checked_cast<ShaderTable*>(state.shaderTable);
    RayTracingPipeline* pipeline = sbt->pipeline;
    
    if(!pipeline)
        return;
    
    // Bind ray tracing pipeline
    vkCmdBindPipeline(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->pipeline);
    
    // Bind descriptor sets
    if(state.bindings.size() > 0 && pipeline->pipelineLayout != VK_NULL_HANDLE){
        for(usize i = 0; i < state.bindings.size(); i++){
            if(state.bindings[i]){
                BindingSet* bindingSet = checked_cast<BindingSet*>(state.bindings[i]);
                if(!bindingSet->descriptorSets.empty())
                    vkCmdBindDescriptorSets(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                        pipeline->pipelineLayout, static_cast<u32>(i),
                        static_cast<u32>(bindingSet->descriptorSets.size()),
                        bindingSet->descriptorSets.data(), 0, nullptr);
            }
        }
    }
}

//-----------------------------------------------------------------------------
// CommandList - Ray Tracing commands
//-----------------------------------------------------------------------------

void CommandList::buildBottomLevelAccelStruct(IRayTracingAccelStruct* _as, const RayTracingGeometryDesc* pGeometries, usize numGeometries, RayTracingAccelStructBuildFlags::Mask buildFlags){
    if(!_as || !pGeometries || numGeometries == 0)
        return;
    
    if(!m_context->extensions.KHR_acceleration_structure)
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
            const auto& triangles = geomDesc.geometryData.triangles;
            
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
            const auto& aabbs = geomDesc.geometryData.aabbs;
            
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
    vkGetAccelerationStructureBuildSizesKHR(m_context->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, 
                                             &buildInfo, primitiveCounts.data(), &sizeInfo);
    
    // Allocate scratch buffer
    BufferDesc scratchDesc;
    scratchDesc.byteSize = sizeInfo.buildScratchSize;
    scratchDesc.structStride = 1;
    scratchDesc.debugName = "AS_BuildScratch";
    
    BufferHandle scratchBuffer = m_device->createBuffer(scratchDesc);
    if(scratchBuffer){
        buildInfo.scratchData.deviceAddress = getBufferDeviceAddress(scratchBuffer.get());
        
        // Build acceleration structure
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfos = rangeInfos.data();
        vkCmdBuildAccelerationStructuresKHR(currentCmdBuf->cmdBuf, 1, &buildInfo, &pRangeInfos);
        
        // Track scratch buffer until command completes
        currentCmdBuf->referencedStagingBuffers.push_back(scratchBuffer);
    }
    
    // Track for compaction if requested
    if(buildFlags & RayTracingAccelStructBuildFlags::AllowCompaction)
        m_pendingCompactions.push_back(as);
    
    currentCmdBuf->referencedResources.push_back(_as);
}

void CommandList::compactBottomLevelAccelStructs(){
    if(!m_context->extensions.KHR_acceleration_structure)
        return;
    
    if(m_pendingCompactions.empty())
        return;
    
    for(auto& as : m_pendingCompactions){
        // Query compacted size
        vkCmdWriteAccelerationStructuresPropertiesKHR(
            currentCmdBuf->cmdBuf,
            1, &as->accelStruct,
            VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
            VK_NULL_HANDLE, 0);
        
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
    
    if(!m_context->extensions.KHR_acceleration_structure)
        return;
    
    AccelStruct* as = checked_cast<AccelStruct*>(_as);
    
    // Create instance buffer
    u64 instanceBufferSize = numInstances * sizeof(VkAccelerationStructureInstanceKHR);
    BufferDesc instanceBufferDesc;
    instanceBufferDesc.byteSize = instanceBufferSize;
    instanceBufferDesc.cpuAccess = CpuAccessMode::Write;
    instanceBufferDesc.isAccelStructBuildInput = true;
    instanceBufferDesc.debugName = "TLAS_InstanceBuffer";
    
    BufferHandle instanceBuffer = m_device->createBuffer(instanceBufferDesc);
    if(!instanceBuffer)
        return;
    
    // Fill instance data
    VkAccelerationStructureInstanceKHR* mappedInstances = 
        static_cast<VkAccelerationStructureInstanceKHR*>(m_device->mapBuffer(instanceBuffer.get(), CpuAccessMode::Write));
    
    if(mappedInstances){
        for(usize i = 0; i < numInstances; i++){
            const auto& inst = pInstances[i];
            VkAccelerationStructureInstanceKHR& vkInst = mappedInstances[i];
            
            // Copy transform (row-major 3x4)
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
            
            // Get BLAS device address
            AccelStruct* blas = checked_cast<AccelStruct*>(inst.bottomLevelAS);
            vkInst.accelerationStructureReference = blas ? blas->deviceAddress : 0;
        }
        
        m_device->unmapBuffer(instanceBuffer.get());
    }
    
    // Set up geometry for instances
    VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data.deviceAddress = getBufferDeviceAddress(instanceBuffer.get());
    
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
    vkGetAccelerationStructureBuildSizesKHR(m_context->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, 
                                             &buildInfo, &primitiveCount, &sizeInfo);
    
    // Allocate scratch buffer
    BufferDesc scratchDesc;
    scratchDesc.byteSize = sizeInfo.buildScratchSize;
    scratchDesc.structStride = 1;
    scratchDesc.debugName = "TLAS_BuildScratch";
    
    BufferHandle scratchBuffer = m_device->createBuffer(scratchDesc);
    if(scratchBuffer){
        buildInfo.scratchData.deviceAddress = getBufferDeviceAddress(scratchBuffer.get());
        
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

void CommandList::dispatchRays(const RayTracingDispatchRaysArguments& args){
    if(!m_context->extensions.KHR_ray_tracing_pipeline)
        return;
    
    // Get shader binding table regions from current ray tracing state
    RayTracingState& state = currentRayTracingState;
    
    if(!state.shaderTable)
        return;
    
    // Get SBT addresses from shader table
    ShaderTable* sbt = checked_cast<ShaderTable*>(state.shaderTable);
    
    VkStridedDeviceAddressRegionKHR raygenRegion = {};
    VkStridedDeviceAddressRegionKHR missRegion = {};
    VkStridedDeviceAddressRegionKHR hitRegion = {};
    VkStridedDeviceAddressRegionKHR callableRegion = {};
    
    u32 handleSize = m_context->rayTracingPipelineProperties.shaderGroupHandleSize;
    u32 handleAlignment = m_context->rayTracingPipelineProperties.shaderGroupHandleAlignment;
    u32 baseAlignment = m_context->rayTracingPipelineProperties.shaderGroupBaseAlignment;
    
    u32 handleSizeAligned = (handleSize + handleAlignment - 1) & ~(handleAlignment - 1);
    
    if(sbt->raygenBuffer){
        raygenRegion.deviceAddress = getBufferDeviceAddress(sbt->raygenBuffer.get(), sbt->raygenOffset);
        raygenRegion.stride = handleSizeAligned;
        raygenRegion.size = handleSizeAligned;
    }
    
    if(sbt->missBuffer){
        missRegion.deviceAddress = getBufferDeviceAddress(sbt->missBuffer.get(), sbt->missOffset);
        missRegion.stride = handleSizeAligned;
        missRegion.size = sbt->missCount * handleSizeAligned;
    }
    
    if(sbt->hitBuffer){
        hitRegion.deviceAddress = getBufferDeviceAddress(sbt->hitBuffer.get(), sbt->hitOffset);
        hitRegion.stride = handleSizeAligned;
        hitRegion.size = sbt->hitCount * handleSizeAligned;
    }
    
    if(sbt->callableBuffer){
        callableRegion.deviceAddress = getBufferDeviceAddress(sbt->callableBuffer.get(), sbt->callableOffset);
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
