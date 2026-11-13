// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "device_crash_diagnostics.h"

#include "backend.h"
#include "arena_names.h"
#include "amd_breadcrumb_ring.h"
#include "aftermath.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_device_diagnostics{

[[nodiscard]] static constexpr StringView BoolToAvailabilityText(const b8 value)noexcept{
    return value ? StringView("available") : StringView("unavailable");
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeviceCrashDiagnostics::DeviceCrashDiagnostics(GraphicsArena& arena, const b8 enabled)
    : m_enabled(enabled)
    , m_tracker(arena)
    , m_reportArena(VulkanArenaScope::s_GpuCrashReportArena, Alloc::PersistentArena::StructureAlignedSize(s_GpuCrashReportArenaSize))
    , m_vendorBinaryArena(VulkanArenaScope::s_GpuCrashVendorBinaryArena, Alloc::PersistentArena::StructureAlignedSize(s_MaxDeviceFaultVendorBinaryBytes))
{}


void DeviceCrashDiagnostics::capture(Device& device, const AStringView context){
    if(!m_enabled)
        return;

    const b8 hasCheckpoints = device.m_context.extensions.nvDeviceDiagnosticCheckpoints;
    const b8 hasDeviceFault = device.m_context.extensions.extDeviceFault;
    const b8 hasBufferMarker = device.m_context.extensions.amdBufferMarker && device.m_amdBreadcrumb.buffer != VK_NULL_HANDLE;

    // Capture only the first concurrent device-loss report.
    if(m_captured.exchange(true))
        return;

    GpuCrashReport report(m_reportArena);
    Vector<u8, Alloc::PersistentArena> vendorBinary(m_vendorBinaryArena);

    // Bound all temporary capture allocations to the fixed crash arenas.
    {
        report.details.reserve(s_MaxGpuCrashReportChars);
        report.context.append(context.data(), context.size());

        // One aggregate budget bounds all fault sections.
        u32 remainingEntries = s_MaxGpuCrashCaptureEntries;

        if(hasCheckpoints){
            for(const auto physicalQueue : device.m_physicalQueues){

                if(remainingEntries == 0u)
                    continue;

                // Vulkan checkpoint retrieval is a device-loss diagnostic and has no VkQueue host-synchronization requirement.
                // Do not wait behind a potentially wedged submission lock while collecting it.
                VkQueue queue = physicalQueue->m_nativeQueue.queue;
                uint32_t checkpointCount = 0;
                device.m_context.deviceDispatch.vkGetQueueCheckpointDataNV(queue, &checkpointCount, nullptr);
                if(checkpointCount == 0)
                    continue;
                if(checkpointCount > remainingEntries)
                    checkpointCount = remainingEntries;

                Vector<VkCheckpointDataNV, Alloc::PersistentArena> checkpoints(m_reportArena);
                checkpoints.resize(checkpointCount, VulkanDetail::MakeVkStruct<VkCheckpointDataNV>(VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV));
                device.m_context.deviceDispatch.vkGetQueueCheckpointDataNV(queue, &checkpointCount, checkpoints.data());

                for(const auto& checkpoint : checkpoints){
                    const usize markerHash = reinterpret_cast<usize>(checkpoint.pCheckpointMarker);
                    if(markerHash == 0)
                        continue;

                    const auto resolved = m_tracker.resolveMarker(markerHash);
                    if(!resolved.first())
                        continue;

                    StringAppendFormat(report.details, "last executed marker (stage 0x{:x}): {}\n", static_cast<u32>(checkpoint.stage), TruncateView(resolved.second(), s_MaxGpuCrashMarkerChars));
                }

                remainingEntries -= checkpointCount;
            }
        }

        if(hasBufferMarker && remainingEntries > 0u){
            FOUNDATION_ASSERT(device.m_amdBreadcrumb.mappedMemory && device.m_amdBreadcrumb.metadata && device.m_amdBreadcrumb.metadata->slotRecordCount == device.m_amdBreadcrumb.layout.totalSlotCount && device.m_amdBreadcrumb.metadata->nextSerialCount == device.m_amdBreadcrumb.layout.physicalQueueCount);
            const auto breadcrumbSlots = MakeNotNull(static_cast<const u32*>(device.m_amdBreadcrumb.mappedMemory));
            // Select each exact queue independently because physical queues remain unordered globally.
            for(usize queueIndex = 0u; queueIndex < device.m_amdBreadcrumb.layout.physicalQueueCount && remainingEntries > 0u; ++queueIndex){
                const GpuPhysicalQueueId& queue = device.m_physicalQueueInfos[queueIndex].id;
                const auto firstSlot = VulkanDetail::AmdBreadcrumbRing::ResolveSlot(device.m_amdBreadcrumb.layout, queue, 0u);
                if(!firstSlot)
                    continue;

                AmdBreadcrumbSlotRecord newestRecord;
                b8 hasObservedMarker = false;
                {
                    // Lock with breadcrumb reservations to avoid pairing an observation with a torn record.
                    ScopedLock lock(device.m_amdBreadcrumb.slotMutex);
                    for(usize localSlot = 0u; localSlot < device.m_amdBreadcrumb.layout.slotsPerQueue; ++localSlot){
                        const usize flatSlot = firstSlot->flatSlot + localSlot;
                        const u32 observedMarker = breadcrumbSlots.get()[flatSlot];
                        if(observedMarker == 0u)
                            continue;

                        hasObservedMarker = true;
                        const AmdBreadcrumbSlotRecord& record = device.m_amdBreadcrumb.metadata->slotRecords[flatSlot];
                        if(VulkanDetail::AmdBreadcrumbRing::MatchesObservation(observedMarker, record.marker) && record.serial > newestRecord.serial)
                            newestRecord = record;
                    }
                }

                if(!hasObservedMarker)
                    continue;

                if(newestRecord.serial != 0u){
                    const auto resolved = m_tracker.resolveMarker(newestRecord.markerHash);
                    if(resolved.first()){
                        StringAppendFormat(
                            report.details,
                            "best-effort last-observed breadcrumb (queue {}:{}, reservation {}, marker {}): {}\n",
                            queue.index,
                            queue.deviceGeneration,
                            newestRecord.serial,
                            newestRecord.marker,
                            TruncateView(resolved.second(), s_MaxGpuCrashMarkerChars)
                        );
                    }
                    else{
                        StringAppendFormat(
                            report.details,
                            "best-effort last-observed breadcrumb (queue {}:{}, reservation {}, marker {}): "
                            "<unresolved marker>\n",
                            queue.index,
                            queue.deviceGeneration,
                            newestRecord.serial,
                            newestRecord.marker
                        );
                    }
                }
                else{
                    StringAppendFormat(report.details, "best-effort last-observed breadcrumb (queue {}:{}): <label overwritten>\n", queue.index, queue.deviceGeneration);
                }
                --remainingEntries;
            }
        }

        if(hasDeviceFault && remainingEntries > 0u){
            auto faultCounts = VulkanDetail::MakeVkStruct<VkDeviceFaultCountsEXT>(VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT);
            if(device.m_context.deviceDispatch.vkGetDeviceFaultInfoEXT(device.m_context.device, &faultCounts, nullptr) == VK_SUCCESS){
                const VkDeviceSize vendorBinaryByteSize = faultCounts.vendorBinarySize;
                const b8 vendorBinaryIsRgd = device.m_context.physicalDeviceProperties.vendorID == s_AmdVendorId;
                if(faultCounts.addressInfoCount > remainingEntries)
                    faultCounts.addressInfoCount = remainingEntries;
                remainingEntries -= faultCounts.addressInfoCount;
                if(faultCounts.vendorInfoCount > remainingEntries)
                    faultCounts.vendorInfoCount = remainingEntries;
                remainingEntries -= faultCounts.vendorInfoCount;

                Vector<VkDeviceFaultAddressInfoEXT, Alloc::PersistentArena> addressInfos(m_reportArena);
                Vector<VkDeviceFaultVendorInfoEXT, Alloc::PersistentArena> vendorInfos(m_reportArena);
                addressInfos.resize(faultCounts.addressInfoCount, VkDeviceFaultAddressInfoEXT{});
                vendorInfos.resize(faultCounts.vendorInfoCount, VkDeviceFaultVendorInfoEXT{});
                if(vendorBinaryIsRgd && vendorBinaryByteSize != 0u && vendorBinaryByteSize <= static_cast<VkDeviceSize>(s_MaxDeviceFaultVendorBinaryBytes))
                    vendorBinary.resize(static_cast<usize>(vendorBinaryByteSize));

                auto faultInfo = VulkanDetail::MakeVkStruct<VkDeviceFaultInfoEXT>(VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT);
                faultInfo.pAddressInfos = addressInfos.empty() ? nullptr : addressInfos.data();
                faultInfo.pVendorInfos = vendorInfos.empty() ? nullptr : vendorInfos.data();
                faultInfo.pVendorBinaryData = vendorBinary.empty() ? nullptr : vendorBinary.data();

                const VkResult faultResult = device.m_context.deviceDispatch.vkGetDeviceFaultInfoEXT(device.m_context.device, &faultCounts, &faultInfo);
                if(faultResult == VK_SUCCESS || faultResult == VK_INCOMPLETE){
                    const AStringView faultDescription(faultInfo.description, FOUNDATION_STRNLEN(faultInfo.description, VK_MAX_DESCRIPTION_SIZE));
                    StringAppendFormat(report.details, "device fault: {}\n", TruncateView(faultDescription, s_MaxGpuCrashMarkerChars));
                    if(vendorBinaryByteSize != 0u){
                        if(!vendorBinary.empty()){
                            StringAppendFormat(report.details, "device fault vendor binary (RGD): {} bytes\n", vendorBinary.size());
                            report.binaryDumpKind = GpuCrashDumpKind::RadeonGpuDetective;
                            report.binaryDump = vendorBinary.data();
                            report.binaryDumpSize = vendorBinary.size();
                        }
                        else if(!vendorBinaryIsRgd){
                            StringAppendFormat(
                                report.details,
                                "device fault vendor binary not packaged: {} bytes from vendor 0x{:x}\n",
                                static_cast<u64>(vendorBinaryByteSize),
                                static_cast<u32>(device.m_context.physicalDeviceProperties.vendorID)
                            );
                        }
                        else{
                            StringAppendFormat(report.details, "device fault vendor binary skipped: {} bytes exceeds {} byte cap\n", static_cast<u64>(vendorBinaryByteSize), static_cast<u64>(s_MaxDeviceFaultVendorBinaryBytes));
                        }
                    }

                    for(u32 i = 0u; i < faultCounts.addressInfoCount; ++i){
                        const VkDeviceFaultAddressInfoEXT& addressInfo = addressInfos[i];
                        StringAppendFormat(report.details, "fault address 0x{:x} (type {}, precision 0x{:x})\n"
                            , static_cast<u64>(addressInfo.reportedAddress)
                            , static_cast<u32>(addressInfo.addressType)
                            , static_cast<u64>(addressInfo.addressPrecision)
                        );
                    }

                    for(u32 i = 0u; i < faultCounts.vendorInfoCount; ++i){
                        const VkDeviceFaultVendorInfoEXT& vendorInfo = vendorInfos[i];
                        const AStringView vendorDescription(vendorInfo.description, FOUNDATION_STRNLEN(vendorInfo.description, VK_MAX_DESCRIPTION_SIZE));
                        StringAppendFormat(report.details, "vendor fault '{}' (code 0x{:x}, data 0x{:x})\n"
                            , TruncateView(vendorDescription, s_MaxGpuCrashMarkerChars)
                            , static_cast<u64>(vendorInfo.vendorFaultCode)
                            , static_cast<u64>(vendorInfo.vendorFaultData)
                        );
                    }
                }
            }
        }

        if(report.details.empty())
            StringAppendFormat(
                report.details,
                "minimal GPU crash report: no vendor GPU dump or device-fault details were available\n"
                "capture context: {}\n"
                "device: {} (vendor 0x{:x}, device 0x{:x}, driver 0x{:x})\n"
                "diagnostic paths: NV_device_diagnostic_checkpoints={}, AMD_buffer_marker={}, VK_EXT_device_fault={}, NVIDIA Aftermath={}\n"
                , TruncateView(context, s_MaxGpuCrashMarkerChars)
                , TruncateView(AStringView(device.m_context.physicalDeviceProperties.deviceName), s_MaxGpuCrashMarkerChars)
                , static_cast<u32>(device.m_context.physicalDeviceProperties.vendorID)
                , static_cast<u32>(device.m_context.physicalDeviceProperties.deviceID)
                , static_cast<u32>(device.m_context.physicalDeviceProperties.driverVersion)
                , __hidden_device_diagnostics::BoolToAvailabilityText(hasCheckpoints)
                , __hidden_device_diagnostics::BoolToAvailabilityText(hasBufferMarker)
                , __hidden_device_diagnostics::BoolToAvailabilityText(hasDeviceFault)
                , __hidden_device_diagnostics::BoolToAvailabilityText(Aftermath::IsActive())
            );
    }

    NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: GPU crash detected during {}:\n{}"), ConvertString(report.context), ConvertString(report.details));

    // Attach available Aftermath dump while its bytes remain owned by the module.
    if(Aftermath::IsActive()){
        const Aftermath::GpuCrashDumpView dump = Aftermath::WaitForCrashDump();
        if(dump.data && dump.size != 0u){
            report.binaryDumpKind = GpuCrashDumpKind::Aftermath;
            report.binaryDump = dump.data;
            report.binaryDumpSize = dump.size;
        }
    }

    DispatchGpuCrash(report);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

