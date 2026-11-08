// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "amd_breadcrumb_ring.h"

#include <core/common/log.h>

#include <global/atomic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Device::captureDeviceLoss(const AStringView context){
    // Device loss must be recorded even when diagnostics are disabled.
    markDeviceLost();
    m_gpuCrashDiagnostics.capture(*this, context);
}

Device::AmdBreadcrumbWrite Device::reserveAmdBreadcrumb(const GpuPhysicalQueueId& queue, const usize markerHash)noexcept{
    AmdBreadcrumbWrite write;
    if(m_amdBreadcrumb.buffer == VK_NULL_HANDLE || !getPhysicalQueueInfo(queue))
        return write;
    // A live breadcrumb buffer exists only after its metadata was built from the same layout.
    NWB_ASSERT(
        m_amdBreadcrumb.metadata
        && m_amdBreadcrumb.metadata->slotRecordCount == m_amdBreadcrumb.layout.totalSlotCount
        && m_amdBreadcrumb.metadata->nextSerialCount == m_amdBreadcrumb.layout.physicalQueueCount
        && static_cast<usize>(queue.index) < m_amdBreadcrumb.metadata->nextSerialCount
    );

    {
        // Serialize the queue-local reservation and its paired CPU record.
        ScopedLock lock(m_amdBreadcrumb.slotMutex);
        const auto reservation = VulkanDetail::AmdBreadcrumbRing::BuildNextReservation(m_amdBreadcrumb.metadata->nextSerials[queue.index], m_amdBreadcrumb.layout.slotsPerQueue);
        if(!reservation)
            return write;
        const auto slot = VulkanDetail::AmdBreadcrumbRing::ResolveSlot(m_amdBreadcrumb.layout, queue, reservation->localSlot);
        if(!slot)
            return write;

        m_amdBreadcrumb.metadata->nextSerials[queue.index] = reservation->serial;
        m_amdBreadcrumb.metadata->slotRecords[slot->flatSlot].serial = reservation->serial;
        m_amdBreadcrumb.metadata->slotRecords[slot->flatSlot].markerHash = markerHash;
        m_amdBreadcrumb.metadata->slotRecords[slot->flatSlot].marker = reservation->marker;

        write.offset = slot->byteOffset;
        write.marker = reservation->marker;
    }

    write.buffer = m_amdBreadcrumb.buffer;
    write.valid = true;
    return write;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

