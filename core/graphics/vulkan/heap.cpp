// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_heap.h"

#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_heap{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr StringView s_HeapLifecycleDiagnosticCategory = "vulkan_heap_lifecycle";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Heap::Heap(const VulkanContext& context, VulkanAllocator& allocator)noexcept
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_bindingReservations(context.objectArena)
    , m_context(context)
    , m_allocator(allocator)
{}
Heap::~Heap()noexcept{
    {
        ScopedLock lock(m_bindingMutex);
        if(!m_bindingReservations.empty()){
            CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = __hidden_vulkan_heap::s_HeapLifecycleDiagnosticCategory, .message = "Vulkan: Refusing to free a heap with live placed-resource bindings", .file = __FILE__, .line = __LINE__, });
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Heap destroyed with live placed-resource bindings"));
            return;
        }
    }
    m_allocator.freeHeap(*this);
}

Object Heap::getNativeHandle(ObjectType objectType)noexcept{
    if(objectType == ObjectTypes::s_DeviceMemory)
        return Object(m_memory);
    return Object(nullptr);
}

void Heap::eraseBindingReservationLocked(const void* owner)noexcept{
    for(auto reservation = m_bindingReservations.begin(); reservation != m_bindingReservations.end(); ++reservation){
        if(reservation->owner != owner)
            continue;

        m_bindingReservations.erase(reservation);
        return;
    }

    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventName::s_Error, .category = __hidden_vulkan_heap::s_HeapLifecycleDiagnosticCategory, .message = "Vulkan: Failed to unregister a placed-resource heap binding", .file = __FILE__, .line = __LINE__, });
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Missing placed-resource heap binding"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<HeapHandle> Device::createHeap(const HeapDesc& d){
    VkResult res = VK_SUCCESS;

    if(d.capacity == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create heap: capacity is zero"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create heap: capacity is zero"));
        return MakeUnexpected(Failure{});
    }

    switch(d.type){
    case HeapType::DeviceLocal:
    case HeapType::Upload:
    case HeapType::Readback:
        break;
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create heap: invalid heap type"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create heap: invalid heap type"));
        return MakeUnexpected(Failure{});
    }

    HeapHandle heap(NewArenaObject<Heap>(m_context.objectArena, m_context, m_allocator), HeapHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!heap)
        return MakeUnexpected(Failure{});
    heap->m_desc = d;

    res = m_allocator.allocateHeap(*heap);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate heap memory ({} bytes): {}"), d.capacity, ResultToString(res));
        return MakeUnexpected(Failure{});
    }

    return heap;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

