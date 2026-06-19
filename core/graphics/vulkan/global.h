// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>
#include <core/alloc/module.h>

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
#elif defined(NWB_PLATFORM_ANDROID)
#define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(NWB_PLATFORM_APPLE)
#define VK_USE_PLATFORM_METAL_EXT
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
inline constexpr const char* s_AppName = "NWB";

// Device memory and upload defaults.
inline constexpr u64 s_DefaultUploadChunkSize = 64 * 1024 * 1024; // 64 MB
inline constexpr u64 s_DefaultScratchChunkSize = 16 * 1024 * 1024; // 16 MB
inline constexpr u64 s_ScratchMemoryLimit = 256 * 1024 * 1024; // 256 MB
inline constexpr u64 s_LargeBufferThreshold = 16 * 1024 * 1024; // 16 MB
inline constexpr u64 s_BufferAlignmentBytes = 4;

// Fixed-size, pre-reserved arena for GPU crash reports. Captured on device-lost, so it must
// not touch the growable heap (which may be unsafe at crash time); the block is reserved up front.
inline constexpr usize s_GpuCrashReportArenaSize = 64u * 1024u; // 64 KB
inline constexpr u32 s_MaxGpuCrashCaptureEntries = 64u; // cap on markers/fault entries formatted into the fixed crash arena

// Queue and swap chain defaults.
inline constexpr u32 s_GraphicsQueueIndex = 0;
inline constexpr u32 s_ComputeQueueIndex = 0;
inline constexpr u32 s_TransferQueueIndex = 0;
inline constexpr u32 s_PresentQueueIndex = 0;
inline constexpr u32 s_MaxMutableSwapChainFormats = 2;
inline constexpr usize s_MaxRetryCountAcquireNextImage = 3;

// Query and raster defaults.
inline constexpr u32 s_TimerQueryTimestampCount = 2;
inline constexpr u32 s_TimerQueryBeginIndex = 0;
inline constexpr u32 s_TimerQueryEndIndex = 1;
inline constexpr u32 s_SingleQueryCount = 1;
inline constexpr f32 s_DefaultRasterLineWidth = 1.0f;
inline constexpr usize s_MeshletPipelineStageReserveCount = 3;

// CPU-side parallel tuning for Vulkan helper paths.
inline constexpr usize s_CopyHostMemoryParallelThreshold = 1024 * 1024;
inline constexpr usize s_CopyHostMemoryChunkSize = 256 * 1024;
inline constexpr usize s_ParallelAdapterThreshold = 4;
inline constexpr usize s_ParallelCoopVecThreshold = 128;
inline constexpr usize s_ParallelSpecializationThreshold = 256;
inline constexpr usize s_ParallelInputLayoutThreshold = 128;
inline constexpr usize s_InputLayoutGrainSize = 64;
inline constexpr usize s_ParallelConvertThreshold = 256;
inline constexpr usize s_ConvertGrainSize = 64;
inline constexpr usize s_ParallelTileCountThreshold = 128;
inline constexpr usize s_TileCountGrainSize = 16;
inline constexpr usize s_ParallelTileMappingThreshold = 128;
inline constexpr usize s_TileMappingGrainSize = 8;
inline constexpr usize s_ParallelGeometryThreshold = 256;
inline constexpr usize s_GeometryGrainSize = 64;
inline constexpr usize s_ParallelTlasInstanceThreshold = 1024;
inline constexpr usize s_TlasInstanceGrainSize = 256;

// Ray tracing helper defaults.
inline constexpr u64 s_DefaultTopLevelASBufferSize = 1024 * 1024;
inline constexpr u32 s_RayTracingHitGroupShaderStageCount = 3;
inline constexpr u32 s_TrianglesPerPrimitive = 3;
inline constexpr u32 s_InstanceFieldMask24Bit = 0x00FFFFFF;

// Scratch arena presets for transient Vulkan-side CPU allocations.
inline constexpr usize s_GraphicsPipelineScratchArenaBytes = 2048;
inline constexpr usize s_DescriptorBindingScratchArenaBytes = 4096;
inline constexpr usize s_RayTracingScratchArenaBytes = 4096;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

