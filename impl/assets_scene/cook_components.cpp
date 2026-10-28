// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

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

static constexpr AStringView s_PositionField = "position";
static constexpr AStringView s_RotationField = "rotation";
static constexpr AStringView s_RotationEulerField = "rotation_euler";
static constexpr AStringView s_RotationEulerDegreesField = "rotation_euler_degrees";
static constexpr AStringView s_ScaleField = "scale";
static constexpr AStringView s_VerticalFovDegreesField = "vertical_fov_degrees";
static constexpr AStringView s_VerticalFovRadiansField = "vertical_fov_radians";
static constexpr AStringView s_NearPlaneField = "near_plane";
static constexpr AStringView s_FarPlaneField = "far_plane";
static constexpr AStringView s_AspectRatioField = "aspect_ratio";
static constexpr AStringView s_LightTypeField = "type";
static constexpr AStringView s_ColorField = "color";
static constexpr AStringView s_IntensityField = "intensity";
static constexpr AStringView s_RangeField = "range";
static constexpr AStringView s_InnerConeCosField = "inner_cone_cos";
static constexpr AStringView s_OuterConeCosField = "outer_cone_cos";
static constexpr AStringView s_AngularRadiusField = "angular_radius";
static constexpr AStringView s_SourceRadiusField = "source_radius";
static constexpr AStringView s_EnableCausticsField = "enable_caustics";
static constexpr f32 s_DegreesToRadians = s_PI / 180.0f;


[[nodiscard]] static bool CheckOptionalMetadataAssetMap(const Path& nwbFilePath, const Value& asset, const AStringView diagnosticPrefix){
    if(asset.isNull() || asset.isMap())
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset is not a map"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath));
    return false;
}

[[nodiscard]] static bool ReadOptionalFiniteF32Field(
    const Path& nwbFilePath,
    const Value& asset,
    const AStringView diagnosticPrefix,
    const AStringView fieldLabel,
    f32& outValue
){
    const Value* const fieldValue = asset.findField(fieldLabel);
    if(!fieldValue)
        return true;
    const auto parsed = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, *fieldValue, diagnosticPrefix, fieldLabel);
    if(!parsed)
        return false;
    outValue = *parsed;
    return true;
}

