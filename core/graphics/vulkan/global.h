// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>
#include <core/alloc/module.h>
#include <core/graphics/vulkan/default_values.adhd.h>

#if defined(NWB_PLATFORM_WINDOWS)
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#elif defined(NWB_PLATFORM_LINUX)
#ifndef VK_USE_PLATFORM_XLIB_KHR
#define VK_USE_PLATFORM_XLIB_KHR
#endif
#if defined(NWB_WITH_WAYLAND)
#ifndef VK_USE_PLATFORM_WAYLAND_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#endif
#endif

#include <volk/volk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_VULKAN_BEGIN NWB_CORE_BEGIN namespace GraphicsBackend{
#define NWB_VULKAN_END }; NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Application and API versions.
inline constexpr u32 s_MinimumVersion = static_cast<u32>(VK_MAKE_API_VERSION(0, 1, 3, 0));
inline constexpr u32 s_AppVersion = static_cast<u32>(VK_MAKE_API_VERSION(0, 1, 0, 0));
inline constexpr u32 s_EngineVersion = static_cast<u32>(VK_MAKE_API_VERSION(0, 1, 0, 0));
inline constexpr StringView s_AppName = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultAppName;

// Device memory and upload defaults.
inline constexpr u64 s_DefaultUploadChunkSize = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultDefaultUploadChunkSize;
inline constexpr u64 s_DefaultScratchChunkSize = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultDefaultScratchChunkSize;
inline constexpr u64 s_ScratchMemoryLimit = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultScratchMemoryLimit;
inline constexpr u64 s_DefaultAccelStructInputChunkSize = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultDefaultAccelStructInputChunkSize;
inline constexpr u64 s_AccelStructInputMemoryLimit = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultAccelStructInputMemoryLimit;
inline constexpr u64 s_LargeBufferThreshold = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultLargeBufferThreshold;
inline constexpr u64 s_BufferAlignmentBytes = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultBufferAlignmentBytes;
inline constexpr u64 s_BufferAlignmentMask = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultBufferAlignmentMask;
inline constexpr usize s_SpirvWordAlignmentBytes = sizeof(u32);
inline constexpr usize s_SpirvWordAlignmentMask = s_SpirvWordAlignmentBytes - 1u;
inline constexpr u32 s_VulkanMemoryTypeBitCount = sizeof(u32) * 8u;

// Fixed-size, pre-reserved arena for GPU crash reports.
// Captured on device-lost, so it must not touch the growable heap (which may be unsafe at crash time); the block is reserved up front.
inline constexpr usize s_GpuCrashReportArenaSize = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultGpuCrashReportArenaSize;
inline constexpr u32 s_MaxGpuCrashCaptureEntries = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultMaxGpuCrashCaptureEntries; // aggregate cap across all queues and fault sections
inline constexpr usize s_MaxGpuCrashMarkerChars = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultMaxGpuCrashMarkerChars; // per-marker truncation so one deep label cannot blow the budget
inline constexpr usize s_MaxGpuCrashReportChars = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultMaxGpuCrashReportChars; // report.details capacity; keeps peak use under the arena (no reallocs)
inline constexpr usize s_MaxDeviceFaultVendorBinaryBytes = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultMaxDeviceFaultVendorBinaryBytes; // vendor blob cap; large dumps are skipped on device-lost.
// Slots in each exact physical queue's disjoint AMD breadcrumb ring.
inline constexpr u32 s_MaxAmdBreadcrumbSlots = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultMaxAmdBreadcrumbSlots;
inline constexpr u32 s_AmdVendorId = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultAmdVendorId; // PCI vendor ID; gates Radeon GPU Detective naming.
inline constexpr u32 s_NvidiaVendorId = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultNvidiaVendorId; // PCI vendor ID; gates NVIDIA-only Aftermath setup

// Queue and swap chain defaults.
// Local sentinel used until a compatible queue family is selected.
inline constexpr i32 s_InvalidQueueFamilyIndex = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultInvalidQueueFamilyIndex;
inline constexpr u32 s_GraphicsQueueIndex = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultGraphicsQueueIndex;
inline constexpr u32 s_SecondaryGraphicsQueueIndex = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultSecondaryGraphicsQueueIndex;
inline constexpr u32 s_ComputeQueueIndex = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultComputeQueueIndex;
inline constexpr u32 s_TransferQueueIndex = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultTransferQueueIndex;
inline constexpr u32 s_PresentQueueIndex = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultPresentQueueIndex;
inline constexpr u32 s_MaxMutableSwapChainFormats = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultMaxMutableSwapChainFormats;

// Query and raster defaults.
inline constexpr u32 s_TimerQueryTimestampCount = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultTimerQueryTimestampCount;
inline constexpr u32 s_TimerQueryBeginIndex = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultTimerQueryBeginIndex;
inline constexpr u32 s_TimerQueryEndIndex = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultTimerQueryEndIndex;
inline constexpr f32 s_DefaultRasterLineWidth = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultDefaultRasterLineWidth;
inline constexpr usize s_MeshletPipelineStageReserveCount = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultMeshletPipelineStageReserveCount;

// CPU-side parallel tuning for Vulkan helper paths.
inline constexpr usize s_ParallelAdapterThreshold = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultParallelAdapterThreshold;
inline constexpr usize s_ParallelCoopVecThreshold = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultParallelCoopVecThreshold;
inline constexpr usize s_ParallelSpecializationThreshold = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultParallelSpecializationThreshold;
inline constexpr usize s_ParallelInputLayoutThreshold = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultParallelInputLayoutThreshold;
inline constexpr usize s_InputLayoutGrainSize = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultInputLayoutGrainSize;
inline constexpr usize s_ParallelGeometryThreshold = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultParallelGeometryThreshold;
inline constexpr usize s_GeometryGrainSize = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultGeometryGrainSize;
inline constexpr usize s_ParallelTlasInstanceThreshold = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultParallelTlasInstanceThreshold;
inline constexpr usize s_TlasInstanceGrainSize = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultTlasInstanceGrainSize;
inline constexpr usize s_DeviceSetupScratchArenaBytes = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultDeviceSetupScratchArenaBytes;

// Ray tracing helper defaults.
inline constexpr u64 s_DefaultTopLevelASBufferSize = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultDefaultTopLevelASBufferSize;
inline constexpr u32 s_RayTracingHitGroupShaderStageCount = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultRayTracingHitGroupShaderStageCount;
inline constexpr u32 s_TrianglesPerPrimitive = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultTrianglesPerPrimitive;
inline constexpr u32 s_InstanceFieldMask24Bit = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultInstanceFieldMask24Bit;

// Scratch arena presets for transient Vulkan-side CPU allocations.
inline constexpr usize s_GraphicsPipelineScratchArenaBytes = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultGraphicsPipelineScratchArenaBytes;
inline constexpr usize s_RayTracingScratchArenaBytes = NWB::Core::GraphicsBackend::VulkanDefaults::s_VulkanDefaultRayTracingScratchArenaBytes;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

