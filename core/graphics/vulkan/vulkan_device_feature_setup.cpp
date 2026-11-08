// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_device_feature_setup.h"
#include "backend.h"
#include "arena_names.h"
#include "calibrated_timestamp_probe.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


void VulkanDeviceFeatureSetup::initialize(VulkanContext& context, const DeviceDesc& desc, const b8 gpuCrashDiagnosticsEnabled){
    context.extensions.bufferDeviceAddress = desc.bufferDeviceAddressSupported;
    context.hostQueryResetFeatureEnabled = desc.hostQueryResetFeatureEnabled;
    context.textureCompressionBcFeatureEnabled = desc.textureCompressionBcFeatureEnabled;
    context.textureCompressionAstcLdrFeatureEnabled = desc.textureCompressionAstcLdrFeatureEnabled;
    context.textureCompressionAstcHdrFeatureEnabled = desc.textureCompressionAstcHdrFeatureEnabled;
    context.extensions.khrDynamicRendering = desc.dynamicRenderingSupported;
    context.extensions.khrSynchronization2 = desc.synchronization2Supported;
    context.independentBlendFeatureEnabled = desc.independentBlendFeatureEnabled;
    context.fullDrawIndexUint32FeatureEnabled = desc.fullDrawIndexUint32FeatureEnabled;
    context.multiDrawIndirectFeatureEnabled = desc.multiDrawIndirectFeatureEnabled;
    context.drawIndirectFirstInstanceFeatureEnabled = desc.drawIndirectFirstInstanceFeatureEnabled;
    context.accelerationStructureFeatureEnabled = desc.accelerationStructureFeatureEnabled;
    context.rayTracingPipelineFeatureEnabled = desc.rayTracingPipelineFeatureEnabled;
    context.rayQueryFeatureEnabled = desc.rayQueryFeatureEnabled;
    context.opacityMicromapFeatureEnabled = desc.opacityMicromapFeatureEnabled;
    context.clusterAccelerationStructureFeatureEnabled = desc.clusterAccelerationStructureFeatureEnabled;
    context.rayTracingInvocationReorderFeatureEnabled = desc.rayTracingInvocationReorderFeatureEnabled;
    context.rayTracingInvocationReorderExtFeatureEnabled = desc.rayTracingInvocationReorderExtFeatureEnabled;

    for(usize i = 0u; i < desc.numInstanceExtensions; ++i){
        const AStringView ext = desc.instanceExtensions[i];
        if(ext == VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
            context.extensions.extDebugUtils = true;
    }

    for(usize i = 0u; i < desc.numDeviceExtensions; ++i){
        const AStringView ext = desc.deviceExtensions[i];
        if(ext == VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)
            context.extensions.khrSynchronization2 = true;
        else if(ext == VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)
            context.extensions.khrCalibratedTimestamps = true;
        else if(ext == VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME)
            context.extensions.khrRayTracingPipeline = true;
        else if(ext == VK_KHR_RAY_QUERY_EXTENSION_NAME)
            context.extensions.khrRayQuery = true;
        else if(ext == VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME)
            context.extensions.khrAccelerationStructure = true;
        else if(ext == VK_KHR_SWAPCHAIN_EXTENSION_NAME)
            context.extensions.khrSwapchain = true;
        else if(ext == VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)
            context.extensions.khrDynamicRendering = true;
        else if(ext == VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME)
            context.extensions.extDescriptorBuffer = true;
        else if(ext == VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME)
            context.extensions.extCalibratedTimestamps = true;
        else if(ext == VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME)
            context.extensions.extOpacityMicromap = true;
        else if(ext == VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME)
            context.extensions.nvCooperativeVector = true;
        else if(ext == VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME)
            context.extensions.nvClusterAccelerationStructure = true;
        else if(ext == VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME)
            context.extensions.nvDeviceDiagnosticCheckpoints = true;
        else if(ext == VK_EXT_DEVICE_FAULT_EXTENSION_NAME)
            context.extensions.extDeviceFault = true;
        else if(ext == VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME)
            context.extensions.extTextureCompressionAstcHdr = true;
        else if(ext == VK_AMD_BUFFER_MARKER_EXTENSION_NAME)
            context.extensions.amdBufferMarker = true;
        else if(ext == VK_EXT_MESH_SHADER_EXTENSION_NAME)
            context.extensions.extMeshShader = true;
        else if(ext == VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME)
            context.extensions.khrFragmentShadingRate = true;
        else if(ext == VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME)
            context.extensions.extRayTracingInvocationReorder = true;
        else if(ext == VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME)
            context.extensions.nvRayTracingInvocationReorder = true;
        else if(ext == VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME)
            context.extensions.nvRayTracingLinearSweptSpheres = true;
    }

    Alloc::ScratchArena calibratedTimestampProbeArena(VulkanArenaScope::s_DeviceExtensionSetupArena);
    b8 comparableGpuTimestamps = false;
    if(context.extensions.khrCalibratedTimestamps){
        comparableGpuTimestamps = VulkanDetail::CalibratedTimestampProbe::query(
            context.physicalDevice,
            context.device,
            context.instanceDispatch.vkGetPhysicalDeviceCalibrateableTimeDomainsKHR,
            context.deviceDispatch.vkGetCalibratedTimestampsKHR,
            calibratedTimestampProbeArena
        );
    }
    if(!comparableGpuTimestamps && context.extensions.extCalibratedTimestamps){
        comparableGpuTimestamps = VulkanDetail::CalibratedTimestampProbe::query(
            context.physicalDevice,
            context.device,
            context.instanceDispatch.vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
            context.deviceDispatch.vkGetCalibratedTimestampsEXT,
            calibratedTimestampProbeArena
        );
    }
    context.comparableGpuTimestamps = comparableGpuTimestamps;

    context.meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    if(context.extensions.extMeshShader){
        context.meshShaderFeatures.meshShader = desc.meshShaderFeatureEnabled ? VK_TRUE : VK_FALSE;
        context.meshShaderFeatures.taskShader = desc.meshTaskShaderSupported ? VK_TRUE : VK_FALSE;
    }

    context.rayTracingLinearSweptSpheresFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV;
    if(context.extensions.nvRayTracingLinearSweptSpheres){
        context.rayTracingLinearSweptSpheresFeatures.spheres = desc.rayTracingSpheresSupported ? VK_TRUE : VK_FALSE;
        context.rayTracingLinearSweptSpheresFeatures.linearSweptSpheres = desc.rayTracingLinearSweptSpheresSupported ? VK_TRUE : VK_FALSE;
    }

    context.coopVecFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_FEATURES_NV;
    if(context.extensions.nvCooperativeVector){
        context.coopVecFeatures.cooperativeVector = desc.cooperativeVectorFeatureEnabled ? VK_TRUE : VK_FALSE;
        context.coopVecFeatures.cooperativeVectorTraining = desc.cooperativeVectorTrainingFeatureEnabled ? VK_TRUE : VK_FALSE;
    }

    if(context.extensions.extDebugUtils && (!context.instanceDispatch.vkCmdBeginDebugUtilsLabelEXT || !context.instanceDispatch.vkCmdEndDebugUtilsLabelEXT)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Debug utils marker entry points are unavailable."));
        context.extensions.extDebugUtils = false;
    }

    if(context.extensions.nvDeviceDiagnosticCheckpoints && (!context.deviceDispatch.vkCmdSetCheckpointNV || !context.deviceDispatch.vkGetQueueCheckpointDataNV)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Device diagnostic checkpoint entry points are unavailable."));
        context.extensions.nvDeviceDiagnosticCheckpoints = false;
    }

    if(context.extensions.extDeviceFault && !context.deviceDispatch.vkGetDeviceFaultInfoEXT){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Device fault info entry point is unavailable."));
        context.extensions.extDeviceFault = false;
    }

    if(context.extensions.amdBufferMarker && !context.deviceDispatch.vkCmdWriteBufferMarkerAMD){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Buffer marker entry point is unavailable."));
        context.extensions.amdBufferMarker = false;
    }

    if(gpuCrashDiagnosticsEnabled && !context.extensions.nvDeviceDiagnosticCheckpoints && !context.extensions.amdBufferMarker && !context.extensions.extDeviceFault){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Vulkan: GPU crash diagnostics will use minimal text reports; device checkpoints, buffer markers, ") NWB_TEXT("and device fault are unavailable."));
    }

    if(
        context.extensions.khrAccelerationStructure
        && (
            !context.deviceDispatch.vkCreateAccelerationStructureKHR
            || !context.deviceDispatch.vkDestroyAccelerationStructureKHR
            || !context.deviceDispatch.vkGetAccelerationStructureBuildSizesKHR
            || !context.deviceDispatch.vkGetAccelerationStructureDeviceAddressKHR
            || !context.deviceDispatch.vkCmdBuildAccelerationStructuresKHR
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Acceleration structure entry points are unavailable."));
        context.extensions.khrAccelerationStructure = false;
        context.accelerationStructureFeatureEnabled = false;
    }

    if(context.extensions.khrRayTracingPipeline && (!context.deviceDispatch.vkCreateRayTracingPipelinesKHR || !context.deviceDispatch.vkGetRayTracingShaderGroupHandlesKHR || !context.deviceDispatch.vkCmdTraceRaysKHR)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ray tracing pipeline entry points are unavailable."));
        context.extensions.khrRayTracingPipeline = false;
        context.rayTracingPipelineFeatureEnabled = false;
    }

    if(context.extensions.extOpacityMicromap && (!context.deviceDispatch.vkCreateMicromapEXT || !context.deviceDispatch.vkDestroyMicromapEXT || !context.deviceDispatch.vkGetMicromapBuildSizesEXT || !context.deviceDispatch.vkCmdBuildMicromapsEXT)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Opacity micromap entry points are unavailable."));
        context.extensions.extOpacityMicromap = false;
        context.opacityMicromapFeatureEnabled = false;
    }

    if(context.extensions.nvClusterAccelerationStructure && (!context.deviceDispatch.vkGetClusterAccelerationStructureBuildSizesNV || !context.deviceDispatch.vkCmdBuildClusterAccelerationStructureIndirectNV)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster acceleration structure entry points are unavailable."));
        context.extensions.nvClusterAccelerationStructure = false;
        context.clusterAccelerationStructureFeatureEnabled = false;
    }

    if(
        context.extensions.nvCooperativeVector
        && (!context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV || !context.deviceDispatch.vkConvertCooperativeVectorMatrixNV || !context.deviceDispatch.vkCmdConvertCooperativeVectorMatrixNV)
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cooperative vector entry points are unavailable."));
        context.extensions.nvCooperativeVector = false;
        context.coopVecFeatures.cooperativeVector = VK_FALSE;
        context.coopVecFeatures.cooperativeVectorTraining = VK_FALSE;
    }

    if(context.extensions.extMeshShader && !context.deviceDispatch.vkCmdDrawMeshTasksEXT){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Mesh shader draw entry point is unavailable."));
        context.extensions.extMeshShader = false;
        context.meshShaderFeatures.meshShader = VK_FALSE;
        context.meshShaderFeatures.taskShader = VK_FALSE;
    }

    {
        auto props2 = VulkanDetail::MakeVkStruct<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
        void* pNext = nullptr;

        context.subgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
        context.subgroupProperties.pNext = pNext;
        pNext = &context.subgroupProperties;

        if(context.extensions.khrRayTracingPipeline){
            context.rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
            context.rayTracingPipelineProperties.pNext = pNext;
            pNext = &context.rayTracingPipelineProperties;
        }

        if(context.extensions.khrAccelerationStructure){
            context.accelStructProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
            context.accelStructProperties.pNext = pNext;
            pNext = &context.accelStructProperties;
        }

        if(context.extensions.extDescriptorBuffer){
            context.descriptorBufferProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;
            context.descriptorBufferProperties.pNext = pNext;
            pNext = &context.descriptorBufferProperties;
        }

        if(context.extensions.extMeshShader){
            context.meshShaderProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;
            context.meshShaderProperties.pNext = pNext;
            pNext = &context.meshShaderProperties;
        }

        if(context.extensions.nvClusterAccelerationStructure){
            context.nvClusterAccelerationStructureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_PROPERTIES_NV;
            context.nvClusterAccelerationStructureProperties.pNext = pNext;
            pNext = &context.nvClusterAccelerationStructureProperties;
        }

        if(context.extensions.nvCooperativeVector){
            context.coopVecProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_PROPERTIES_NV;
            context.coopVecProperties.pNext = pNext;
            pNext = &context.coopVecProperties;
        }

        if(pNext){
            props2.pNext = pNext;
            context.instanceDispatch.vkGetPhysicalDeviceProperties2(context.physicalDevice, &props2);
        }
    }
}


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
