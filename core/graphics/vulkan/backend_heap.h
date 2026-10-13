// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_resource_context.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Heap final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Buffer;
    friend class Device;
    friend class Texture;
    friend class VulkanAllocator;
    friend class Queue;


private:
    struct BindingReservation{
        const void* owner = nullptr;
        VulkanDetail::HeapBindingRange range;
        VulkanDetail::HeapBindingResourceClass::Enum resourceClass = VulkanDetail::HeapBindingResourceClass::Buffer;
    };


public:
    Heap(const VulkanContext& context, VulkanAllocator& allocator);
    ~Heap();


public:
    [[nodiscard]] const HeapDesc& getDescription()const noexcept{ return m_desc; }
    Object getNativeHandle(ObjectType objectType);


private:
    void eraseBindingReservationLocked(const void* owner);


private:
    HeapDesc m_desc;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VulkanAllocationHandle m_allocation = nullptr;
    VkDeviceSize m_memoryOffset = 0;
    void* m_mappedMemory = nullptr;
    Vector<BindingReservation, Alloc::GlobalArena> m_bindingReservations;
    Futex m_bindingMutex;

    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
    u32 m_memoryTypeIndex = UINT32_MAX;
    bool m_requiresInvalidate = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

