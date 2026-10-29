// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_graphics/components.adhd.h>
#include "component_binary_payload.h"

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


[[nodiscard]] static b8 LoadSingleAssetRefComponentPayload(
    const Span<const u8> payload,
    const TStringView context,
    Name& outVirtualPath
){
    const auto binaryPayload = ComponentAssetRuntime::ReadSinglePodPayload<ComponentBinaryPayload::SingleAssetRefComponentPayloadBinary>(payload, context);
    if(!binaryPayload)
        return false;
    outVirtualPath = Name(binaryPayload->assetHash);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ModelComponentAsset::loadComponentPayload(const Span<const u8> payload){
    return __hidden_assets_graphics_runtime_components::LoadSingleAssetRefComponentPayload(
        payload,
        NWB_TEXT("ModelComponentAsset::loadComponentPayload"),
        component.model.virtualPath
    );
}

b8 ModelComponentAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("ModelComponentAsset::validatePayload")))
        return false;
    if(!component.model.valid()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ModelComponentAsset::validatePayload failed: model_component '{}' asset must not be empty"),
            ConvertString(virtualPath().resolvedText())
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshComponentAsset::loadComponentPayload(const Span<const u8> payload){
    return __hidden_assets_graphics_runtime_components::LoadSingleAssetRefComponentPayload(
        payload,
        NWB_TEXT("MeshComponentAsset::loadComponentPayload"),
        component.mesh.virtualPath
    );
}

b8 MeshComponentAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("MeshComponentAsset::validatePayload")))
        return false;
    if(!component.mesh.valid()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("MeshComponentAsset::validatePayload failed: mesh_component '{}' asset must not be empty"),
            ConvertString(virtualPath().resolvedText())
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererComponentAsset::loadComponentPayload(const Span<const u8> payload){
    const auto binaryPayload = ComponentAssetRuntime::ReadSinglePodPayload<ComponentBinaryPayload::RendererComponentPayloadBinary>(
        payload,
        NWB_TEXT("RendererComponentAsset::loadComponentPayload")
    );
    if(!binaryPayload)
        return false;
    if(
        binaryPayload->visible > 1u
        || binaryPayload->opticalVolumeCoincidence > static_cast<u8>(OpticalVolumeCoincidence::SharedGroup)
        || binaryPayload->opticalBoundaryMode > static_cast<u8>(OpticalBoundaryMode::ClosedPriority)
        || binaryPayload->reserved0 != 0u
        || binaryPayload->reserved1 != 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererComponentAsset::loadComponentPayload failed: invalid renderer flags"));
        return false;
    }
    component = RendererComponent{};
    component.material.virtualPath = Name(binaryPayload->materialHash);
    component.visible = binaryPayload->visible != 0u;
    component.opticalVolumeCoincidence = static_cast<OpticalVolumeCoincidence::Enum>(binaryPayload->opticalVolumeCoincidence);
    component.opticalVolumeGroup = Name(binaryPayload->opticalVolumeGroupHash);
    component.opticalVolumePriority = binaryPayload->opticalVolumePriority;
    component.opticalBoundaryMode = static_cast<OpticalBoundaryMode::Enum>(binaryPayload->opticalBoundaryMode);
    component.opticalMediumPriority = binaryPayload->opticalMediumPriority;
    return true;
}

b8 RendererComponentAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("RendererComponentAsset::validatePayload")))
        return false;
    if(!component.material.valid()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererComponentAsset::validatePayload failed: renderer_component '{}' material must not be empty"),
            ConvertString(virtualPath().resolvedText())
        );
        return false;
    }
    if(
        static_cast<u8>(component.opticalVolumeCoincidence) > static_cast<u8>(OpticalVolumeCoincidence::SharedGroup)
        || static_cast<u8>(component.opticalBoundaryMode) > static_cast<u8>(OpticalBoundaryMode::ClosedPriority)
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererComponentAsset::validatePayload failed: renderer_component '{}' has invalid optical enum value"),
            ConvertString(virtualPath().resolvedText())
        );
        return false;
    }
    if(component.opticalVolumeCoincidence == OpticalVolumeCoincidence::SharedGroup && !component.opticalVolumeGroup){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererComponentAsset::validatePayload failed: renderer_component '{}' shared_group optical volume requires optical_volume_group"),
            ConvertString(virtualPath().resolvedText())
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
