// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "arena_names.h"
#include "binary_payload.h"

#include <core/assets/binary_payload_io.h>
#include <core/assets/paths.h>
#include <core/common/log.h>

#include <global/binary.h>
#include <global/simd/vector.h>
#include <global/simplemath.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ComponentAssetSerialization{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Serialize(const IComponent& component, Core::Assets::AssetBytes& outBinary){
    if(!component.validatePayload())
        return false;

    auto& assetArena = outBinary.get_allocator().arena();
    Core::Assets::AssetBytes payload(assetArena);
    if(!component.serializeComponentPayload(payload))
        return false;

    ComponentBinaryPayload::ComponentHeaderBinary header;
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


bool ComponentAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    const IComponent* const component = CastComponent(&asset);
    if(!component){
        NWB_LOGGER_ERROR(
            NWB_TEXT("{}: unexpected asset type '{}'"),
            ComponentBinaryPayload::s_ComponentSerializeContext,
            ConvertString(asset.assetType().resolvedText())
        );
        return false;
    }
    return ComponentAssetSerialization::Serialize(*component, outBinary);
}

bool TransformComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    ComponentBinaryPayload::TransformComponentPayloadBinary binaryPayload;
    binaryPayload.position = component.position;
    binaryPayload.rotation = component.rotation;
    binaryPayload.scale = component.scale;

    outPayload.clear();
    outPayload.reserve(sizeof(binaryPayload));
    AppendPOD(outPayload, binaryPayload);
    return true;
}

bool CameraComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    ComponentBinaryPayload::CameraComponentPayloadBinary binaryPayload;
    binaryPayload.projection = component.projection;

    outPayload.clear();
    outPayload.reserve(sizeof(binaryPayload));
    AppendPOD(outPayload, binaryPayload);
    return true;
}

bool LightComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    ComponentBinaryPayload::LightComponentPayloadBinary binaryPayload;
    binaryPayload.colorIntensity = component.colorIntensity;
    binaryPayload.range = component.range;
    binaryPayload.innerConeCos = component.innerConeCos;
    binaryPayload.outerConeCos = component.outerConeCos;
    binaryPayload.angularRadius = component.angularRadius;
    binaryPayload.sourceRadius = component.sourceRadius;
    binaryPayload.type = static_cast<u8>(component.type);
    binaryPayload.enableCaustics = component.enableCaustics ? 1u : 0u;
    binaryPayload.reserved = 0u;

    outPayload.clear();
    outPayload.reserve(sizeof(binaryPayload));
    AppendPOD(outPayload, binaryPayload);
    return true;
}

bool EntityAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, EntityBinaryPayload::s_EntitySerializeContext))
        return false;

    const EntityAsset& entity = *MakeNotNull(checked_cast<const EntityAsset*>(&asset));
    Core::Alloc::ScratchArena scratchArena(AssetsSceneArenaScope::s_EntitySerializeArena);
    if(!entity.validatePayload(scratchArena))
        return false;

    Vector<NameHash, Core::Alloc::ScratchArena> componentHashes(scratchArena);
    componentHashes.reserve(entity.components().size());
    for(const EntityAsset::ComponentRef& item : entity.components())
        componentHashes.push_back(item.name().hash());

    usize reserveBytes = sizeof(EntityBinaryPayload::EntityHeaderBinary);
    const bool canReserve = AddBinaryVectorReserveBytes(reserveBytes, componentHashes);

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    EntityBinaryPayload::EntityHeaderBinary header;
    header.componentCount = static_cast<u64>(componentHashes.size());
    AppendPOD(outBinary, header);

    return Core::Assets::AppendVectorPayload(outBinary, componentHashes, EntityBinaryPayload::s_EntitySerializeContext, EntityBinaryPayload::s_ComponentsPayloadLabel);
}

bool ProjectAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, ProjectBinaryPayload::s_ProjectSerializeContext))
        return false;

    const ProjectAsset& project = *MakeNotNull(checked_cast<const ProjectAsset*>(&asset));
    if(!project.validatePayload())
        return false;

    ProjectBinaryPayload::ProjectHeaderBinary header;
    header.startSceneHash = project.startScene().name().hash();

    outBinary.clear();
    outBinary.reserve(sizeof(header));
    AppendPOD(outBinary, header);
    return true;
}

