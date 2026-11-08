// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "resource_bindings_detail.h"

#include <core/common/log.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_resource_bindings_descriptor_buffer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_ResourceSegmentDebugName = VulkanDefaults::s_VulkanDefaultResourceSegmentDebugName;
static constexpr AStringView s_SamplerSegmentDebugName = VulkanDefaults::s_VulkanDefaultSamplerSegmentDebugName;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DescriptorBufferManager::DescriptorBufferManager(Device& device, const VulkanContext& context, VulkanAllocator& allocator)noexcept
    : m_device(device)
    , m_context(context)
    , m_allocator(allocator)
    , m_resourceSegment(context.objectArena, VulkanDetail::AllocateDescriptorBufferStorageIdentity())
    , m_samplerSegment(context.objectArena, VulkanDetail::AllocateDescriptorBufferStorageIdentity())
{}

b8 DescriptorBufferManager::initialize(){
    UniqueLock<Futex> operationLock(m_lifecycleOperationMutex);

    const auto idleResult = shutdownForLifecycleOperation();
    if(!idleResult)
        return false;
    if(*idleResult == VK_ERROR_DEVICE_LOST){
        operationLock.unlock();
        m_device.captureDeviceLoss("descriptor-buffer initialization idle");
        return false;
    }

    ScopedLock lifecycleLock(m_lifecycleMutex);
    if(m_lifecycleTransitioning){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor-buffer initialization rejected during another lifecycle transition."));
        return false;
    }
    m_lifecycleTransitioning = true;
    const auto failInitialization = [this]()noexcept{
        shutdownSegment(m_resourceSegment);
        shutdownSegment(m_samplerSegment);
        m_bindingGeneration = 0u;
        m_enabled = false;
        m_lifecycleTransitioning = false;
        return false;
    };

    if(!VulkanDetail::HasDescriptorBufferStartupPrerequisites(VulkanDetail::QueryDescriptorBufferStartupPrerequisites(m_context))){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor-buffer startup prerequisites are unavailable."));
        return failInitialization();
    }
    if(!m_resourceSegment.storageIdentity || !m_samplerSegment.storageIdentity){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor-buffer storage identity space is exhausted."));
        return failInitialization();
    }
    if(m_nextBindingGeneration == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor-buffer binding generation space is exhausted."));
        return failInitialization();
    }

    const auto& props = m_context.descriptorBufferProperties;

    // Initial working sizes; the first heap reserves larger driver-reported layouts before any allocation is published.
    constexpr u32 s_TargetResourceSegmentBytes = 32u * 1024u * 1024u;
    constexpr u32 s_TargetSamplerSegmentBytes = 2u * 1024u * 1024u;

    if(props.descriptorBufferOffsetAlignment == 0 || props.descriptorBufferOffsetAlignment > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer offset alignment is outside the supported 32-bit range."));
        return failInitialization();
    }

    const u32 offsetAlignment = static_cast<u32>(props.descriptorBufferOffsetAlignment);
    if(props.maxDescriptorBufferBindings < DescriptorBufferManager::s_PersistentDescriptorBufferCount || props.maxResourceDescriptorBufferBindings == 0u || props.maxSamplerDescriptorBufferBindings == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer limits cannot bind the required resource and sampler segments."));
        return failInitialization();
    }

    const VkDeviceSize resourceMaxBytes = Min<VkDeviceSize>(props.resourceDescriptorBufferAddressSpaceSize, props.maxResourceDescriptorBufferRange);
    const VkDeviceSize samplerMaxBytes = Min<VkDeviceSize>(props.samplerDescriptorBufferAddressSpaceSize, props.maxSamplerDescriptorBufferRange);

    const auto makeCapacity = [&](const VkDeviceSize maximumBytes, const u32 targetBytes)noexcept -> Expected<u32>{
        const VkDeviceSize cappedBytes = Min<VkDeviceSize>(maximumBytes, targetBytes);
        const VkDeviceSize alignedBytes = cappedBytes - (cappedBytes % offsetAlignment);
        if(alignedBytes == 0u)
            return MakeUnexpected(Failure{});
        return static_cast<u32>(alignedBytes);
    };

    const auto resourceCapacityBytes = makeCapacity(resourceMaxBytes, s_TargetResourceSegmentBytes);
    const auto samplerCapacityBytes = makeCapacity(samplerMaxBytes, s_TargetSamplerSegmentBytes);
    if(!resourceCapacityBytes || !samplerCapacityBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer properties do not allow aligned global segments."));
        return failInitialization();
    }

    const VkDeviceSize totalCapacityBytes = static_cast<VkDeviceSize>(*resourceCapacityBytes) + *samplerCapacityBytes;
    if(totalCapacityBytes > props.descriptorBufferAddressSpaceSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer global address space {} cannot hold the requested {} bytes of resource and sampler segments."), props.descriptorBufferAddressSpaceSize, totalCapacityBytes);
        return failInitialization();
    }

    if(!initializeSegment(m_resourceSegment, __hidden_vulkan_resource_bindings_descriptor_buffer::s_ResourceSegmentDebugName, *resourceCapacityBytes))
        return failInitialization();

    if(!initializeSegment(m_samplerSegment, __hidden_vulkan_resource_bindings_descriptor_buffer::s_SamplerSegmentDebugName, *samplerCapacityBytes))
        return failInitialization();

    m_bindingGeneration = m_nextBindingGeneration;
    m_nextBindingGeneration = m_bindingGeneration == UINT64_MAX ? 0u : m_bindingGeneration + 1u;
    m_enabled = true;
    m_lifecycleTransitioning = false;
    return true;
}

