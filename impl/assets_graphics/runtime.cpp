// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "components.h"
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

#include <impl/assets_scene/binary_payload.h>

#include <core/common/log.h>

#include <global/binary.h>


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
NWB_DEFINE_COMPONENT_ASSET_REGISTRAR(s_ModelComponentAssetAutoRegistrar, ModelComponentAsset);
NWB_DEFINE_COMPONENT_ASSET_REGISTRAR(s_MeshComponentAssetAutoRegistrar, MeshComponentAsset);
NWB_DEFINE_COMPONENT_ASSET_REGISTRAR(s_RendererComponentAssetAutoRegistrar, RendererComponentAsset);


[[nodiscard]] static bool LoadSingleAssetRefComponentPayload(
    const Span<const u8> payload,
    const tchar* const context,
    Name& outVirtualPath
){
    usize cursor = 0u;
    const auto binaryPayload = ReadPOD<ComponentBinaryPayload::SingleAssetRefComponentPayloadBinary>(payload, cursor);
    if(!binaryPayload || cursor != payload.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} failed: invalid payload size"), context);
        return false;
    }
    outVirtualPath = Name(binaryPayload->assetHash);
    return true;
}

#if defined(NWB_COOK)
[[nodiscard]] static bool SerializeSingleAssetRefComponentPayload(
    const Name& assetName,
    Core::Assets::AssetBytes& outPayload
){
    ComponentBinaryPayload::SingleAssetRefComponentPayloadBinary binaryPayload;
    binaryPayload.assetHash = assetName.hash();

    outPayload.clear();
    outPayload.reserve(sizeof(binaryPayload));
    AppendPOD(outPayload, binaryPayload);
    return true;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ModelComponentAsset::loadComponentPayload(const Span<const u8> payload){
    return __hidden_assets_graphics_runtime::LoadSingleAssetRefComponentPayload(
        payload,
        NWB_TEXT("ModelComponentAsset::loadComponentPayload"),
        component.model.virtualPath
    );
}

bool ModelComponentAsset::validatePayload()const{
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

void ModelComponentAsset::applyToEntity(Core::ECS::Entity& entity)const{
    entity.addComponent<ModelComponent>() = component;
}

#if defined(NWB_COOK)
bool ModelComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    return __hidden_assets_graphics_runtime::SerializeSingleAssetRefComponentPayload(component.model.name(), outPayload);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshComponentAsset::loadComponentPayload(const Span<const u8> payload){
    return __hidden_assets_graphics_runtime::LoadSingleAssetRefComponentPayload(
        payload,
        NWB_TEXT("MeshComponentAsset::loadComponentPayload"),
        component.mesh.virtualPath
    );
}

bool MeshComponentAsset::validatePayload()const{
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

void MeshComponentAsset::applyToEntity(Core::ECS::Entity& entity)const{
    entity.addComponent<MeshComponent>() = component;
}

#if defined(NWB_COOK)
bool MeshComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    return __hidden_assets_graphics_runtime::SerializeSingleAssetRefComponentPayload(component.mesh.name(), outPayload);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererComponentAsset::loadComponentPayload(const Span<const u8> payload){
    usize cursor = 0u;
    const auto binaryPayload = ReadPOD<ComponentBinaryPayload::RendererComponentPayloadBinary>(payload, cursor);
    if(!binaryPayload || cursor != payload.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererComponentAsset::loadComponentPayload failed: invalid payload size"));
        return false;
    }
    if(binaryPayload->visible > 1u || binaryPayload->reserved0 != 0u || binaryPayload->reserved1 != 0u || binaryPayload->reserved2 != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererComponentAsset::loadComponentPayload failed: invalid renderer flags"));
        return false;
    }
    component = RendererComponent{};
    component.material.virtualPath = Name(binaryPayload->materialHash);
    component.visible = binaryPayload->visible != 0u;
    return true;
}

bool RendererComponentAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("RendererComponentAsset::validatePayload")))
        return false;
    if(!component.material.valid()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererComponentAsset::validatePayload failed: renderer_component '{}' material must not be empty"),
            ConvertString(virtualPath().resolvedText())
        );
        return false;
    }
    return true;
}

void RendererComponentAsset::applyToEntity(Core::ECS::Entity& entity)const{
    entity.addComponent<RendererComponent>() = component;
}

#if defined(NWB_COOK)
bool RendererComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    ComponentBinaryPayload::RendererComponentPayloadBinary binaryPayload;
    binaryPayload.materialHash = component.material.name().hash();
    binaryPayload.visible = component.visible ? 1u : 0u;
    binaryPayload.reserved0 = 0u;
    binaryPayload.reserved1 = 0u;
    binaryPayload.reserved2 = 0u;

    outPayload.clear();
    outPayload.reserve(sizeof(binaryPayload));
    AppendPOD(outPayload, binaryPayload);
    return true;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