bool SceneAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, SceneBinaryPayload::s_SceneSerializeContext))
        return false;

    const SceneAsset& scene = *MakeNotNull(checked_cast<const SceneAsset*>(&asset));
    Core::Alloc::ScratchArena scratchArena(AssetsSceneArenaScope::s_SceneSerializeArena);
    if(!scene.validatePayload(scratchArena))
        return false;

    Vector<NameHash, Core::Alloc::ScratchArena> subsystemHashes(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> entityHashes(scratchArena);
    Vector<SceneBinaryPayload::SceneAssetEntryBinary, Core::Alloc::ScratchArena> assetEntries(scratchArena);

    subsystemHashes.reserve(scene.subsystems().size());
    for(const SceneAsset::SubsystemRef& item : scene.subsystems())
        subsystemHashes.push_back(item.name().hash());

    entityHashes.reserve(scene.entities().size());
    for(const SceneAsset::EntityRef& item : scene.entities())
        entityHashes.push_back(item.name().hash());

    assetEntries.reserve(scene.assets().size());
    for(const auto& [nickname, item] : scene.assets()){
        SceneBinaryPayload::SceneAssetEntryBinary entry;
        entry.nicknameHash = nickname.hash();
        entry.assetHash = item.name().hash();
        assetEntries.push_back(entry);
    }
    Sort(assetEntries.begin(), assetEntries.end(), [](const SceneBinaryPayload::SceneAssetEntryBinary& lhs, const SceneBinaryPayload::SceneAssetEntryBinary& rhs)noexcept{
        return LessNameHash(lhs.nicknameHash, rhs.nicknameHash);
    });

    usize reserveBytes = sizeof(SceneBinaryPayload::SceneHeaderBinary);
    const bool canReserve =
        AddBinaryVectorReserveBytes(reserveBytes, subsystemHashes)
        && AddBinaryVectorReserveBytes(reserveBytes, entityHashes)
        && AddBinaryVectorReserveBytes(reserveBytes, assetEntries)
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    SceneBinaryPayload::SceneHeaderBinary header;
    header.subsystemCount = static_cast<u64>(subsystemHashes.size());
    header.entityCount = static_cast<u64>(entityHashes.size());
    header.assetCount = static_cast<u64>(assetEntries.size());
    AppendPOD(outBinary, header);

    return
        Core::Assets::AppendVectorPayload(outBinary, subsystemHashes, SceneBinaryPayload::s_SceneSerializeContext, SceneBinaryPayload::s_SubsystemsPayloadLabel)
        && Core::Assets::AppendVectorPayload(outBinary, entityHashes, SceneBinaryPayload::s_SceneSerializeContext, SceneBinaryPayload::s_EntitiesPayloadLabel)
        && Core::Assets::AppendVectorPayload(outBinary, assetEntries, SceneBinaryPayload::s_SceneSerializeContext, SceneBinaryPayload::s_AssetsPayloadLabel)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;

static constexpr AStringView s_StartSceneField = "start_scene";
static constexpr AStringView s_SubsystemsField = "subsystems";
static constexpr AStringView s_EntitiesField = "entities";
static constexpr AStringView s_AssetsField = "assets";
static constexpr AStringView s_ComponentsField = "components";
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
static constexpr AStringView s_TransformDiagnosticPrefix = "Transform component meta";
static constexpr AStringView s_CameraDiagnosticPrefix = "Camera component meta";
static constexpr AStringView s_LightDiagnosticPrefix = "Light component meta";
static constexpr AStringView s_EntityDiagnosticPrefix = "Entity meta";
static constexpr AStringView s_ProjectDiagnosticPrefix = "Project meta";
static constexpr AStringView s_SceneDiagnosticPrefix = "Scene meta";
static constexpr f32 s_DegreesToRadians = s_PI / 180.0f;


[[nodiscard]] static bool CheckOptionalMetadataAssetMap(const Path& nwbFilePath, const Value& asset, const AStringView diagnosticPrefix){
    if(asset.isNull() || asset.isMap())
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset is not a map"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath));
    return false;
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

[[nodiscard]] static Expected<bool> ParseMetadataBoolField(
    const Path& nwbFilePath,
    const AStringView diagnosticPrefix,
    const Value& value,
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

template<typename TAsset>
[[nodiscard]] static Expected<Core::Assets::AssetRef<TAsset>> ParseAssetRefText(
    const Path& nwbFilePath,
    const AStringView diagnosticPrefix,
    const AStringView fieldLabel,
    const AStringView text,
    Core::Alloc::ScratchArena& scratchArena
){
    if(TrimView(text).empty() || text != TrimView(text)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' must be a non-empty trimmed virtual path"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
        return MakeUnexpected(Failure{});
    }
    if(text.find('\\') != AStringView::npos || !Core::Assets::HasReservedAssetVirtualRoot(text, scratchArena)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' must be an engine/- or project/-rooted virtual path"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
        return MakeUnexpected(Failure{});
    }
    const auto ref = Core::Assets::AssetRef<TAsset>::FromText(text);
    if(!ref || !ref->valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entry '{}' is invalid"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
        return MakeUnexpected(Failure{});
    }
    return *ref;
}

template<typename TAsset>
[[nodiscard]] static Expected<Core::Assets::AssetVector<Core::Assets::AssetRef<TAsset>>> ParseAssetRefList(
    const Path& nwbFilePath,
    const AStringView diagnosticPrefix,
    const Value& listValue,
    const AStringView fieldLabel,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!listValue.isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a list"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
        return MakeUnexpected(Failure{});
    }
    const auto& items = listValue.asList();
    Core::Assets::AssetVector<Core::Assets::AssetRef<TAsset>> refs(arena);
    refs.reserve(items.size());
    for(const Value& item : items){
        if(!item.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entries must be strings"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
            return MakeUnexpected(Failure{});
        }
        const AStringView text(item.asString().data(), item.asString().size());
        const auto parsedRef = ParseAssetRefText<TAsset>(nwbFilePath, diagnosticPrefix, fieldLabel, text, scratchArena);
        if(!parsedRef)
            return MakeUnexpected(Failure{});
        refs.push_back(*parsedRef);
    }
    return refs;
}

[[nodiscard]] static Expected<SceneAsset::AssetMap> ParseAssetsMap(const Path& nwbFilePath, const Value& mapValue, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    if(!mapValue.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a map"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_AssetsField));
        return MakeUnexpected(Failure{});
    }

    const auto& map = mapValue.asMap();
    SceneAsset::AssetMap assets(0, Hasher<Name>(), EqualTo<Name>(), arena);
    assets.reserve(map.size());
    for(const auto& [nicknameKey, assetValue] : map){
        const AStringView nicknameView(nicknameKey.data(), nicknameKey.size());
        if(TrimView(nicknameView).empty() || nicknameView != TrimView(nicknameView)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset nickname '{}' must not be empty or padded"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(nicknameView));
            return MakeUnexpected(Failure{});
        }
        const auto nickname = Name::FromText(nicknameView);
        if(!nickname || !*nickname){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset nickname '{}' contains an embedded null"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(nicknameView));
            return MakeUnexpected(Failure{});
        }
        if(!assetValue.isString()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' entries must be strings"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(nicknameView));
            return MakeUnexpected(Failure{});
        }
        const AStringView text(assetValue.asString().data(), assetValue.asString().size());
        const auto parsedRef = ParseAssetRefText<Core::Assets::IAsset>(nwbFilePath, s_SceneDiagnosticPrefix, nicknameView, text, scratchArena);
        if(!parsedRef)
            return MakeUnexpected(Failure{});
        if(!assets.emplace(*nickname, *parsedRef).second){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': duplicate asset nickname '{}'"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(nicknameView));
            return MakeUnexpected(Failure{});
        }
    }
    return assets;
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
    using namespace __hidden_scene_cook;

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

    TransformComponentAsset candidate(arena, entry.virtualPath);
    candidate.component = entry.component;
    if(!candidate.validatePayload())
        return MakeUnexpected(Failure{});

    return entry;
}

Expected<TransformComponentCookEntry> ParseTransformComponentCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, __hidden_scene_cook::s_TransformDiagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseTransformComponentCookMetadata(*virtualPathResult, nwbFilePath, **asset, arena, scratchArena);
}

Expected<TransformComponentAsset> BuildTransformComponentAsset(TransformComponentCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){
    TransformComponentAsset asset(arena, entry.virtualPath);
    asset.component = entry.component;
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CameraComponentCookEntry> ParseCameraComponentCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena&
){
    using namespace __hidden_scene_cook;

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

        if(const Core::Metascript::Value* nearPlaneValue = asset.findField(s_NearPlaneField)){
            const auto nearPlane = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, *nearPlaneValue, s_CameraDiagnosticPrefix, s_NearPlaneField);
            if(!nearPlane)
                return MakeUnexpected(Failure{});
            entry.component.setNearPlane(*nearPlane);
        }
        if(const Core::Metascript::Value* farPlaneValue = asset.findField(s_FarPlaneField)){
            const auto farPlane = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, *farPlaneValue, s_CameraDiagnosticPrefix, s_FarPlaneField);
            if(!farPlane)
                return MakeUnexpected(Failure{});
            entry.component.setFarPlane(*farPlane);
        }
        if(const Core::Metascript::Value* aspectRatioValue = asset.findField(s_AspectRatioField)){
            const auto aspectRatio = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, *aspectRatioValue, s_CameraDiagnosticPrefix, s_AspectRatioField);
            if(!aspectRatio)
                return MakeUnexpected(Failure{});
            entry.component.setAspectRatio(*aspectRatio);
        }
    }

    CameraComponentAsset candidate(arena, entry.virtualPath);
    candidate.component = entry.component;
    if(!candidate.validatePayload())
        return MakeUnexpected(Failure{});

    return entry;
}