b8 DescriptorBufferManager::shutdown(){
    UniqueLock<Futex> operationLock(m_lifecycleOperationMutex);

    const auto result = shutdownForLifecycleOperation();
    operationLock.unlock();
    if(result && *result == VK_ERROR_DEVICE_LOST)
        m_device.captureDeviceLoss("descriptor-buffer shutdown idle");
    return result.has_value();
}

b8 DescriptorBufferManager::isEnabled()const noexcept{
    ScopedLock lifecycleLock(m_lifecycleMutex);

    return m_enabled && !m_lifecycleTransitioning && m_bindingGeneration != 0u;
}

u32 DescriptorBufferManager::getDescriptorSize(const VkDescriptorType descriptorType)const noexcept{
    ScopedLock lifecycleLock(m_lifecycleMutex);

    return VulkanDetail::GetDescriptorSize(m_context, m_enabled, descriptorType);
}

u32 DescriptorBufferManager::getOffsetAlignmentBytes()const noexcept{
    return VulkanDetail::GetDescriptorBufferOffsetAlignmentBytes(m_context);
}

Expected<VkResult, VkResult> DescriptorBufferManager::shutdownForLifecycleOperation(){
    VkResult idleResult = VK_SUCCESS;
    {
        ScopedLock lifecycleLock(m_lifecycleMutex);
        if(m_lifecycleTransitioning){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor-buffer shutdown rejected during another lifecycle transition."));
            return MakeUnexpected(idleResult == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : idleResult);
        }
        if(m_resourceSegment.buffer == VK_NULL_HANDLE && m_samplerSegment.buffer == VK_NULL_HANDLE && !m_resourceSegment.allocation && !m_samplerSegment.allocation){
            m_bindingGeneration = 0u;
            m_enabled = false;
            return idleResult;
        }

        m_lifecycleTransitioning = true;
        m_bindingGeneration = 0u;
        m_enabled = false;
    }

    idleResult = m_device.waitForNativeIdle();
    if(idleResult == VK_ERROR_DEVICE_LOST)
        m_device.markDeviceLost();
    if(idleResult != VK_SUCCESS && idleResult != VK_ERROR_DEVICE_LOST){
        {
            ScopedLock lifecycleLock(m_lifecycleMutex);

            m_lifecycleTransitioning = false;
        }
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor-buffer shutdown is refusing to destroy storage after device-idle wait failed."));
        return MakeUnexpected(idleResult == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : idleResult);
    }

    ScopedLock lifecycleLock(m_lifecycleMutex);
    ScopedLock resourceLock(m_resourceSegment.mutex);
    ScopedLock samplerLock(m_samplerSegment.mutex);

    shutdownSegment(m_resourceSegment);
    shutdownSegment(m_samplerSegment);
    m_lifecycleTransitioning = false;
    return idleResult;
}

