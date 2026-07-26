// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_descriptor_heap{
    // Defaults when GpuDescriptorHeapDesc leaves a capacity at 0. Clamped to the descriptor-layout limits in
    // initialize().
    inline constexpr u32 s_DefaultResourceCapacity = 16384u;
    inline constexpr u32 s_DefaultSamplerCapacity = 2048u;

    // Non-sampler resource classes that share the one resource-heap register set. Their per-class descriptor arrays
    // each occupy the full resourceCapacity, so the total live in one set is this many times the capacity - used to
    // respect the aggregate per-stage descriptor-resource limits.
    //
    // Sampled-image dimensions are deliberately appended after Sampler in the public enum to preserve existing
    // handle tags, so do not rely on enum contiguity when iterating the resource classes below.
    inline constexpr u32 s_ResourceClassCount = 8u;
    // Four sampled-image arrays plus the uniform-texel-buffer array all count against Vulkan's sampled-image
    // descriptor limits.
    inline constexpr u32 s_SampledImageOrTexelClassCount = 5u;

    // The descriptor-buffer heap owns a distinct block per live TLAS generation. Two in-flight frames plus the
    // current replacement need at most three slots; keep one spare so a burst of capacity growth still preserves
    // immutable old blocks through the deferred-free quarantine.
    inline constexpr u32 s_AccelStructCapacity = s_MaxFramesInFlight + 2u;

    // Canonical descriptor type each class writes as. write() forces item.type to this so the handle's class - not
    // the caller's factory choice - is authoritative (matters for StorageBuffer: structured/raw SRV+UAV all resolve
    // to one STORAGE_BUFFER descriptor, and descriptor writes match on the exact ResourceType).
    ResourceType::Enum ClassToResourceType(const GpuDescriptorClass::Enum descriptorClass){
        switch(descriptorClass){
        case GpuDescriptorClass::SampledImage:  return ResourceType::Texture_SRV;
        case GpuDescriptorClass::StorageImage:  return ResourceType::Texture_UAV;
        case GpuDescriptorClass::SampledBuffer: return ResourceType::TypedBuffer_SRV;
        case GpuDescriptorClass::StorageBuffer: return ResourceType::StructuredBuffer_UAV;
        case GpuDescriptorClass::UniformBuffer: return ResourceType::ConstantBuffer;
        case GpuDescriptorClass::AccelStruct:   return ResourceType::RayTracingAccelStruct;
        case GpuDescriptorClass::Sampler:       return ResourceType::Sampler;
        case GpuDescriptorClass::SampledImage2DArray: return ResourceType::Texture_SRV;
        case GpuDescriptorClass::SampledImage3D: return ResourceType::Texture_SRV;
        case GpuDescriptorClass::SampledImage2DArrayUint: return ResourceType::Texture_SRV;
        default:                                return ResourceType::None;
        }
    }

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
    , m_resourceDescriptorResources(device.m_context.objectArena)
    , m_samplerDescriptorResources(device.m_context.objectArena)
    , m_resourceSlots(device.m_context.objectArena)
    , m_samplerSlots(device.m_context.objectArena)
    , m_accelStructSlots(device.m_context.objectArena)
    , m_retired(device.m_context.objectArena)
{}
GpuDescriptorHeap::~GpuDescriptorHeap(){
    shutdown();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 GpuDescriptorHeap::getRegisterSlot(const GpuDescriptorClass::Enum descriptorClass)const{
    // Register-space binding numbers inside each fixed-capacity descriptor-buffer layout. Sampler counts from 0 in
    // its own table.
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
    default:                                return 0u;
    }
}

