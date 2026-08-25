// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_descriptor_heap{
    inline constexpr u32 s_DefaultResourceCapacity = 16384u;
    inline constexpr u32 s_DefaultSamplerCapacity = 2048u;

    // Non-sampler classes share one resource-heap register set.
    //
    // Public descriptor-class tags are non-contiguous for ABI compatibility.
    inline constexpr u32 s_ResourceClassCount = 9u;
    inline constexpr u32 s_SampledImageOrTexelClassCount = 6u;

    // Reserve TLAS blocks across in-flight generation replacement.
    inline constexpr u32 s_AccelStructCapacity = s_MaxFramesInFlight + 2u;

    bool IsBindlessHeapAbiValid(const GpuDescriptorHeapAbi& abi){
        if(!abi.valid())
            return false;
        if(
            abi.resourceSetIndex < s_MaxBindingLayouts
            || abi.samplerSetIndex < s_MaxBindingLayouts
            || abi.accelStructSetIndex < s_MaxBindingLayouts
            || abi.resourceSetIndex == abi.samplerSetIndex
            || abi.resourceSetIndex == abi.accelStructSetIndex
            || abi.samplerSetIndex == abi.accelStructSetIndex
        )
            return false;

        const u32 resourceBindings[s_ResourceClassCount] = {
            abi.sampledImageBinding,
            abi.storageImageBinding,
            abi.sampledBufferBinding,
            abi.storageBufferBinding,
            abi.uniformBufferBinding,
            abi.sampledImage2DArrayBinding,
            abi.sampledImage3DBinding,
            abi.sampledImage2DArrayUintBinding,
            abi.sampledImageCubeBinding,
        };
        for(u32 bindingIndex = 0u; bindingIndex < s_ResourceClassCount; ++bindingIndex){
            const u32 binding = resourceBindings[bindingIndex];
            if(
                binding >= s_MaxBindlessRegisterSpaces
                || (bindingIndex > 0u && resourceBindings[bindingIndex - 1u] >= binding)
            )
                return false;
        }
        return abi.samplerBinding < s_MaxBindlessRegisterSpaces && abi.accelStructBinding < s_MaxBindlessRegisterSpaces;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuDescriptorHeap::GpuDescriptorHeap(Device& device)
    : m_device(device)
    , m_context(device.m_context)
    , m_accelStructBufferBlocks(device.m_context.objectArena)
    , m_accelStructResources(device.m_context.objectArena)
    , m_resourceDescriptorBuffers(device.m_context.objectArena)
    , m_resourceDescriptorTextures(device.m_context.objectArena)
    , m_samplerDescriptorResources(device.m_context.objectArena)
    , m_resourceSlots(device.m_context.objectArena)
    , m_samplerSlots(device.m_context.objectArena)
    , m_accelStructSlots(device.m_context.objectArena)
    , m_retired(device.m_context.objectArena)
    , m_heapUses(device.m_context.objectArena)
{}
GpuDescriptorHeap::~GpuDescriptorHeap(){
    shutdown();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 GpuDescriptorHeap::getRegisterSlot(const GpuDescriptorClass::Enum descriptorClass)const{
    const GpuDescriptorHeapAbi& abi = m_desc.bindlessHeapAbi;
    switch(descriptorClass){
    case GpuDescriptorClass::SampledImage:  return abi.sampledImageBinding;
    case GpuDescriptorClass::StorageImage:  return abi.storageImageBinding;
    case GpuDescriptorClass::SampledBuffer: return abi.sampledBufferBinding;
    case GpuDescriptorClass::StorageBuffer: return abi.storageBufferBinding;
    case GpuDescriptorClass::UniformBuffer: return abi.uniformBufferBinding;
    case GpuDescriptorClass::AccelStruct:   return abi.accelStructBinding;
    case GpuDescriptorClass::Sampler:       return abi.samplerBinding;
    case GpuDescriptorClass::SampledImage2DArray: return abi.sampledImage2DArrayBinding;
    case GpuDescriptorClass::SampledImage3D: return abi.sampledImage3DBinding;
    case GpuDescriptorClass::SampledImage2DArrayUint: return abi.sampledImage2DArrayUintBinding;
    case GpuDescriptorClass::SampledImageCube: return abi.sampledImageCubeBinding;
    default:                                return 0u;
    }
}

GpuDescriptorHeap::SlotAllocator& GpuDescriptorHeap::allocatorForClass(const GpuDescriptorClass::Enum descriptorClass){
    // Ordinary resources share slots; TLAS selects immutable set-10 blocks.
    if(descriptorClass == GpuDescriptorClass::Sampler)
        return m_samplerSlots;
    if(descriptorClass == GpuDescriptorClass::AccelStruct)
        return m_accelStructSlots;
    return m_resourceSlots;
}

DescriptorBufferSegment GpuDescriptorHeap::getAccelStructBufferBlock(const GpuDescriptorHandle handle)const{
    ScopedLock lock(m_mutex);
    if(
        !m_initialized
        || !handle.valid()
        || handle.descriptorClass() != GpuDescriptorClass::AccelStruct
        || handle.slot() >= m_accelStructBufferBlocks.size()
        || handle.slot() >= m_accelStructResources.size()
        || handle.slot() >= m_accelStructSlots.liveSlots.size()
        || handle.slot() >= m_accelStructSlots.allocatedClasses.size()
        || m_accelStructSlots.liveSlots[handle.slot()] == 0u
        || m_accelStructSlots.allocatedClasses[handle.slot()] != static_cast<u8>(GpuDescriptorClass::AccelStruct)
        || !m_accelStructResources[handle.slot()]
    )
        return {};
    return m_accelStructBufferBlocks[handle.slot()];
}

void GpuDescriptorHeap::releaseAccelStructDescriptorBlock(const u32 slot){
    if(slot >= m_accelStructBufferBlocks.size())
        return;

    DescriptorBufferSegment& block = m_accelStructBufferBlocks[slot];
    if(block.valid() && m_context.descriptorBufferManager)
        m_context.descriptorBufferManager->free(block);
    block = {};
    if(slot < m_accelStructResources.size())
        m_accelStructResources[slot] = nullptr;
}

void GpuDescriptorHeap::releaseRetainedDescriptorResource(const GpuDescriptorHandle handle){
    if(handle.descriptorClass() == GpuDescriptorClass::AccelStruct){
        releaseAccelStructDescriptorBlock(handle.slot());
        return;
    }

    if(handle.descriptorClass() == GpuDescriptorClass::Sampler){
        if(handle.slot() < m_samplerDescriptorResources.size())
            m_samplerDescriptorResources[handle.slot()].reset();
        return;
    }

    if(handle.slot() < m_resourceDescriptorBuffers.size())
        m_resourceDescriptorBuffers[handle.slot()].reset();
    if(handle.slot() < m_resourceDescriptorTextures.size())
        m_resourceDescriptorTextures[handle.slot()].reset();
}

void GpuDescriptorHeap::trackCommandBufferUse(TrackedCommandBuffer& commandBuffer){
    ScopedLock lock(m_mutex);
    if(!m_initialized)
        return;

    for(GpuDescriptorHeap* trackedHeap : commandBuffer.m_referencedDescriptorHeaps){
        if(trackedHeap == this)
            return;
    }

    commandBuffer.m_referencedDescriptorHeaps.push_back(this);
    m_heapUses.push_back(HeapUse{ &commandBuffer, {}, ++m_lastHeapUseID });
}

void GpuDescriptorHeap::submitCommandBufferUse(
    TrackedCommandBuffer& commandBuffer,
    const QueueSubmissionToken submissionToken
){
    if(
        !submissionToken.valid()
        || !submissionToken.hasPhysicalQueueIdentity()
        || !m_device.matchesPhysicalQueueIdentity(
            GpuPhysicalQueueId{ submissionToken.physicalQueueIndex, submissionToken.deviceGeneration }
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap received an invalid physical command-buffer submission token."));
        return;
    }

    ScopedLock lock(m_mutex);
    for(HeapUse& heapUse : m_heapUses){
        if(heapUse.commandBuffer != &commandBuffer || heapUse.submissionToken.valid())
            continue;

        heapUse.submissionToken = submissionToken;
        return;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap could not resolve command-buffer heap use for accepted submission {}."), submissionToken.value);
}

void GpuDescriptorHeap::discardCommandBufferUse(TrackedCommandBuffer& commandBuffer){
    ScopedLock lock(m_mutex);
    for(HeapUse& heapUse : m_heapUses){
        if(heapUse.commandBuffer == &commandBuffer)
            heapUse.commandBuffer = nullptr;
    }
}

void GpuDescriptorHeap::collectRetired(){
    struct QueueCompletion{
        GpuPhysicalQueueId queue;
        u64 value = 0u;
    };
    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_DescriptorBindingArena);
    Vector<QueueCompletion, Alloc::ScratchArena> completions(scratchArena);

    {
        ScopedLock lock(m_mutex);
        completions.reserve(m_heapUses.size());
        for(const HeapUse& heapUse : m_heapUses){
            const QueueSubmissionToken& token = heapUse.submissionToken;
            if(!token.valid() || !token.hasPhysicalQueueIdentity())
                continue;

            const GpuPhysicalQueueId queue{ token.physicalQueueIndex, token.deviceGeneration };
            bool alreadyTracked = false;
            for(const QueueCompletion& completion : completions){
                if(completion.queue == queue){
                    alreadyTracked = true;
                    break;
                }
            }
            if(!alreadyTracked)
                completions.push_back(QueueCompletion{
                    .queue = queue,
                    .value = 0u,
                });
        }
    }

    for(QueueCompletion& completion : completions)
        completion.value = m_device.queueGetCompletedInstance(completion.queue);

    ScopedLock lock(m_mutex);
    const auto heapUseComplete = [&](const HeapUse& heapUse) -> bool {
        const QueueSubmissionToken& token = heapUse.submissionToken;
        if(!token.valid())
            return heapUse.commandBuffer == nullptr;

        if(!token.hasPhysicalQueueIdentity())
            return false;

        const GpuPhysicalQueueId queue{ token.physicalQueueIndex, token.deviceGeneration };
        for(const QueueCompletion& completion : completions){
            if(completion.queue == queue)
                return completion.value >= token.value;
        }
        return false;
    };

    usize keptRetired = 0u;
    for(usize retiredIndex = 0u; retiredIndex < m_retired.size(); ++retiredIndex){
        const RetiredSlot& retired = m_retired[retiredIndex];
        bool canRetire = true;
        for(const HeapUse& heapUse : m_heapUses){
            if(heapUse.id > retired.lastRequiredHeapUseID)
                continue;
            if(!heapUseComplete(heapUse)){
                canRetire = false;
                break;
            }
        }

        if(canRetire){
            SlotAllocator& allocator = allocatorForClass(retired.handle.descriptorClass());
            const u32 slot = retired.handle.slot();
            if(
                slot < allocator.liveSlots.size()
                && slot < allocator.allocatedClasses.size()
                && allocator.liveSlots[slot] == 0u
                && allocator.allocatedClasses[slot] == static_cast<u8>(retired.handle.descriptorClass())
            ){
                releaseRetainedDescriptorResource(retired.handle);
                allocator.allocatedClasses[slot] = static_cast<u8>(GpuDescriptorClass::kCount);
                allocator.freeList.push_back(retired.handle.slot());
            }
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::collectRetired found an invalid retired handle {}."), retired.handle.value);
            }
        }
        else{
            m_retired[keptRetired] = retired;
            ++keptRetired;
        }
    }
    m_retired.resize(keptRetired);

    usize keptHeapUses = 0u;
    for(usize heapUseIndex = 0u; heapUseIndex < m_heapUses.size(); ++heapUseIndex){
        const HeapUse& heapUse = m_heapUses[heapUseIndex];
        if(heapUseComplete(heapUse))
            continue;

        m_heapUses[keptHeapUses] = heapUse;
        ++keptHeapUses;
    }
    m_heapUses.resize(keptHeapUses);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuDescriptorHeap::initialize(const GpuDescriptorHeapDesc& desc){
    using namespace __hidden_vulkan_descriptor_heap;

    if(m_initialized)
        return true;

    // Idempotent shutdown recovers partially initialized descriptor blocks.
    shutdown();
    m_desc = desc;
    const auto failInitialization = [this](){
        shutdown();
        return false;
    };
    if(!IsBindlessHeapAbiValid(m_desc.bindlessHeapAbi)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap requires a complete, distinct high-set bindless ABI with ascending resource bindings."));
        return failInitialization();
    }

    if(
        !m_context.extensions.EXT_descriptor_buffer
        || !m_context.descriptorBufferManager
        || !m_context.descriptorBufferManager->isEnabled()
    ){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: GpuDescriptorHeap requires VK_EXT_descriptor_buffer and an initialized DescriptorBufferManager."));
        return failInitialization();
    }
    u32 resourceCapacity = desc.resourceCapacity > 0 ? desc.resourceCapacity : s_DefaultResourceCapacity;
    u32 samplerCapacity = desc.samplerCapacity > 0 ? desc.samplerCapacity : s_DefaultSamplerCapacity;

    const VkPhysicalDeviceLimits& limits = m_context.physicalDeviceProperties.limits;
    u32 resourceLimit = limits.maxDescriptorSetSampledImages / s_SampledImageOrTexelClassCount;
    resourceLimit = Min(resourceLimit, limits.maxDescriptorSetStorageImages);
    resourceLimit = Min(resourceLimit, limits.maxDescriptorSetStorageBuffers);
    resourceLimit = Min(resourceLimit, limits.maxDescriptorSetUniformBuffers);
    resourceLimit = Min(resourceLimit, limits.maxPerStageDescriptorSampledImages / s_SampledImageOrTexelClassCount);
    resourceLimit = Min(resourceLimit, limits.maxPerStageDescriptorStorageImages);
    resourceLimit = Min(resourceLimit, limits.maxPerStageDescriptorStorageBuffers);
    resourceLimit = Min(resourceLimit, limits.maxPerStageDescriptorUniformBuffers);
    resourceLimit = Min(resourceLimit, limits.maxPerStageResources / s_ResourceClassCount);

    const u32 samplerLimit = Min(limits.maxDescriptorSetSamplers, limits.maxPerStageDescriptorSamplers);

    if(resourceCapacity > resourceLimit){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: GpuDescriptorHeap resource capacity {} exceeds device descriptor-layout limit {}; clamping.")
            , resourceCapacity
            , resourceLimit
        );
        resourceCapacity = resourceLimit;
    }
    if(samplerCapacity > samplerLimit){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: GpuDescriptorHeap sampler capacity {} exceeds device descriptor-layout limit {}; clamping.")
            , samplerCapacity
            , samplerLimit
        );
        samplerCapacity = samplerLimit;
    }
    if(resourceCapacity == 0u || samplerCapacity == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap cannot initialize: effective capacity is zero (resource={}, sampler={})")
            , resourceCapacity
            , samplerCapacity
        );
        return failInitialization();
    }

    // Resource block contains bindless non-sampler classes in slot order.
    BindlessLayoutDesc resourceLayoutDesc;
    resourceLayoutDesc
        .setLayoutType(BindlessLayoutType::MutableSrvUavCbv)
        .setMaxCapacity(resourceCapacity)
        .setVisibility(ShaderType::All)
        .setDescriptorSetIndex(m_desc.bindlessHeapAbi.resourceSetIndex)
        .addRegisterSpace(BindingLayoutItem::Texture_SRV(getRegisterSlot(GpuDescriptorClass::SampledImage), resourceCapacity))
        .addRegisterSpace(BindingLayoutItem::Texture_UAV(getRegisterSlot(GpuDescriptorClass::StorageImage), resourceCapacity))
        .addRegisterSpace(BindingLayoutItem::TypedBuffer_SRV(getRegisterSlot(GpuDescriptorClass::SampledBuffer), resourceCapacity))
        .addRegisterSpace(BindingLayoutItem::StructuredBuffer_UAV(getRegisterSlot(GpuDescriptorClass::StorageBuffer), resourceCapacity))
        .addRegisterSpace(BindingLayoutItem::ConstantBuffer(getRegisterSlot(GpuDescriptorClass::UniformBuffer), resourceCapacity))
        .addRegisterSpace(BindingLayoutItem::Texture_SRV(getRegisterSlot(GpuDescriptorClass::SampledImage2DArray), resourceCapacity))
        .addRegisterSpace(BindingLayoutItem::Texture_SRV(getRegisterSlot(GpuDescriptorClass::SampledImage3D), resourceCapacity))
        .addRegisterSpace(BindingLayoutItem::Texture_SRV(getRegisterSlot(GpuDescriptorClass::SampledImage2DArrayUint), resourceCapacity))
        .addRegisterSpace(BindingLayoutItem::Texture_SRV(getRegisterSlot(GpuDescriptorClass::SampledImageCube), resourceCapacity))
    ;

    m_resourceLayout = m_device.createBindlessLayout(resourceLayoutDesc);
    if(!m_resourceLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap failed to create resource bindless layout."));
        return failInitialization();
    }
    if(!m_resourceLayout->isDescriptorBufferCompatible()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap resource layout is not descriptor-buffer-compatible despite descriptor-buffer initialization."));
        return failInitialization();
    }
    // Samplers require a separate descriptor-buffer layout.
    BindlessLayoutDesc samplerLayoutDesc;
    samplerLayoutDesc
        .setLayoutType(BindlessLayoutType::MutableSampler)
        .setMaxCapacity(samplerCapacity)
        .setVisibility(ShaderType::All)
        .setDescriptorSetIndex(m_desc.bindlessHeapAbi.samplerSetIndex)
        .addRegisterSpace(BindingLayoutItem::Sampler(getRegisterSlot(GpuDescriptorClass::Sampler), samplerCapacity))
    ;

    m_samplerLayout = m_device.createBindlessLayout(samplerLayoutDesc);
    if(!m_samplerLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap failed to create sampler bindless layout."));
        return failInitialization();
    }
    if(!m_samplerLayout->isDescriptorBufferCompatible()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap sampler layout is not descriptor-buffer-compatible despite descriptor-buffer initialization."));
        return failInitialization();
    }
    // TLAS uses immutable one-descriptor generation blocks at set 10.
    if(m_context.extensions.KHR_acceleration_structure){
        BindlessLayoutDesc accelStructLayoutDesc;
        accelStructLayoutDesc
            .setLayoutType(BindlessLayoutType::Immutable)
            .setMaxCapacity(1u)
            .setVisibility(ShaderType::All)
            .setDescriptorSetIndex(m_desc.bindlessHeapAbi.accelStructSetIndex)
            .addRegisterSpace(BindingLayoutItem::RayTracingAccelStruct(getRegisterSlot(GpuDescriptorClass::AccelStruct), 1u))
        ;
        m_accelStructLayout = m_device.createBindlessLayout(accelStructLayoutDesc);
        if(!m_accelStructLayout || !m_accelStructLayout->isDescriptorBufferCompatible()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap failed to create the TLAS descriptor-buffer layout."));
            return failInitialization();
        }

        const auto& bindingOffsets = m_accelStructLayout->getDescriptorBufferBindingOffsets();
        const auto offsetIt = bindingOffsets.find(getRegisterSlot(GpuDescriptorClass::AccelStruct));
        if(offsetIt == bindingOffsets.end()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap TLAS descriptor-buffer layout has no binding offset."));
            return failInitialization();
        }
        m_accelStructBufferBindingOffset = offsetIt->second;
        m_accelStructSlots.capacity = s_AccelStructCapacity;
        m_accelStructSlots.nextFresh = 0u;
        m_accelStructSlots.liveSlots.reserve(s_AccelStructCapacity);
        m_accelStructSlots.allocatedClasses.reserve(s_AccelStructCapacity);
        for(u32 slot = 0u; slot < s_AccelStructCapacity; ++slot){
            m_accelStructSlots.liveSlots.emplace_back(0u);
            m_accelStructSlots.allocatedClasses.emplace_back(static_cast<u8>(GpuDescriptorClass::kCount));
        }
        m_accelStructBufferBlocks.resize(s_AccelStructCapacity);
        m_accelStructResources.reserve(s_AccelStructCapacity);
        for(u32 slot = 0u; slot < s_AccelStructCapacity; ++slot){
            m_accelStructResources.emplace_back(
                nullptr,
                RayTracingAccelStructHandle::deleter_type(&m_context.objectArena)
            );
        }
    }

    // Carve persistent mapped resource and sampler blocks once.
    const u32 offsetAlignmentBytes = m_context.descriptorBufferManager->getOffsetAlignmentBytes();
    if(!initializeDescriptorBufferBlocks(offsetAlignmentBytes))
        return failInitialization();

    m_resourceSlots.capacity = resourceCapacity;
    m_resourceSlots.nextFresh = 0u;
    m_resourceSlots.liveSlots.reserve(resourceCapacity);
    m_resourceSlots.allocatedClasses.reserve(resourceCapacity);
    for(u32 slot = 0u; slot < resourceCapacity; ++slot){
        m_resourceSlots.liveSlots.emplace_back(0u);
        m_resourceSlots.allocatedClasses.emplace_back(static_cast<u8>(GpuDescriptorClass::kCount));
    }
    m_samplerSlots.capacity = samplerCapacity;
    m_samplerSlots.nextFresh = 0u;
    m_samplerSlots.liveSlots.reserve(samplerCapacity);
    m_samplerSlots.allocatedClasses.reserve(samplerCapacity);
    for(u32 slot = 0u; slot < samplerCapacity; ++slot){
        m_samplerSlots.liveSlots.emplace_back(0u);
        m_samplerSlots.allocatedClasses.emplace_back(static_cast<u8>(GpuDescriptorClass::kCount));
    }
    m_resourceDescriptorBuffers.reserve(resourceCapacity);
    m_resourceDescriptorTextures.reserve(resourceCapacity);
    for(u32 slot = 0u; slot < resourceCapacity; ++slot){
        m_resourceDescriptorBuffers.emplace_back(
            nullptr,
            BufferHandle::deleter_type(&m_context.objectArena)
        );
        m_resourceDescriptorTextures.emplace_back(
            nullptr,
            TextureHandle::deleter_type(&m_context.objectArena)
        );
    }
    m_samplerDescriptorResources.reserve(samplerCapacity);
    for(u32 slot = 0u; slot < samplerCapacity; ++slot){
        m_samplerDescriptorResources.emplace_back(
            nullptr,
            SamplerHandle::deleter_type(&m_context.objectArena)
        );
    }
    m_lastHeapUseID = 0u;
    m_initialized = true;

    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: GpuDescriptorHeap initialized (descriptor buffer): resource capacity {}, sampler capacity {} (sets {}/{}).")
        , resourceCapacity
        , samplerCapacity
        , m_desc.bindlessHeapAbi.resourceSetIndex
        , m_desc.bindlessHeapAbi.samplerSetIndex
    );
    return true;
}