Expected<CameraComponentCookEntry> ParseCameraComponentCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, __hidden_scene_cook::s_CameraDiagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseCameraComponentCookMetadata(*virtualPathResult, nwbFilePath, **asset, arena, scratchArena);
}

Expected<CameraComponentAsset> BuildCameraComponentAsset(CameraComponentCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){
    CameraComponentAsset asset(arena, entry.virtualPath);
    asset.component = entry.component;
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<LightComponentCookEntry> ParseLightComponentCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena&
){
    using namespace __hidden_scene_cook;

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
        if(const Core::Metascript::Value* intensityValue = asset.findField(s_IntensityField)){
            const auto intensity = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, *intensityValue, s_LightDiagnosticPrefix, s_IntensityField);
            if(!intensity)
                return MakeUnexpected(Failure{});
            entry.component.setIntensity(*intensity);
        }
        if(const Core::Metascript::Value* rangeValue = asset.findField(s_RangeField)){
            const auto range = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, *rangeValue, s_LightDiagnosticPrefix, s_RangeField);
            if(!range)
                return MakeUnexpected(Failure{});
            entry.component.range = *range;
        }
        if(const Core::Metascript::Value* innerConeValue = asset.findField(s_InnerConeCosField)){
            const auto innerCone = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, *innerConeValue, s_LightDiagnosticPrefix, s_InnerConeCosField);
            if(!innerCone)
                return MakeUnexpected(Failure{});
            entry.component.innerConeCos = *innerCone;
        }
        if(const Core::Metascript::Value* outerConeValue = asset.findField(s_OuterConeCosField)){
            const auto outerCone = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, *outerConeValue, s_LightDiagnosticPrefix, s_OuterConeCosField);
            if(!outerCone)
                return MakeUnexpected(Failure{});
            entry.component.outerConeCos = *outerCone;
        }
        if(const Core::Metascript::Value* angularRadiusValue = asset.findField(s_AngularRadiusField)){
            const auto angularRadius = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, *angularRadiusValue, s_LightDiagnosticPrefix, s_AngularRadiusField);
            if(!angularRadius)
                return MakeUnexpected(Failure{});
            entry.component.angularRadius = *angularRadius;
        }
        if(const Core::Metascript::Value* sourceRadiusValue = asset.findField(s_SourceRadiusField)){
            const auto sourceRadius = Core::Assets::ReadMetadataFiniteF32Value(nwbFilePath, *sourceRadiusValue, s_LightDiagnosticPrefix, s_SourceRadiusField);
            if(!sourceRadius)
                return MakeUnexpected(Failure{});
            entry.component.sourceRadius = *sourceRadius;
        }
        if(const Core::Metascript::Value* enableCausticsValue = asset.findField(s_EnableCausticsField)){
            const auto enableCaustics = ParseMetadataBoolField(nwbFilePath, s_LightDiagnosticPrefix, *enableCausticsValue, s_EnableCausticsField);
            if(!enableCaustics)
                return MakeUnexpected(Failure{});
            entry.component.enableCaustics = *enableCaustics;
        }
    }

    LightComponentAsset candidate(arena, entry.virtualPath);
    candidate.component = entry.component;
    if(!candidate.validatePayload())
        return MakeUnexpected(Failure{});

    return entry;
}

