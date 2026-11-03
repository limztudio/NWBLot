// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <impl/assets_scene/component_assets.adhd.h>

#include <core/common/log.h>

#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_cook_components{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;
using namespace SceneAssetCook;

[[nodiscard]] static b8 CheckOptionalMetadataAssetMap(const Path& nwbFilePath, const Value& asset, const AStringView diagnosticPrefix){
    if(asset.isNull() || asset.isMap())
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset is not a map"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath));
    return false;
}

[[nodiscard]] static Expected<Float4> ParseFloat3Field(const Path& nwbFilePath, const AStringView diagnosticPrefix, const Value& listValue, const AStringView fieldLabel){
    if(!listValue.isList() || listValue.asList().size() != 3u){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a 3-element numeric list"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
        return MakeUnexpected(Failure{});
    }
    const auto& items = listValue.asList();
    const auto x = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, items[0], diagnosticPrefix, fieldLabel);
    const auto y = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, items[1], diagnosticPrefix, fieldLabel);
    const auto z = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, items[2], diagnosticPrefix, fieldLabel);
    if(!x || !y || !z)
        return MakeUnexpected(Failure{});
    return Float4(*x, *y, *z, 0.0f);
}

[[nodiscard]] static Expected<Float4> ParseQuaternionField(const Path& nwbFilePath, const AStringView diagnosticPrefix, const Value& listValue, const AStringView fieldLabel){
    if(!listValue.isList() || listValue.asList().size() != 4u){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a 4-element numeric list"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
        return MakeUnexpected(Failure{});
    }
    const auto& items = listValue.asList();
    const auto x = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, items[0], diagnosticPrefix, fieldLabel);
    const auto y = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, items[1], diagnosticPrefix, fieldLabel);
    const auto z = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, items[2], diagnosticPrefix, fieldLabel);
    const auto w = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, items[3], diagnosticPrefix, fieldLabel);
    if(!x || !y || !z || !w)
        return MakeUnexpected(Failure{});

    const Expected<Float4> normalized = TryNormalizeTransformQuaternion(*x, *y, *z, *w);
    if(!normalized){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a non-zero quaternion"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
        return MakeUnexpected(Failure{});
    }
    return *normalized;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ComponentAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    return PolymorphicAssetCodecDetail::SerializePolymorphicAsset<IComponent, &ComponentAssetSerialization::Serialize>(asset, s_ComponentSerializeContext, outBinary);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ComponentAssetSerialization{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Serialize(const IComponent& component, Core::Assets::AssetBytes& outBinary){
    if(!component.validatePayload())
        return false;

    auto& assetArena = outBinary.get_allocator().arena();
    Core::Assets::AssetBytes payload(assetArena);
    if(!component.serializeComponentPayload(payload))
        return false;

    ComponentHeaderBinary header;
    header.componentTypeHash = component.assetType().hash();
    header.payloadByteCount = static_cast<u64>(payload.size());

    outBinary.clear();
    outBinary.reserve(sizeof(header) + payload.size());
    AppendPOD(outBinary, header);
    if(!payload.empty())
        outBinary.insert(outBinary.end(), payload.begin(), payload.end());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TransformComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    const TransformComponentPayloadBinary binaryPayload(component.position, component.rotation, component.scale);
    return ComponentAssetSerialization::WriteSinglePodPayload(binaryPayload, outPayload);
}

b8 CameraComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    const CameraComponentPayloadBinary binaryPayload(component.projection);
    return ComponentAssetSerialization::WriteSinglePodPayload(binaryPayload, outPayload);
}

