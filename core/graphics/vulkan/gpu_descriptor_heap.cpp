// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace{
    // Phase-1 defaults when GpuDescriptorHeapDesc leaves a capacity at 0. Clamped to device update-after-bind limits
    // in initialize(); real sizing is a Phase-2 concern.
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
    case GpuDescriptorClass::SampledImage:  return 0u;
    case GpuDescriptorClass::StorageImage:  return 1u;
    case GpuDescriptorClass::SampledBuffer: return 2u;
    case GpuDescriptorClass::StorageBuffer: return 3u;
    case GpuDescriptorClass::UniformBuffer: return 4u;
    case GpuDescriptorClass::Sampler:       return 0u;
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

    // Backend A - descriptor indexing. Resource table: one mutable-SRV/UAV/CBV bindless layout with one register
    // space per non-sampler class, added in ascending slot order (see getRegisterSlot).
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
    ;

    m_resourceLayout = m_device.createBindlessLayout(resourceLayoutDesc);
    if(!m_resourceLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap failed to create resource bindless layout."));
        return false;
    }
    m_resourceTable = m_device.createDescriptorTable(m_resourceLayout);
    if(!m_resourceTable){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap failed to create resource descriptor table."));
        m_resourceLayout = nullptr;
        return false;
    }

    // Sampler table: separate namespace, separate layout (samplers cannot share an array with sampled images).
    BindlessLayoutDesc samplerLayoutDesc;
    samplerLayoutDesc
        .setLayoutType(BindlessLayoutType::MutableSampler)
        .setMaxCapacity(samplerCapacity)
        .setVisibility(ShaderType::All)
        .setDescriptorSetIndex(m_samplerSetIndex)   // reserved set 9
        .addRegisterSpace(BindingLayoutItem::Sampler(getRegisterSlot(GpuDescriptorClass::Sampler), samplerCapacity))
    ;

    m_samplerLayout = m_device.createBindlessLayout(samplerLayoutDesc);
    if(!m_samplerLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap failed to create sampler bindless layout."));
        m_resourceTable = nullptr;
        m_resourceLayout = nullptr;
        return false;
    }
    m_samplerTable = m_device.createDescriptorTable(m_samplerLayout);
    if(!m_samplerTable){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap failed to create sampler descriptor table."));
        m_samplerLayout = nullptr;
        m_resourceTable = nullptr;
        m_resourceLayout = nullptr;
        return false;
    }

    m_resourceSlots.capacity = resourceCapacity;
    m_resourceSlots.nextFresh = 0u;
    m_samplerSlots.capacity = samplerCapacity;
    m_samplerSlots.nextFresh = 0u;
    m_frameCounter = 0u;
    m_initialized = true;

    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: GpuDescriptorHeap initialized (Backend A - descriptor indexing): resource capacity {}, sampler capacity {} (sets {}/{}).")
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
        // IsDescriptorHeapCompatibleType() rejects RayTracingAccelStruct; AS lands with the descriptor-buffer
        // backend in a later phase (see rhi/gpu_descriptor_heap.h).
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

    DescriptorTable* table = tableForClass(descriptorClass);
    if(!table){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap::write: no descriptor table for class {}."), static_cast<u32>(descriptorClass));
        return false;
    }

    // The handle's class is authoritative: force the register space (slot), the global index (arrayElement), and the
    // canonical descriptor type. The caller supplies only the resource and its view params (range/subresources).
    BindingSetItem writeItem = item;
    writeItem.slot = getRegisterSlot(descriptorClass);
    writeItem.arrayElement = handle.slot();
    writeItem.type = ClassToResourceType(descriptorClass);

    return m_device.writeDescriptorTable(table, writeItem);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuDescriptorHeap::bind(CommandList& commandList, const VkPipelineBindPoint bindPoint, const VkPipelineLayout pipelineLayout){
    commandList.bindDescriptorHeap(*this, bindPoint, pipelineLayout);
}

void GpuDescriptorHeap::bindCompute(CommandList& commandList, const ComputePipeline& pipeline){
    // m_pipelineLayout is a public member of the PipelineBindingState base; reading it here keeps every Vk type out of
    // the caller's translation unit.
    commandList.bindDescriptorHeap(*this, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.m_pipelineLayout);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
