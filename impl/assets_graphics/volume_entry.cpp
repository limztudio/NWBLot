// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_components.h"
#include <impl/assets_graphics/bvh/subsystem.adhd.h>
#include <impl/assets_graphics/csg/subsystem.adhd.h>
#include <impl/assets_graphics/deferred/subsystem.adhd.h>
#include <impl/assets_graphics/gi/subsystem.adhd.h>
#include <impl/assets_graphics/mesh/subsystem.adhd.h>
#include <impl/assets_graphics/reflection/subsystem.adhd.h>
#include <impl/assets_graphics/refractive/subsystem.adhd.h>
#include <impl/assets_graphics/renderer/subsystem.adhd.h>
#include <impl/assets_graphics/shadow/subsystem.adhd.h>
#include <impl/assets_graphics/transparent/subsystem.adhd.h>

#include <impl/assets_subsystem/cook.h>

#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_entry_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_volume_entry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 RegisterGraphicsSubsystemCookEntries(Core::Assets::CookEntryRegistry& registry){
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
        && RegisterGraphicsComponentCookEntries(registry)
    ;
}

static b8 RegisterGraphicsSubsystemCookDependencies(Core::Assets::CookDependencyRegistry& registry){
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
        && RegisterGraphicsComponentCookDependencies(registry)
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
