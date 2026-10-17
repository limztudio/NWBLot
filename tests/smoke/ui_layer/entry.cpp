// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"
#include "world.h"

#include <impl/assets_graphics/bindless/runtime_abi.h>

#include <core/graphics/runtime/runtime.h>
#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 960u, 540u };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{
    return NWB_TEXT("NWB UI Layer Smoke");
}

bool NWB::ConfigureProjectRuntime(ProjectStartupContext& context)noexcept{
    return context.graphics.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi()) && context.graphics.setHDR10OutputEnabled(false) && context.graphics.setSwapChainReadbackEnabled(true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(ProjectRuntimeContext& context){
    auto world = Tests::Smoke::CreateUiLayerSmokeWorld(context);
    if(!world)
        return nullptr;
    return MakeUnique<Tests::Smoke::UiLayerSmokeProject>(context, Move(*world));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

