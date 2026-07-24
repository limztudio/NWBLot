// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_descriptor_heap{
    // Defaults when GpuDescriptorHeapDesc leaves a capacity at 0. Clamped to device update-after-bind limits in
    // initialize().
    inline constexpr u32 s_DefaultResourceCapacity = 16384u;
    inline constexpr u32 s_DefaultSamplerCapacity = 2048u;

    // Non-sampler resource classes that share the one resource-heap register set (SampledImage..UniformBuffer).
    // Their per-class descriptor arrays each occupy the full resourceCapacity, so the total live in one set is this
    // many times the capacity - used to respect the aggregate maxPerStageUpdateAfterBindResources limit.
    inline constexpr u32 s_ResourceClassCount = 5u;

    // Canonical descriptor type each class writes as. write() forces item.type to this so the handle's class - not
    // the caller's factory choice - is authoritative (matters for StorageBuffer: structured/raw SRV+UAV all resolve
    // to one STORAGE_BUFFER descriptor, and writeDescriptorTable() matches on the exact ResourceType).
    ResourceType::Enum ClassToResourceType(const GpuDescriptorClass::Enum descriptorClass){
        switch(descriptorClass){
        case GpuDescriptorClass::SampledImage:  return ResourceType::Texture_SRV;
        case GpuDescriptorClass::StorageImage:  return ResourceType::Texture_UAV;
        case GpuDescriptorClass::SampledBuffer: return ResourceType::TypedBuffer_SRV;
        case GpuDescriptorClass::StorageBuffer: return ResourceType::StructuredBuffer_UAV;
        case GpuDescriptorClass::UniformBuffer: return ResourceType::ConstantBuffer;
        case GpuDescriptorClass::AccelStruct:   return ResourceType::RayTracingAccelStruct;
        case GpuDescriptorClass::Sampler:       return ResourceType::Sampler;
        default:                                return ResourceType::None;
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuDescriptorHeap::GpuDescriptorHeap(Device& device)
    : m_device(device)
    , m_context(device.m_context)
    , m_resourceSlots(device.m_context.objectArena)
    , m_samplerSlots(device.m_context.objectArena)
    , m_retired(device.m_context.objectArena)
{}
GpuDescriptorHeap::~GpuDescriptorHeap(){
    shutdown();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 GpuDescriptorHeap::getRegisterSlot(const GpuDescriptorClass::Enum descriptorClass){
    // Register-space binding numbers inside each table (must be added in ascending order in initialize() so the
    // highest-numbered binding is the VARIABLE_DESCRIPTOR_COUNT one). Sampler counts from 0 in its own table.
    switch(descriptorClass){
    case GpuDescriptorClass::SampledImage:  return NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE;
    case GpuDescriptorClass::StorageImage:  return NWB_BINDLESS_HEAP_BINDING_STORAGE_IMAGE;
    case GpuDescriptorClass::SampledBuffer: return NWB_BINDLESS_HEAP_BINDING_SAMPLED_BUFFER;
    case GpuDescriptorClass::StorageBuffer: return NWB_BINDLESS_HEAP_BINDING_STORAGE_BUFFER;
    case GpuDescriptorClass::UniformBuffer: return NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER;
    case GpuDescriptorClass::Sampler:       return NWB_BINDLESS_HEAP_BINDING_SAMPLER;
    default:                                return 0u;
    }
}

GpuDescriptorHeap::SlotAllocator& GpuDescriptorHeap::allocatorForClass(const GpuDescriptorClass::Enum descriptorClass){
    // All non-sampler classes share one global slot namespace (design 3.4): a slot index maps to exactly one
    // resource across every resource class, which is what makes a handle portable to the unified EXT heap later.
    return descriptorClass == GpuDescriptorClass::Sampler ? m_samplerSlots : m_resourceSlots;
}

DescriptorTable* GpuDescriptorHeap::tableForClass(const GpuDescriptorClass::Enum descriptorClass){
    return descriptorClass == GpuDescriptorClass::Sampler ? m_samplerTable.get() : m_resourceTable.get();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuDescriptorHeap::initialize(const GpuDescriptorHeapDesc& desc){
    using namespace __hidden_vulkan_descriptor_heap;

    if(m_initialized)
        return true;

    m_desc = desc;

    u32 resourceCapacity = desc.resourceCapacity > 0 ? desc.resourceCapacity : s_DefaultResourceCapacity;
    u32 samplerCapacity = desc.samplerCapacity > 0 ? desc.samplerCapacity : s_DefaultSamplerCapacity;

    // Clamp to the device's update-after-bind limits and log the effective caps (no silent truncation, design 12.3).
    auto indexingProps = VulkanDetail::MakeVkStruct<VkPhysicalDeviceDescriptorIndexingProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES);
    auto props2 = VulkanDetail::MakeVkStruct<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    props2.pNext = &indexingProps;
    vkGetPhysicalDeviceProperties2(m_context.physicalDevice, &props2);

    // SampledBuffer (uniform texel) counts under the sampled-image limit; StorageBuffer under the storage-buffer
    // limit - so these four set-level caps cover all five resource classes.
    u32 resourceLimit = indexingProps.maxDescriptorSetUpdateAfterBindSampledImages;
    resourceLimit = Min(resourceLimit, indexingProps.maxDescriptorSetUpdateAfterBindStorageImages);
    resourceLimit = Min(resourceLimit, indexingProps.maxDescriptorSetUpdateAfterBindStorageBuffers);
    resourceLimit = Min(resourceLimit, indexingProps.maxDescriptorSetUpdateAfterBindUniformBuffers);
    // Every resource class array is sized to resourceCapacity and they all live in one set, so the aggregate must
    // fit maxPerStageUpdateAfterBindResources.
    const u32 aggregateLimit = indexingProps.maxPerStageUpdateAfterBindResources / s_ResourceClassCount;
    resourceLimit = Min(resourceLimit, aggregateLimit);

    if(resourceCapacity > resourceLimit){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: GpuDescriptorHeap resource capacity {} exceeds device update-after-bind limit {}; clamping.")
            , resourceCapacity
            , resourceLimit
        );
        resourceCapacity = resourceLimit;
    }
    if(samplerCapacity > indexingProps.maxDescriptorSetUpdateAfterBindSamplers){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: GpuDescriptorHeap sampler capacity {} exceeds device update-after-bind limit {}; clamping.")
            , samplerCapacity
            , indexingProps.maxDescriptorSetUpdateAfterBindSamplers
        );
        samplerCapacity = indexingProps.maxDescriptorSetUpdateAfterBindSamplers;
    }
    if(resourceCapacity == 0u || samplerCapacity == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap cannot initialize: effective capacity is zero (resource={}, sampler={})")
            , resourceCapacity
            , samplerCapacity
        );
        return false;
    }

    // Backend selection: prefer Backend C (VK_EXT_descriptor_buffer) where the device advertises the extension and
    // the DescriptorBufferManager initialized its mapped segments; fall back to Backend A (descriptor indexing)
    // otherwise. The heap's two bindless layouts are each pure-class by construction (resource set / sampler set), so
    // they are always segment-coherent and the descriptor-buffer path serves them wholesale. When Backend C is
    // selected the layouts carry VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT, which makes them
    // descriptor-buffer-compatible so a heap-coupled pipeline that embeds them can opt into Backend C too.
    m_usesDescriptorBuffer =
        m_context.extensions.EXT_descriptor_buffer
        && m_context.descriptorBufferManager
        && m_context.descriptorBufferManager->isEnabled()
    ;

    // Resource table: one mutable-SRV/UAV/CBV bindless layout with one register space per non-sampler class, added
    // in ascending slot order (see getRegisterSlot).
    BindlessLayoutDesc resourceLayoutDesc;
    resourceLayoutDesc
        .setLayoutType(BindlessLayoutType::MutableSrvUavCbv)
        .setMaxCapacity(resourceCapacity)
        .setVisibility(ShaderType::All)
        .setDescriptorSetIndex(m_resourceSetIndex)   // reserved set 8 - createPipelineLayoutForBindingLayouts gap-fills 0..7
        .addRegisterSpace(BindingLayoutItem::Texture_SRV(getRegisterSlot(GpuDescriptorClass::SampledImage), resourceCapacity))
        .addRegisterSpace(BindingLayoutItem::Texture_UAV(getRegisterSlot(GpuDescriptorClass::StorageImage), resourceCapacity))
        .addRegisterSpace(BindingLayoutItem::TypedBuffer_SRV(getRegisterSlot(GpuDescriptorClass::SampledBuffer), resourceCapacity))
        .addRegisterSpace(BindingLayoutItem::StructuredBuffer_UAV(getRegisterSlot(GpuDescriptorClass::StorageBuffer), resourceCapacity))
        .addRegisterSpace(BindingLayoutItem::ConstantBuffer(getRegisterSlot(GpuDescriptorClass::UniformBuffer), resourceCapacity))
        .setUseDescriptorBuffer(m_usesDescriptorBuffer)
    ;

    m_resourceLayout = m_device.createBindlessLayout(resourceLayoutDesc);
    if(!m_resourceLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap failed to create resource bindless layout."));
        return false;
    }
    // On Backend C the layout should be descriptor-buffer-compatible (the resource set is pure-class); if the driver
    // downgraded it, abandon Backend C for the heap so the classic table path serves it instead of mixing.
    if(m_usesDescriptorBuffer && !m_resourceLayout->isDescriptorBufferCompatible()){
        // A pure MutableSrvUavCbv bindless set is always segment-coherent; a downgrade here is a driver/extension
        // problem, not a recoverable shape mismatch, so treat it as a hard init failure rather than silently mixing.
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap resource layout is not descriptor-buffer-compatible despite Backend C selection."));
        m_resourceLayout = nullptr;
        return false;
    }
    // Backend C stores descriptors as bytes in carved buffer blocks, so it needs no classic descriptor table; in fact a
    // DESCRIPTOR_BUFFER_BIT_EXT layout cannot back a classic descriptor set. Only Backend A allocates the tables here.
    if(!m_usesDescriptorBuffer){
        m_resourceTable = m_device.createDescriptorTable(m_resourceLayout);
        if(!m_resourceTable){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap failed to create resource descriptor table."));
            m_resourceLayout = nullptr;
            return false;
        }
    }

    // Sampler table: separate namespace, separate layout (samplers cannot share an array with sampled images).
    BindlessLayoutDesc samplerLayoutDesc;
    samplerLayoutDesc
        .setLayoutType(BindlessLayoutType::MutableSampler)
        .setMaxCapacity(samplerCapacity)
        .setVisibility(ShaderType::All)
        .setDescriptorSetIndex(m_samplerSetIndex)   // reserved set 9
        .addRegisterSpace(BindingLayoutItem::Sampler(getRegisterSlot(GpuDescriptorClass::Sampler), samplerCapacity))
        .setUseDescriptorBuffer(m_usesDescriptorBuffer)
    ;

    m_samplerLayout = m_device.createBindlessLayout(samplerLayoutDesc);
    if(!m_samplerLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap failed to create sampler bindless layout."));
        m_resourceTable = nullptr;
        m_resourceLayout = nullptr;
        return false;
    }
    if(m_usesDescriptorBuffer && !m_samplerLayout->isDescriptorBufferCompatible()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap sampler layout is not descriptor-buffer-compatible despite Backend C selection."));
        m_samplerLayout = nullptr;
        m_resourceTable = nullptr;
        m_resourceLayout = nullptr;
        return false;
    }
    if(!m_usesDescriptorBuffer){
        m_samplerTable = m_device.createDescriptorTable(m_samplerLayout);
        if(!m_samplerTable){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap failed to create sampler descriptor table."));
            m_samplerLayout = nullptr;
            m_resourceTable = nullptr;
            m_resourceLayout = nullptr;
            return false;
        }
    }

    // Backend C: carve one persistent block per segment sized to the driver-queried set block for each layout, and
    // cache each class's binding offset within it. The heap's descriptors are written into these blocks' mapped
    // memory via write() and never re-carved (the heap outlives any single frame). The blocks are freed in
    // shutdown(). Backend A instead persists its descriptors in the classic tables created above and binds via
    // vkCmdBindDescriptorSets, so no carve happens here on that path.
    if(m_usesDescriptorBuffer){
        const u32 offsetAlignmentBytes = VulkanDetail::GetDescriptorBufferOffsetAlignmentBytes(m_context);
        if(!initializeDescriptorBufferBlocks(offsetAlignmentBytes))
            return false;
    }

    m_resourceSlots.capacity = resourceCapacity;
    m_resourceSlots.nextFresh = 0u;
    m_samplerSlots.capacity = samplerCapacity;
    m_samplerSlots.nextFresh = 0u;
    m_frameCounter = 0u;
    m_initialized = true;

    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: GpuDescriptorHeap initialized ({}): resource capacity {}, sampler capacity {} (sets {}/{}).")
        , m_usesDescriptorBuffer ? NWB_TEXT("Backend C - descriptor buffer") : NWB_TEXT("Backend A - descriptor indexing")
        , resourceCapacity
        , samplerCapacity
        , m_resourceSetIndex
        , m_samplerSetIndex
    );
    return true;
}

