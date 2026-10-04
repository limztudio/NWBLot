// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"

#include <impl/assets/graphics/bindless/runtime_abi.h>

#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 960u, 540u };
}

TStringView NWB::QueryProjectWindowTitle(){
    return GLB_TEXT("NWB UI Layer Smoke");
}

bool NWB::ConfigureProjectRuntime(ProjectStartupContext& context){
    return
        context.graphics.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())
        && context.graphics.setHDR10OutputEnabled(false)
        && context.graphics.setSwapChainReadbackEnabled(true)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(ProjectRuntimeContext& context){
    return MakeUnique<Tests::Smoke::UiLayerSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