[[nodiscard]] static Expected<Float4> ParseFloat3Field(
    const Path& nwbFilePath,
    const AStringView diagnosticPrefix,
    const Value& listValue,
    const AStringView fieldLabel
){
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

[[nodiscard]] static Expected<Float4> ParseQuaternionField(
    const Path& nwbFilePath,
    const AStringView diagnosticPrefix,
    const Value& listValue,
    const AStringView fieldLabel
){
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

    const SIMDVectorSingle rawQuaternion = VectorSet(*x, *y, *z, *w);
    const SIMDVectorSingle lengthSq = QuaternionLengthSq(rawQuaternion);
    constexpr f32 s_MinQuaternionLengthSq = 0.000001f;
    if(!VectorIsFinite(lengthSq, VectorComponentMask::s_XYZW) || !Vector4Greater(lengthSq, VectorReplicate(s_MinQuaternionLengthSq))){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a non-zero quaternion"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
        return MakeUnexpected(Failure{});
    }

    Float4 normalized;
    StoreFloat(QuaternionNormalize(rawQuaternion), normalized);
    return normalized;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SceneAssetCook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<bool> ParseMetadataBoolField(
    const Path& nwbFilePath,
    const AStringView diagnosticPrefix,
    const Core::Metascript::Value& value,
    const AStringView fieldLabel
){
    if(!value.isInteger()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be an integer boolean (0 or 1)"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
        return MakeUnexpected(Failure{});
    }
    const i64 integerValue = value.asInteger();
    if(integerValue != 0 && integerValue != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be 0 or 1"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
        return MakeUnexpected(Failure{});
    }
    return integerValue != 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TransformComponentCookEntry> ParseTransformComponentCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena&
){
    using namespace __hidden_scene_cook_components;

    TransformComponentCookEntry entry(arena);
    if(!CheckOptionalMetadataAssetMap(nwbFilePath, asset, s_TransformDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_TransformDiagnosticPrefix))
        return MakeUnexpected(Failure{});

    if(asset.isMap()){
        if(!Core::Assets::ValidateMetadataAssetFields(
            nwbFilePath,
            asset,
            s_TransformDiagnosticPrefix,
            { s_PositionField, s_RotationField, s_RotationEulerField, s_RotationEulerDegreesField, s_ScaleField }
        ))
            return MakeUnexpected(Failure{});

        if(const Core::Metascript::Value* positionValue = asset.findField(s_PositionField)){
            const auto position = ParseFloat3Field(nwbFilePath, s_TransformDiagnosticPrefix, *positionValue, s_PositionField);
            if(!position)
                return MakeUnexpected(Failure{});
            entry.component.position = *position;
        }

        const Core::Metascript::Value* const rotationValue = asset.findField(s_RotationField);
        const Core::Metascript::Value* const eulerValue = asset.findField(s_RotationEulerField);
        const Core::Metascript::Value* const eulerDegreesValue = asset.findField(s_RotationEulerDegreesField);
        const usize rotationFieldCount = static_cast<usize>(rotationValue != nullptr) + static_cast<usize>(eulerValue != nullptr) + static_cast<usize>(eulerDegreesValue != nullptr);
        if(rotationFieldCount > 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': only one of 'rotation', 'rotation_euler', or 'rotation_euler_degrees' may be specified"), ConvertString(s_TransformDiagnosticPrefix), PathToString<tchar>(nwbFilePath));
            return MakeUnexpected(Failure{});
        }
        if(rotationValue){
            const auto rotation = ParseQuaternionField(nwbFilePath, s_TransformDiagnosticPrefix, *rotationValue, s_RotationField);
            if(!rotation)
                return MakeUnexpected(Failure{});
            entry.component.rotation = *rotation;
        }
        else if(eulerValue){
            const auto eulerRadians = ParseFloat3Field(nwbFilePath, s_TransformDiagnosticPrefix, *eulerValue, s_RotationEulerField);
            if(!eulerRadians)
                return MakeUnexpected(Failure{});
            StoreFloat(QuaternionRotationRollPitchYaw(eulerRadians->x, eulerRadians->y, eulerRadians->z), entry.component.rotation);
        }
        else if(eulerDegreesValue){
            const auto eulerDegrees = ParseFloat3Field(nwbFilePath, s_TransformDiagnosticPrefix, *eulerDegreesValue, s_RotationEulerDegreesField);
            if(!eulerDegrees)
                return MakeUnexpected(Failure{});
            StoreFloat(
                QuaternionRotationRollPitchYaw(
                    eulerDegrees->x * s_DegreesToRadians,
                    eulerDegrees->y * s_DegreesToRadians,
                    eulerDegrees->z * s_DegreesToRadians
                ),
                entry.component.rotation
            );
        }

        if(const Core::Metascript::Value* scaleValue = asset.findField(s_ScaleField)){
            const auto scale = ParseFloat3Field(nwbFilePath, s_TransformDiagnosticPrefix, *scaleValue, s_ScaleField);
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


Expected<CameraComponentCookEntry> ParseCameraComponentCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena&
){
    using namespace __hidden_scene_cook_components;

    CameraComponentCookEntry entry(arena);
    if(!CheckOptionalMetadataAssetMap(nwbFilePath, asset, s_CameraDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_CameraDiagnosticPrefix))
        return MakeUnexpected(Failure{});

    if(asset.isMap()){
        if(!Core::Assets::ValidateMetadataAssetFields(
            nwbFilePath,
            asset,
            s_CameraDiagnosticPrefix,
            { s_VerticalFovDegreesField, s_VerticalFovRadiansField, s_NearPlaneField, s_FarPlaneField, s_AspectRatioField }
        ))
            return MakeUnexpected(Failure{});

        const Core::Metascript::Value* const fovDegreesValue = asset.findField(s_VerticalFovDegreesField);
        const Core::Metascript::Value* const fovRadiansValue = asset.findField(s_VerticalFovRadiansField);
        if(fovDegreesValue && fovRadiansValue){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': only one of 'vertical_fov_degrees' or 'vertical_fov_radians' may be specified"), ConvertString(s_CameraDiagnosticPrefix), PathToString<tchar>(nwbFilePath));
            return MakeUnexpected(Failure{});
        }
        if(fovDegreesValue){
            const auto fovDegrees = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, *fovDegreesValue, s_CameraDiagnosticPrefix, s_VerticalFovDegreesField);
            if(!fovDegrees)
                return MakeUnexpected(Failure{});
            entry.component.setVerticalFovRadians(*fovDegrees * s_DegreesToRadians);
        }
        else if(fovRadiansValue){
            const auto fovRadians = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, *fovRadiansValue, s_CameraDiagnosticPrefix, s_VerticalFovRadiansField);
            if(!fovRadians)
                return MakeUnexpected(Failure{});
            entry.component.setVerticalFovRadians(*fovRadians);
        }

        f32 nearPlane = entry.component.nearPlane();
        f32 farPlane = entry.component.farPlane();
        f32 aspectRatio = entry.component.aspectRatio();
        if(
            !ReadOptionalFiniteF32Field(nwbFilePath, asset, s_CameraDiagnosticPrefix, s_NearPlaneField, nearPlane)
            || !ReadOptionalFiniteF32Field(nwbFilePath, asset, s_CameraDiagnosticPrefix, s_FarPlaneField, farPlane)
            || !ReadOptionalFiniteF32Field(nwbFilePath, asset, s_CameraDiagnosticPrefix, s_AspectRatioField, aspectRatio)
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


Expected<LightComponentCookEntry> ParseLightComponentCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena&
){
    using namespace __hidden_scene_cook_components;

    LightComponentCookEntry entry(arena);
    if(!CheckOptionalMetadataAssetMap(nwbFilePath, asset, s_LightDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_LightDiagnosticPrefix))
        return MakeUnexpected(Failure{});

    if(asset.isMap()){
        if(!Core::Assets::ValidateMetadataAssetFields(
            nwbFilePath,
            asset,
            s_LightDiagnosticPrefix,
            {
                s_LightTypeField,
                s_ColorField,
                s_IntensityField,
                s_RangeField,
                s_InnerConeCosField,
                s_OuterConeCosField,
                s_AngularRadiusField,
                s_SourceRadiusField,
                s_EnableCausticsField
            }
        ))
            return MakeUnexpected(Failure{});

        if(asset.findField(s_LightTypeField)){
            static constexpr ::NamedEnumCase<Scene::LightType::Enum> s_LightTypeCases[] = {
                { "directional", Scene::LightType::Directional },
                { "point", Scene::LightType::Point },
                { "spot", Scene::LightType::Spot },
            };
            const auto parsedType = Core::Assets::ParseNamedMetadataEnumField<Scene::LightType::Enum>(
                nwbFilePath,
                asset,
                s_LightDiagnosticPrefix,
                s_LightTypeField,
                Span<const ::NamedEnumCase<Scene::LightType::Enum>>(s_LightTypeCases, LengthOf(s_LightTypeCases)),
                "must be 'directional', 'point', or 'spot'"
            );
            if(!parsedType)
                return MakeUnexpected(Failure{});
            entry.component.type = *parsedType;
        }

        if(const Core::Metascript::Value* colorValue = asset.findField(s_ColorField)){
            const auto color = ParseFloat3Field(nwbFilePath, s_LightDiagnosticPrefix, *colorValue, s_ColorField);
            if(!color)
                return MakeUnexpected(Failure{});
            entry.component.setColor(*color);
        }

        f32 intensity = entry.component.intensity();
        if(
            !ReadOptionalFiniteF32Field(nwbFilePath, asset, s_LightDiagnosticPrefix, s_IntensityField, intensity)
            || !ReadOptionalFiniteF32Field(nwbFilePath, asset, s_LightDiagnosticPrefix, s_RangeField, entry.component.range)
            || !ReadOptionalFiniteF32Field(nwbFilePath, asset, s_LightDiagnosticPrefix, s_InnerConeCosField, entry.component.innerConeCos)
            || !ReadOptionalFiniteF32Field(nwbFilePath, asset, s_LightDiagnosticPrefix, s_OuterConeCosField, entry.component.outerConeCos)
            || !ReadOptionalFiniteF32Field(nwbFilePath, asset, s_LightDiagnosticPrefix, s_AngularRadiusField, entry.component.angularRadius)
            || !ReadOptionalFiniteF32Field(nwbFilePath, asset, s_LightDiagnosticPrefix, s_SourceRadiusField, entry.component.sourceRadius)
        )
            return MakeUnexpected(Failure{});
        entry.component.setIntensity(intensity);

        if(const Core::Metascript::Value* enableCausticsValue = asset.findField(s_EnableCausticsField)){
            const auto enableCaustics = ParseMetadataBoolField(nwbFilePath, s_LightDiagnosticPrefix, *enableCausticsValue, s_EnableCausticsField);
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
