// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize GpuDescriptorHeap::SlotAllocator::RequiredBytes(const u32 capacity){
    return AddSize(
        AddSize(FixedTable<u32>::RequiredBytes(capacity), FixedTable<SlotState>::RequiredBytes(capacity)),
        FixedTable<u8>::RequiredBytes(capacity)
    );
}

bool GpuDescriptorHeap::SlotAllocator::initialize(Alloc::PersistentArena& arena, const u32 newCapacity){
    clear();
    if(
        !freeList.initialize(arena, newCapacity)
        || !slotStates.initialize(arena, newCapacity)
        || !allocatedClasses.initialize(arena, newCapacity)
    )
        return false;
    for(u32 slot = 0u; slot < newCapacity; ++slot){
        slotStates[slot] = SlotState::Free;
        allocatedClasses[slot] = static_cast<u8>(GpuDescriptorClass::kCount);
    }
    capacity = newCapacity;
    return true;
}

void GpuDescriptorHeap::SlotAllocator::clear()noexcept{
    freeList.clear();
    slotStates.clear();
    allocatedClasses.clear();
    capacity = 0u;
    nextFresh = 0u;
    freeCount = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

