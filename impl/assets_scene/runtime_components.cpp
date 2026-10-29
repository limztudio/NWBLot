// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "components.h"
#include <impl/assets_scene/component_assets.adhd.h>
#include "binary_payload.h"

#include <impl/ecs_scene/camera.h>

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>

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

static const ComponentTypeAutoRegistrar* s_RegisteredComponentTypesHead = nullptr;


[[nodiscard]] static UniquePtr<IComponent> CreateComponentByTypeHash(Core::Assets::AssetArena& arena, const Name& virtualPath, const NameHash& componentTypeHash){
    for(const ComponentTypeAutoRegistrar* node = s_RegisteredComponentTypesHead; node != nullptr; node = node->next()){
        if(node->assetType().hash() == componentTypeHash && node->create() != nullptr)
            return node->create()(arena, virtualPath);
    }
    return nullptr;
}

[[nodiscard]] static b8 IsComponentAssetType(const Name& assetType)noexcept{
    if(!assetType)
        return false;
    for(const ComponentTypeAutoRegistrar* node = s_RegisteredComponentTypesHead; node != nullptr; node = node->next()){
        if(node->assetType() == assetType)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


IComponent::~IComponent()noexcept = default;


ComponentTypeAutoRegistrar::ComponentTypeAutoRegistrar(const Name& assetType, const ComponentInstanceFactory create)noexcept
    : m_assetType(assetType)
    , m_create(create)
    , m_next(__hidden_scene_runtime_components::s_RegisteredComponentTypesHead)
{
    __hidden_scene_runtime_components::s_RegisteredComponentTypesHead = this;
}


const IComponent* CastComponent(const Core::Assets::IAsset* const asset)noexcept{
    if(!asset || !__hidden_scene_runtime_components::IsComponentAssetType(asset->assetType()))
        return nullptr;
    return checked_cast<const IComponent*>(asset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ComponentAssetRuntime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 LoadBinary(IComponent& component, const Span<const u8> binary){
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<ComponentBinaryPayload::ComponentHeaderBinary>(
        binary,
        ComponentBinaryPayload::s_ComponentMagic,
        ComponentBinaryPayload::s_ComponentLoadBinaryContext,
        ComponentBinaryPayload::s_ComponentAssetKindLabel,
        cursor
    );
    if(!headerResult)
        return false;
    const ComponentBinaryPayload::ComponentHeaderBinary& header = *headerResult;
    if(header.version != ComponentBinaryPayload::s_ComponentVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("IComponent::loadBinary failed: unsupported version {}"), header.version);
        return false;
    }
    if(header.componentTypeHash != component.assetType().hash()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("IComponent::loadBinary failed: component binary type does not match '{}'"),
            ConvertString(component.assetType().resolvedText())
        );
        return false;
    }
    if(header.payloadByteCount > binary.size() - cursor){
        NWB_LOGGER_ERROR(NWB_TEXT("IComponent::loadBinary failed: truncated component payload"));
        return false;
    }

    const usize payloadByteCount = static_cast<usize>(header.payloadByteCount);
    const Span<const u8> payload = binary.subspan(cursor, payloadByteCount);
    cursor += payloadByteCount;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, ComponentBinaryPayload::s_ComponentLoadBinaryContext))
        return false;
    if(!component.loadComponentPayload(payload))
        return false;
    return component.validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<UniquePtr<Core::Assets::IAsset>> ComponentAssetCodec::deserialize(Core::Assets::AssetArena& arena, const Name& virtualPath, const Span<const u8> binary)const{
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<ComponentBinaryPayload::ComponentHeaderBinary>(
        binary,
        ComponentBinaryPayload::s_ComponentMagic,
        ComponentBinaryPayload::s_ComponentLoadBinaryContext,
        ComponentBinaryPayload::s_ComponentAssetKindLabel,
        cursor
    );
    if(!headerResult)
        return MakeUnexpected(Failure{});

    auto asset = __hidden_scene_runtime_components::CreateComponentByTypeHash(arena, virtualPath, headerResult->componentTypeHash);
    if(!asset){
        NWB_LOGGER_ERROR(
            NWB_TEXT("ComponentAssetCodec::deserialize failed: unregistered component type in '{}'"),
            ConvertString(virtualPath.resolvedText())
        );
        return MakeUnexpected(Failure{});
    }
    if(!asset->loadBinary(binary))
        return MakeUnexpected(Failure{});

    return UniquePtr<Core::Assets::IAsset>(Move(asset));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TransformComponentAsset::loadComponentPayload(const Span<const u8> payload){
    const auto binaryPayload = ComponentAssetRuntime::ReadSinglePodPayload<ComponentBinaryPayload::TransformComponentPayloadBinary>(
        payload,
        NWB_TEXT("TransformComponentAsset::loadComponentPayload")
    );
    if(!binaryPayload)
        return false;
    component.position = binaryPayload->position;
    component.rotation = binaryPayload->rotation;
    component.scale = binaryPayload->scale;
    return true;
}

b8 TransformComponentAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("TransformComponentAsset::validatePayload")))
        return false;

    const SIMDVectorSingle position = LoadFloat(component.position);
    const SIMDVectorSingle rotation = LoadFloat(component.rotation);
    const SIMDVectorSingle scale = LoadFloat(component.scale);
    const SIMDVectorSingle rotationLengthSq = QuaternionLengthSq(rotation);
    constexpr f32 s_RotationUnitLengthSquaredTolerance = 0.001f;
    if(
        !Vector3IsFinite(position)
        || QuaternionIsNaN(rotation)
        || QuaternionIsInfinite(rotation)
        || !VectorIsFinite(rotationLengthSq, VectorComponentMask::s_XYZW)
        || !Vector4NearEqual(rotationLengthSq, s_SIMDOne, VectorReplicate(s_RotationUnitLengthSquaredTolerance))
        || !Vector3IsFinite(scale)
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("TransformComponentAsset::validatePayload failed: transform '{}' must have finite position/scale and unit quaternion rotation"),
            ConvertString(virtualPath().resolvedText())
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CameraComponentAsset::loadComponentPayload(const Span<const u8> payload){
    const auto binaryPayload = ComponentAssetRuntime::ReadSinglePodPayload<ComponentBinaryPayload::CameraComponentPayloadBinary>(
        payload,
        NWB_TEXT("CameraComponentAsset::loadComponentPayload")
    );
    if(!binaryPayload)
        return false;
    component.projection = binaryPayload->projection;
    return true;
}

b8 CameraComponentAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("CameraComponentAsset::validatePayload")))
        return false;

    const f32 aspectRatio = component.aspectRatio();
    const auto projection = Scene::TryBuildCameraProjection(
        VectorReplicate(component.verticalFovRadians()),
        VectorReplicate(component.nearPlane()),
        VectorReplicate(component.farPlane()),
        VectorReplicate(aspectRatio),
        VectorReplicate(Scene::s_CameraFallbackAspectRatio)
    );
    if(!IsFinite(aspectRatio) || aspectRatio < 0.0f || !projection){
        NWB_LOGGER_ERROR(
            NWB_TEXT("CameraComponentAsset::validatePayload failed: camera '{}' has invalid projection parameters"),
            ConvertString(virtualPath().resolvedText())
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 LightComponentAsset::loadComponentPayload(const Span<const u8> payload){
    const auto binaryPayload = ComponentAssetRuntime::ReadSinglePodPayload<ComponentBinaryPayload::LightComponentPayloadBinary>(
        payload,
        NWB_TEXT("LightComponentAsset::loadComponentPayload")
    );
    if(!binaryPayload)
        return false;
    if(binaryPayload->type >= Scene::LightType::kCount || binaryPayload->enableCaustics > 1u || binaryPayload->reserved != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("LightComponentAsset::loadComponentPayload failed: invalid light flags"));
        return false;
    }
    component.colorIntensity = binaryPayload->colorIntensity;
    component.range = binaryPayload->range;
    component.innerConeCos = binaryPayload->innerConeCos;
    component.outerConeCos = binaryPayload->outerConeCos;
    component.angularRadius = binaryPayload->angularRadius;
    component.sourceRadius = binaryPayload->sourceRadius;
    component.type = static_cast<Scene::LightType::Enum>(binaryPayload->type);
    component.enableCaustics = binaryPayload->enableCaustics != 0u;
    return true;
}

b8 LightComponentAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("LightComponentAsset::validatePayload")))
        return false;

    const Float4 color = component.color();
    const f32 intensity = component.intensity();
    constexpr f32 s_ConeCosineMin = -1.0f;
    constexpr f32 s_ConeCosineMax = 1.0f;
    if(
        component.type >= Scene::LightType::kCount
        || !IsFinite(color.x) || !IsFinite(color.y) || !IsFinite(color.z)
        || color.x < 0.0f || color.y < 0.0f || color.z < 0.0f
        || !IsFinite(intensity) || intensity <= 0.0f
        || !IsFinite(component.range) || component.range <= 0.0f
        || !IsFinite(component.innerConeCos) || !IsFinite(component.outerConeCos)
        || component.outerConeCos < s_ConeCosineMin || component.outerConeCos > component.innerConeCos || component.innerConeCos > s_ConeCosineMax
        || !IsFinite(component.angularRadius) || component.angularRadius < 0.0f
        || !IsFinite(component.sourceRadius) || component.sourceRadius < 0.0f
    ){
        NWB_LOGGER_ERROR(
            NWB_TEXT("LightComponentAsset::validatePayload failed: light '{}' has invalid parameters"),
            ConvertString(virtualPath().resolvedText())
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
