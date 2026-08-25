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

void Device::captureGpuCrash(const AStringView context)noexcept{
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

                VkQueue queue = physicalQueue->m_queue;
                uint32_t checkpointCount = 0;
                vkGetQueueCheckpointDataNV(queue, &checkpointCount, nullptr);
                if(checkpointCount == 0)
                    continue;
                if(checkpointCount > remainingEntries)
                    checkpointCount = remainingEntries;

                Vector<VkCheckpointDataNV, Alloc::PersistentArena> checkpoints(m_gpuCrashReportArena);
                checkpoints.resize(checkpointCount, VulkanDetail::MakeVkStruct<VkCheckpointDataNV>(VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV));
                vkGetQueueCheckpointDataNV(queue, &checkpointCount, checkpoints.data());

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
                // Largest CPU sequence is best-effort: physical queues may execute unordered, and ring wrap reorders slots.
                u32 furthestSequence = 0u;
                u32 furthestSlot = 0u;
                for(u32 slot = 0u; slot < s_MaxAmdBreadcrumbSlots; ++slot){
                    if(breadcrumbSlots[slot] > furthestSequence){
                        furthestSequence = breadcrumbSlots[slot];
                        furthestSlot = slot;
                    }
                }

                if(furthestSequence != 0u){
                    AmdBreadcrumbSlotRecord record;
                    {
                        // Lock with breadcrumb writes to avoid torn records.
                        ScopedLock lock(m_amdBreadcrumb.slotMutex);
                        record = m_amdBreadcrumb.slotRecords[furthestSlot];
                    }
                    if(record.sequence == furthestSequence){
                        const auto resolved = m_gpuCrashTracker.resolveMarker(record.markerHash);
                        if(resolved.first()){
                            report.details.append(StringFormat(
                                m_gpuCrashReportArena,
                                "best-effort last-observed breadcrumb (seq {}): {}\n",
                                furthestSequence,
                                __hidden_vulkan_device_diagnostics::TrimGpuCrashText(resolved.second())
                            ));
                        }
                        else{
                            report.details.append(StringFormat(
                                m_gpuCrashReportArena,
                                "best-effort last-observed breadcrumb (seq {}): <unresolved marker>\n",
                                furthestSequence
                            ));
                        }
                    }
                    else{
                        report.details.append(StringFormat(
                            m_gpuCrashReportArena,
                            "best-effort last-observed breadcrumb (seq {}): <label overwritten>\n",
                            furthestSequence
                        ));
                    }
                    --remainingEntries;
                }
            }
        }

        if(hasDeviceFault && remainingEntries > 0u){
            auto faultCounts = VulkanDetail::MakeVkStruct<VkDeviceFaultCountsEXT>(VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT);
            if(vkGetDeviceFaultInfoEXT(m_context.device, &faultCounts, nullptr) == VK_SUCCESS){
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

                const VkResult faultResult = vkGetDeviceFaultInfoEXT(m_context.device, &faultCounts, &faultInfo);
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

Device::AmdBreadcrumbWrite Device::reserveAmdBreadcrumb(const usize markerHash){
    AmdBreadcrumbWrite write;
    if(m_amdBreadcrumb.buffer == VK_NULL_HANDLE)
        return write;

    // CPU sequence labels best-effort observations; physical-queue execution and ring-wrap ordering remain approximate.
    const u32 sequence = m_amdBreadcrumb.nextSequence.fetch_add(1u) + 1u;
    const u32 slot = sequence % s_MaxAmdBreadcrumbSlots;
    {
        // Serialize paired breadcrumb stores to avoid torn readback.
        ScopedLock lock(m_amdBreadcrumb.slotMutex);
        m_amdBreadcrumb.slotRecords[slot].markerHash = markerHash;
        m_amdBreadcrumb.slotRecords[slot].sequence = sequence;
    }

    write.buffer = m_amdBreadcrumb.buffer;
    write.offset = static_cast<VkDeviceSize>(slot) * sizeof(u32);
    write.marker = sequence;
    write.valid = true;
    return write;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