void DescriptorBufferManager::shutdownForDeviceTeardown()noexcept{
    NothrowScopedLock operationLock(m_lifecycleOperationMutex);
    NothrowScopedLock lifecycleLock(m_lifecycleMutex);
    NothrowScopedLock resourceLock(m_resourceSegment.mutex);
    NothrowScopedLock samplerLock(m_samplerSegment.mutex);

    m_bindingGeneration = 0u;
    m_enabled = false;
    shutdownSegment(m_resourceSegment);
    shutdownSegment(m_samplerSegment);
    m_lifecycleTransitioning = false;
}

b8 DescriptorBufferManager::reserveInitialCapacityForBindingGeneration(const u32 resourceBytes, const u32 samplerBytes, const u64 requiredGeneration){
    ScopedLock lifecycleLock(m_lifecycleMutex);
    if(!m_enabled || m_lifecycleTransitioning || requiredGeneration == 0u || requiredGeneration != m_bindingGeneration)
        return false;

    const u32 alignmentBytes = getOffsetAlignmentBytes();
    const auto resourceCapacityBytes = AlignUpChecked<u32>(Max(resourceBytes, m_resourceSegment.capacityBytes), alignmentBytes);
    const auto samplerCapacityBytes = AlignUpChecked<u32>(Max(samplerBytes, m_samplerSegment.capacityBytes), alignmentBytes);
    if(!resourceCapacityBytes || !samplerCapacityBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Initial descriptor-buffer layout capacity exceeds aligned 32-bit storage."));
        return false;
    }
    if(*resourceCapacityBytes == m_resourceSegment.capacityBytes && *samplerCapacityBytes == m_samplerSegment.capacityBytes)
        return true;

    // A freed allocation still leaves a high-water mark, so no recorded address can survive this startup-only replacement.
    ScopedLock resourceLock(m_resourceSegment.mutex);
    ScopedLock samplerLock(m_samplerSegment.mutex);
    if(m_resourceSegment.allocations.writableOffsetBytes() != 0u || m_samplerSegment.allocations.writableOffsetBytes() != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor-buffer capacity cannot change after allocations have been published."));
        return false;
    }

    const auto& props = m_context.descriptorBufferProperties;
    const VkDeviceSize resourceMaxBytes = Min<VkDeviceSize>(props.resourceDescriptorBufferAddressSpaceSize, props.maxResourceDescriptorBufferRange);
    const VkDeviceSize samplerMaxBytes = Min<VkDeviceSize>(props.samplerDescriptorBufferAddressSpaceSize, props.maxSamplerDescriptorBufferRange);
    const VkDeviceSize totalBytes = static_cast<VkDeviceSize>(*resourceCapacityBytes) + *samplerCapacityBytes;
    if(*resourceCapacityBytes > resourceMaxBytes || *samplerCapacityBytes > samplerMaxBytes || totalBytes > props.descriptorBufferAddressSpaceSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Initial descriptor-buffer layouts require {} resource and {} sampler bytes, exceeding device segment or address-space limits."), *resourceCapacityBytes, *samplerCapacityBytes);
        return false;
    }

    const auto failReservation = [this]()noexcept{
        shutdownSegment(m_resourceSegment);
        shutdownSegment(m_samplerSegment);
        m_bindingGeneration = 0u;
        m_enabled = false;
        return false;
    };
    if(*resourceCapacityBytes != m_resourceSegment.capacityBytes && !initializeSegment(m_resourceSegment, __hidden_vulkan_resource_bindings_descriptor_buffer::s_ResourceSegmentDebugName, *resourceCapacityBytes))
        return failReservation();
    if(*samplerCapacityBytes != m_samplerSegment.capacityBytes && !initializeSegment(m_samplerSegment, __hidden_vulkan_resource_bindings_descriptor_buffer::s_SamplerSegmentDebugName, *samplerCapacityBytes))
        return failReservation();

    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Sized initial descriptor buffers to driver layouts (resource={} bytes, sampler={} bytes)."), *resourceCapacityBytes, *samplerCapacityBytes);
    return true;
}