void GpuDescriptorHeap::shutdown(){
    if(!m_initialized)
        return;

    // Backend C: return the two persistent heap blocks to the descriptor-buffer manager before dropping the layouts.
    // The manager outlives the heap (it is device-owned), so freeing here is safe and ordering-independent.
    if(m_usesDescriptorBuffer && m_context.descriptorBufferManager){
        if(m_resourceBufferBlock.valid())
            m_context.descriptorBufferManager->free(m_resourceBufferBlock);
        if(m_samplerBufferBlock.valid())
            m_context.descriptorBufferManager->free(m_samplerBufferBlock);
    }
    m_resourceBufferBlock = {};
    m_samplerBufferBlock = {};
    m_usesDescriptorBuffer = false;
    for(u32 i = 0; i < GpuDescriptorClass::kCount; ++i)
        m_classBufferOffset[i] = 0u;

    m_resourceTable = nullptr;
    m_samplerTable = nullptr;
    m_resourceLayout = nullptr;
    m_samplerLayout = nullptr;

    m_resourceSlots.freeList.clear();
    m_resourceSlots.capacity = 0u;
    m_resourceSlots.nextFresh = 0u;
    m_samplerSlots.freeList.clear();
    m_samplerSlots.capacity = 0u;
    m_samplerSlots.nextFresh = 0u;
    m_retired.clear();
    m_frameCounter = 0u;

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
    if(descriptorClass == GpuDescriptorClass::AccelStruct){
        // IsDescriptorHeapCompatibleType() rejects RayTracingAccelStruct on the descriptor-indexing backend.
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::allocate: AccelStruct class is unsupported on the descriptor-indexing backend (Backend A)."));
        return GpuDescriptorHandle::invalid();
    }

    ScopedLock lock(m_mutex);

    SlotAllocator& allocator = allocatorForClass(descriptorClass);
    u32 slot;
    if(!allocator.freeList.empty()){
        slot = allocator.freeList.back();
        allocator.freeList.pop_back();
    }
    else if(allocator.nextFresh < allocator.capacity){
        slot = allocator.nextFresh++;
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::allocate: {} namespace exhausted (capacity {}).")
            , descriptorClass == GpuDescriptorClass::Sampler ? NWB_TEXT("sampler") : NWB_TEXT("resource")
            , allocator.capacity
        );
        return GpuDescriptorHandle::invalid();
    }

    return GpuDescriptorHandle::make(descriptorClass, slot);
}

