// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_graphics/components.adhd.h>

#include <core/common/log.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_runtime_components{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_COMPONENT_ASSET_REGISTRAR(s_ModelComponentAssetAutoRegistrar, ModelComponentAsset);
NWB_DEFINE_COMPONENT_ASSET_REGISTRAR(s_MeshComponentAssetAutoRegistrar, MeshComponentAsset);
NWB_DEFINE_COMPONENT_ASSET_REGISTRAR(s_RendererComponentAssetAutoRegistrar, RendererComponentAsset);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ModelComponentAsset::loadComponentPayload(const Span<const u8> payload){
    const auto binaryPayload = ComponentAssetRuntime::ReadSinglePodPayload<SingleAssetRefComponentPayloadBinary>(payload, NWB_TEXT("ModelComponentAsset::loadComponentPayload"));
    if(!binaryPayload)
        return false;
    component.model.virtualPath = Name(binaryPayload->assetHash);
    return true;
}

b8 ModelComponentAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("ModelComponentAsset::validatePayload")))
        return false;
    if(!component.model.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("ModelComponentAsset::validatePayload failed: model_component '{}' asset must not be empty"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshComponentAsset::loadComponentPayload(const Span<const u8> payload){
    const auto binaryPayload = ComponentAssetRuntime::ReadSinglePodPayload<SingleAssetRefComponentPayloadBinary>(payload, NWB_TEXT("MeshComponentAsset::loadComponentPayload"));
    if(!binaryPayload)
        return false;
    component.mesh.virtualPath = Name(binaryPayload->assetHash);
    return true;
}

b8 MeshComponentAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("MeshComponentAsset::validatePayload")))
        return false;
    if(!component.mesh.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshComponentAsset::validatePayload failed: mesh_component '{}' asset must not be empty"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererComponentAsset::loadComponentPayload(const Span<const u8> payload){
    const auto binaryPayload = ComponentAssetRuntime::ReadSinglePodPayload<RendererComponentPayloadBinary>(payload, NWB_TEXT("RendererComponentAsset::loadComponentPayload"));
    if(!binaryPayload)
        return false;
    if(!UnpackRendererComponentPayloadBinary(*binaryPayload, component)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererComponentAsset::loadComponentPayload failed: invalid renderer flags"));
        return false;
    }
    return true;
}

b8 RendererComponentAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("RendererComponentAsset::validatePayload")))
        return false;
    if(!component.material.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererComponentAsset::validatePayload failed: renderer_component '{}' material must not be empty"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    if(!IsValidRendererComponentOpticalConfig(component)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererComponentAsset::validatePayload failed: renderer_component '{}' has invalid optical enum value"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    if(component.opticalVolumeCoincidence == OpticalVolumeCoincidence::SharedGroup && !component.opticalVolumeGroup){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererComponentAsset::validatePayload failed: renderer_component '{}' shared_group optical volume requires optical_volume_group"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