GpuDescriptorHeap::SlotAllocator& GpuDescriptorHeap::allocatorForClass(const GpuDescriptorClass::Enum descriptorClass){
    // All ordinary non-sampler classes share one global slot namespace (design 3.4). Acceleration structures are
    // intentionally separate: each slot selects a fixed descriptor-buffer block at set 10 rather than an element
    // of the resource descriptor array.
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
        || handle.slot() >= m_accelStructSlots.liveSlots.size()
        || m_accelStructSlots.liveSlots[handle.slot()] == 0u
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

    auto& resources = handle.descriptorClass() == GpuDescriptorClass::Sampler
        ? m_samplerDescriptorResources
        : m_resourceDescriptorResources
    ;
    if(handle.slot() < resources.size())
        resources[handle.slot()].reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuDescriptorHeap::initialize(const GpuDescriptorHeapDesc& desc){
    using namespace __hidden_vulkan_descriptor_heap;

    if(m_initialized)
        return true;

    // A previous attempt may have failed after carving one descriptor-buffer block. shutdown() is deliberately
    // idempotent so a retry begins from a clean state instead of leaking that partial generation.
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

    // The renderer's global heap has no fallback. Fail initialization unless the descriptor-buffer manager is live
    // before any heap layout or slot state is created.
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

    // Descriptor-buffer layouts use fixed descriptor capacity. Clamp only against ordinary descriptor-layout limits
    // before creating the global heap layouts.
    const VkPhysicalDeviceLimits& limits = m_context.physicalDeviceProperties.limits;
    // The heap is visible to every shader stage. The sampled-image limit includes UNIFORM_TEXEL_BUFFER, so all five
    // sampled-image/texel arrays count together; every resource class also counts toward maxPerStageResources.
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

    // Resource descriptor-buffer block: one mutable-SRV/UAV/CBV bindless layout with one register space per
    // non-sampler class, added in ascending slot order (see getRegisterSlot).
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
    ;

    m_resourceLayout = m_device.createBindlessLayout(resourceLayoutDesc);
    if(!m_resourceLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap failed to create resource bindless layout."));
        return failInitialization();
    }
    // The resource set is pure-class, so a non-compatible result is an extension/driver failure rather than a
    // recoverable layout shape.
    if(!m_resourceLayout->isDescriptorBufferCompatible()){
        // A pure MutableSrvUavCbv bindless set is always segment-coherent; a downgrade here is a driver/extension
        // problem, not a recoverable shape mismatch, so treat it as a hard init failure rather than silently mixing.
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap resource layout is not descriptor-buffer-compatible despite descriptor-buffer initialization."));
        return failInitialization();
    }
    // Sampler descriptor-buffer block: separate namespace/layout because samplers cannot share an array with sampled
    // images.
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
    // TLAS surface. It is an immutable one-descriptor global-heap layout rather than a mutable HLSL descriptor
    // array: VK_EXT_descriptor_buffer encodes the AS address directly. Each allocated AS handle later receives its
    // own one-descriptor block and bindCompute/bindRayTracing selects that block at set 10.
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
        for(u32 slot = 0u; slot < s_AccelStructCapacity; ++slot)
            m_accelStructSlots.liveSlots.emplace_back(0u);
        m_accelStructBufferBlocks.resize(s_AccelStructCapacity);
        m_accelStructResources.resize(s_AccelStructCapacity);
    }

    // Carve one persistent block per segment sized to the driver-queried set block for each layout, and cache each
    // class's binding offset within it. The heap's descriptors are written into these blocks' mapped memory via
    // write() and never re-carved (the heap outlives any single frame). The blocks are freed in shutdown().
    const u32 offsetAlignmentBytes = m_context.descriptorBufferManager->getOffsetAlignmentBytes();
    if(!initializeDescriptorBufferBlocks(offsetAlignmentBytes))
        return failInitialization();

    m_resourceSlots.capacity = resourceCapacity;
    m_resourceSlots.nextFresh = 0u;
    m_resourceSlots.liveSlots.reserve(resourceCapacity);
    for(u32 slot = 0u; slot < resourceCapacity; ++slot)
        m_resourceSlots.liveSlots.emplace_back(0u);
    m_samplerSlots.capacity = samplerCapacity;
    m_samplerSlots.nextFresh = 0u;
    m_samplerSlots.liveSlots.reserve(samplerCapacity);
    for(u32 slot = 0u; slot < samplerCapacity; ++slot)
        m_samplerSlots.liveSlots.emplace_back(0u);
    m_resourceDescriptorResources.reserve(resourceCapacity);
    for(u32 slot = 0u; slot < resourceCapacity; ++slot){
        m_resourceDescriptorResources.emplace_back(
            nullptr,
            Handle<GraphicsResource>::deleter_type(&m_context.objectArena)
        );
    }
    m_samplerDescriptorResources.reserve(samplerCapacity);
    for(u32 slot = 0u; slot < samplerCapacity; ++slot){
        m_samplerDescriptorResources.emplace_back(
            nullptr,
            Handle<GraphicsResource>::deleter_type(&m_context.objectArena)
        );
    }
    m_frameCounter = 0u;
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

    // Return the persistent resource/sampler blocks and every per-generation TLAS block before dropping
    // layouts. This also handles a partially initialized heap after an allocation/layout failure. The manager outlives
    // the heap (it is device-owned), so freeing here is safe and ordering-independent.
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
    m_resourceDescriptorResources.clear();
    m_samplerDescriptorResources.clear();
    m_accelStructBufferBindingOffset = 0u;
    for(u32 i = 0; i < GpuDescriptorClass::kCount; ++i)
        m_classBufferOffset[i] = 0u;

    m_resourceLayout = nullptr;
    m_samplerLayout = nullptr;
    m_accelStructLayout = nullptr;

    m_resourceSlots.freeList.clear();
    m_resourceSlots.liveSlots.clear();
    m_resourceSlots.capacity = 0u;
    m_resourceSlots.nextFresh = 0u;
    m_samplerSlots.freeList.clear();
    m_samplerSlots.liveSlots.clear();
    m_samplerSlots.capacity = 0u;
    m_samplerSlots.nextFresh = 0u;
    m_accelStructSlots.freeList.clear();
    m_accelStructSlots.liveSlots.clear();
    m_accelStructSlots.capacity = 0u;
    m_accelStructSlots.nextFresh = 0u;
    m_retired.clear();
    m_frameCounter = 0u;
    m_desc = {};

    m_initialized = false;
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
    if(!allocator.freeList.empty()){
        slot = allocator.freeList.back();
        allocator.freeList.pop_back();
    }
    else if(allocator.nextFresh < allocator.capacity){
        slot = allocator.nextFresh++;
    }
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

    if(slot >= allocator.liveSlots.size() || allocator.liveSlots[slot] != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::allocate: slot allocator state is invalid for class {} slot {}.")
            , static_cast<u32>(descriptorClass)
            , slot
        );
        return GpuDescriptorHandle::invalid();
    }
    allocator.liveSlots[slot] = 1u;

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

    ScopedLock lock(m_mutex);
    SlotAllocator& allocator = allocatorForClass(handle.descriptorClass());
    if(handle.slot() >= allocator.liveSlots.size() || allocator.liveSlots[handle.slot()] == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::free rejected stale or already-retired handle {}."), handle.value);
        return;
    }
    allocator.liveSlots[handle.slot()] = 0u;
    m_retired.push_back(RetiredSlot{handle, m_frameCounter + s_MaxFramesInFlight});
}

