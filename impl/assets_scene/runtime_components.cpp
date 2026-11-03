// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include <impl/ecs_scene/camera.h>

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>

#include <global/binary.h>
#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_runtime_components{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_ComponentAssetCodecAutoRegistrar, ComponentAssetCodec);
NWB_DEFINE_COMPONENT_ASSET_REGISTRAR(s_TransformComponentAssetAutoRegistrar, TransformComponentAsset);
NWB_DEFINE_COMPONENT_ASSET_REGISTRAR(s_CameraComponentAssetAutoRegistrar, CameraComponentAsset);
NWB_DEFINE_COMPONENT_ASSET_REGISTRAR(s_LightComponentAssetAutoRegistrar, LightComponentAsset);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


IComponent::~IComponent()noexcept = default;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ComponentAssetRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 LoadBinary(const Span<const u8> binary, IComponent& outComponent){
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<ComponentHeaderBinary>(binary, s_ComponentMagic, s_ComponentLoadBinaryContext, s_ComponentAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const ComponentHeaderBinary& header = *headerResult;
    if(header.version != s_ComponentVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("IComponent::loadBinary failed: unsupported version {}"), header.version);
        return false;
    }
    if(header.componentTypeHash != outComponent.assetType().hash()){
        NWB_LOGGER_ERROR(NWB_TEXT("IComponent::loadBinary failed: component binary type does not match '{}'"), ConvertString(outComponent.assetType().resolvedText()));
        return false;
    }
    if(header.payloadByteCount > binary.size() - cursor){
        NWB_LOGGER_ERROR(NWB_TEXT("IComponent::loadBinary failed: truncated component payload"));
        return false;
    }

    const usize payloadByteCount = static_cast<usize>(header.payloadByteCount);
    const Span<const u8> payload = binary.subspan(cursor, payloadByteCount);
    cursor += payloadByteCount;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, s_ComponentLoadBinaryContext))
        return false;
    if(!outComponent.loadComponentPayload(payload))
        return false;
    return outComponent.validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<UniquePtr<Core::Assets::IAsset>> ComponentAssetCodec::deserialize(const Name& virtualPath, const Span<const u8> binary, Core::Assets::AssetArena& arena)const{
    return PolymorphicAssetCodecDetail::DeserializePolymorphicAsset<IComponent, ComponentHeaderBinary, &ComponentHeaderBinary::componentTypeHash>(
        virtualPath,
        binary,
        s_ComponentMagic,
        s_ComponentLoadBinaryContext,
        s_ComponentAssetKindLabel,
        NWB_TEXT("ComponentAssetCodec::deserialize"),
        arena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TransformComponentAsset::loadComponentPayload(const Span<const u8> payload){
    const auto binaryPayload = ComponentAssetRuntime::ReadSinglePodPayload<TransformComponentPayloadBinary>(payload, NWB_TEXT("TransformComponentAsset::loadComponentPayload"));
    if(!binaryPayload)
        return false;
    component = UnpackTransformComponentPayloadBinary(*binaryPayload);
    return true;
}

b8 TransformComponentAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("TransformComponentAsset::validatePayload")))
        return false;

    if(!IsValidTransformComponentParameters(component)){
        NWB_LOGGER_ERROR(NWB_TEXT("TransformComponentAsset::validatePayload failed: transform '{}' must have finite position/scale and unit quaternion rotation"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CameraComponentAsset::loadComponentPayload(const Span<const u8> payload){
    const auto binaryPayload = ComponentAssetRuntime::ReadSinglePodPayload<CameraComponentPayloadBinary>(payload, NWB_TEXT("CameraComponentAsset::loadComponentPayload"));
    if(!binaryPayload)
        return false;
    component = UnpackCameraComponentPayloadBinary(*binaryPayload);
    return true;
}

b8 CameraComponentAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("CameraComponentAsset::validatePayload")))
        return false;

    const SIMDVectorSingle projection = LoadFloat(component.projection);
    const SIMDVectorSingle aspectRatio = VectorSplatW(projection);
    const auto builtProjection = Scene::TryBuildCameraProjection(
        VectorSplatX(projection),
        VectorSplatY(projection),
        VectorSplatZ(projection),
        aspectRatio,
        VectorReplicate(Scene::s_CameraFallbackAspectRatio)
    );
    if(!VectorIsFinite(projection, VectorComponentMask::s_XYZW) || Vector4Less(aspectRatio, s_SIMDZero) || !builtProjection){
        NWB_LOGGER_ERROR(NWB_TEXT("CameraComponentAsset::validatePayload failed: camera '{}' has invalid projection parameters"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 LightComponentAsset::loadComponentPayload(const Span<const u8> payload){
    const auto binaryPayload = ComponentAssetRuntime::ReadSinglePodPayload<LightComponentPayloadBinary>(payload, NWB_TEXT("LightComponentAsset::loadComponentPayload"));
    if(!binaryPayload)
        return false;
    const Expected<LightComponent> unpacked = UnpackLightComponentPayloadBinary(*binaryPayload);
    if(!unpacked){
        NWB_LOGGER_ERROR(NWB_TEXT("LightComponentAsset::loadComponentPayload failed: invalid light flags"));
        return false;
    }
    component = *unpacked;
    return true;
}

b8 LightComponentAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("LightComponentAsset::validatePayload")))
        return false;

    if(!IsValidLightComponentParameters(component) || !Scene::IsValidLightCone(component.innerConeCos, component.outerConeCos)){
        NWB_LOGGER_ERROR(NWB_TEXT("LightComponentAsset::validatePayload failed: light '{}' has invalid parameters"), ConvertString(virtualPath().resolvedText()));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

