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

template<typename AssetRefT>
[[nodiscard]] static b8 LoadSingleAssetRefComponentPayload(const Span<const u8> payload, const TStringView context, AssetRefT& outRef){
    const auto binaryPayload = ComponentAssetRuntime::ReadSinglePodPayload<SingleAssetRefComponentPayloadBinary>(payload, context);
    if(!binaryPayload)
        return false;
    outRef.virtualPath = Name(binaryPayload->assetHash);
    return true;
}

template<typename ComponentAssetT, typename AssetRefT>
[[nodiscard]] static b8 ValidateSingleAssetRefComponentPayload(const ComponentAssetT& componentAsset, const AssetRefT& assetRef, const TStringView context, const TStringView kindLabel){
    if(!componentAsset.checkVirtualPath(context))
        return false;
    if(!assetRef.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: {} '{}' asset must not be empty"), context, kindLabel, ConvertString(componentAsset.virtualPath().resolvedText()));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ModelComponentAsset::loadComponentPayload(const Span<const u8> payload){
    return __hidden_assets_graphics_runtime_components::LoadSingleAssetRefComponentPayload(payload, NWB_TEXT("ModelComponentAsset::loadComponentPayload"), component.model);
}

b8 ModelComponentAsset::validatePayload()const{
    return __hidden_assets_graphics_runtime_components::ValidateSingleAssetRefComponentPayload(*this, component.model, NWB_TEXT("ModelComponentAsset::validatePayload"), NWB_TEXT("model_component"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshComponentAsset::loadComponentPayload(const Span<const u8> payload){
    return __hidden_assets_graphics_runtime_components::LoadSingleAssetRefComponentPayload(payload, NWB_TEXT("MeshComponentAsset::loadComponentPayload"), component.mesh);
}

b8 MeshComponentAsset::validatePayload()const{
    return __hidden_assets_graphics_runtime_components::ValidateSingleAssetRefComponentPayload(*this, component.mesh, NWB_TEXT("MeshComponentAsset::validatePayload"), NWB_TEXT("mesh_component"));
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

