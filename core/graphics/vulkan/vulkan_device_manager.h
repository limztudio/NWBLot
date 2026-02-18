// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeviceManager final : public IDeviceManager{
private:
    struct VulkanExtensionSet{
        HashSet<AString> instance;
        HashSet<AString> layers;
        HashMap<AString, void*> device; // extension name → feature struct pointer (nullptr if none)
    };

    struct SwapChainImage{
        VkImage image;
        TextureHandle rhiHandle;
    };


public:
    [[nodiscard]] virtual IDevice* getDevice()const override;
    [[nodiscard]] virtual GraphicsAPI::Enum getGraphicsAPI()const override{ return GraphicsAPI::VULKAN; }
    [[nodiscard]] virtual const tchar* getRendererString()const override;

    virtual bool enumerateAdapters(Vector<AdapterInfo>& outAdapters) override;

    virtual bool isVulkanInstanceExtensionEnabled(const char* extensionName)const override;
    virtual bool isVulkanDeviceExtensionEnabled(const char* extensionName)const override;
    virtual bool isVulkanLayerEnabled(const char* layerName)const override;
    virtual void getEnabledVulkanInstanceExtensions(Vector<AString>& extensions)const override;
    virtual void getEnabledVulkanDeviceExtensions(Vector<AString>& extensions)const override;
    virtual void getEnabledVulkanLayers(Vector<AString>& layers)const override;

    virtual ITexture* getCurrentBackBuffer() override;
    virtual ITexture* getBackBuffer(u32 index) override;
    virtual u32 getCurrentBackBufferIndex() override;
    virtual u32 getBackBufferCount() override;

protected:
    virtual bool createInstanceInternal() override;
    virtual bool createDeviceInternal() override;
    virtual bool createSwapChainInternal() override;
    virtual void destroyDeviceAndSwapChain() override;
    virtual void resizeSwapChain() override;
    virtual bool beginFrame() override;
    virtual bool present() override;

private:
    bool createInstance();
    bool createWindowSurface();
    void installDebugCallback();
    bool pickPhysicalDevice();
    bool findQueueFamilies(VkPhysicalDevice physicalDevice);
    bool createDevice();
    bool createSwapChain();
    void destroySwapChain();


private:
    // Feature structs for optional device extensions (must outlive createDevice)
    VkPhysicalDeviceAccelerationStructureFeaturesKHR m_accelStructFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, nullptr,
        VK_TRUE, // accelerationStructure
    };
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR m_rayPipelineFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, nullptr,
        VK_TRUE, // rayTracingPipeline
        VK_FALSE, // rayTracingPipelineShaderGroupHandleCaptureReplay
        VK_FALSE, // rayTracingPipelineShaderGroupHandleCaptureReplayMixed
        VK_FALSE, // rayTracingPipelineTraceRaysIndirect
        VK_TRUE, // rayTraversalPrimitiveCulling
    };
    VkPhysicalDeviceRayQueryFeaturesKHR m_rayQueryFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, nullptr,
        VK_TRUE, // rayQuery
    };
    VkPhysicalDeviceMeshShaderFeaturesNV m_meshletFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV, nullptr,
        VK_TRUE, // taskShader
        VK_TRUE, // meshShader
    };
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR m_vrsFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR, nullptr,
        VK_TRUE, // pipelineFragmentShadingRate
        VK_TRUE, // primitiveFragmentShadingRate
        VK_TRUE, // attachmentFragmentShadingRate
    };
    VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT m_mutableDescriptorTypeFeatures = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT, nullptr,
        VK_TRUE, // mutableDescriptorType
    };


private:
    // minimal set of required extensions
    VulkanExtensionSet m_enabledExtensions = {
        { // instance
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
        },
        { // layers
        },
        { // device (name → feature struct)
            { VK_KHR_MAINTENANCE1_EXTENSION_NAME, nullptr },
        },
    };

    // optional extensions (name → feature struct; nullptr = no feature struct needed)
    VulkanExtensionSet m_optionalExtensions = {
        { // instance
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
            VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME,
        },
        { // layers
        },
        { // device
            { VK_EXT_DEBUG_MARKER_EXTENSION_NAME, nullptr },
            { VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, nullptr },
            { VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, nullptr },
            { VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, &m_vrsFeatures },
            { VK_KHR_MAINTENANCE_4_EXTENSION_NAME, nullptr },
            { VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME, nullptr },
            { VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME, nullptr },
            { VK_NV_MESH_SHADER_EXTENSION_NAME, &m_meshletFeatures },
            { VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME, &m_mutableDescriptorTypeFeatures },
        },
    };

    HashMap<AString, void*> m_rayTracingExtensions = {
        { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, &m_accelStructFeatures },
        { VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, nullptr },
        { VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME, nullptr },
        { VK_KHR_RAY_QUERY_EXTENSION_NAME, &m_rayQueryFeatures },
        { VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, &m_rayPipelineFeatures },
    };

    TString m_rendererString;

    VkInstance m_vulkanInstance = VK_NULL_HANDLE;
    VkDebugReportCallbackEXT m_debugReportCallback = VK_NULL_HANDLE;

    VkPhysicalDevice m_vulkanPhysicalDevice = VK_NULL_HANDLE;
    i32 m_graphicsQueueFamily = -1;
    i32 m_computeQueueFamily = -1;
    i32 m_transferQueueFamily = -1;
    i32 m_presentQueueFamily = -1;

    VkDevice m_vulkanDevice = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    VkSurfaceKHR m_windowSurface = VK_NULL_HANDLE;

    VkSurfaceFormatKHR m_swapChainFormat = {};
    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    bool m_swapChainMutableFormatSupported = false;

    Vector<SwapChainImage> m_swapChainImages;
    uint32_t m_swapChainIndex = static_cast<uint32_t>(-1);

    DeviceHandle m_rhiDevice;

    Vector<VkSemaphore> m_acquireSemaphores;
    Vector<VkSemaphore> m_presentSemaphores;
    uint32_t m_acquireSemaphoreIndex = 0;

    ::Queue<EventQueryHandle> m_framesInFlight;
    Vector<EventQueryHandle> m_queryPool;

    bool m_bufferDeviceAddressSupported = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