Expected<DescriptorBufferSegment> DescriptorBufferManager::allocateForBindingGeneration(const DescriptorBufferSegmentKind::Enum kind, const u32 sizeBytes, const u32 alignmentBytes, const u64 requiredGeneration){
    ScopedLock lifecycleLock(m_lifecycleMutex);

    if(requiredGeneration != 0u && requiredGeneration != m_bindingGeneration)
        return MakeUnexpected(Failure{});
    if(!m_enabled || sizeBytes == 0)
        return MakeUnexpected(Failure{});
    if(kind != DescriptorBufferSegmentKind::Resource && kind != DescriptorBufferSegmentKind::Sampler){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer allocation rejected: invalid segment kind {}."), static_cast<u32>(kind));
        return MakeUnexpected(Failure{});
    }

    const u32 requiredAlignmentBytes = getOffsetAlignmentBytes();
    if(alignmentBytes == 0u || (alignmentBytes % requiredAlignmentBytes) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer allocation rejected: alignment {} is not a non-zero multiple of required alignment {}."), alignmentBytes, requiredAlignmentBytes);
        return MakeUnexpected(Failure{});
    }

    SegmentStorage& segment = kind == DescriptorBufferSegmentKind::Sampler ? m_samplerSegment : m_resourceSegment;
    NWB_ASSERT(segment.storageIdentity);
    ScopedLock lock(segment.mutex);
    return segment.allocations.allocate(kind, sizeBytes, alignmentBytes, *segment.storageIdentity, segment.capacityBytes, segment.mappedMemory);
}

void DescriptorBufferManager::freeForBindingGeneration(const DescriptorBufferSegment& segment, const u64 requiredGeneration){
    ScopedLock lifecycleLock(m_lifecycleMutex);

    if(requiredGeneration != 0u && requiredGeneration != m_bindingGeneration)
        return;
    if(!m_enabled || segment.sizeBytes == 0u)
        return;
    if(segment.kind != DescriptorBufferSegmentKind::Resource && segment.kind != DescriptorBufferSegmentKind::Sampler){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: invalid segment kind {}."), static_cast<u32>(segment.kind));
        return;
    }
    if(segment.storageIdentity == 0u || segment.allocationSerial == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: allocation identity is invalid."));
        return;
    }

    SegmentStorage& storage = segment.kind == DescriptorBufferSegmentKind::Sampler ? m_samplerSegment : m_resourceSegment;

    ScopedLock lock(storage.mutex);

    NWB_ASSERT(storage.storageIdentity);
    storage.allocations.free(segment, *storage.storageIdentity, storage.capacityBytes);
}

