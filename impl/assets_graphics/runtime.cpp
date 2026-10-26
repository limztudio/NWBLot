// limztudio@gmail.com
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(s_RendererSubsystemAssetAutoRegistrar, RendererSubsystem);
NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(s_MeshSubsystemAssetAutoRegistrar, MeshSubsystem);
NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(s_DeferredSubsystemAssetAutoRegistrar, DeferredSubsystem);
NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(s_BvhSubsystemAssetAutoRegistrar, BvhSubsystem);
NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(s_GiSubsystemAssetAutoRegistrar, GiSubsystem);
NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(s_ReflectionSubsystemAssetAutoRegistrar, ReflectionSubsystem);
NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(s_ShadowSubsystemAssetAutoRegistrar, ShadowSubsystem);
NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(s_TransparentSubsystemAssetAutoRegistrar, TransparentSubsystem);
NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(s_RefractiveSubsystemAssetAutoRegistrar, RefractiveSubsystem);
NWB_DEFINE_SUBSYSTEM_ASSET_REGISTRAR(s_CsgSubsystemAssetAutoRegistrar, CsgSubsystem);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

