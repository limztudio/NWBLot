// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/graphics/module.h>
#include <impl/assets/graphics/bindless/runtime_abi.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool NWB::ConfigureProjectGraphics(Core::Graphics& graphics){
    if(!graphics.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi()))
        return false;

    // The M4 async-shadow benchmark builds this translation unit into a distinct executable. Keep the experiment
    // opt-in at device creation, where Graphics can select a real compute-only family or cleanly collapse back to
    // Graphics. Ordinary smoke targets continue to exercise the production Graphics-only default.
#if defined(NWB_SMOKE_ENABLE_ASYNC_COMPUTE)
    return graphics.setAsyncComputeLaneEnabled(true);
#else
    return true;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

