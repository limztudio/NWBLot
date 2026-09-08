// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/graphics/runtime/runtime.h>
#include <impl/assets/graphics/bindless/runtime_abi.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool NWB::ConfigureProjectRuntime(ProjectStartupContext& context){
    Core::GraphicsRuntime& graphics = context.graphics;
    if(!graphics.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi()))
        return false;

#if defined(NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK)
    if(!graphics.setSwapChainReadbackEnabled(true))
        return false;
#endif

    // The M4 synchronous baseline explicitly opts out; normal projects request the best-effort asynchronous Compute topology.
#if defined(NWB_SMOKE_DISABLE_ASYNC_COMPUTE)
    return graphics.setAsyncComputeLaneEnabled(false);
#else
    return graphics.setAsyncComputeLaneEnabled(true);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