Expected<LightComponentCookEntry> ParseLightComponentCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, __hidden_scene_cook::s_LightDiagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseLightComponentCookMetadata(*virtualPathResult, nwbFilePath, **asset, arena, scratchArena);
}

Expected<LightComponentAsset> BuildLightComponentAsset(LightComponentCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){
    LightComponentAsset asset(arena, entry.virtualPath);
    asset.component = entry.component;
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<EntityCookEntry> ParseEntityCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_scene_cook;

    EntityCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_EntityDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_EntityDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_EntityDiagnosticPrefix, { s_ComponentsField }))
        return MakeUnexpected(Failure{});

    const Core::Metascript::Value* componentsValue = asset.findField(s_ComponentsField);
    if(!componentsValue){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required"), ConvertString(s_EntityDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_ComponentsField));
        return MakeUnexpected(Failure{});
    }
    auto components = ParseAssetRefList<IComponent>(nwbFilePath, s_EntityDiagnosticPrefix, *componentsValue, s_ComponentsField, arena, scratchArena);
    if(!components)
        return MakeUnexpected(Failure{});
    entry.components = Move(*components);

    EntityAsset candidate(arena, entry.virtualPath);
    candidate.setComponents(EntityAsset::ComponentRefVector(entry.components));
    if(!candidate.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});

    return entry;
}

