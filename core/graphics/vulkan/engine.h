// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/client/logger.h>

#include "../common.h"
#include "../graphics.h"

#include "config.h"
#include "utility.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{
    struct VmaAllocatorDeleter{
        constexpr VmaAllocatorDeleter()noexcept = default;
        void operator()(VmaAllocator p)const noexcept{ vmaDestroyAllocator(p); }
    };
    using VmaAllocatorPtr = UniquePtr<VmaAllocator_T, VmaAllocatorDeleter>;

    struct VkInstanceDeleter{
        constexpr VkInstanceDeleter()noexcept = default;
        constexpr VkInstanceDeleter(VkAllocationCallbacks** _callback)noexcept : callback(_callback){}
        void operator()(VkInstance p)const noexcept{ vkDestroyInstance(p, *callback); }
        VkAllocationCallbacks** callback = nullptr;
    };
    using VkInstancePtr = UniquePtr<VkInstance_T, VkInstanceDeleter>;

    struct VkPhysicalDeviceRefDeleter{
        constexpr VkPhysicalDeviceRefDeleter()noexcept = default;
        void operator()(VkPhysicalDevice)const noexcept{}
    };
    using VkPhysicalDeviceRef = UniquePtr<VkPhysicalDevice_T, VkPhysicalDeviceRefDeleter>;

    struct VkLogicalDeviceDeleter{
        constexpr VkLogicalDeviceDeleter()noexcept = default;
        constexpr VkLogicalDeviceDeleter(VkAllocationCallbacks** _callback)noexcept : callback(_callback){}
        void operator()(VkDevice p)const noexcept{ vkDestroyDevice(p, *callback); }
        VkAllocationCallbacks** callback = nullptr;
    };
    using VkLogicalDevicePtr = UniquePtr<VkDevice_T, VkLogicalDeviceDeleter>;

    struct VkQueueRefDeleter{
        constexpr VkQueueRefDeleter()noexcept = default;
        void operator()(VkQueue)const noexcept{}
    };
    using VkQueueRef = UniquePtr<VkQueue_T, VkQueueRefDeleter>;

    struct VkSurfaceDeleter{
        constexpr VkSurfaceDeleter()noexcept = default;
        constexpr VkSurfaceDeleter(VkAllocationCallbacks** _callback, VkInstancePtr* _inst)noexcept : callback(_callback), inst(_inst){}
        void operator()(VkSurfaceKHR p)const noexcept{ vkDestroySurfaceKHR(inst->get(), p, *callback); }
        VkAllocationCallbacks** callback = nullptr;
        VkInstancePtr* inst = nullptr;
    };
    using VkSurfacePtr = UniquePtr<VkSurfaceKHR_T, VkSurfaceDeleter>;

    struct VkSwapchainDeleter{
        constexpr VkSwapchainDeleter()noexcept = default;
        constexpr VkSwapchainDeleter(VkAllocationCallbacks** _callback, VkLogicalDevicePtr* _logiDev)noexcept : callback(_callback), logiDev(_logiDev){}
        void operator()(VkSwapchainKHR p)const noexcept{ vkDestroySwapchainKHR(logiDev->get(), p, *callback); }
        VkAllocationCallbacks** callback = nullptr;
        VkLogicalDevicePtr* logiDev = nullptr;
    };
    using VkSwapchainPtr = UniquePtr<VkSwapchainKHR_T, VkSwapchainDeleter>;

    struct VkImageRefDeleter{
        constexpr VkImageRefDeleter()noexcept = default;
        void operator()(VkImage)const noexcept{}
    };
    using VkImageRef = UniquePtr<VkImage_T, VkImageRefDeleter>;

    struct VkImageViewDeleter{
        constexpr VkImageViewDeleter()noexcept = default;
        constexpr VkImageViewDeleter(VkAllocationCallbacks** _callback, VkLogicalDevicePtr* _logiDev)noexcept : callback(_callback), logiDev(_logiDev){}
        void operator()(VkImageView p)const noexcept{ vkDestroyImageView(logiDev->get(), p, *callback); }
        VkAllocationCallbacks** callback = nullptr;
        VkLogicalDevicePtr* logiDev = nullptr;
    };
    using VkImageViewPtr = UniquePtr<VkImageView_T, VkImageViewDeleter>;

    struct VkFramebufferDeleter{
        constexpr VkFramebufferDeleter()noexcept = default;
        constexpr VkFramebufferDeleter(VkAllocationCallbacks** _callback, VkLogicalDevicePtr* _logiDev)noexcept : callback(_callback), logiDev(_logiDev){}
        void operator()(VkFramebuffer p)const noexcept{ vkDestroyFramebuffer(logiDev->get(), p, *callback); }
        VkAllocationCallbacks** callback = nullptr;
        VkLogicalDevicePtr* logiDev = nullptr;
    };
    using VkFramebufferPtr = UniquePtr<VkFramebuffer_T, VkFramebufferDeleter>;

