// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/client/logger.h>

#include "../common.h"
#include "../graphics.h"

#include "config.h"
#include "utility.h"
#include "primitiveAssets.h"


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
        void operator()(VkInstance p)const noexcept{ vkDestroyInstance(p, callback ? *callback : nullptr); }
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
        void operator()(VkDevice p)const noexcept{ vkDestroyDevice(p, callback ? *callback : nullptr); }
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
        void operator()(VkSurfaceKHR p)const noexcept{ vkDestroySurfaceKHR(inst->get(), p, callback ? *callback : nullptr); }
        VkAllocationCallbacks** callback = nullptr;
        VkInstancePtr* inst = nullptr;
    };
    using VkSurfacePtr = UniquePtr<VkSurfaceKHR_T, VkSurfaceDeleter>;

    struct VkSwapchainDeleter{
        constexpr VkSwapchainDeleter()noexcept = default;
        constexpr VkSwapchainDeleter(VkAllocationCallbacks** _callback, VkLogicalDevicePtr* _logiDev)noexcept : callback(_callback), logiDev(_logiDev){}
        void operator()(VkSwapchainKHR p)const noexcept{ vkDestroySwapchainKHR(logiDev->get(), p, callback ? *callback : nullptr); }
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
        void operator()(VkImageView p)const noexcept{ vkDestroyImageView(logiDev->get(), p, callback ? *callback : nullptr); }
        VkAllocationCallbacks** callback = nullptr;
        VkLogicalDevicePtr* logiDev = nullptr;
    };
    using VkImageViewPtr = UniquePtr<VkImageView_T, VkImageViewDeleter>;

    struct VkFramebufferDeleter{
        constexpr VkFramebufferDeleter()noexcept = default;
        constexpr VkFramebufferDeleter(VkAllocationCallbacks** _callback, VkLogicalDevicePtr* _logiDev)noexcept : callback(_callback), logiDev(_logiDev){}
        void operator()(VkFramebuffer p)const noexcept{ vkDestroyFramebuffer(logiDev->get(), p, callback ? *callback : nullptr); }
        VkAllocationCallbacks** callback = nullptr;
        VkLogicalDevicePtr* logiDev = nullptr;
    };
    using VkFramebufferPtr = UniquePtr<VkFramebuffer_T, VkFramebufferDeleter>;

    struct VkDescriptorPoolDeleter{
        constexpr VkDescriptorPoolDeleter()noexcept = default;
        constexpr VkDescriptorPoolDeleter(VkAllocationCallbacks** _callback, VkLogicalDevicePtr* _logiDev)noexcept : callback(_callback), logiDev(_logiDev){}
        void operator()(VkDescriptorPool p)const noexcept{ vkDestroyDescriptorPool(logiDev->get(), p, callback ? *callback : nullptr); }
        VkAllocationCallbacks** callback = nullptr;
        VkLogicalDevicePtr* logiDev = nullptr;
    };
    using VkDescriptorPoolPtr = UniquePtr<VkDescriptorPool_T, VkDescriptorPoolDeleter>;

    struct VkDescriptorSetLayoutDeleter{
        constexpr VkDescriptorSetLayoutDeleter()noexcept = default;
        constexpr VkDescriptorSetLayoutDeleter(VkAllocationCallbacks** _callback, VkLogicalDevicePtr* _logiDev)noexcept : callback(_callback), logiDev(_logiDev){}
        void operator()(VkDescriptorSetLayout p)const noexcept{ vkDestroyDescriptorSetLayout(logiDev->get(), p, callback ? *callback : nullptr); }
        VkAllocationCallbacks** callback = nullptr;
        VkLogicalDevicePtr* logiDev = nullptr;
    };
    using VkDescriptorSetLayoutPtr = UniquePtr<VkDescriptorSetLayout_T, VkDescriptorSetLayoutDeleter>;

    struct VkQueryPoolDeleter{
        constexpr VkQueryPoolDeleter()noexcept = default;
        constexpr VkQueryPoolDeleter(VkAllocationCallbacks** _callback, VkLogicalDevicePtr* _logiDev)noexcept : callback(_callback), logiDev(_logiDev){}
        void operator()(VkQueryPool p)const noexcept{ vkDestroyQueryPool(logiDev->get(), p, callback ? *callback : nullptr); }
        VkAllocationCallbacks** callback = nullptr;
        VkLogicalDevicePtr* logiDev = nullptr;
    };
    using VkQueryPoolPtr = UniquePtr<VkQueryPool_T, VkQueryPoolDeleter>;