Expected<EntityCookEntry> ParseEntityCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, __hidden_scene_cook::s_EntityDiagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseEntityCookMetadata(*virtualPathResult, nwbFilePath, **asset, arena, scratchArena);
}

Expected<EntityAsset> BuildEntityAsset(EntityCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    EntityAsset asset(arena, entry.virtualPath);
    asset.setComponents(Move(entry.components));
    if(!asset.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ProjectCookEntry> ParseProjectCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_scene_cook;

    ProjectCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_ProjectDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_ProjectDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_ProjectDiagnosticPrefix, { s_StartSceneField }))
        return MakeUnexpected(Failure{});

    const auto startSceneField = Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_ProjectDiagnosticPrefix, s_StartSceneField, true);
    if(!startSceneField)
        return MakeUnexpected(Failure{});
    const auto startSceneRef = ParseAssetRefText<SceneAsset>(nwbFilePath, s_ProjectDiagnosticPrefix, s_StartSceneField, startSceneField->text, scratchArena);
    if(!startSceneRef)
        return MakeUnexpected(Failure{});
    entry.startScene = *startSceneRef;

    ProjectAsset candidate(arena, entry.virtualPath);
    candidate.setStartScene(entry.startScene);
    if(!candidate.validatePayload())
        return MakeUnexpected(Failure{});

    return entry;
}

Expected<ProjectCookEntry> ParseProjectCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, __hidden_scene_cook::s_ProjectDiagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseProjectCookMetadata(*virtualPathResult, nwbFilePath, **asset, arena, scratchArena);
}

