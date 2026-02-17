// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vulkan_backend.h"

#include <unordered_set>
#include <queue>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeviceManager final : public IDeviceManager{
public:
    [[nodiscard]] IDevice* getDevice()const override;
    [[nodiscard]] GraphicsAPI::Enum getGraphicsAPI()const override{ return GraphicsAPI::VULKAN; }
    [[nodiscard]] const tchar* getRendererString()const override;

    bool enumerateAdapters(Vector<AdapterInfo>& outAdapters) override;

    bool isVulkanInstanceExtensionEnabled(const char* extensionName)const override;
    bool isVulkanDeviceExtensionEnabled(const char* extensionName)const override;
    bool isVulkanLayerEnabled(const char* layerName)const override;
    void getEnabledVulkanInstanceExtensions(Vector<AString>& extensions)const override;
    void getEnabledVulkanDeviceExtensions(Vector<AString>& extensions)const override;
    void getEnabledVulkanLayers(Vector<AString>& layers)const override;

    ITexture* getCurrentBackBuffer() override;
    ITexture* getBackBuffer(u32 index) override;
    u32 getCurrentBackBufferIndex() override;
    u32 getBackBufferCount() override;

protected:
    bool createInstanceInternal() override;
    bool createDeviceInternal() override;
    bool createSwapChainInternal() override;
    void destroyDeviceAndSwapChain() override;
    void resizeSwapChain() override;
    bool beginFrame() override;
    bool present() override;

private:
    bool _createInstance();
    bool _createWindowSurface();
    void _installDebugCallback();
    bool _pickPhysicalDevice();
    bool _findQueueFamilies(VkPhysicalDevice physicalDevice);
    bool _createDevice();
    bool _createSwapChain();
    void _destroySwapChain();

private:
    struct VulkanExtensionSet{
        std::unordered_set<std::string> instance;
        std::unordered_set<std::string> layers;
        std::unordered_set<std::string> device;
    };

    // minimal set of required extensions
    VulkanExtensionSet m_enabledExtensions = {
        // instance
        { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME },
        // layers
        {},
        // device
        { VK_KHR_MAINTENANCE1_EXTENSION_NAME },
    };

    // optional extensions
    VulkanExtensionSet m_optionalExtensions = {
        // instance
        {
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
            VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME,
        },
        // layers
        {},
        // device
        {
            VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
            VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
            VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME,
            VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
            VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME,
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
            VK_NV_MESH_SHADER_EXTENSION_NAME,
            VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME,
        },
    };

    std::unordered_set<std::string> m_rayTracingExtensions = {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
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

    struct SwapChainImage{
        VkImage image;
        TextureHandle rhiHandle;
    };

    Vector<SwapChainImage> m_swapChainImages;
    u32 m_swapChainIndex = static_cast<u32>(-1);

    DeviceHandle m_nvrhiDevice;

    Vector<VkSemaphore> m_acquireSemaphores;
    Vector<VkSemaphore> m_presentSemaphores;
    u32 m_acquireSemaphoreIndex = 0;

    std::queue<EventQueryHandle> m_framesInFlight;
    Vector<EventQueryHandle> m_queryPool;

    bool m_bufferDeviceAddressSupported = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
