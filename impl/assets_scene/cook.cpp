// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 EntityAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, s_EntitySerializeContext))
        return false;

    const EntityAsset& entity = *MakeNotNull(checked_cast<const EntityAsset*>(&asset));
    Core::Alloc::ScratchArena scratchArena(s_EntitySerializeArena);
    if(!entity.validatePayload(scratchArena))
        return false;

    Vector<NameHash, Core::Alloc::ScratchArena> componentHashes(scratchArena);
    componentHashes.reserve(entity.components().size());
    for(const EntityAsset::ComponentRef& item : entity.components())
        componentHashes.push_back(item.name().hash());

    usize reserveBytes = sizeof(EntityHeaderBinary);
    const b8 canReserve = AddBinaryVectorReserveBytes(reserveBytes, componentHashes);

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    EntityHeaderBinary header;
    header.componentCount = static_cast<u64>(componentHashes.size());
    AppendPOD(outBinary, header);

    return Core::Assets::AppendVectorPayload(outBinary, componentHashes, s_EntitySerializeContext, s_EntityComponentsPayloadLabel);
}

b8 ProjectAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, s_ProjectSerializeContext))
        return false;

    const ProjectAsset& project = *MakeNotNull(checked_cast<const ProjectAsset*>(&asset));
    if(!project.validatePayload())
        return false;

    ProjectHeaderBinary header;
    header.startSceneHash = project.startScene().name().hash();
    return ComponentAssetSerialization::WriteSinglePodPayload(header, outBinary);
}

b8 SceneAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, s_SceneSerializeContext))
        return false;

    const SceneAsset& scene = *MakeNotNull(checked_cast<const SceneAsset*>(&asset));
    Core::Alloc::ScratchArena scratchArena(s_SceneSerializeArena);
    if(!scene.validatePayload(scratchArena))
        return false;

    Vector<NameHash, Core::Alloc::ScratchArena> subsystemHashes(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> entityHashes(scratchArena);
    Vector<SceneAssetEntryBinary, Core::Alloc::ScratchArena> assetEntries(scratchArena);

    subsystemHashes.reserve(scene.subsystems().size());
    for(const SceneAsset::SubsystemRef& item : scene.subsystems())
        subsystemHashes.push_back(item.name().hash());

    entityHashes.reserve(scene.entities().size());
    for(const SceneAsset::EntityRef& item : scene.entities())
        entityHashes.push_back(item.name().hash());

    assetEntries.reserve(scene.assets().size());
    for(const auto& [nickname, item] : scene.assets()){
        SceneAssetEntryBinary entry;
        entry.nicknameHash = nickname.hash();
        entry.assetHash = item.name().hash();
        assetEntries.push_back(entry);
    }
    Sort(assetEntries.begin(), assetEntries.end(), [](const SceneAssetEntryBinary& lhs, const SceneAssetEntryBinary& rhs)noexcept{
        return LessNameHash(lhs.nicknameHash, rhs.nicknameHash);
    });

    usize reserveBytes = sizeof(SceneHeaderBinary);
    const b8 canReserve =
        AddBinaryVectorReserveBytes(reserveBytes, subsystemHashes)
        && AddBinaryVectorReserveBytes(reserveBytes, entityHashes)
        && AddBinaryVectorReserveBytes(reserveBytes, assetEntries)
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    SceneHeaderBinary header;
    header.subsystemCount = static_cast<u64>(subsystemHashes.size());
    header.entityCount = static_cast<u64>(entityHashes.size());
    header.assetCount = static_cast<u64>(assetEntries.size());
    AppendPOD(outBinary, header);

    return
        Core::Assets::AppendVectorPayload(outBinary, subsystemHashes, s_SceneSerializeContext, s_SceneSubsystemsPayloadLabel)
        && Core::Assets::AppendVectorPayload(outBinary, entityHashes, s_SceneSerializeContext, s_SceneEntitiesPayloadLabel)
        && Core::Assets::AppendVectorPayload(outBinary, assetEntries, s_SceneSerializeContext, s_SceneAssetsPayloadLabel)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