#if defined(VULKAN_VALIDATE)
    struct VkDebugUtilsMessengerDeleter{
        constexpr VkDebugUtilsMessengerDeleter()noexcept = default;
        constexpr VkDebugUtilsMessengerDeleter(VkAllocationCallbacks** _callback, VkInstancePtr* _inst)noexcept : callback(_callback), inst(_inst){}
        void operator()(VkDebugUtilsMessengerEXT p)const noexcept{
            auto* vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(inst->get(), "vkDestroyDebugUtilsMessengerEXT"));
            if(vkDestroyDebugUtilsMessengerEXT)
                vkDestroyDebugUtilsMessengerEXT(inst->get(), p, *callback);
            else
                NWB_LOGGER_WARNING(NWB_TEXT("Failed to get vkDestroyDebugUtilsMessengerEXT function pointer"));
        }
        VkAllocationCallbacks** callback = nullptr;
        VkInstancePtr* inst = nullptr;
    };
    using VkDebugUtilsMessengerPtr = UniquePtr<VkDebugUtilsMessengerEXT_T, VkDebugUtilsMessengerDeleter>;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VulkanEngine{
private:
#if defined(VULKAN_VALIDATE)
    static constexpr const char* s_validationLayerName[] = {
        "VK_LAYER_KHRONOS_validation",
        //"VK_LAYER_LUNARG_core_validation",
        //"VK_LAYER_LUNARG_image",
        //"VK_LAYER_LUNARG_parameter_validation",
        //"VK_LAYER_LUNARG_object_tracker",
    };
#endif
#if defined(VULKAN_SYNC_VALIDATE)
    static constexpr const VkValidationFeatureEnableEXT s_validationFeaturesRequested[] = {
        VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
        //VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
    };
#endif

    static const char* s_requestedExtensions[];

    static constexpr const f32 s_queuePriorities[] = {
        1.f,
    };

    static constexpr const char* s_deviceExtensions[] = {
        "VK_KHR_swapchain",
    };

    static constexpr const VkFormat s_surfaceFormats[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM,
        VK_FORMAT_R8G8B8_UNORM,
    };
    static constexpr const VkColorSpaceKHR s_surfaceColorSpaces[] = {
        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    };


public:
    VulkanEngine(Graphics* parent);
    ~VulkanEngine();


public:
    bool init(const Common::FrameData& data);
    void destroy();


private:
    void updatePresentMode(PresentMode mode);

    inline bool CreateSwapchain(){
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkImage swapchainImages[s_maxSwapchainImages] = { VK_NULL_HANDLE };
        VkImageView swapchainImagesViews[s_maxSwapchainImages] = { VK_NULL_HANDLE };
        bool bRet = Core::CreateSwapchain(
            m_allocCallbacks,
            m_physDev.get(),
            m_logiDev.get(),
            m_queueFamilly,
            m_winSurf.get(),
            m_winSurfFormat.format,
            m_presentMode,
            &m_parent.m_swapchainWidth,
            &m_parent.m_swapchainHeight,
            &m_parent.m_swapchainImageCount,
            &swapchain,
            &swapchainImages,
            &swapchainImagesViews
        );
        m_swapchain.reset(swapchain);
        for(usize i = 0; i < s_maxSwapchainImages; ++i){
            m_swapchainImages[i].reset(swapchainImages[i]);
            m_swapchainImageViews[i].reset(swapchainImagesViews[i]);
        }
        return bRet;
    }
    void destroySwapchain();


private:
    Graphics& m_parent;

private:
    VkAllocationCallbacks* m_allocCallbacks;

private:
    __hidden_vulkan::VkInstancePtr m_inst;

    __hidden_vulkan::VkPhysicalDeviceRef m_physDev;
    VkPhysicalDeviceProperties m_physDevProps;

    __hidden_vulkan::VkLogicalDevicePtr m_logiDev;

    __hidden_vulkan::VkQueueRef m_queue;
    u32 m_queueFamilly;

    __hidden_vulkan::VkImageRef m_swapchainImages[s_maxSwapchainImages];
    __hidden_vulkan::VkImageViewPtr m_swapchainImageViews[s_maxSwapchainImages];
    __hidden_vulkan::VkFramebufferPtr m_swapchainFrameBuffers[s_maxSwapchainImages];

    __hidden_vulkan::VkSurfacePtr m_winSurf;
    VkSurfaceFormatKHR m_winSurfFormat;
    VkPresentModeKHR m_presentMode;
    __hidden_vulkan::VkSwapchainPtr m_swapchain;

    __hidden_vulkan::VmaAllocatorPtr m_allocator;

    f32 m_timestampFrequency;
    u64 m_uboAlignment;
    u64 m_ssboAlignment;

private:
#if defined(VULKAN_VALIDATE)
    bool m_debugUtilsExtensionPresents;
    __hidden_vulkan::VkDebugUtilsMessengerPtr m_debugMessenger;

    PFN_vkSetDebugUtilsObjectNameEXT fnSetDebugUtilsObjectNameEXT;
    PFN_vkCmdBeginDebugUtilsLabelEXT fnCmdBeginDebugUtilsLabelEXT;
    PFN_vkCmdEndDebugUtilsLabelEXT fnCmdEndDebugUtilsLabelEXT;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