Expected<DescriptorBufferManager::BindingSnapshot> DescriptorBufferManager::captureBindingSnapshotLocked()const noexcept{
    BindingSnapshot snapshot;
    if(!m_enabled || m_lifecycleTransitioning || m_bindingGeneration == 0u)
        return MakeUnexpected(Failure{});

    // Enabled storage keeps both segments fully initialized; every segment rebuild disables storage on failure.
    NWB_ASSERT(
        m_resourceSegment.storageIdentity
        && m_samplerSegment.storageIdentity
        && *m_resourceSegment.storageIdentity != *m_samplerSegment.storageIdentity
        && m_resourceSegment.buffer != VK_NULL_HANDLE
        && m_samplerSegment.buffer != VK_NULL_HANDLE
        && m_resourceSegment.allocation
        && m_samplerSegment.allocation
        && m_resourceSegment.mappedMemory
        && m_samplerSegment.mappedMemory
        && m_resourceSegment.deviceAddress != 0u
        && m_samplerSegment.deviceAddress != 0u
        && m_resourceSegment.capacityBytes != 0u
        && m_samplerSegment.capacityBytes != 0u
        && m_resourceSegment.bindingInfo.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT
        && m_samplerSegment.bindingInfo.sType == VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT
        && m_resourceSegment.bindingInfo.address == m_resourceSegment.deviceAddress
        && m_samplerSegment.bindingInfo.address == m_samplerSegment.deviceAddress
        && m_resourceSegment.bindingInfo.usage == VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
        && m_samplerSegment.bindingInfo.usage == VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT
    );

    const u32 offsetAlignmentBytes = VulkanDetail::GetDescriptorBufferOffsetAlignmentBytes(m_context);
    if((m_resourceSegment.deviceAddress % offsetAlignmentBytes) != 0u || (m_samplerSegment.deviceAddress % offsetAlignmentBytes) != 0u)
        return MakeUnexpected(Failure{});

    snapshot.bindingInfos[s_ResourceDescriptorBufferIndex] = m_resourceSegment.bindingInfo;
    snapshot.bindingInfos[s_SamplerDescriptorBufferIndex] = m_samplerSegment.bindingInfo;
    snapshot.generation = m_bindingGeneration;
    snapshot.offsetAlignmentBytes = offsetAlignmentBytes;
    return snapshot;
}

b8 DescriptorBufferManager::isLiveSegmentLocked(const SegmentStorage& storage, const DescriptorBufferSegment& segment, const DescriptorBufferSegmentKind::Enum expectedKind)const noexcept{
    NWB_ASSERT(storage.storageIdentity);
    return storage.allocations.isLive(segment, expectedKind, *storage.storageIdentity, storage.capacityBytes);
}

b8 DescriptorBufferManager::initializeSegment(SegmentStorage& segment, const AStringView debugName, const u32 capacityBytes){
    shutdownSegment(segment);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = capacityBytes;
    bufferInfo.usage = (&segment == &m_resourceSegment ? VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT : VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT) | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const QueueFamilySharingInfo sharingInfo = ResolveQueueFamilySharing(ResourceQueueSharing::GraphicsAndAsyncCompute, m_context);
    bufferInfo.sharingMode = sharingInfo.mode;
    bufferInfo.queueFamilyIndexCount = sharingInfo.familyIndexCount;
    bufferInfo.pQueueFamilyIndices = sharingInfo.data();

    const auto allocation = m_allocator.createHostMappedBuffer(bufferInfo);
    if(!allocation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor buffer '{}': {}"), ConvertString(debugName), ResultToString(allocation.error()));
        return false;
    }
    segment.buffer = allocation->buffer;
    segment.allocation = allocation->allocation;
    segment.mappedMemory = allocation->mappedMemory;
    if(!segment.mappedMemory){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map descriptor buffer memory '{}'"), ConvertString(debugName));
        shutdownSegment(segment);
        return false;
    }

    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = segment.buffer;
    segment.deviceAddress = m_context.deviceDispatch.vkGetBufferDeviceAddress(m_context.device, &addressInfo);
    if(segment.deviceAddress == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to query descriptor buffer device address '{}'."), ConvertString(debugName));
        shutdownSegment(segment);
        return false;
    }

    segment.capacityBytes = capacityBytes;
    segment.bindingInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    segment.bindingInfo.address = segment.deviceAddress;
    segment.bindingInfo.usage = (&segment == &m_resourceSegment ? VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT : VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT);
    segment.allocations.resetOffset();

    NWB_MEMSET(segment.mappedMemory, 0, capacityBytes);
    return true;
}

void DescriptorBufferManager::shutdownSegment(SegmentStorage& segment)noexcept{
    m_allocator.destroyHostMappedBuffer(segment.buffer, segment.allocation, segment.mappedMemory);
    segment.deviceAddress = 0;
    segment.capacityBytes = 0;
    segment.allocations.resetOffset();
    segment.bindingInfo = {};
    segment.allocations.clearAllocations();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