void GpuDescriptorHeap::free(const GpuDescriptorHandle handle){
    if(!handle.valid())
        return;
    if(handle.descriptorClass() == GpuDescriptorClass::AccelStruct)
        return; // never allocated on Backend A

    ScopedLock lock(m_mutex);
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
            allocatorForClass(retired.handle.descriptorClass()).freeList.push_back(retired.handle.slot());
        }
        else{
            m_retired[kept] = retired;
            ++kept;
        }
    }
    m_retired.resize(kept);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuDescriptorHeap::write(const GpuDescriptorHandle handle, const BindingSetItem& item){
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
    if(descriptorClass == GpuDescriptorClass::AccelStruct){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: AccelStruct class is unsupported on the descriptor-indexing backend (Backend A)."));
        return false;
    }
    if(descriptorClass >= GpuDescriptorClass::kCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: handle has invalid class {}."), static_cast<u32>(descriptorClass));
        return false;
    }

    // The handle's class is authoritative: force the register space (slot), the global index (arrayElement), and the
    // canonical descriptor type. The caller supplies only the resource and its view params (range/subresources).
    BindingSetItem writeItem = item;
    writeItem.slot = getRegisterSlot(descriptorClass);
    writeItem.arrayElement = handle.slot();
    writeItem.type = ClassToResourceType(descriptorClass);

    // Backend C: write the descriptor as bytes into the heap's persistent carved block at
    // block.offsetBytes + classBindingOffset + slotIndex*stride, exactly mirroring how createBindingSet lays out a
    // frozen set (blockOffset + bindingOffset + arrayElement*stride). The block was carved once in initialize() and is
    // never re-carved -- the heap is persistent, so every write() rewrites in place. Backend A keeps the classic table.
    if(m_usesDescriptorBuffer){
        return writeDescriptorBuffer(writeItem, descriptorClass);
    }

    DescriptorTable* table = tableForClass(descriptorClass);
    if(!table){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: no descriptor table for class {}."), static_cast<u32>(descriptorClass));
        return false;
    }

    return m_device.writeDescriptorTable(table, writeItem);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuDescriptorHeap::bind(CommandList& commandList, const VkPipelineBindPoint bindPoint, const VkPipelineLayout pipelineLayout){
    commandList.bindDescriptorHeap(*this, bindPoint, pipelineLayout);
}

void GpuDescriptorHeap::bindCompute(CommandList& commandList, const ComputePipeline& pipeline){
    // Backend C: bind the two persistent heap blocks at sets 8/9 via vkCmdSetDescriptorBufferOffsetsEXT against the
    // pipeline's descriptor-buffer layout (the segments are already bound by bindDescriptorBufferState once per
    // command buffer, but the heap binds its OWN offsets here, after setComputeState). Backend A binds the classic
    // descriptor tables at sets 8/9 via vkCmdBindDescriptorSets. m_pipelineLayout is a public member of the
    // PipelineBindingState base; reading it here keeps every Vk type out of the caller's translation unit.
    if(m_usesDescriptorBuffer){
        commandList.bindDescriptorBufferHeap(*this, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.m_pipelineLayout);
        return;
    }
    commandList.bindDescriptorHeap(*this, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.m_pipelineLayout);
}

void GpuDescriptorHeap::bindRayTracing(CommandList& commandList, const RayTracingPipeline& pipeline){
    // Same PipelineBindingState::m_pipelineLayout the RT dispatch binds its set-0 material table against (the mixed
    // classic+heap layout createPipelineLayoutForBindingLayouts builds when the descriptor-heap accelerator is off);
    // bind the heap tables at sets 8/9 against it at the ray-tracing bind point. Backend C routes through the
    // descriptor-buffer offset path, Backend A through classic descriptor sets.
    if(m_usesDescriptorBuffer){
        commandList.bindDescriptorBufferHeap(*this, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.m_pipelineLayout);
        return;
    }
    commandList.bindDescriptorHeap(*this, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline.m_pipelineLayout);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuDescriptorHeap::initializeDescriptorBufferBlocks(const u32 offsetAlignmentBytes){
    if(!m_context.descriptorBufferManager)
        return false;

    auto carve = [&](const BindingLayoutHandle& layout, DescriptorBufferSegment& outBlock, GpuDescriptorClass::Enum firstClass, u32 classCount) -> bool{
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
        // block.offsetBytes + m_classBufferOffset[class] + slot*stride.
        const auto& bindingOffsets = bindingLayout->getDescriptorBufferBindingOffsets();
        for(u32 c = 0; c < classCount; ++c){
            const GpuDescriptorClass::Enum cls = static_cast<GpuDescriptorClass::Enum>(static_cast<u32>(firstClass) + c);
            const auto it = bindingOffsets.find(getRegisterSlot(cls));
            if(it == bindingOffsets.end()){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap: descriptor-buffer layout has no offset for class {}."), static_cast<u32>(cls));
                return false;
            }
            m_classBufferOffset[static_cast<u32>(cls)] = it->second;
        }
        return true;
    };

    // Resource set holds the five non-sampler classes in ascending register-slot order; the sampler set holds Sampler.
    if(!carve(m_resourceLayout, m_resourceBufferBlock, GpuDescriptorClass::SampledImage, 5u))
        return false;
    if(!carve(m_samplerLayout, m_samplerBufferBlock, GpuDescriptorClass::Sampler, 1u))
        return false;

    return true;
}

bool GpuDescriptorHeap::writeDescriptorBuffer(const BindingSetItem& writeItem, const GpuDescriptorClass::Enum descriptorClass){
    if(!m_context.descriptorBufferManager)
        return false;

    const bool isSampler = (descriptorClass == GpuDescriptorClass::Sampler);
    const DescriptorBufferSegment& block = isSampler ? m_samplerBufferBlock : m_resourceBufferBlock;
    if(!block.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: heap block not carved for class {}."), static_cast<u32>(descriptorClass));
        return false;
    }

    const VkDescriptorType descriptorType = VulkanDetail::ConvertDescriptorType(writeItem.type);
    const u32 stride = m_context.descriptorBufferManager->getDescriptorStride(descriptorType);
    if(stride == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::writeDescriptorBuffer: zero stride for class {}."), static_cast<u32>(descriptorClass));
        return false;
    }

    const u32 baseOffset = block.offsetBytes + m_classBufferOffset[static_cast<u32>(descriptorClass)] + writeItem.arrayElement * stride;
    return m_context.descriptorBufferManager->writeDescriptor(writeItem, baseOffset, descriptorType);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

