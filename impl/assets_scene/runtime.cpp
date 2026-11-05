// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_EntityAssetCodecAutoRegistrar, EntityAssetCodec);
NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_ProjectAssetCodecAutoRegistrar, ProjectAssetCodec);
NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_SceneAssetCodecAutoRegistrar, SceneAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 EntityAsset::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!checkVirtualPath(NWB_TEXT("EntityAsset::validatePayload")))
        return false;
    if(!ValidateEntityAssetComponentCount(m_components.size()))
        return false;
    if(!ValidateUniqueAssetRefs(Span<const ComponentRef>(m_components), "EntityAsset::validatePayload", "component", scratchArena))
        return false;
    return true;
}

b8 EntityAsset::loadBinary(const Span<const u8> binary){
    Core::Alloc::ScratchArena scratchArena(s_EntityLoadBinaryArena);
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<EntityHeaderBinary>(binary, s_EntityMagic, s_EntityLoadBinaryContext, s_EntityAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const EntityHeaderBinary& header = *headerResult;
    if(!ValidateEntityHeaderBinary(header))
        return false;

    Vector<NameHash, Core::Alloc::ScratchArena> componentHashes(scratchArena);
    if(!Core::Assets::ReadVectorPayload(binary, header.componentCount, s_EntityLoadBinaryContext, s_EntityComponentsPayloadLabel, cursor, componentHashes))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, s_EntityLoadBinaryContext))
        return false;

    auto& assetArena = m_components.get_allocator().arena();
    EntityAsset candidate(assetArena, virtualPath());
    AssignAssetRefsFromHashes(Span<const NameHash>(componentHashes), candidate.m_components);

    if(!candidate.validatePayload(scratchArena))
        return false;
    *this = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ProjectAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("ProjectAsset::validatePayload")))
        return false;
    return ValidateProjectAssetPayload(m_startScene.valid());
}

b8 ProjectAsset::loadBinary(const Span<const u8> binary){
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<ProjectHeaderBinary>(binary, s_ProjectMagic, s_ProjectLoadBinaryContext, s_ProjectAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const ProjectHeaderBinary& header = *headerResult;
    if(!ValidateProjectHeaderBinary(header))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, s_ProjectLoadBinaryContext))
        return false;

    m_startScene.virtualPath = Name(header.startSceneHash);
    return validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SceneAsset::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!checkVirtualPath(NWB_TEXT("SceneAsset::validatePayload")))
        return false;
    if(!ValidateSceneAssetSubsystemCount(m_subsystems.size()))
        return false;
    if(!ValidateUniqueAssetRefs(Span<const SubsystemRef>(m_subsystems), "SceneAsset::validatePayload", "subsystem", scratchArena))
        return false;
    if(!ValidateUniqueAssetRefs(Span<const EntityRef>(m_entities), "SceneAsset::validatePayload", "entity", scratchArena))
        return false;
    if(!ValidateSceneAssetMap(m_assets))
        return false;
    return true;
}

b8 SceneAsset::loadBinary(const Span<const u8> binary){
    Core::Alloc::ScratchArena scratchArena(s_SceneLoadBinaryArena);
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SceneHeaderBinary>(binary, s_SceneMagic, s_SceneLoadBinaryContext, s_SceneAssetKindLabel, cursor);
    if(!headerResult)
        return false;
    const SceneHeaderBinary& header = *headerResult;
    if(!ValidateSceneHeaderBinary(header))
        return false;

    Vector<NameHash, Core::Alloc::ScratchArena> subsystemHashes(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> entityHashes(scratchArena);
    Vector<SceneAssetEntryBinary, Core::Alloc::ScratchArena> assetEntries(scratchArena);

    if(!Core::Assets::ReadVectorPayload(binary, header.subsystemCount, s_SceneLoadBinaryContext, s_SceneSubsystemsPayloadLabel, cursor, subsystemHashes))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.entityCount, s_SceneLoadBinaryContext, s_SceneEntitiesPayloadLabel, cursor, entityHashes))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.assetCount, s_SceneLoadBinaryContext, s_SceneAssetsPayloadLabel, cursor, assetEntries))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, s_SceneLoadBinaryContext))
        return false;

    auto& assetArena = m_subsystems.get_allocator().arena();
    SceneAsset candidate(assetArena, virtualPath());

    AssignAssetRefsFromHashes(Span<const NameHash>(subsystemHashes), candidate.m_subsystems);
    DeduplicateSubsystemRefs(candidate.m_subsystems);

    AssignAssetRefsFromHashes(Span<const NameHash>(entityHashes), candidate.m_entities);

    if(!UnpackSceneAssetEntriesBinary(Span<const SceneAssetEntryBinary>(assetEntries), candidate.m_assets))
        return false;

    if(!candidate.validatePayload(scratchArena))
        return false;
    *this = Move(candidate);
    return true;
}

void SceneAsset::setContents(SubsystemRefVector&& subsystems, AssetMap&& assets, EntityRefVector&& entities)noexcept{
    m_subsystems = Move(subsystems);
    DeduplicateSubsystemRefs(m_subsystems);
    m_entities = Move(entities);
    m_assets = Move(assets);
}

void SceneAsset::takeContents(SubsystemRefVector& outSubsystems, AssetMap& outAssets, EntityRefVector& outEntities)noexcept{
    outSubsystems = Move(m_subsystems);
    outAssets = Move(m_assets);
    outEntities = Move(m_entities);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