Expected<ProjectAsset> BuildProjectAsset(ProjectCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena&){
    ProjectAsset asset(arena, entry.virtualPath);
    asset.setStartScene(entry.startScene);
    if(!asset.validatePayload())
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SceneCookEntry> ParseSceneCookMetadata(
    const Name virtualPath,
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_scene_cook;

    SceneCookEntry entry(arena);
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_SceneDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::AssignCookEntryVirtualPath(entry, virtualPath, nwbFilePath, s_SceneDiagnosticPrefix))
        return MakeUnexpected(Failure{});
    if(!Core::Assets::ValidateMetadataAssetFields(nwbFilePath, asset, s_SceneDiagnosticPrefix, { s_SubsystemsField, s_EntitiesField, s_AssetsField }))
        return MakeUnexpected(Failure{});

    const Core::Metascript::Value* subsystemsValue = asset.findField(s_SubsystemsField);
    if(!subsystemsValue){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required"), ConvertString(s_SceneDiagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_SubsystemsField));
        return MakeUnexpected(Failure{});
    }
    auto subsystems = ParseAssetRefList<ISubsystem>(nwbFilePath, s_SceneDiagnosticPrefix, *subsystemsValue, s_SubsystemsField, arena, scratchArena);
    if(!subsystems)
        return MakeUnexpected(Failure{});
    usize writeIndex = 0u;
    for(usize readIndex = 0u; readIndex < subsystems->size(); ++readIndex){
        const SceneAsset::SubsystemRef& candidateRef = (*subsystems)[readIndex];
        bool duplicate = false;
        for(usize prevIndex = 0u; prevIndex < writeIndex; ++prevIndex){
            if((*subsystems)[prevIndex].name() == candidateRef.name()){
                duplicate = true;
                break;
            }
        }
        if(!duplicate){
            if(writeIndex != readIndex)
                (*subsystems)[writeIndex] = candidateRef;
            ++writeIndex;
        }
    }
    subsystems->resize(writeIndex);
    entry.subsystems = Move(*subsystems);

    if(const Core::Metascript::Value* entitiesValue = asset.findField(s_EntitiesField)){
        auto entities = ParseAssetRefList<EntityAsset>(nwbFilePath, s_SceneDiagnosticPrefix, *entitiesValue, s_EntitiesField, arena, scratchArena);
        if(!entities)
            return MakeUnexpected(Failure{});
        entry.entities = Move(*entities);
    }

    if(const Core::Metascript::Value* assetsValue = asset.findField(s_AssetsField)){
        auto assets = ParseAssetsMap(nwbFilePath, *assetsValue, arena, scratchArena);
        if(!assets)
            return MakeUnexpected(Failure{});
        entry.assets = Move(*assets);
    }

    SceneAsset candidate(arena, entry.virtualPath);
    candidate.setContents(
        SceneAsset::SubsystemRefVector(entry.subsystems),
        SceneAsset::AssetMap(entry.assets),
        SceneAsset::EntityRefVector(entry.entities)
    );
    if(!candidate.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});

    return entry;
}

Expected<SceneCookEntry> ParseSceneCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    Core::Assets::AssetArena& arena,
    Core::Alloc::ScratchArena& scratchArena
){
    auto virtualPathResult = Core::Assets::BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
    if(!virtualPathResult)
        return MakeUnexpected(Failure{});
    const auto asset = Core::Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(nwbFilePath, doc, __hidden_scene_cook::s_SceneDiagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    return ParseSceneCookMetadata(*virtualPathResult, nwbFilePath, **asset, arena, scratchArena);
}

Expected<SceneAsset> BuildSceneAsset(SceneCookEntry& entry, Core::Assets::AssetArena& arena, Core::Alloc::ScratchArena& scratchArena){
    SceneAsset asset(arena, entry.virtualPath);
    asset.setContents(Move(entry.subsystems), Move(entry.assets), Move(entry.entities));
    if(!asset.validatePayload(scratchArena))
        return MakeUnexpected(Failure{});
    return asset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