void GpuDescriptorHeap::advanceFrame(){
    ScopedLock lock(m_mutex);

    ++m_frameCounter;

    // Return every slot whose quarantine has matured to its class's free list; compact the rest in place.
    usize kept = 0;
    for(usize i = 0; i < m_retired.size(); ++i){
        const RetiredSlot& retired = m_retired[i];
        if(retired.retireAtFrame <= m_frameCounter){
            releaseRetainedDescriptorResource(retired.handle);
            SlotAllocator& allocator = allocatorForClass(retired.handle.descriptorClass());
            if(retired.handle.slot() < allocator.liveSlots.size() && allocator.liveSlots[retired.handle.slot()] == 0u)
                allocator.freeList.push_back(retired.handle.slot());
            else{
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::advanceFrame found an invalid retired handle {}."), retired.handle.value);
            }
        }
        else{
            m_retired[kept] = retired;
            ++kept;
        }
    }
    m_retired.resize(kept);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuDescriptorHeap::write(const GpuDescriptorHandle handle, const DescriptorWriteItem& item){
    using namespace __hidden_vulkan_descriptor_heap;

    if(!m_initialized){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write called before initialize."));
        return false;
    }
    if(!handle.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write called with an invalid handle."));
        return false;
    }

    const GpuDescriptorClass::Enum descriptorClass = handle.descriptorClass();
    if(descriptorClass >= GpuDescriptorClass::kCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: handle has invalid class {}."), static_cast<u32>(descriptorClass));
        return false;
    }
    if(descriptorClass == GpuDescriptorClass::AccelStruct && !m_accelStructLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: AccelStruct requires the descriptor-buffer TLAS layout."));
        return false;
    }

    ScopedLock lock(m_mutex);
    SlotAllocator& allocator = allocatorForClass(descriptorClass);
    if(handle.slot() >= allocator.liveSlots.size() || allocator.liveSlots[handle.slot()] == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write rejected stale or retired handle {}."), handle.value);
        return false;
    }

    // The handle's class is authoritative: force the register space (slot), the global index (arrayElement), and the
    // canonical descriptor type. The caller supplies only the resource and its view params (range/subresources).
    DescriptorWriteItem writeItem = item;
    writeItem.slot = getRegisterSlot(descriptorClass);
    writeItem.arrayElement = handle.slot();
    writeItem.type = ClassToResourceType(descriptorClass);

    // Write the descriptor as bytes into the heap's persistent carved block at
    // block.offsetBytes + classBindingOffset + slotIndex*descriptorSize. The block was carved once in
    // initialize() and is never re-carved -- the heap is persistent, so every write() rewrites in place.
    if(descriptorClass == GpuDescriptorClass::AccelStruct){
        // A TLAS descriptor block is owned by its handle slot, so retain the AS until that slot's deferred-free
        // quarantine matures. Replacing a populated slot would mutate descriptor bytes an in-flight command buffer
        // may still reference; callers must allocate a fresh handle for a new TLAS generation instead.
        if(handle.slot() >= m_accelStructBufferBlocks.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: AccelStruct handle slot {} is out of range."), handle.slot());
            return false;
        }
        RayTracingAccelStructHandle& retained = m_accelStructResources[handle.slot()];
        if(retained && retained.get() != writeItem.resourceHandle){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: cannot replace a live AccelStruct descriptor slot; allocate a fresh handle."));
            return false;
        }
        if(!writeDescriptorBuffer(writeItem, descriptorClass))
            return false;
        if(!retained)
            retained = RayTracingAccelStructHandle(static_cast<RayTracingAccelStruct*>(writeItem.resourceHandle));
        return true;
    }

    // Persistent heap descriptors retain each resource until free()'s in-flight quarantine matures. Replacing a
    // populated slot would mutate a descriptor an older command buffer may still use;
    // callers must allocate a fresh handle for a different resource generation.
    auto& retainedResources = descriptorClass == GpuDescriptorClass::Sampler
        ? m_samplerDescriptorResources
        : m_resourceDescriptorResources
    ;
    if(handle.slot() >= retainedResources.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: descriptor slot {} is outside the retained-resource table."), handle.slot());
        return false;
    }
    GraphicsResource* const resource = static_cast<GraphicsResource*>(writeItem.resourceHandle);
    if(!resource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: resource is null for descriptor slot {}."), handle.slot());
        return false;
    }
    Handle<GraphicsResource>& retained = retainedResources[handle.slot()];
    if(retained && retained.get() != resource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: cannot replace a live descriptor slot; allocate a fresh handle."));
        return false;
    }

    if(!writeDescriptorBuffer(writeItem, descriptorClass))
        return false;
    if(!retained)
        retained = resource;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuDescriptorHeap::bindCompute(
    CommandList& commandList,
    const ComputePipeline& pipeline,
    const GpuDescriptorHandle accelStructHandle
){
    // Bind persistent resource/sampler blocks at sets 8/9 and, when supplied, the current TLAS generation's
    // immutable descriptor block at set 10. m_pipelineLayout is a public PipelineBindingState member so impl/
    // need not name Vk.
    commandList.bindDescriptorBufferHeap(*this, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.m_pipelineLayout, accelStructHandle);
}

void GpuDescriptorHeap::bindGraphics(CommandList& commandList, const GraphicsPipeline& pipeline){
    // The fullscreen deferred passes have no TLAS; the ordinary resource/sampler heap sets are identical to their
    // compute/RT siblings and must be selected only after setGraphicsState() has installed this pipeline layout.
    commandList.bindDescriptorBufferHeap(*this, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.m_pipelineLayout);
}

void GpuDescriptorHeap::bindGraphics(CommandList& commandList, const MeshletPipeline& pipeline){
    // Mesh shader pipelines share the graphics bind point but carry their own PipelineBindingState type. Keep the
    // overload here rather than exposing VkPipelineLayout above the Vulkan RHI boundary.
    commandList.bindDescriptorBufferHeap(*this, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.m_pipelineLayout);
}

void GpuDescriptorHeap::bindRayTracing(
    CommandList& commandList,
    const RayTracingPipeline& pipeline,
    const GpuDescriptorHandle accelStructHandle
){
    // Same PipelineBindingState::m_pipelineLayout the RT dispatch binds its set-0 material table against. The heap
    // routes through descriptor-buffer offsets, including the optional set-10 TLAS generation.
    commandList.bindDescriptorBufferHeap(*this, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.m_pipelineLayout, accelStructHandle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuDescriptorHeap::initializeDescriptorBufferBlocks(const u32 offsetAlignmentBytes){
    if(!m_context.descriptorBufferManager || !m_context.descriptorBufferManager->isEnabled())
        return false;

    auto carve = [&](const BindingLayoutHandle& layout, DescriptorBufferSegment& outBlock, const GpuDescriptorClass::Enum* classes, const u32 classCount) -> bool{
        const auto* bindingLayout = layout.get();
        if(!bindingLayout || !bindingLayout->isDescriptorBufferCompatible()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: bindless layout is not descriptor-buffer-compatible; cannot carve heap block."));
            return false;
        }
        const u32 setSizeBytes = bindingLayout->getDescriptorBufferSetSizeBytes();
        if(setSizeBytes == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: descriptor-buffer layout reports a zero set size."));
            return false;
        }
        const BindlessLayoutDesc* bindlessDesc = bindingLayout->getBindlessDesc();
        const u32 descriptorCount = bindlessDesc ? bindlessDesc->maxCapacity : 0u;
        if(descriptorCount == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: bindless layout has no descriptor capacity."));
            return false;
        }
        const DescriptorBufferSegment block = m_context.descriptorBufferManager->allocate(
            bindingLayout->getDescriptorBufferSegmentKind(),
            setSizeBytes,
            offsetAlignmentBytes
        );
        if(!block.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: failed to carve {}-byte descriptor-buffer heap block."), setSizeBytes);
            return false;
        }
        outBlock = block;
        // Cache each class's driver-queried binding offset within the set block; write() addresses a descriptor as
        // block.offsetBytes + m_classBufferOffset[class] + slot*descriptorSize.
        const auto& bindingOffsets = bindingLayout->getDescriptorBufferBindingOffsets();
        for(u32 c = 0; c < classCount; ++c){
            const GpuDescriptorClass::Enum cls = classes[c];
            const auto it = bindingOffsets.find(getRegisterSlot(cls));
            if(it == bindingOffsets.end()){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: descriptor-buffer layout has no offset for class {}."), static_cast<u32>(cls));
                return false;
            }
            const VkDescriptorType descriptorType = VulkanDetail::ConvertDescriptorType(__hidden_vulkan_descriptor_heap::ClassToResourceType(cls));
            const u32 descriptorSize = m_context.descriptorBufferManager->getDescriptorSize(descriptorType);
            const u64 requiredBytes = static_cast<u64>(descriptorCount) * descriptorSize;
            if(
                descriptorSize == 0u
                || it->second > setSizeBytes
                || requiredBytes > static_cast<u64>(setSizeBytes - it->second)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: descriptor-buffer binding range is invalid for class {}."), static_cast<u32>(cls));
                return false;
            }
            m_classBufferOffset[static_cast<u32>(cls)] = it->second;
        }
        return true;
    };

    // The public class enum intentionally keeps legacy tags stable, so the resource classes are listed explicitly
    // rather than assuming the non-sampler subset is contiguous. They stay in ascending register-slot order.
    static constexpr GpuDescriptorClass::Enum s_ResourceClasses[] = {
        GpuDescriptorClass::SampledImage,
        GpuDescriptorClass::StorageImage,
        GpuDescriptorClass::SampledBuffer,
        GpuDescriptorClass::StorageBuffer,
        GpuDescriptorClass::UniformBuffer,
        GpuDescriptorClass::SampledImage2DArray,
        GpuDescriptorClass::SampledImage3D,
        GpuDescriptorClass::SampledImage2DArrayUint
    };
    static constexpr GpuDescriptorClass::Enum s_SamplerClasses[] = {
        GpuDescriptorClass::Sampler
    };
    if(!carve(m_resourceLayout, m_resourceBufferBlock, s_ResourceClasses, static_cast<u32>(sizeof(s_ResourceClasses) / sizeof(s_ResourceClasses[0]))))
        return false;
    if(!carve(m_samplerLayout, m_samplerBufferBlock, s_SamplerClasses, static_cast<u32>(sizeof(s_SamplerClasses) / sizeof(s_SamplerClasses[0]))))
        return false;

    return true;
}

bool GpuDescriptorHeap::writeDescriptorBuffer(const DescriptorWriteItem& writeItem, const GpuDescriptorClass::Enum descriptorClass){
    if(!m_context.descriptorBufferManager || !m_context.descriptorBufferManager->isEnabled())
        return false;

    if(descriptorClass == GpuDescriptorClass::AccelStruct){
        if(!m_accelStructLayout || writeItem.arrayElement >= m_accelStructBufferBlocks.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: invalid TLAS descriptor slot {}."), writeItem.arrayElement);
            return false;
        }

        const u32 descriptorSize = m_context.descriptorBufferManager->getDescriptorSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
        const u32 setSizeBytes = m_accelStructLayout->getDescriptorBufferSetSizeBytes();
        if(
            descriptorSize == 0u
            || setSizeBytes == 0u
            || m_accelStructLayout->getDescriptorBufferSegmentKind() != DescriptorBufferSegmentKind::Resource
            || m_accelStructBufferBindingOffset > setSizeBytes
            || descriptorSize > setSizeBytes - m_accelStructBufferBindingOffset
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: TLAS layout footprint is invalid."));
            return false;
        }

        DescriptorBufferSegment& block = m_accelStructBufferBlocks[writeItem.arrayElement];
        if(!block.valid()){
            block = m_context.descriptorBufferManager->allocate(
                m_accelStructLayout->getDescriptorBufferSegmentKind(),
                setSizeBytes,
                m_context.descriptorBufferManager->getOffsetAlignmentBytes()
            );
            if(!block.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: failed to carve {}-byte TLAS block."), setSizeBytes);
                return false;
            }
        }

        if(
            block.kind != DescriptorBufferSegmentKind::Resource
            || m_accelStructBufferBindingOffset > block.sizeBytes
            || descriptorSize > block.sizeBytes - m_accelStructBufferBindingOffset
            || static_cast<u64>(block.offsetBytes) + m_accelStructBufferBindingOffset > UINT32_MAX
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: TLAS descriptor block is invalid."));
            return false;
        }

        return m_context.descriptorBufferManager->writeDescriptor(
            writeItem,
            block,
            static_cast<u32>(static_cast<u64>(block.offsetBytes) + m_accelStructBufferBindingOffset),
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
        );
    }

    const bool isSampler = (descriptorClass == GpuDescriptorClass::Sampler);
    const DescriptorBufferSegment& block = isSampler ? m_samplerBufferBlock : m_resourceBufferBlock;
    const DescriptorBufferSegmentKind::Enum expectedSegmentKind = isSampler
        ? DescriptorBufferSegmentKind::Sampler
        : DescriptorBufferSegmentKind::Resource
    ;
    if(!block.valid() || block.kind != expectedSegmentKind){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: heap block not carved for class {}."), static_cast<u32>(descriptorClass));
        return false;
    }

    const VkDescriptorType descriptorType = VulkanDetail::ConvertDescriptorType(writeItem.type);
    const u32 descriptorSize = m_context.descriptorBufferManager->getDescriptorSize(descriptorType);
    if(descriptorSize == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: zero descriptor size for class {}."), static_cast<u32>(descriptorClass));
        return false;
    }

    const u64 relativeOffsetBytes = static_cast<u64>(m_classBufferOffset[static_cast<u32>(descriptorClass)])
        + static_cast<u64>(writeItem.arrayElement) * descriptorSize
    ;
    if(
        relativeOffsetBytes > block.sizeBytes
        || descriptorSize > block.sizeBytes - relativeOffsetBytes
        || static_cast<u64>(block.offsetBytes) + relativeOffsetBytes > UINT32_MAX
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: descriptor range exceeds the carved block for class {}."), static_cast<u32>(descriptorClass));
        return false;
    }

    const u32 baseOffset = static_cast<u32>(static_cast<u64>(block.offsetBytes) + relativeOffsetBytes);
    return m_context.descriptorBufferManager->writeDescriptor(writeItem, block, baseOffset, descriptorType);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