#if defined(VULKAN_VALIDATE)
    struct VkDebugUtilsMessengerDeleter{
        constexpr VkDebugUtilsMessengerDeleter()noexcept = default;
        constexpr VkDebugUtilsMessengerDeleter(VkAllocationCallbacks** _callback, VkInstancePtr* _inst)noexcept : callback(_callback), inst(_inst){}
        void operator()(VkDebugUtilsMessengerEXT p)const noexcept{
            auto* vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(inst->get(), "vkDestroyDebugUtilsMessengerEXT"));
            if(vkDestroyDebugUtilsMessengerEXT)
                vkDestroyDebugUtilsMessengerEXT(inst->get(), p, callback ? *callback : nullptr);
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
    static constexpr usize s_maxMemoryAllocSize = 32 * 1024 * 1024;

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

    static constexpr const u32 s_maxGlobalPoolElement = 128;
    static constexpr const VkDescriptorPoolSize s_globalPoolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, s_maxGlobalPoolElement },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, s_maxGlobalPoolElement },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, s_maxGlobalPoolElement },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s_maxGlobalPoolElement },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, s_maxGlobalPoolElement },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, s_maxGlobalPoolElement },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, s_maxGlobalPoolElement },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, s_maxGlobalPoolElement },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, s_maxGlobalPoolElement },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, s_maxGlobalPoolElement },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, s_maxGlobalPoolElement }
    };

    static constexpr const u32 s_maxBindlessRes = 1024;
    static constexpr const u32 s_maxBindlessTex = 10;
    static constexpr const VkDescriptorPoolSize s_bindlessSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, s_maxBindlessRes }, // isn't this suppossed be VK_DESCRIPTOR_TYPE_SAMPLER?
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s_maxBindlessRes },
    };
    static constexpr const u32 s_numBinding = 4;
    static constexpr const VkDescriptorBindingFlags s_flagBindless = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT
        //| VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT
        | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT
        ;

    static constexpr const u32 s_maxFrame = 3;
    static constexpr const u32 s_timeQueryPerFrame = 32;

    static constexpr const u8 s_maxSwapchainImages = 3;


public:
    VulkanEngine(Graphics* parent);
    ~VulkanEngine();


public:
    bool init(const Common::FrameData& data);
    void destroy();


private:
    void updatePresentMode(PresentMode::Enum mode);

    inline bool createSwapchain(){
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
            static_cast<usize>(s_maxSwapchainImages),
            reinterpret_cast<VkImage**>(&swapchainImages),
            reinterpret_cast<VkImageView**>(&swapchainImagesViews)
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
    BufferPool<Alloc::MemoryAllocator> m_buffers;
    TexturePool<Alloc::MemoryAllocator> m_textures;
    PipelinePool<Alloc::MemoryAllocator> m_pipelines;
    SamplerPool<Alloc::MemoryAllocator> m_samplers;
    DescriptorSetLayoutPool<Alloc::MemoryAllocator> m_descriptorSetLayouts;
    DescriptorSetPool<Alloc::MemoryAllocator> m_descriptorSets;
    RenderPassPool<Alloc::MemoryAllocator> m_renderPasses;
    BufferPool<Alloc::MemoryAllocator> m_commandBuffers;
    ShaderStatePool<Alloc::MemoryAllocator> m_shaderStates;

private:
    BufferHandle m_fullscreenVertexBuffer;
    RenderPassHandle m_swapchainPass;
    SamplerHandle m_defaultSampler;

    TextureHandle m_dummyTexture;
    BufferHandle m_dummyConstantBuffer;

private:
    RenderPassOutput m_swapchainOutput;

private:
    Alloc::MemoryArena m_persistentArena;

private:
    VkAllocationCallbacks* m_allocCallbacks;

private:
    __hidden_vulkan::VkInstancePtr m_inst;

    __hidden_vulkan::VkPhysicalDeviceRef m_physDev;
    VkPhysicalDeviceProperties m_physDevProps;

    __hidden_vulkan::VkLogicalDevicePtr m_logiDev;

    __hidden_vulkan::VkQueueRef m_queue;
    u32 m_queueFamilly;

    __hidden_vulkan::VkDescriptorPoolPtr m_descriptorPool;

    __hidden_vulkan::VkDescriptorSetLayoutPtr m_bindlessDescriptorSetLayout;
    VkDescriptorSet m_bindlessDescriptorSet;
    __hidden_vulkan::VkDescriptorPoolPtr m_bindlessDescriptorPool;

    __hidden_vulkan::VkQueryPoolPtr m_timestampQueryPool;

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

    bool m_supportBindless;

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

