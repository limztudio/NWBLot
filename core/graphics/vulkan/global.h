// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>
#include <core/alloc/alloc.h>

#include <core/graphics/common.h>

#if defined(NWB_PLATFORM_WINDOWS)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(NWB_PLATFORM_LINUX)
#define VK_USE_PLATFORM_XCB_KHR
#elif defined(NWB_PLATFORM_ANDROID)
#define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(NWB_PLATFORM_APPLE)
#define VK_USE_PLATFORM_METAL_EXT
#endif

#include <volk/volk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_VULKAN_BEGIN NWB_CORE_BEGIN namespace Vulkan{
#define NWB_VULKAN_END }; NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ObjectTypes{
    constexpr ObjectType NWB_VK_Device = 0x00030101;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Application and API versions.
constexpr u32 s_MinimumVersion = static_cast<u32>(VK_MAKE_API_VERSION(0, 1, 3, 0));
constexpr u32 s_AppVersion = static_cast<u32>(VK_MAKE_API_VERSION(0, 1, 0, 0));
constexpr u32 s_EngineVersion = static_cast<u32>(VK_MAKE_API_VERSION(0, 1, 0, 0));
constexpr const char* s_AppName = "NWBLot";

// Device memory and upload defaults.
constexpr u64 s_DefaultUploadChunkSize = 64 * 1024 * 1024; // 64 MB
constexpr u64 s_DefaultScratchChunkSize = 16 * 1024 * 1024; // 16 MB
constexpr u64 s_ScratchMemoryLimit = 256 * 1024 * 1024; // 256 MB
constexpr u64 s_LargeBufferThreshold = 16 * 1024 * 1024; // 16 MB
constexpr u32 s_DefaultUploadSuballocationAlignment = s_ConstantBufferOffsetSizeAlignment;
constexpr u64 s_BufferAlignmentBytes = 4;

// Queue and swap chain defaults.
constexpr u32 s_GraphicsQueueIndex = 0;
constexpr u32 s_ComputeQueueIndex = 0;
constexpr u32 s_TransferQueueIndex = 0;
constexpr u32 s_PresentQueueIndex = 0;
constexpr u32 s_MaxMutableSwapChainFormats = 2;
constexpr usize s_MaxRetryCountAcquireNextImage = 3;

// Query and raster defaults.
constexpr u32 s_TimerQueryTimestampCount = 2;
constexpr u32 s_TimerQueryBeginIndex = 0;
constexpr u32 s_TimerQueryEndIndex = 1;
constexpr u32 s_SingleQueryCount = 1;
constexpr f32 s_DefaultRasterLineWidth = 1.0f;
constexpr usize s_MeshletPipelineStageReserveCount = 3;

// CPU-side parallel tuning for Vulkan helper paths.
constexpr usize s_CopyHostMemoryParallelThreshold = 1024 * 1024;
constexpr usize s_CopyHostMemoryChunkSize = 256 * 1024;
constexpr usize s_ParallelAdapterThreshold = 4;
constexpr usize s_ParallelCoopVecThreshold = 128;
constexpr usize s_ParallelSpecializationThreshold = 256;
constexpr usize s_ParallelInputLayoutThreshold = 128;
constexpr usize s_InputLayoutGrainSize = 64;
constexpr usize s_ParallelConvertThreshold = 256;
constexpr usize s_ConvertGrainSize = 64;
constexpr usize s_ParallelTileCountThreshold = 128;
constexpr usize s_TileCountGrainSize = 16;
constexpr usize s_ParallelTileMappingThreshold = 128;
constexpr usize s_TileMappingGrainSize = 8;
constexpr usize s_ParallelGeometryThreshold = 256;
constexpr usize s_GeometryGrainSize = 64;
constexpr usize s_ParallelTlasInstanceThreshold = 1024;
constexpr usize s_TlasInstanceGrainSize = 256;

// Ray tracing helper defaults.
constexpr u64 s_DefaultTopLevelASBufferSize = 1024 * 1024;
constexpr u32 s_DefaultTlasBufferSizeMultiplier = 2;
constexpr u64 s_AccelerationStructureAlignment = s_ConstantBufferOffsetSizeAlignment;
constexpr u32 s_RayTracingHitGroupShaderStageCount = 3;
constexpr u32 s_TrianglesPerPrimitive = 3;
constexpr u32 s_InstanceFieldMask24Bit = 0x00FFFFFF;

// Texture view key packing.
constexpr u32 s_TextureViewKeyBaseMipShift = 0;
constexpr u32 s_TextureViewKeyMipCountShift = 8;
constexpr u32 s_TextureViewKeyBaseArraySliceShift = 16;
constexpr u32 s_TextureViewKeyArraySliceCountShift = 24;
constexpr u32 s_TextureViewKeyDimensionShift = 32;
constexpr u32 s_TextureViewKeyFormatShift = 40;
constexpr u32 s_TextureViewKeyReadOnlyDsvShift = 48;

// Scratch arena presets for transient Vulkan-side CPU allocations.
constexpr usize s_GraphicsPipelineScratchArenaBytes = 2048;
constexpr usize s_DescriptorBindingScratchArenaBytes = 4096;
constexpr usize s_RayTracingScratchArenaBytes = 4096;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename U>
inline T checked_cast(U u){
    static_assert(!IsSame<T, U>::value, "Unnecessary checked_cast");
    if(!u)
        return nullptr;
    T t = static_cast<T>(u);
#ifdef NWB_DEBUG
    NWB_ASSERT(t);
#endif
    return t;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

