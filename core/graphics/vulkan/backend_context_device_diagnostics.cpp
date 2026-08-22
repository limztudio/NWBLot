// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"
#include "backend_context_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BackendContext::logVulkanDeviceConfiguration(
    Alloc::ScratchArena& scratchArena,
    const VkPhysicalDeviceProperties& physicalDeviceProperties,
    const bool maintenance4Enabled,
    const VkPhysicalDeviceMaintenance4Features& maintenance4Features,
    const bool createAsyncComputeQueue,
    const bool createCrossFamilySecondaryComputeQueue,
    const i32 secondaryComputeQueueFamily,
    const bool createDedicatedTransferQueue,
    const bool createCrossFamilySecondaryTransferQueue,
    const i32 secondaryTransferQueueFamily
){
    if(m_deviceParams.enableDebugRuntime){
        const u64 deviceLocalMemoryMiB = VulkanDetail::BytesToMiB(VulkanDetail::GetDeviceLocalMemoryBytes(m_vulkanPhysicalDevice));

        auto ss = VulkanDetail::MakeScratchStringStream(scratchArena);
        ss << "Vulkan GPU debug: selected device"
           << "\n    name: " << physicalDeviceProperties.deviceName
           << "\n    type: " << VulkanDetail::PhysicalDeviceTypeToString(physicalDeviceProperties.deviceType)
           << "\n    vendor/device id: 0x" << StreamHex << physicalDeviceProperties.vendorID << "/0x" << physicalDeviceProperties.deviceID << StreamDec
           << "\n    Vulkan API: " << VulkanDetail::VulkanVersionToString(scratchArena, physicalDeviceProperties.apiVersion)
           << "\n    driver version: " << physicalDeviceProperties.driverVersion
           << "\n    device-local memory: " << deviceLocalMemoryMiB << " MiB"
           << "\n    queue families: graphics=" << m_graphicsQueueFamily
        ;

        if(m_deviceParams.headlessDevice)
            ss << " present=headless";
        else
            ss << " present=" << m_presentQueueFamily;

        if(createAsyncComputeQueue)
            ss << " asyncCompute=" << m_computeQueueFamily;
        else
            ss << " asyncCompute=not-requested";
        if(createCrossFamilySecondaryComputeQueue)
            ss << " auxiliaryAsyncCompute=" << secondaryComputeQueueFamily;

        if(createDedicatedTransferQueue)
            ss << " transfer=" << m_transferQueueFamily;
        else
            ss << " transfer=not-requested";
        if(createCrossFamilySecondaryTransferQueue)
            ss << " auxiliaryTransfer=" << secondaryTransferQueueFamily;

        ss << "\n    key features: dynamicRendering=" << VulkanDetail::BoolToString(m_dynamicRenderingSupported)
           << " synchronization2=" << VulkanDetail::BoolToString(m_synchronization2Supported)
           << " bufferDeviceAddress=" << VulkanDetail::BoolToString(m_bufferDeviceAddressSupported)
           << " descriptorBuffer=" << VulkanDetail::BoolToString(isDeviceExtensionEnabled(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME))
           << " meshTaskShader=" << VulkanDetail::BoolToString(m_meshTaskShaderSupported)
           << " maintenance4=" << VulkanDetail::BoolToString(maintenance4Enabled && maintenance4Features.maintenance4 == VK_TRUE)
           << "\n    optional paths: meshShader=" << VulkanDetail::BoolToString(isDeviceExtensionEnabled(VK_EXT_MESH_SHADER_EXTENSION_NAME))
           << " rayTracingPipeline=" << VulkanDetail::BoolToString(isDeviceExtensionEnabled(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME))
           << " rayQuery=" << VulkanDetail::BoolToString(isDeviceExtensionEnabled(VK_KHR_RAY_QUERY_EXTENSION_NAME))
           << " shaderExecutionReordering=" << VulkanDetail::BoolToString(
                isDeviceExtensionEnabled(VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME)
                || isDeviceExtensionEnabled(VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME)
            )
           << " spheres=" << VulkanDetail::BoolToString(m_rayTracingSpheresSupported)
           << " linearSweptSpheres=" << VulkanDetail::BoolToString(m_rayTracingLinearSweptSpheresSupported)
           << " cooperativeVector=" << VulkanDetail::BoolToString(m_cooperativeVectorFeatureEnabled)
           << "\n    enabled device extensions: " << m_enabledExtensions.device.size()
        ;
        NWB_LOGGER_ESSENTIAL_INFO(StringConvert(ss.str()));
    }

    const auto sameClassQueueCount = [this](const CommandQueue::Enum queueClass){
        usize count = 0u;
        for(const VulkanPhysicalQueueDesc& queue : m_sameClassQueues){
            if(queue.queueClass == queueClass)
                ++count;
        }
        return count;
    };
    const usize sameClassGraphicsQueueCount = sameClassQueueCount(CommandQueue::Graphics);
    const usize sameClassComputeQueueCount = sameClassQueueCount(CommandQueue::Compute);
    const usize sameClassTransferQueueCount = sameClassQueueCount(CommandQueue::Transfer);

    const char* const sameClassGraphicsQueueReason = !m_deviceParams.enableSameClassMultiQueue
        ? "disabled"
        : m_sameClassGraphicsQueueEnabled
            ? m_secondaryGraphicsQueueFamily != m_graphicsQueueFamily
                ? "cross-family and/or primary-family Graphics queues selected"
                : "additional queues from the Graphics family selected"
            : m_deviceParams.enableCrossFamilySameClassQueueRouting
                ? "no alternate Graphics family or additional Graphics queues"
                : "Graphics family exposes no additional queues"
    ;
    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: same-class graphics queue requested={} crossFamilyRequested={} effective={} count={} graphicsFamily={} auxiliaryGraphicsFamily={} ({})")
        , StringConvert(VulkanDetail::BoolToString(m_deviceParams.enableSameClassMultiQueue))
        , StringConvert(VulkanDetail::BoolToString(m_deviceParams.enableCrossFamilySameClassQueueRouting))
        , StringConvert(VulkanDetail::BoolToString(m_sameClassGraphicsQueueEnabled))
        , sameClassGraphicsQueueCount
        , m_graphicsQueueFamily
        , m_secondaryGraphicsQueueFamily
        , StringConvert(sameClassGraphicsQueueReason)
    );

    m_asyncComputeLaneEnabled =
        m_deviceParams.enableAsyncComputeLane
        && m_computeQueue != VK_NULL_HANDLE
        && m_computeQueueFamily != s_InvalidQueueFamilyIndex
        && m_computeQueueFamily != m_graphicsQueueFamily
    ;
    const bool asyncComputeLaneEffective = m_asyncComputeLaneEnabled;
    const char* const asyncComputeLaneReason = !m_deviceParams.enableAsyncComputeLane
        ? "disabled"
        : asyncComputeLaneEffective
            ? "dedicated compute family selected"
            : "no dedicated compute-only family"
    ;
    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: async compute lane requested={} effective={} graphicsFamily={} computeFamily={} ({})")
        , StringConvert(VulkanDetail::BoolToString(m_deviceParams.enableAsyncComputeLane))
        , StringConvert(VulkanDetail::BoolToString(asyncComputeLaneEffective))
        , m_graphicsQueueFamily
        , m_computeQueueFamily
        , StringConvert(asyncComputeLaneReason)
    );

    const char* const sameClassComputeQueueReason = !m_deviceParams.enableSameClassMultiQueue
        ? "disabled"
        : !asyncComputeLaneEffective
            ? "primary dedicated Compute queue unavailable"
            : m_sameClassComputeQueueEnabled
                ? m_secondaryComputeQueueFamily != m_computeQueueFamily
                    ? "cross-family and/or primary-family Compute queues selected"
                    : "additional queues from the Compute family selected"
                : m_deviceParams.enableCrossFamilySameClassQueueRouting
                    ? "no alternate dedicated Compute family or additional Compute queues"
                    : "Compute family exposes no additional queues"
    ;
    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: same-class compute queue requested={} crossFamilyRequested={} effective={} count={} computeFamily={} auxiliaryComputeFamily={} ({})")
        , StringConvert(VulkanDetail::BoolToString(m_deviceParams.enableSameClassMultiQueue))
        , StringConvert(VulkanDetail::BoolToString(m_deviceParams.enableCrossFamilySameClassQueueRouting))
        , StringConvert(VulkanDetail::BoolToString(m_sameClassComputeQueueEnabled))
        , sameClassComputeQueueCount
        , m_computeQueueFamily
        , m_secondaryComputeQueueFamily
        , StringConvert(sameClassComputeQueueReason)
    );

    m_transferQueueEnabled =
        m_deviceParams.enableTransferQueue
        && m_transferQueue != VK_NULL_HANDLE
        && m_transferQueueFamily != s_InvalidQueueFamilyIndex
        && m_transferQueueFamily != m_graphicsQueueFamily
        && m_transferQueueFamily != m_computeQueueFamily
    ;
    const bool transferQueueEffective = m_transferQueueEnabled;
    const char* const transferQueueReason = !m_deviceParams.enableTransferQueue
        ? "disabled"
        : transferQueueEffective
            ? "dedicated transfer-only family selected"
            : "no dedicated transfer-only family"
    ;
    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: transfer queue requested={} effective={} graphicsFamily={} computeFamily={} transferFamily={} ({})")
        , StringConvert(VulkanDetail::BoolToString(m_deviceParams.enableTransferQueue))
        , StringConvert(VulkanDetail::BoolToString(transferQueueEffective))
        , m_graphicsQueueFamily
        , m_computeQueueFamily
        , m_transferQueueFamily
        , StringConvert(transferQueueReason)
    );

    const char* const sameClassTransferQueueReason = !m_deviceParams.enableSameClassMultiQueue
        ? "disabled"
        : !transferQueueEffective
            ? "primary dedicated Transfer queue unavailable"
            : m_sameClassTransferQueueEnabled
                ? m_secondaryTransferQueueFamily != m_transferQueueFamily
                    ? "cross-family and/or primary-family Transfer queues selected"
                    : "additional queues from the Transfer family selected"
                : m_deviceParams.enableCrossFamilySameClassQueueRouting
                    ? "no alternate dedicated Transfer family or additional Transfer queues"
                    : "Transfer family exposes no additional queues"
    ;
    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: same-class transfer queue requested={} crossFamilyRequested={} effective={} count={} transferFamily={} auxiliaryTransferFamily={} ({})")
        , StringConvert(VulkanDetail::BoolToString(m_deviceParams.enableSameClassMultiQueue))
        , StringConvert(VulkanDetail::BoolToString(m_deviceParams.enableCrossFamilySameClassQueueRouting))
        , StringConvert(VulkanDetail::BoolToString(m_sameClassTransferQueueEnabled))
        , sameClassTransferQueueCount
        , m_transferQueueFamily
        , m_secondaryTransferQueueFamily
        , StringConvert(sameClassTransferQueueReason)
    );

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Vulkan: created device '{}'"), m_rendererString);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

