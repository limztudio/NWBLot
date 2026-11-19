// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>
#include <core/alloc/module.h>

#if defined(FOUNDATION_PLATFORM_WINDOWS)
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#elif defined(FOUNDATION_PLATFORM_LINUX)
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


inline constexpr u32 s_MinimumVersion = static_cast<u32>(VK_MAKE_API_VERSION(0, 1, 3, 0));
inline constexpr u32 s_AppVersion = static_cast<u32>(VK_MAKE_API_VERSION(0, 1, 0, 0));
inline constexpr u32 s_EngineVersion = static_cast<u32>(VK_MAKE_API_VERSION(0, 1, 0, 0));
inline constexpr StringView s_AppName = "NWB";

inline constexpr u64 s_DefaultUploadChunkSize = 64 * 1024 * 1024;
inline constexpr u64 s_DefaultScratchChunkSize = 16 * 1024 * 1024;
inline constexpr u64 s_ScratchMemoryLimit = 256 * 1024 * 1024;
inline constexpr u64 s_DefaultAccelStructInputChunkSize = 4 * 1024 * 1024;
inline constexpr u64 s_AccelStructInputMemoryLimit = 64 * 1024 * 1024;
inline constexpr u64 s_LargeBufferThreshold = 16 * 1024 * 1024;
inline constexpr u64 s_BufferAlignmentBytes = 4;
inline constexpr u64 s_BufferAlignmentMask = s_BufferAlignmentBytes - 1u;
inline constexpr usize s_SpirvWordAlignmentBytes = sizeof(u32);
inline constexpr usize s_SpirvWordAlignmentMask = s_SpirvWordAlignmentBytes - 1u;
inline constexpr u32 s_VulkanMemoryTypeBitCount = sizeof(u32) * 8u;

inline constexpr usize s_GpuCrashReportArenaSize = 64u * 1024u;
inline constexpr u32 s_MaxGpuCrashCaptureEntries = 64u;
inline constexpr usize s_MaxGpuCrashMarkerChars = 256u;
inline constexpr usize s_MaxGpuCrashReportChars = 32u * 1024u;
inline constexpr usize s_MaxDeviceFaultVendorBinaryBytes = 512u * 1024u;
inline constexpr u32 s_MaxAmdBreadcrumbSlots = 256u;
inline constexpr u32 s_AmdVendorId = 0x1002;
inline constexpr u32 s_NvidiaVendorId = 0x10DE;

inline constexpr i32 s_InvalidQueueFamilyIndex = -1;
inline constexpr u32 s_GraphicsQueueIndex = 0;
inline constexpr u32 s_SecondaryGraphicsQueueIndex = s_GraphicsQueueIndex + 1u;
inline constexpr u32 s_ComputeQueueIndex = 0;
inline constexpr u32 s_TransferQueueIndex = 0;
inline constexpr u32 s_PresentQueueIndex = 0;
inline constexpr u32 s_MaxMutableSwapChainFormats = 2;

inline constexpr u32 s_TimerQueryTimestampCount = 2;
inline constexpr u32 s_TimerQueryBeginIndex = 0;
inline constexpr u32 s_TimerQueryEndIndex = 1;
inline constexpr f32 s_DefaultRasterLineWidth = 1.0f;
inline constexpr usize s_MeshletPipelineStageReserveCount = 3;

inline constexpr usize s_ParallelAdapterThreshold = 4;
inline constexpr usize s_ParallelCoopVecThreshold = 128;
inline constexpr usize s_ParallelSpecializationThreshold = 256;
inline constexpr usize s_ParallelInputLayoutThreshold = 128;
inline constexpr usize s_InputLayoutGrainSize = 64;
inline constexpr usize s_ParallelGeometryThreshold = 256;
inline constexpr usize s_GeometryGrainSize = 64;
inline constexpr usize s_ParallelTlasInstanceThreshold = 1024;
inline constexpr usize s_TlasInstanceGrainSize = 256;
inline constexpr usize s_DeviceSetupScratchArenaBytes = 32u * 1024u;

inline constexpr u64 s_DefaultTopLevelASBufferSize = 1024 * 1024;
inline constexpr u32 s_RayTracingHitGroupShaderStageCount = 3;
inline constexpr u32 s_TrianglesPerPrimitive = 3;
inline constexpr u32 s_InstanceFieldMask24Bit = 0x00FFFFFF;

inline constexpr usize s_GraphicsPipelineScratchArenaBytes = 2048;
inline constexpr usize s_RayTracingScratchArenaBytes = 4096;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

