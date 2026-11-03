// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_graphics/components.adhd.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_runtime_components{


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
NWB_DEFINE_COMPONENT_ASSET_REGISTRAR(s_ModelComponentAssetAutoRegistrar, ModelComponentAsset);
NWB_DEFINE_COMPONENT_ASSET_REGISTRAR(s_MeshComponentAssetAutoRegistrar, MeshComponentAsset);
NWB_DEFINE_COMPONENT_ASSET_REGISTRAR(s_RendererComponentAssetAutoRegistrar, RendererComponentAsset);

template<typename TAsset>
[[nodiscard]] static b8 LoadSingleAssetRefComponentPayload(const Span<const u8> payload, const TStringView context, Core::Assets::AssetRef<TAsset>& outRef){
    const auto binaryPayload = ComponentAssetRuntime::ReadSinglePodPayload<SingleAssetRefComponentPayloadBinary>(payload, context);
    if(!binaryPayload)
        return false;
    outRef = UnpackSingleAssetRefComponentPayloadBinary<TAsset>(*binaryPayload);
    return true;
}

template<typename ComponentAssetT, typename TAsset>
[[nodiscard]] static b8 ValidateSingleAssetRefComponentPayload(const ComponentAssetT& componentAsset, const Core::Assets::AssetRef<TAsset>& assetRef, const TStringView context, const AStringView contextAscii, const AStringView kindLabel){
    if(!componentAsset.checkVirtualPath(context))
        return false;
    return ValidateSingleAssetRefComponentAssetPayload(assetRef.valid(), componentAsset.virtualPath(), contextAscii, kindLabel);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ModelComponentAsset::loadComponentPayload(const Span<const u8> payload){
    return __hidden_assets_graphics_runtime_components::LoadSingleAssetRefComponentPayload(payload, NWB_TEXT("ModelComponentAsset::loadComponentPayload"), component.model);
}

b8 ModelComponentAsset::validatePayload()const{
    return __hidden_assets_graphics_runtime_components::ValidateSingleAssetRefComponentPayload(*this, component.model, NWB_TEXT("ModelComponentAsset::validatePayload"), "ModelComponentAsset::validatePayload", "model_component");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshComponentAsset::loadComponentPayload(const Span<const u8> payload){
    return __hidden_assets_graphics_runtime_components::LoadSingleAssetRefComponentPayload(payload, NWB_TEXT("MeshComponentAsset::loadComponentPayload"), component.mesh);
}

b8 MeshComponentAsset::validatePayload()const{
    return __hidden_assets_graphics_runtime_components::ValidateSingleAssetRefComponentPayload(*this, component.mesh, NWB_TEXT("MeshComponentAsset::validatePayload"), "MeshComponentAsset::validatePayload", "mesh_component");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererComponentAsset::loadComponentPayload(const Span<const u8> payload){
    const auto binaryPayload = ComponentAssetRuntime::ReadSinglePodPayload<RendererComponentPayloadBinary>(payload, NWB_TEXT("RendererComponentAsset::loadComponentPayload"));
    if(!binaryPayload)
        return false;
    const Expected<RendererComponent> unpacked = UnpackRendererComponentPayloadBinary(*binaryPayload);
    if(!unpacked){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererComponentAsset::loadComponentPayload failed: invalid renderer flags"));
        return false;
    }
    component = *unpacked;
    return true;
}

b8 RendererComponentAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("RendererComponentAsset::validatePayload")))
        return false;
    return ValidateRendererComponentAssetPayload(component, virtualPath());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

