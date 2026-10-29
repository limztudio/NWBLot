// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <impl/assets_graphics/bindless/runtime_abi.h>

#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 NWB::ConfigureProjectRuntime(ProjectStartupContext& context)noexcept{
    Core::GraphicsRuntime& graphics = context.graphics;
    if(!graphics.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi()))
        return false;

#if defined(NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK)
    if(!graphics.setSwapChainReadbackEnabled(true))
        return false;
#endif

    // Request asynchronous compute unless the synchronous baseline opts out.
#if defined(NWB_SMOKE_DISABLE_ASYNC_COMPUTE)
    return graphics.setAsyncComputeLaneEnabled(false);
#else
    return graphics.setAsyncComputeLaneEnabled(true);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

