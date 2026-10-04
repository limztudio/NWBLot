// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanArenaScope{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_InstanceCreateArena("core/graphics/backend_instance_create");
inline constexpr TStringView s_TaskStageLabel = GLOBAL_TEXT("task");
inline constexpr TStringView s_MeshStageLabel = GLOBAL_TEXT("mesh");
inline constexpr TStringView s_FragmentStageLabel = GLOBAL_TEXT("fragment");
inline constexpr TStringView s_BufferResourceLabel = GLOBAL_TEXT("buffer");
inline constexpr TStringView s_TextureResourceLabel = GLOBAL_TEXT("texture");
inline constexpr TStringView s_WriteBufferCommandLabel = GLOBAL_TEXT("write buffer");
inline constexpr TStringView s_SetComputeStateCommandLabel = GLOBAL_TEXT("set compute state");
inline constexpr TStringView s_SetPermanentTextureStateCommandLabel = GLOBAL_TEXT("set permanent texture state");
inline constexpr TStringView s_DispatchMeshCommandLabel = GLOBAL_TEXT("dispatch mesh");
inline constexpr TStringView s_DispatchIndirectCommandLabel = GLOBAL_TEXT("dispatch indirect");
inline constexpr AStringView s_PresentationSignalCancellationContext = "presentation signal cancellation";
inline constexpr AStringView s_AcquireSlotReuseContext = "acquire slot reuse";
inline constexpr AStringView s_AcquireNextImageContext = "acquire next image";
inline constexpr AStringView s_QueueSubmitContext = "queue submit";
inline constexpr AStringView s_NativePresentAdmissionContext = "native present admission";
inline constexpr AStringView s_AcquiredImageSemaphoreBridgeContext = "acquired image semaphore bridge";
inline constexpr AStringView s_AbandonedPresentationSignalIdleContext = "abandoned presentation signal idle";
inline constexpr AStringView s_UnconsumedPresentationSignalIdleContext = "unconsumed presentation signal idle";
inline constexpr AStringView s_PresentContext = "present";
inline constexpr Name s_QueueFamilyQueryArena("core/graphics/backend_queue_family_query");
inline constexpr Name s_PhysicalDeviceSelectArena("core/graphics/backend_physical_device_select");
inline constexpr Name s_DeviceCreateArena("core/graphics/backend_device_create");
inline constexpr Name s_SwapChainPresentModeArena("core/graphics/backend_swap_chain_present_mode");
inline constexpr Name s_DeviceExtensionSetupArena("core/graphics/backend_device_extension_setup");
inline constexpr Name s_AdapterEnumerateArena("core/graphics/backend_adapter_enumerate");

inline constexpr Name s_ComputePipelineArena("core/graphics/compute_pipeline");

inline constexpr Name s_PipelineCacheSaveArena("core/graphics/device_pipeline_cache_save");
inline constexpr Name s_CommandListExecuteArena("core/graphics/device_command_list_execute");
inline constexpr Name s_StateHandoffArena("core/graphics/command_list_state_handoff");
inline constexpr Name s_CooperativeVectorQueryArena("core/graphics/device_cooperative_vector_query");

inline constexpr Name s_CooperativeVectorConvertArena("core/graphics/extensions_cooperative_vector_convert");

inline constexpr Name s_GraphicsPipelineArena("core/graphics/graphics_pipeline");

inline constexpr Name s_MeshletPipelineArena("core/graphics/meshlet_pipeline");

inline constexpr Name s_RayTracingArena("core/graphics/ray_tracing");

inline constexpr Name s_DescriptorBindingArena("core/graphics/descriptor_binding");
inline constexpr Name s_DescriptorHeapStorageArena("core/graphics/descriptor_heap_storage");

inline constexpr Name s_InputLayoutArena("core/graphics/shader_input_layout");

inline constexpr Name s_TextureClearArena("core/graphics/texture_clear");
inline constexpr Name s_TextureResolveArena("core/graphics/texture_resolve");

inline constexpr Name s_GpuCrashReportArena("core/graphics/gpu_crash_report");
inline constexpr Name s_GpuCrashVendorBinaryArena("core/graphics/gpu_crash_vendor_binary");
inline constexpr Name s_AmdBreadcrumbMetadataArena("core/graphics/amd_breadcrumb_metadata");
inline constexpr Name s_AftermathDumpArena("core/graphics/aftermath_dump");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

