// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/global.h>
#include <global/thread.h>
#include <core/alloc/general.h>
#include <core/alloc/job.h>
#include <core/alloc/thread.h>
#include <core/graphics/module.h>
#include <core/perf/timing.h>
#include <impl/assets/graphics/bindless/runtime_abi.h>

#include <volk/volk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Keep an unavailable validation layer as a GPU-optional test condition. BackendContext correctly reports a
// missing required layer as an error, but that debug diagnostic breaks before Google Test can turn it into a skip.
#if !defined(NWB_FINAL)
[[nodiscard]] inline bool HasKhronosValidationLayer(){
    if(volkInitialize() != VK_SUCCESS)
        return false;

    u32 layerCount = 0u;
    if(vkEnumerateInstanceLayerProperties(&layerCount, nullptr) != VK_SUCCESS || layerCount == 0u)
        return false;

    Core::Alloc::ScratchArena scratchArena(Name("tests/common/headless_graphics/validation_layer_query_scratch"));
    Vector<VkLayerProperties, Core::Alloc::ScratchArena> availableLayers(layerCount, scratchArena);
    if(vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data()) != VK_SUCCESS)
        return false;

    for(u32 layerIndex = 0u; layerIndex < layerCount; ++layerIndex){
        if(NWB_STRCMP(availableLayers[layerIndex].layerName, "VK_LAYER_KHRONOS_validation") == 0)
            return true;
    }
    return false;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Brings up a real headless GPU device with the minimum dependency set Graphics requires, mirroring Core::Frame's
// construction. createHeadlessDevice() creates no window/swap chain, so this runs on any host with a Vulkan driver
// that exposes the required graphics capabilities.
class HeadlessGraphicsScope final : NoCopy{
public:
    HeadlessGraphicsScope()
        : m_objectArena(s_TestArenaName)
        , m_allocator(m_objectArena)
        , m_threadPool(s_TestWorkerThreadCount, CpuAffinity::Any)
        , m_jobSystem(m_threadPool)
        , m_gpuTiming(m_objectArena)
        , m_graphics(m_allocator, m_threadPool, m_jobSystem, m_gpuTiming)
    {}

    ~HeadlessGraphicsScope(){}

    // Returns false when the test-local validation runtime or headless Vulkan device cannot be created. The caller
    // SKIPS in that case rather than failing -- a CI runner without a GPU or validation layer is an environment
    // condition.
    [[nodiscard]] bool initialize(){
#if !defined(NWB_FINAL)
        // The graph smoke paths must be validation-backed. This configuration is owned by each Graphics instance
        // and must precede createHeadlessDevice(), which creates the Vulkan instance and enables
        // VK_LAYER_KHRONOS_validation.
        if(!HasKhronosValidationLayer())
            return false;
        if(!m_graphics.setDebugRuntimeEnabled(true))
            return false;
#endif
        if(!m_graphics.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi()))
            return false;
        return m_graphics.createHeadlessDevice();
    }

    [[nodiscard]] bool setAsyncComputeLaneEnabled(const bool enabled){
        return m_graphics.setAsyncComputeLaneEnabled(enabled);
    }

    [[nodiscard]] bool setTransferQueueEnabled(const bool enabled){
        return m_graphics.setTransferQueueEnabled(enabled);
    }

    [[nodiscard]] bool setSameClassMultiQueueEnabled(const bool enabled){
        return m_graphics.setSameClassMultiQueueEnabled(enabled);
    }

    [[nodiscard]] bool setCrossFamilySameClassQueueRoutingEnabled(const bool enabled){
        return m_graphics.setCrossFamilySameClassQueueRoutingEnabled(enabled);
    }

    [[nodiscard]] Core::Graphics& graphics(){ return m_graphics; }
    [[nodiscard]] Core::Alloc::GlobalArena& arena(){ return m_objectArena; }
    [[nodiscard]] Core::Perf::TimingRecorder& gpuTimingSink(){ return m_gpuTiming; }

    void setGpuTimingEnabled(const bool enabled){
        m_gpuTiming.setEnabled(enabled);
        m_graphics.gpuTiming().setQueryCollectionEnabled(enabled);
    }

private:
    static inline constexpr Name s_TestArenaName{"tests/common/headless_graphics/object_arena"};
    static inline constexpr u32 s_TestWorkerThreadCount = 2u;

    Core::Alloc::GlobalArena m_objectArena;
    Core::GraphicsAllocator m_allocator;
    Core::Alloc::ThreadPool m_threadPool;
    Core::Alloc::JobSystem m_jobSystem;
    Core::Perf::TimingRecorder m_gpuTiming;
    Core::Graphics m_graphics;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

