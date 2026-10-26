// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bvh/subsystem.h"
#include "csg/subsystem.h"
#include "deferred/subsystem.h"
#include "gi/subsystem.h"
#include "mesh/subsystem.h"
#include "reflection/subsystem.h"
#include "refractive/subsystem.h"
#include "renderer/subsystem.h"
#include "shadow/subsystem.h"
#include "transparent/subsystem.h"

#include <impl/assets_subsystem/cook.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool RegisterGraphicsSubsystemCookEntries(Core::Assets::CookEntryRegistry& registry){
    return
        AssetsSubsystemCook::RegisterSubsystemCookEntry<RendererSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<MeshSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<DeferredSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<BvhSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<GiSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<ReflectionSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<ShadowSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<TransparentSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<RefractiveSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookEntry<CsgSubsystem>(registry)
    ;
}

static bool RegisterGraphicsSubsystemCookDependencies(Core::Assets::CookDependencyRegistry& registry){
    return
        AssetsSubsystemCook::RegisterSubsystemCookDependency<RendererSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<MeshSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<DeferredSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<BvhSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<GiSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<ReflectionSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<ShadowSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<TransparentSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<RefractiveSubsystem>(registry)
        && AssetsSubsystemCook::RegisterSubsystemCookDependency<CsgSubsystem>(registry)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_COOK_ENTRY_REGISTRAR(s_GraphicsSubsystemCookEntryRegistrar, RegisterGraphicsSubsystemCookEntries);
NWB_DEFINE_COOK_DEPENDENCY_REGISTRAR(s_GraphicsSubsystemCookDependencyRegistrar, RegisterGraphicsSubsystemCookDependencies);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

