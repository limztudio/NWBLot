
#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanArenaScope{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_InstanceCreateArena("core/graphics/backend_instance_create");
inline constexpr Name s_QueueFamilyQueryArena("core/graphics/backend_queue_family_query");
inline constexpr Name s_PhysicalDeviceSelectArena("core/graphics/backend_physical_device_select");
inline constexpr Name s_DeviceCreateArena("core/graphics/backend_device_create");
inline constexpr Name s_SwapChainPresentModeArena("core/graphics/backend_swap_chain_present_mode");
inline constexpr Name s_DeviceExtensionSetupArena("core/graphics/backend_device_extension_setup");
inline constexpr Name s_AdapterEnumerateArena("core/graphics/backend_adapter_enumerate");

inline constexpr Name s_ComputePipelineArena("core/graphics/compute_pipeline");

inline constexpr Name s_PipelineCacheSaveArena("core/graphics/device_pipeline_cache_save");
inline constexpr Name s_CommandListExecuteArena("core/graphics/device_command_list_execute");
inline constexpr Name s_CooperativeVectorQueryArena("core/graphics/device_cooperative_vector_query");

inline constexpr Name s_CooperativeVectorConvertArena("core/graphics/extensions_cooperative_vector_convert");
inline constexpr Name s_TextureTilingQueryArena("core/graphics/extensions_texture_tiling_query");

inline constexpr Name s_GraphicsPipelineArena("core/graphics/graphics_pipeline");

inline constexpr Name s_MeshletPipelineArena("core/graphics/meshlet_pipeline");

inline constexpr Name s_QueueSubmitArena("core/graphics/queue_submit");
inline constexpr Name s_SparseTextureBindArena("core/graphics/queue_sparse_texture_bind");

inline constexpr Name s_RayTracingArena("core/graphics/ray_tracing");

inline constexpr Name s_DescriptorBindingArena("core/graphics/descriptor_binding");

inline constexpr Name s_ShaderReflectionArena("core/graphics/shader_reflection");
inline constexpr Name s_InputLayoutArena("core/graphics/shader_input_layout");

inline constexpr Name s_TextureClearArena("core/graphics/texture_clear");
inline constexpr Name s_TextureResolveArena("core/graphics/texture_resolve");

inline constexpr Name s_SubmitChunksArena("core/graphics/upload_submit_chunks");

inline constexpr Name s_GpuCrashReportArena("core/graphics/gpu_crash_report");
inline constexpr Name s_GpuCrashVendorBinaryArena("core/graphics/gpu_crash_vendor_binary");
inline constexpr Name s_AftermathDumpArena("core/graphics/aftermath_dump");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

