// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "aftermath.h"

#include <core/common/log.h>
#include <global/atomic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_device_diagnostics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static AStringView TrimGpuCrashText(const AStringView text){
    return AStringView(text.data(), Min(text.size(), s_MaxGpuCrashMarkerChars));
}

static AStringView TrimGpuCrashText(const char* const text){
    return text ? TrimGpuCrashText(AStringView(text)) : AStringView();
}

static const char* GpuCrashAvailabilityText(const bool available){
    return available ? "available" : "unavailable";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Device::captureDeviceLoss(const AStringView context)noexcept{
    // Device-loss state must not depend on optional crash diagnostics.
    m_deviceLost.store(true, MemoryOrder::release);
    if(!m_gpuCrashDiagnosticsEnabled)
        return;

    const bool hasCheckpoints = m_context.extensions.NV_device_diagnostic_checkpoints;
    const bool hasDeviceFault = m_context.extensions.EXT_device_fault;
    const bool hasBufferMarker = m_context.extensions.AMD_buffer_marker && m_amdBreadcrumb.buffer != VK_NULL_HANDLE;

    // Capture only the first concurrent device-loss report.
    if(m_gpuCrashCaptured.exchange(true))
        return;

    GpuCrashReport report(m_gpuCrashReportArena);
    Vector<u8, Alloc::PersistentArena> vendorBinary(m_gpuCrashVendorBinaryArena);

    // Fixed crash arena permits partial reports without allocation failure.
    try{
        report.details.reserve(s_MaxGpuCrashReportChars);
        report.context.append(context.data(), context.size());

        // One aggregate budget bounds all fault sections.
        u32 remainingEntries = s_MaxGpuCrashCaptureEntries;

        if(hasCheckpoints){
            for(Queue* physicalQueue : m_physicalQueues){
                if(!physicalQueue || remainingEntries == 0u)
                    continue;

                // Vulkan checkpoint retrieval is a device-loss diagnostic and has no VkQueue host-synchronization
                // requirement. Do not wait behind a potentially wedged submission lock while collecting it.
                VkQueue queue = physicalQueue->m_nativeQueue.queue;
                uint32_t checkpointCount = 0;
                m_context.deviceDispatch.vkGetQueueCheckpointDataNV(queue, &checkpointCount, nullptr);
                if(checkpointCount == 0)
                    continue;
                if(checkpointCount > remainingEntries)
                    checkpointCount = remainingEntries;

                Vector<VkCheckpointDataNV, Alloc::PersistentArena> checkpoints(m_gpuCrashReportArena);
                checkpoints.resize(checkpointCount, VulkanDetail::MakeVkStruct<VkCheckpointDataNV>(VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV));
                m_context.deviceDispatch.vkGetQueueCheckpointDataNV(queue, &checkpointCount, checkpoints.data());

                for(const auto& checkpoint : checkpoints){
                    const usize markerHash = reinterpret_cast<usize>(checkpoint.pCheckpointMarker);
                    if(markerHash == 0)
                        continue;

                    const auto resolved = m_gpuCrashTracker.resolveMarker(markerHash);
                    if(!resolved.first())
                        continue;

                    report.details.append(StringFormat(m_gpuCrashReportArena, "last executed marker (stage 0x{:x}): {}\n", static_cast<u32>(checkpoint.stage), __hidden_vulkan_device_diagnostics::TrimGpuCrashText(resolved.second())));
                }

                remainingEntries -= checkpointCount;
            }
        }

        if(hasBufferMarker && remainingEntries > 0u){
            const u32* breadcrumbSlots = static_cast<const u32*>(m_amdBreadcrumb.mappedMemory);
            if(breadcrumbSlots){
                // Select each exact queue independently because physical queues remain unordered globally.
                for(
                    usize queueIndex = 0u;
                    queueIndex < m_amdBreadcrumb.layout.physicalQueueCount && remainingEntries > 0u;
                    ++queueIndex
                ){
                    const GpuPhysicalQueueId& queue = m_physicalQueueInfos[queueIndex].id;
                    usize queueFirstSlot = 0u;
                    VkDeviceSize queueFirstOffset = 0u;
                    if(!VulkanDetail::TryResolveAmdBreadcrumbRingSlot(
                        m_amdBreadcrumb.layout,
                        queue,
                        0u,
                        queueFirstSlot,
                        queueFirstOffset
                    ))
                        continue;

                    AmdBreadcrumbSlotRecord newestRecord;
                    bool hasObservedMarker = false;
                    {
                        // Lock with breadcrumb reservations to avoid pairing an observation with a torn record.
                        ScopedLock lock(m_amdBreadcrumb.slotMutex);
                        for(usize localSlot = 0u; localSlot < m_amdBreadcrumb.layout.slotsPerQueue; ++localSlot){
                            const usize flatSlot = queueFirstSlot + localSlot;
                            const u32 observedMarker = breadcrumbSlots[flatSlot];
                            if(observedMarker == 0u)
                                continue;

                            hasObservedMarker = true;
                            const AmdBreadcrumbSlotRecord& record = m_amdBreadcrumb.slotRecords[flatSlot];
                            if(
                                VulkanDetail::MatchesAmdBreadcrumbObservation(observedMarker, record.marker)
                                && record.serial > newestRecord.serial
                            )
                                newestRecord = record;
                        }
                    }

                    if(!hasObservedMarker)
                        continue;

                    if(newestRecord.serial != 0u){
                        const auto resolved = m_gpuCrashTracker.resolveMarker(newestRecord.markerHash);
                        if(resolved.first()){
                            report.details.append(StringFormat(
                                m_gpuCrashReportArena,
                                "best-effort last-observed breadcrumb (queue {}:{}, reservation {}, marker {}): {}\n",
                                queue.index,
                                queue.deviceGeneration,
                                newestRecord.serial,
                                newestRecord.marker,
                                __hidden_vulkan_device_diagnostics::TrimGpuCrashText(resolved.second())
                            ));
                        }
                        else{
                            report.details.append(StringFormat(
                                m_gpuCrashReportArena,
                                "best-effort last-observed breadcrumb (queue {}:{}, reservation {}, marker {}): "
                                "<unresolved marker>\n",
                                queue.index,
                                queue.deviceGeneration,
                                newestRecord.serial,
                                newestRecord.marker
                            ));
                        }
                    }
                    else{
                        report.details.append(StringFormat(
                            m_gpuCrashReportArena,
                            "best-effort last-observed breadcrumb (queue {}:{}): <label overwritten>\n",
                            queue.index,
                            queue.deviceGeneration
                        ));
                    }
                    --remainingEntries;
                }
            }
        }

        if(hasDeviceFault && remainingEntries > 0u){
            auto faultCounts = VulkanDetail::MakeVkStruct<VkDeviceFaultCountsEXT>(VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT);
            if(m_context.deviceDispatch.vkGetDeviceFaultInfoEXT(m_context.device, &faultCounts, nullptr) == VK_SUCCESS){
                const VkDeviceSize vendorBinaryByteSize = faultCounts.vendorBinarySize;
                const bool vendorBinaryIsRgd = m_context.physicalDeviceProperties.vendorID == s_AmdVendorId;
                if(faultCounts.addressInfoCount > remainingEntries)
                    faultCounts.addressInfoCount = remainingEntries;
                remainingEntries -= faultCounts.addressInfoCount;
                if(faultCounts.vendorInfoCount > remainingEntries)
                    faultCounts.vendorInfoCount = remainingEntries;
                remainingEntries -= faultCounts.vendorInfoCount;

                Vector<VkDeviceFaultAddressInfoEXT, Alloc::PersistentArena> addressInfos(m_gpuCrashReportArena);
                Vector<VkDeviceFaultVendorInfoEXT, Alloc::PersistentArena> vendorInfos(m_gpuCrashReportArena);
                addressInfos.resize(faultCounts.addressInfoCount, VkDeviceFaultAddressInfoEXT{});
                vendorInfos.resize(faultCounts.vendorInfoCount, VkDeviceFaultVendorInfoEXT{});
                if(vendorBinaryIsRgd && vendorBinaryByteSize != 0u && vendorBinaryByteSize <= static_cast<VkDeviceSize>(s_MaxDeviceFaultVendorBinaryBytes))
                    vendorBinary.resize(static_cast<usize>(vendorBinaryByteSize));

                auto faultInfo = VulkanDetail::MakeVkStruct<VkDeviceFaultInfoEXT>(VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT);
                faultInfo.pAddressInfos = addressInfos.empty() ? nullptr : addressInfos.data();
                faultInfo.pVendorInfos = vendorInfos.empty() ? nullptr : vendorInfos.data();
                faultInfo.pVendorBinaryData = vendorBinary.empty() ? nullptr : vendorBinary.data();

                const VkResult faultResult = m_context.deviceDispatch.vkGetDeviceFaultInfoEXT(m_context.device, &faultCounts, &faultInfo);
                if(faultResult == VK_SUCCESS || faultResult == VK_INCOMPLETE){
                    const char* faultDescription = faultInfo.description;
                    report.details.append(StringFormat(m_gpuCrashReportArena, "device fault: {}\n", __hidden_vulkan_device_diagnostics::TrimGpuCrashText(faultDescription)));
                    if(vendorBinaryByteSize != 0u){
                        if(!vendorBinary.empty()){
                            report.details.append(StringFormat(m_gpuCrashReportArena, "device fault vendor binary (RGD): {} bytes\n", vendorBinary.size()));
                            report.binaryDumpKind = GpuCrashDumpKind::RadeonGpuDetective;
                            report.binaryDump = vendorBinary.data();
                            report.binaryDumpSize = vendorBinary.size();
                        }
                        else if(!vendorBinaryIsRgd){
                            report.details.append(StringFormat(m_gpuCrashReportArena, "device fault vendor binary not packaged: {} bytes from vendor 0x{:x}\n", static_cast<u64>(vendorBinaryByteSize), static_cast<u32>(m_context.physicalDeviceProperties.vendorID)));
                        }
                        else{
                            report.details.append(StringFormat(m_gpuCrashReportArena, "device fault vendor binary skipped: {} bytes exceeds {} byte cap\n", static_cast<u64>(vendorBinaryByteSize), static_cast<u64>(s_MaxDeviceFaultVendorBinaryBytes)));
                        }
                    }

                    for(u32 i = 0; i < faultCounts.addressInfoCount; ++i){
                        const VkDeviceFaultAddressInfoEXT& addressInfo = addressInfos[i];
                        report.details.append(StringFormat(m_gpuCrashReportArena, "fault address 0x{:x} (type {}, precision 0x{:x})\n"
                            , static_cast<u64>(addressInfo.reportedAddress)
                            , static_cast<u32>(addressInfo.addressType)
                            , static_cast<u64>(addressInfo.addressPrecision)
                        ));
                    }

                    for(u32 i = 0; i < faultCounts.vendorInfoCount; ++i){
                        const VkDeviceFaultVendorInfoEXT& vendorInfo = vendorInfos[i];
                        const char* vendorDescription = vendorInfo.description;
                        report.details.append(StringFormat(m_gpuCrashReportArena, "vendor fault '{}' (code 0x{:x}, data 0x{:x})\n"
                            , __hidden_vulkan_device_diagnostics::TrimGpuCrashText(vendorDescription)
                            , static_cast<u64>(vendorInfo.vendorFaultCode)
                            , static_cast<u64>(vendorInfo.vendorFaultData)
                        ));
                    }
                }
            }
        }

        if(report.details.empty())
            report.details.append(StringFormat(m_gpuCrashReportArena,
                "minimal GPU crash report: no vendor GPU dump or device-fault details were available\n"
                "capture context: {}\n"
                "device: {} (vendor 0x{:x}, device 0x{:x}, driver 0x{:x})\n"
                "diagnostic paths: NV_device_diagnostic_checkpoints={}, AMD_buffer_marker={}, VK_EXT_device_fault={}, NVIDIA Aftermath={}\n"
                , __hidden_vulkan_device_diagnostics::TrimGpuCrashText(context)
                , __hidden_vulkan_device_diagnostics::TrimGpuCrashText(m_context.physicalDeviceProperties.deviceName)
                , static_cast<u32>(m_context.physicalDeviceProperties.vendorID)
                , static_cast<u32>(m_context.physicalDeviceProperties.deviceID)
                , static_cast<u32>(m_context.physicalDeviceProperties.driverVersion)
                , __hidden_vulkan_device_diagnostics::GpuCrashAvailabilityText(hasCheckpoints)
                , __hidden_vulkan_device_diagnostics::GpuCrashAvailabilityText(hasBufferMarker)
                , __hidden_vulkan_device_diagnostics::GpuCrashAvailabilityText(hasDeviceFault)
                , __hidden_vulkan_device_diagnostics::GpuCrashAvailabilityText(Aftermath::IsActive())
            ));
    }
    catch(...){
    }

    try{
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: GPU crash detected during {}:\n{}"), StringConvert(report.context.c_str()), StringConvert(report.details.c_str()));
    }
    catch(...){
    }

    // Attach available Aftermath dump while its bytes remain owned by the module.
    if(Aftermath::IsActive()){
        const Aftermath::GpuCrashDumpView dump = Aftermath::WaitForCrashDump();
        if(dump.data && dump.size != 0u){
            report.binaryDumpKind = GpuCrashDumpKind::Aftermath;
            report.binaryDump = dump.data;
            report.binaryDumpSize = dump.size;
        }
    }

    try{
        DispatchGpuCrash(report);
    }
    catch(...){
    }
}

Device::AmdBreadcrumbWrite Device::reserveAmdBreadcrumb(
    const GpuPhysicalQueueId& queue,
    const usize markerHash
){
    AmdBreadcrumbWrite write;
    const GpuPhysicalQueueInfo* const queueInfo = getPhysicalQueueInfo(queue);
    if(
        m_amdBreadcrumb.buffer == VK_NULL_HANDLE
        || !queueInfo
        || queueInfo->id != queue
        || static_cast<usize>(queue.index) >= m_amdBreadcrumb.nextSerials.size()
        || m_amdBreadcrumb.slotRecords.size() != m_amdBreadcrumb.layout.totalSlotCount
    )
        return write;

    VulkanDetail::AmdBreadcrumbReservation reservation;
    usize flatSlot = 0u;
    VkDeviceSize byteOffset = 0u;
    {
        // Serialize the queue-local reservation and its paired CPU record.
        ScopedLock lock(m_amdBreadcrumb.slotMutex);
        if(!VulkanDetail::TryBuildNextAmdBreadcrumbReservation(
            m_amdBreadcrumb.nextSerials[queue.index],
            m_amdBreadcrumb.layout.slotsPerQueue,
            reservation
        ))
            return write;
        if(!VulkanDetail::TryResolveAmdBreadcrumbRingSlot(
            m_amdBreadcrumb.layout,
            queue,
            reservation.localSlot,
            flatSlot,
            byteOffset
        ))
            return write;

        m_amdBreadcrumb.nextSerials[queue.index] = reservation.serial;
        m_amdBreadcrumb.slotRecords[flatSlot].serial = reservation.serial;
        m_amdBreadcrumb.slotRecords[flatSlot].markerHash = markerHash;
        m_amdBreadcrumb.slotRecords[flatSlot].marker = reservation.marker;
    }

    write.buffer = m_amdBreadcrumb.buffer;
    write.offset = byteOffset;
    write.marker = reservation.marker;
    write.valid = true;
    return write;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

