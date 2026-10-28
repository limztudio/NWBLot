// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "arena_names.h"
#include "binary_payload.h"

#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ComponentAssetSerialization{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TPod>
[[nodiscard]] bool WriteSinglePodPayload(const TPod& pod, Core::Assets::AssetBytes& outBytes){
    outBytes.clear();
    outBytes.reserve(sizeof(pod));
    AppendPOD(outBytes, pod);
    return true;
}

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
    return ComponentAssetSerialization::WriteSinglePodPayload(binaryPayload, outPayload);
}

bool CameraComponentAsset::serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const{
    ComponentBinaryPayload::CameraComponentPayloadBinary binaryPayload;
    binaryPayload.projection = component.projection;
    return ComponentAssetSerialization::WriteSinglePodPayload(binaryPayload, outPayload);
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
    return ComponentAssetSerialization::WriteSinglePodPayload(binaryPayload, outPayload);
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
    return ComponentAssetSerialization::WriteSinglePodPayload(header, outBinary);
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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