b8 LightComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    const LightComponentPayloadBinary binaryPayload = PackLightComponentPayloadBinary(component);
    return ComponentAssetSerialization::WriteSinglePodPayload(binaryPayload, outPayload);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TransformComponentCookEntry> ParseTransformComponentCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){
    using namespace __hidden_scene_cook_components;

    TransformComponentCookEntry entry(arena);
    if(!CheckOptionalMetadataAssetMap(nwbFilePath, asset, s_TransformDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_TransformDiagnosticPrefix, entry))
        return MakeUnexpected(Failure{});

    if(asset.isMap()){
        if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_TransformDiagnosticPrefix, &IsAllowedTransformComponentField))
            return MakeUnexpected(Failure{});

        if(const Core::Metascript::Value* positionValue = asset.findField(s_TransformPositionField)){
            const auto position = ParseFloat3Field(nwbFilePath, s_TransformDiagnosticPrefix, *positionValue, s_TransformPositionField);
            if(!position)
                return MakeUnexpected(Failure{});
            entry.component.position = *position;
        }

        const Core::Metascript::Value* const rotationValue = asset.findField(s_TransformRotationField);
        const Core::Metascript::Value* const eulerValue = asset.findField(s_TransformRotationEulerField);
        const Core::Metascript::Value* const eulerDegreesValue = asset.findField(s_TransformRotationEulerDegreesField);
        const usize rotationFieldCount = static_cast<usize>(rotationValue != nullptr) + static_cast<usize>(eulerValue != nullptr) + static_cast<usize>(eulerDegreesValue != nullptr);
        if(rotationFieldCount > 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': only one of 'rotation', 'rotation_euler', or 'rotation_euler_degrees' may be specified"), ConvertString(s_TransformDiagnosticPrefix), PathToString<tchar>(nwbFilePath));
            return MakeUnexpected(Failure{});
        }
        if(rotationValue){
            const auto rotation = ParseQuaternionField(nwbFilePath, s_TransformDiagnosticPrefix, *rotationValue, s_TransformRotationField);
            if(!rotation)
                return MakeUnexpected(Failure{});
            entry.component.rotation = *rotation;
        }
        else if(eulerValue){
            const auto eulerRadians = ParseFloat3Field(nwbFilePath, s_TransformDiagnosticPrefix, *eulerValue, s_TransformRotationEulerField);
            if(!eulerRadians)
                return MakeUnexpected(Failure{});
            StoreFloat(QuaternionRotationRollPitchYaw(eulerRadians->x, eulerRadians->y, eulerRadians->z), entry.component.rotation);
        }
        else if(eulerDegreesValue){
            const auto eulerDegrees = ParseFloat3Field(nwbFilePath, s_TransformDiagnosticPrefix, *eulerDegreesValue, s_TransformRotationEulerDegreesField);
            if(!eulerDegrees)
                return MakeUnexpected(Failure{});
            StoreFloat(QuaternionRotationRollPitchYaw(eulerDegrees->x * s_SceneDegreesToRadians, eulerDegrees->y * s_SceneDegreesToRadians, eulerDegrees->z * s_SceneDegreesToRadians), entry.component.rotation);
        }

        if(const Core::Metascript::Value* scaleValue = asset.findField(s_TransformScaleField)){
            const auto scale = ParseFloat3Field(nwbFilePath, s_TransformDiagnosticPrefix, *scaleValue, s_TransformScaleField);
            if(!scale)
                return MakeUnexpected(Failure{});
            entry.component.scale = *scale;
        }
    }

    if(!BuildComponentAssetFromEntry<TransformComponentAsset>(entry, arena))
        return MakeUnexpected(Failure{});

    return entry;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CameraComponentCookEntry> ParseCameraComponentCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){
    using namespace __hidden_scene_cook_components;

    CameraComponentCookEntry entry(arena);
    if(!CheckOptionalMetadataAssetMap(nwbFilePath, asset, s_CameraDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_CameraDiagnosticPrefix, entry))
        return MakeUnexpected(Failure{});

    if(asset.isMap()){
        if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_CameraDiagnosticPrefix, &IsAllowedCameraComponentField))
            return MakeUnexpected(Failure{});

        const Core::Metascript::Value* const fovDegreesValue = asset.findField(s_CameraVerticalFovDegreesField);
        const Core::Metascript::Value* const fovRadiansValue = asset.findField(s_CameraVerticalFovRadiansField);
        if(fovDegreesValue && fovRadiansValue){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': only one of 'vertical_fov_degrees' or 'vertical_fov_radians' may be specified"), ConvertString(s_CameraDiagnosticPrefix), PathToString<tchar>(nwbFilePath));
            return MakeUnexpected(Failure{});
        }
        if(fovDegreesValue){
            const auto fovDegrees = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, *fovDegreesValue, s_CameraDiagnosticPrefix, s_CameraVerticalFovDegreesField);
            if(!fovDegrees)
                return MakeUnexpected(Failure{});
            entry.component.setVerticalFovRadians(*fovDegrees * s_SceneDegreesToRadians);
        }
        else if(fovRadiansValue){
            const auto fovRadians = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, *fovRadiansValue, s_CameraDiagnosticPrefix, s_CameraVerticalFovRadiansField);
            if(!fovRadians)
                return MakeUnexpected(Failure{});
            entry.component.setVerticalFovRadians(*fovRadians);
        }

        f32 nearPlane = entry.component.nearPlane();
        f32 farPlane = entry.component.farPlane();
        f32 aspectRatio = entry.component.aspectRatio();
        if(
            !Core::Assets::ReadOptionalFiniteF32Field(nwbFilePath, asset, s_CameraDiagnosticPrefix, s_CameraNearPlaneField, nearPlane)
            || !Core::Assets::ReadOptionalFiniteF32Field(nwbFilePath, asset, s_CameraDiagnosticPrefix, s_CameraFarPlaneField, farPlane)
            || !Core::Assets::ReadOptionalFiniteF32Field(nwbFilePath, asset, s_CameraDiagnosticPrefix, s_CameraAspectRatioField, aspectRatio)
        )
            return MakeUnexpected(Failure{});
        entry.component.setNearPlane(nearPlane);
        entry.component.setFarPlane(farPlane);
        entry.component.setAspectRatio(aspectRatio);
    }

    if(!BuildComponentAssetFromEntry<CameraComponentAsset>(entry, arena))
        return MakeUnexpected(Failure{});

    return entry;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<LightComponentCookEntry> ParseLightComponentCookMetadata(const Name virtualPath, const Path& nwbFilePath, const Core::Metascript::Value& asset, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){
    using namespace __hidden_scene_cook_components;

    LightComponentCookEntry entry(arena);
    if(!CheckOptionalMetadataAssetMap(nwbFilePath, asset, s_LightDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(virtualPath, nwbFilePath, s_LightDiagnosticPrefix, entry))
        return MakeUnexpected(Failure{});

    if(asset.isMap()){
        if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_LightDiagnosticPrefix, &IsAllowedLightComponentField))
            return MakeUnexpected(Failure{});

        if(asset.findField(s_LightTypeField)){
            static constexpr Array<::NamedEnumCase<Scene::LightType::Enum>, 3u> s_LightTypeCases = {{
                { s_LightTypeDirectional, Scene::LightType::Directional },
                { s_LightTypePoint, Scene::LightType::Point },
                { s_LightTypeSpot, Scene::LightType::Spot },
            }};
            const auto parsedType = Core::Assets::ParseNamedMetadataEnumField<Scene::LightType::Enum>(
                nwbFilePath,
                asset,
                s_LightDiagnosticPrefix,
                s_LightTypeField,
                Span<const ::NamedEnumCase<Scene::LightType::Enum>>(s_LightTypeCases.data(), s_LightTypeCases.size()),
                s_LightTypeHint
            );
            if(!parsedType)
                return MakeUnexpected(Failure{});
            entry.component.type = *parsedType;
        }

        if(const Core::Metascript::Value* colorValue = asset.findField(s_LightColorField)){
            const auto color = ParseFloat3Field(nwbFilePath, s_LightDiagnosticPrefix, *colorValue, s_LightColorField);
            if(!color)
                return MakeUnexpected(Failure{});
            entry.component.setColor(*color);
        }

        f32 intensity = entry.component.intensity();
        if(
            !Core::Assets::ReadOptionalFiniteF32Field(nwbFilePath, asset, s_LightDiagnosticPrefix, s_LightIntensityField, intensity)
            || !Core::Assets::ReadOptionalFiniteF32Field(nwbFilePath, asset, s_LightDiagnosticPrefix, s_LightRangeField, entry.component.range)
            || !Core::Assets::ReadOptionalFiniteF32Field(nwbFilePath, asset, s_LightDiagnosticPrefix, s_LightInnerConeCosField, entry.component.innerConeCos)
            || !Core::Assets::ReadOptionalFiniteF32Field(nwbFilePath, asset, s_LightDiagnosticPrefix, s_LightOuterConeCosField, entry.component.outerConeCos)
            || !Core::Assets::ReadOptionalFiniteF32Field(nwbFilePath, asset, s_LightDiagnosticPrefix, s_LightAngularRadiusField, entry.component.angularRadius)
            || !Core::Assets::ReadOptionalFiniteF32Field(nwbFilePath, asset, s_LightDiagnosticPrefix, s_LightSourceRadiusField, entry.component.sourceRadius)
        )
            return MakeUnexpected(Failure{});
        entry.component.setIntensity(intensity);

        if(const Core::Metascript::Value* enableCausticsValue = asset.findField(s_LightEnableCausticsField)){
            const auto enableCaustics = Core::Assets::ParseMetadataBoolField(nwbFilePath, s_LightDiagnosticPrefix, *enableCausticsValue, s_LightEnableCausticsField);
            if(!enableCaustics)
                return MakeUnexpected(Failure{});
            entry.component.enableCaustics = *enableCaustics;
        }
    }

    if(!BuildComponentAssetFromEntry<LightComponentAsset>(entry, arena))
        return MakeUnexpected(Failure{});

    return entry;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

