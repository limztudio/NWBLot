// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#define VMA_IMPLEMENTATION
#include "vma.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_allocator{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline VmaAllocationCreateInfo BuildDeviceLocalAllocationInfo(){
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    return allocInfo;
}

inline VmaAllocationCreateInfo BuildMappedHostAllocationInfo(
    const VkMemoryPropertyFlags requiredFlags,
    const VkMemoryPropertyFlags preferredFlags,
    const VmaAllocationCreateFlags accessFlags
){
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | requiredFlags;
    allocInfo.preferredFlags = preferredFlags;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | accessFlags;
    return allocInfo;
}

inline VmaAllocationCreateInfo BuildCpuAccessAllocationInfo(const CpuAccessMode::Enum cpuAccess){
    if(cpuAccess == CpuAccessMode::Read){
        return BuildMappedHostAllocationInfo(
            0,
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
        );
    }

    return BuildMappedHostAllocationInfo(
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        0,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
}

inline VmaAllocationCreateInfo BuildBufferAllocationInfo(const BufferDesc& desc, const VkDeviceSize bufferSize){
    VmaAllocationCreateInfo allocInfo{};
    if(desc.isVolatile || desc.cpuAccess == CpuAccessMode::Write)
        allocInfo = BuildCpuAccessAllocationInfo(CpuAccessMode::Write);
    else if(desc.cpuAccess == CpuAccessMode::Read)
        allocInfo = BuildCpuAccessAllocationInfo(CpuAccessMode::Read);
    else
        allocInfo = BuildDeviceLocalAllocationInfo();

    if(bufferSize >= s_LargeBufferThreshold)
        allocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    return allocInfo;
}

inline VmaAllocationCreateInfo BuildStagingTextureAllocationInfo(const CpuAccessMode::Enum cpuAccess){
    return cpuAccess == CpuAccessMode::None
        ? BuildDeviceLocalAllocationInfo()
        : BuildCpuAccessAllocationInfo(cpuAccess)
    ;
}

inline VmaAllocationCreateInfo BuildHeapAllocationInfo(const HeapDesc& desc){
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_UNKNOWN;

    switch(desc.type){
    case HeapType::DeviceLocal:
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    case HeapType::Upload:
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        allocInfo.flags =
            VMA_ALLOCATION_CREATE_MAPPED_BIT
            | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        ;
        break;
    case HeapType::Readback:
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        allocInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        allocInfo.flags =
            VMA_ALLOCATION_CREATE_MAPPED_BIT
            | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
        ;
        break;
    default:
        break;
    }

    return allocInfo;
}

inline VmaAllocationCreateInfo BuildHostMappedBufferAllocationInfo(){
    return BuildMappedHostAllocationInfo(
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        0,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
    );
}

inline bool BuildRequiresInvalidate(const VkPhysicalDeviceMemoryProperties& memoryProperties, const u32 memoryTypeIndex){
    if(memoryTypeIndex >= memoryProperties.memoryTypeCount)
        return true;

    const VkMemoryPropertyFlags propertyFlags = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
    return (propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 && (propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0;
}

inline VmaAllocator ToVmaAllocator(const VulkanAllocatorHandle allocator){
    static_assert(sizeof(VmaAllocator) == sizeof(VulkanAllocatorHandle));
    return reinterpret_cast<VmaAllocator>(allocator);
}

inline VulkanAllocatorHandle ToVulkanAllocatorHandle(const VmaAllocator allocator){
    static_assert(sizeof(VulkanAllocatorHandle) == sizeof(VmaAllocator));
    return reinterpret_cast<VulkanAllocatorHandle>(allocator);
}

inline VmaAllocation ToVmaAllocation(const VulkanAllocationHandle allocation){
    static_assert(sizeof(VmaAllocation) == sizeof(VulkanAllocationHandle));
    return reinterpret_cast<VmaAllocation>(allocation);
}

inline VulkanAllocationHandle ToVulkanAllocationHandle(const VmaAllocation allocation){
    static_assert(sizeof(VulkanAllocationHandle) == sizeof(VmaAllocation));
    return reinterpret_cast<VulkanAllocationHandle>(allocation);
}

inline VkResult MapAllocation(const VulkanAllocatorHandle allocator, const VulkanAllocationHandle allocation, void** outData){
    if(!outData || !allocation)
        return VK_ERROR_MEMORY_MAP_FAILED;
    return vmaMapMemory(ToVmaAllocator(allocator), ToVmaAllocation(allocation), outData);
}

inline VkResult InvalidateAllocation(
    const VulkanAllocatorHandle allocator,
    const VulkanAllocationHandle allocation,
    const u64 offset,
    const u64 size
){
    if(!allocation)
        return VK_ERROR_MEMORY_MAP_FAILED;
    return vmaInvalidateAllocation(ToVmaAllocator(allocator), ToVmaAllocation(allocation), offset, size);
}

inline void UnmapAllocation(const VulkanAllocatorHandle allocator, const VulkanAllocationHandle allocation){
    NWB_ASSERT(allocation);
    vmaUnmapMemory(ToVmaAllocator(allocator), ToVmaAllocation(allocation));
}

inline void UnmapTransientAllocation(
    const VulkanAllocatorHandle allocator,
    const VulkanAllocationHandle allocation,
    void*& mappedMemory,
    const bool persistentlyMapped
){
    if(mappedMemory && !persistentlyMapped){
        UnmapAllocation(allocator, allocation);
        mappedMemory = nullptr;
    }
}

inline VkResult CreateBufferAllocation(
    const VulkanAllocatorHandle allocator,
    const VkPhysicalDeviceMemoryProperties& memoryProperties,
    const VkBufferCreateInfo& bufferInfo,
    const VmaAllocationCreateInfo& allocInfo,
    VkBuffer& buffer,
    VulkanAllocationHandle& allocation,
    void*& mappedMemory,
    bool* outRequiresInvalidate
){
    VmaAllocationInfo allocationInfo{};
    VmaAllocation vmaAllocation = nullptr;
    const VkResult res = vmaCreateBuffer(
        ToVmaAllocator(allocator),
        &bufferInfo,
        &allocInfo,
        &buffer,
        &vmaAllocation,
        &allocationInfo
    );
    if(res == VK_SUCCESS){
        allocation = ToVulkanAllocationHandle(vmaAllocation);
        mappedMemory = allocationInfo.pMappedData;
        if(outRequiresInvalidate)
            *outRequiresInvalidate = BuildRequiresInvalidate(memoryProperties, allocationInfo.memoryType);
    }
    return res;
}

inline void DestroyBufferAllocation(
    const VulkanAllocatorHandle allocator,
    VkBuffer& buffer,
    VulkanAllocationHandle& allocation,
    void*& mappedMemory,
    const bool persistentlyMapped
){
    if(allocation){
        UnmapTransientAllocation(allocator, allocation, mappedMemory, persistentlyMapped);
        vmaDestroyBuffer(ToVmaAllocator(allocator), buffer, ToVmaAllocation(allocation));
        allocation = nullptr;
    }
    else{
        NWB_ASSERT(buffer == VK_NULL_HANDLE);
    }

    buffer = VK_NULL_HANDLE;
    mappedMemory = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VulkanAllocator::VulkanAllocator(const VulkanContext& context)
    : m_context(context)
    , m_bufferNativeIdentities(0u, Hasher<u64>(), EqualTo<u64>(), context.objectArena)
    , m_textureNativeIdentities(0u, Hasher<VkImage>(), EqualTo<VkImage>(), context.objectArena)
{}
VulkanAllocator::~VulkanAllocator(){
    usize registeredBufferCount = 0u;
    {
        ScopedLock lock(m_bufferNativeIdentityMutex);
        registeredBufferCount = m_bufferNativeIdentities.size();
    }
    usize registeredTextureCount = 0u;
    {
        ScopedLock lock(m_textureNativeIdentityMutex);
        registeredTextureCount = m_textureNativeIdentities.size();
    }
    if(registeredBufferCount != 0u || registeredTextureCount != 0u){
        NWB_LOGGER_CRITICAL_WARNING(
            NWB_TEXT("Vulkan: Allocator destruction found {} live Buffer and {} live Texture native identities")
            , registeredBufferCount
            , registeredTextureCount
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Buffer and Texture resources must not outlive their allocator"));
    }

    if(m_allocator){
        vmaDestroyAllocator(__hidden_vulkan_allocator::ToVmaAllocator(m_allocator));
        m_allocator = nullptr;
    }
}

bool VulkanAllocator::initialize(){
    if(m_allocator)
        return true;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = m_context.physicalDevice;
    allocatorInfo.device = m_context.device;
    allocatorInfo.instance = m_context.instance;
    allocatorInfo.vulkanApiVersion = s_MinimumVersion;
    allocatorInfo.pAllocationCallbacks = m_context.allocationCallbacks;
    if(m_context.extensions.buffer_device_address)
        allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VmaVulkanFunctions vulkanFunctions{};
    VkResult res = vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vulkanFunctions);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to import Vulkan functions for VMA: {}"), ResultToString(res));
        return false;
    }

    allocatorInfo.pVulkanFunctions = &vulkanFunctions;
    VmaAllocator allocator = nullptr;
    res = vmaCreateAllocator(&allocatorInfo, &allocator);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create VMA allocator: {}"), ResultToString(res));
        m_allocator = nullptr;
        return false;
    }

    m_allocator = __hidden_vulkan_allocator::ToVulkanAllocatorHandle(allocator);
    return true;
}

VkResult VulkanAllocator::createBuffer(Buffer& buffer, const VkBufferCreateInfo& bufferInfo){
    if(!m_allocator)
        return VK_ERROR_INITIALIZATION_FAILED;

    VmaAllocationCreateInfo allocInfo = __hidden_vulkan_allocator::BuildBufferAllocationInfo(buffer.m_desc, bufferInfo.size);
    const VkResult res = __hidden_vulkan_allocator::CreateBufferAllocation(
        m_allocator,
        m_context.memoryProperties,
        bufferInfo,
        allocInfo,
        buffer.m_buffer,
        buffer.m_allocation,
        buffer.m_mappedMemory,
        &buffer.m_requiresInvalidate
    );
    if(res == VK_SUCCESS){
        buffer.m_persistentlyMapped = buffer.m_mappedMemory != nullptr;
#if defined(NWB_DEBUG)
        if(buffer.m_desc.debugName)
            vmaSetAllocationName(__hidden_vulkan_allocator::ToVmaAllocator(m_allocator), __hidden_vulkan_allocator::ToVmaAllocation(buffer.m_allocation), buffer.m_desc.debugName.logText());
#endif
    }
    return res;
}

void VulkanAllocator::destroyBuffer(Buffer& buffer){
    __hidden_vulkan_allocator::DestroyBufferAllocation(
        m_allocator,
        buffer.m_buffer,
        buffer.m_allocation,
        buffer.m_mappedMemory,
        buffer.m_persistentlyMapped
    );
    buffer.m_persistentlyMapped = false;
    buffer.m_requiresInvalidate = false;
}

VkResult VulkanAllocator::mapBufferMemory(Buffer& buffer, void** outData){
    return __hidden_vulkan_allocator::MapAllocation(m_allocator, buffer.m_allocation, outData);
}

void VulkanAllocator::unmapBufferMemory(Buffer& buffer){
    __hidden_vulkan_allocator::UnmapAllocation(m_allocator, buffer.m_allocation);
}

VkResult VulkanAllocator::invalidateBufferMemory(Buffer& buffer){
    if(!buffer.m_requiresInvalidate)
        return VK_SUCCESS;
    return __hidden_vulkan_allocator::InvalidateAllocation(m_allocator, buffer.m_allocation, 0, VK_WHOLE_SIZE);
}

VkResult VulkanAllocator::createTexture(Texture& texture, const VkImageCreateInfo& imageInfo){
    if(!m_allocator)
        return VK_ERROR_INITIALIZATION_FAILED;

    VmaAllocationCreateInfo allocInfo = __hidden_vulkan_allocator::BuildDeviceLocalAllocationInfo();
    VmaAllocation allocation = nullptr;
    const VkResult res = vmaCreateImage(
        __hidden_vulkan_allocator::ToVmaAllocator(m_allocator),
        &imageInfo,
        &allocInfo,
        &texture.m_image,
        &allocation,
        nullptr
    );
    if(res == VK_SUCCESS){
        texture.m_allocation = __hidden_vulkan_allocator::ToVulkanAllocationHandle(allocation);
#if defined(NWB_DEBUG)
        if(texture.m_desc.name)
            vmaSetAllocationName(__hidden_vulkan_allocator::ToVmaAllocator(m_allocator), allocation, texture.m_desc.name.logText());
#endif
    }
    return res;
}

void VulkanAllocator::destroyTexture(Texture& texture){
    if(texture.m_allocation){
        vmaDestroyImage(
            __hidden_vulkan_allocator::ToVmaAllocator(m_allocator),
            texture.m_image,
            __hidden_vulkan_allocator::ToVmaAllocation(texture.m_allocation)
        );
    }
    else{
        NWB_ASSERT(texture.m_image == VK_NULL_HANDLE);
    }

    texture.m_image = VK_NULL_HANDLE;
    texture.m_allocation = nullptr;
}

VkResult VulkanAllocator::createStagingTexture(
    StagingTexture& texture,
    const VkBufferCreateInfo& bufferInfo,
    const CpuAccessMode::Enum cpuAccess
){
    if(!m_allocator)
        return VK_ERROR_INITIALIZATION_FAILED;

    VmaAllocationCreateInfo allocInfo = __hidden_vulkan_allocator::BuildStagingTextureAllocationInfo(cpuAccess);
    const VkResult res = __hidden_vulkan_allocator::CreateBufferAllocation(
        m_allocator,
        m_context.memoryProperties,
        bufferInfo,
        allocInfo,
        texture.m_buffer,
        texture.m_allocation,
        texture.m_mappedMemory,
        &texture.m_requiresInvalidate
    );
    if(res == VK_SUCCESS)
        texture.m_persistentlyMapped = texture.m_mappedMemory != nullptr;
    return res;
}

void VulkanAllocator::destroyStagingTexture(StagingTexture& texture){
    __hidden_vulkan_allocator::DestroyBufferAllocation(
        m_allocator,
        texture.m_buffer,
        texture.m_allocation,
        texture.m_mappedMemory,
        texture.m_persistentlyMapped
    );
    texture.m_persistentlyMapped = false;
    texture.m_requiresInvalidate = false;
}

VkResult VulkanAllocator::mapStagingTextureMemory(StagingTexture& texture, void** outData){
    return __hidden_vulkan_allocator::MapAllocation(m_allocator, texture.m_allocation, outData);
}

void VulkanAllocator::unmapStagingTextureMemory(StagingTexture& texture){
    __hidden_vulkan_allocator::UnmapAllocation(m_allocator, texture.m_allocation);
}

VkResult VulkanAllocator::invalidateStagingTextureMemory(StagingTexture& texture, const u64 offset, const u64 size){
    if(!texture.m_requiresInvalidate)
        return VK_SUCCESS;
    return __hidden_vulkan_allocator::InvalidateAllocation(m_allocator, texture.m_allocation, offset, size);
}

VkResult VulkanAllocator::allocateHeap(Heap& heap){
    if(!m_allocator)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkMemoryRequirements memRequirements{};
    memRequirements.size = heap.m_desc.capacity;
    memRequirements.alignment = Max<VkDeviceSize>(m_context.physicalDeviceProperties.limits.bufferImageGranularity, 1u);
    if(heap.m_desc.type == HeapType::Readback){
        memRequirements.alignment = Max<VkDeviceSize>(
            memRequirements.alignment,
            m_context.physicalDeviceProperties.limits.nonCoherentAtomSize
        );
    }
    memRequirements.memoryTypeBits = VulkanDetail::BuildNonProtectedMemoryTypeBits(m_context.memoryProperties);
    if(memRequirements.memoryTypeBits == 0u)
        return VK_ERROR_FEATURE_NOT_PRESENT;

    VmaAllocationCreateInfo allocInfo = __hidden_vulkan_allocator::BuildHeapAllocationInfo(heap.m_desc);
    VmaAllocationInfo allocationInfo{};
    VmaAllocation allocation = nullptr;
    const VkResult res = vmaAllocateDedicatedMemory(
        __hidden_vulkan_allocator::ToVmaAllocator(m_allocator),
        &memRequirements,
        &allocInfo,
        nullptr,
        &allocation,
        &allocationInfo
    );
    if(res == VK_SUCCESS){
        heap.m_allocation = __hidden_vulkan_allocator::ToVulkanAllocationHandle(allocation);
        heap.m_memory = allocationInfo.deviceMemory;
        heap.m_memoryOffset = allocationInfo.offset;
        heap.m_memoryTypeIndex = allocationInfo.memoryType;
        heap.m_mappedMemory = allocationInfo.pMappedData;
        heap.m_requiresInvalidate =
            heap.m_desc.type == HeapType::Readback
            && __hidden_vulkan_allocator::BuildRequiresInvalidate(m_context.memoryProperties, allocationInfo.memoryType)
        ;
        if(heap.m_desc.type != HeapType::DeviceLocal && !heap.m_mappedMemory){
            freeHeap(heap);
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
    }

    return res;
}

void VulkanAllocator::freeHeap(Heap& heap){
    if(heap.m_allocation){
        vmaFreeMemory(
            __hidden_vulkan_allocator::ToVmaAllocator(m_allocator),
            __hidden_vulkan_allocator::ToVmaAllocation(heap.m_allocation)
        );
        heap.m_allocation = nullptr;
    }
    heap.m_memory = VK_NULL_HANDLE;
    heap.m_memoryOffset = 0;
    heap.m_memoryTypeIndex = UINT32_MAX;
    heap.m_mappedMemory = nullptr;
    heap.m_requiresInvalidate = false;
}

VkResult VulkanAllocator::invalidateHeapMemory(Heap& heap, const u64 offset, const u64 size){
    if(!heap.m_allocation || !heap.m_mappedMemory)
        return VK_ERROR_MEMORY_MAP_FAILED;
    if(!heap.m_requiresInvalidate)
        return VK_SUCCESS;
    return __hidden_vulkan_allocator::InvalidateAllocation(m_allocator, heap.m_allocation, offset, size);
}

VkResult VulkanAllocator::bindHeapBufferMemory(Buffer& buffer, Heap& heap, const u64 offset){
    if(!heap.m_allocation)
        return VK_ERROR_INITIALIZATION_FAILED;
    return vmaBindBufferMemory2(
        __hidden_vulkan_allocator::ToVmaAllocator(m_allocator),
        __hidden_vulkan_allocator::ToVmaAllocation(heap.m_allocation),
        offset,
        buffer.m_buffer,
        nullptr
    );
}

VkResult VulkanAllocator::bindHeapTextureMemory(Texture& texture, Heap& heap, const u64 offset){
    if(!heap.m_allocation)
        return VK_ERROR_INITIALIZATION_FAILED;
    return vmaBindImageMemory2(
        __hidden_vulkan_allocator::ToVmaAllocator(m_allocator),
        __hidden_vulkan_allocator::ToVmaAllocation(heap.m_allocation),
        offset,
        texture.m_image,
        nullptr
    );
}

VkResult VulkanAllocator::createHostMappedBuffer(
    VkBuffer& buffer,
    VulkanAllocationHandle& allocation,
    void*& mappedMemory,
    const VkBufferCreateInfo& bufferInfo
){
    if(!m_allocator)
        return VK_ERROR_INITIALIZATION_FAILED;

    VmaAllocationCreateInfo allocInfo = __hidden_vulkan_allocator::BuildHostMappedBufferAllocationInfo();
    return __hidden_vulkan_allocator::CreateBufferAllocation(
        m_allocator,
        m_context.memoryProperties,
        bufferInfo,
        allocInfo,
        buffer,
        allocation,
        mappedMemory,
        nullptr
    );
}

void VulkanAllocator::destroyHostMappedBuffer(VkBuffer& buffer, VulkanAllocationHandle& allocation, void*& mappedMemory){
    __hidden_vulkan_allocator::DestroyBufferAllocation(m_allocator, buffer, allocation, mappedMemory, true);
}

bool VulkanAllocator::tryRegisterBufferNativeIdentity(Buffer& buffer){
    if(buffer.m_buffer == VK_NULL_HANDLE)
        return false;

    const u64 nativeIdentity = Object(buffer.m_buffer).integer;
    ScopedLock lock(m_bufferNativeIdentityMutex);
    return m_bufferNativeIdentities.emplace(nativeIdentity, &buffer).second;
}

void VulkanAllocator::unregisterBufferNativeIdentity(const VkBuffer nativeBuffer, Buffer& buffer)noexcept{
    if(nativeBuffer == VK_NULL_HANDLE)
        return;

    const u64 nativeIdentity = Object(nativeBuffer).integer;
    ScopedLock lock(m_bufferNativeIdentityMutex);
    const auto found = m_bufferNativeIdentities.find(nativeIdentity);
    if(found != m_bufferNativeIdentities.end() && found.value() == &buffer)
        m_bufferNativeIdentities.erase(found);
}

bool VulkanAllocator::isBufferNativeIdentityRegistered(const Buffer& buffer)const noexcept{
    if(buffer.m_buffer == VK_NULL_HANDLE)
        return false;

    const u64 nativeIdentity = Object(buffer.m_buffer).integer;
    ScopedLock lock(m_bufferNativeIdentityMutex);
    const auto found = m_bufferNativeIdentities.find(nativeIdentity);
    return found != m_bufferNativeIdentities.end() && found.value() == &buffer;
}

bool VulkanAllocator::tryRegisterTextureNativeIdentity(Texture& texture){
    if(texture.m_image == VK_NULL_HANDLE)
        return false;

    ScopedLock lock(m_textureNativeIdentityMutex);
    return m_textureNativeIdentities.emplace(texture.m_image, &texture).second;
}

void VulkanAllocator::unregisterTextureNativeIdentity(const VkImage nativeImage, Texture& texture)noexcept{
    if(nativeImage == VK_NULL_HANDLE)
        return;

    ScopedLock lock(m_textureNativeIdentityMutex);
    const auto found = m_textureNativeIdentities.find(nativeImage);
    if(found != m_textureNativeIdentities.end() && found.value() == &texture)
        m_textureNativeIdentities.erase(found);
}

bool VulkanAllocator::isTextureNativeIdentityRegistered(
    const VkImage nativeImage,
    const Texture& texture
)const noexcept{
    if(nativeImage == VK_NULL_HANDLE)
        return false;

    ScopedLock lock(m_textureNativeIdentityMutex);
    const auto found = m_textureNativeIdentities.find(nativeImage);
    return found != m_textureNativeIdentities.end() && found.value() == &texture;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