void GpuDescriptorHeap::shutdown(){
    ScopedLock lock(m_mutex);

    for(HeapUse& heapUse : m_heapUses){
        if(!heapUse.commandBuffer)
            continue;

        auto& referencedHeaps = heapUse.commandBuffer->m_referencedDescriptorHeaps;
        for(usize heapIndex = 0u; heapIndex < referencedHeaps.size(); ++heapIndex){
            if(referencedHeaps[heapIndex] != this)
                continue;

            referencedHeaps.erase(referencedHeaps.begin() + heapIndex);
            break;
        }
        heapUse.commandBuffer = nullptr;
    }

    if(m_context.descriptorBufferManager){
        if(m_resourceBufferBlock.valid())
            m_context.descriptorBufferManager->free(m_resourceBufferBlock);
        if(m_samplerBufferBlock.valid())
            m_context.descriptorBufferManager->free(m_samplerBufferBlock);
        for(const DescriptorBufferSegment& block : m_accelStructBufferBlocks){
            if(block.valid())
                m_context.descriptorBufferManager->free(block);
        }
    }
    m_resourceBufferBlock = {};
    m_samplerBufferBlock = {};
    m_accelStructBufferBlocks.clear();
    m_accelStructResources.clear();
    m_resourceDescriptorBuffers.clear();
    m_resourceDescriptorTextures.clear();
    m_samplerDescriptorResources.clear();
    m_accelStructBufferBindingOffset = 0u;
    for(u32 i = 0; i < GpuDescriptorClass::kCount; ++i)
        m_classBufferOffset[i] = 0u;

    m_resourceLayout = nullptr;
    m_samplerLayout = nullptr;
    m_accelStructLayout = nullptr;

    m_resourceSlots.freeList.clear();
    m_resourceSlots.liveSlots.clear();
    m_resourceSlots.allocatedClasses.clear();
    m_resourceSlots.capacity = 0u;
    m_resourceSlots.nextFresh = 0u;
    m_samplerSlots.freeList.clear();
    m_samplerSlots.liveSlots.clear();
    m_samplerSlots.allocatedClasses.clear();
    m_samplerSlots.capacity = 0u;
    m_samplerSlots.nextFresh = 0u;
    m_accelStructSlots.freeList.clear();
    m_accelStructSlots.liveSlots.clear();
    m_accelStructSlots.allocatedClasses.clear();
    m_accelStructSlots.capacity = 0u;
    m_accelStructSlots.nextFresh = 0u;
    m_retired.clear();
    m_heapUses.clear();
    m_lastHeapUseID = 0u;
    m_desc = {};

    m_initialized = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuDescriptorHeapLifecycleStatistics GpuDescriptorHeap::lifecycleStatistics()const{
    ScopedLock lock(m_mutex);

    GpuDescriptorHeapLifecycleStatistics statistics;
    statistics.initialized = m_initialized;
    statistics.resourceCapacity = m_resourceSlots.capacity;
    statistics.samplerCapacity = m_samplerSlots.capacity;
    statistics.accelStructCapacity = m_accelStructSlots.capacity;
    statistics.pendingRetiredSlotCount = m_retired.size();

    for(const u8 liveSlot : m_resourceSlots.liveSlots){
        if(liveSlot != 0u)
            ++statistics.resourceLiveSlotCount;
    }
    for(const u8 liveSlot : m_samplerSlots.liveSlots){
        if(liveSlot != 0u)
            ++statistics.samplerLiveSlotCount;
    }
    for(const u8 liveSlot : m_accelStructSlots.liveSlots){
        if(liveSlot != 0u)
            ++statistics.accelStructLiveSlotCount;
    }

    for(const HeapUse& heapUse : m_heapUses){
        if(heapUse.submissionToken.valid())
            ++statistics.acceptedHeapUseCount;
        else if(heapUse.commandBuffer)
            ++statistics.unsubmittedHeapUseCount;
        else
            ++statistics.abandonedHeapUseCount;
    }
    return statistics;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuDescriptorHandle GpuDescriptorHeap::allocate(const GpuDescriptorClass::Enum descriptorClass){
    if(!m_initialized){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::allocate called before initialize."));
        return GpuDescriptorHandle::invalid();
    }
    if(descriptorClass >= GpuDescriptorClass::kCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::allocate called with invalid class {}."), static_cast<u32>(descriptorClass));
        return GpuDescriptorHandle::invalid();
    }
    if(descriptorClass == GpuDescriptorClass::AccelStruct && !m_accelStructLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::allocate: AccelStruct requires the descriptor-buffer TLAS layout."));
        return GpuDescriptorHandle::invalid();
    }

    ScopedLock lock(m_mutex);

    SlotAllocator& allocator = allocatorForClass(descriptorClass);
    u32 slot = Limit<u32>::s_Max;
    bool recycled = false;
    if(!allocator.freeList.empty()){
        slot = allocator.freeList.back();
        recycled = true;
    }
    else if(allocator.nextFresh < allocator.capacity)
        slot = allocator.nextFresh;
    else{
        const tchar* const namespaceName = descriptorClass == GpuDescriptorClass::Sampler
            ? NWB_TEXT("sampler")
            : (descriptorClass == GpuDescriptorClass::AccelStruct ? NWB_TEXT("accel-struct") : NWB_TEXT("resource"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::allocate: {} namespace exhausted (capacity {}).")
            , namespaceName
            , allocator.capacity
        );
        return GpuDescriptorHandle::invalid();
    }

    if(
        slot >= allocator.liveSlots.size()
        || slot >= allocator.allocatedClasses.size()
        || allocator.liveSlots[slot] != 0u
        || allocator.allocatedClasses[slot] != static_cast<u8>(GpuDescriptorClass::kCount)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::allocate: slot allocator state is invalid for class {} slot {}.")
            , static_cast<u32>(descriptorClass)
            , slot
        );
        return GpuDescriptorHandle::invalid();
    }
    if(recycled)
        allocator.freeList.pop_back();
    else
        ++allocator.nextFresh;
    allocator.liveSlots[slot] = 1u;
    allocator.allocatedClasses[slot] = static_cast<u8>(descriptorClass);

    return GpuDescriptorHandle::make(descriptorClass, slot);
}

void GpuDescriptorHeap::free(const GpuDescriptorHandle handle){
    if(!m_initialized)
        return;
    if(!handle.valid())
        return;
    if(handle.descriptorClass() >= GpuDescriptorClass::kCount)
        return;
    if(handle.descriptorClass() == GpuDescriptorClass::AccelStruct && !m_accelStructLayout)
        return;

    {
        ScopedLock lock(m_mutex);
        SlotAllocator& allocator = allocatorForClass(handle.descriptorClass());
        if(
            handle.slot() >= allocator.liveSlots.size()
            || handle.slot() >= allocator.allocatedClasses.size()
            || allocator.liveSlots[handle.slot()] == 0u
            || allocator.allocatedClasses[handle.slot()] != static_cast<u8>(handle.descriptorClass())
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::free rejected stale, retagged, or already-retired handle {}.")
                , handle.value
            );
            return;
        }
        allocator.liveSlots[handle.slot()] = 0u;
        m_retired.push_back(RetiredSlot{ handle, m_lastHeapUseID });
    }

    collectRetired();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

