// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"

#include <impl/assets_scene/scene_asset_binary_codec.adhd.h>

#include <core/assets/auto_registration.h>


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
    Vector<NameHash, Core::Alloc::ScratchArena> componentHashes(scratchArena);
    if(!SceneAssetBinaryCodec().deserializeEntity(binary, componentHashes))
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
    if(!SceneAssetBinaryCodec().deserializeProject(binary, m_startScene.virtualPath))
        return false;
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
    Vector<NameHash, Core::Alloc::ScratchArena> subsystemHashes(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> entityHashes(scratchArena);
    Vector<SceneAssetEntryBinary, Core::Alloc::ScratchArena> assetEntries(scratchArena);
    if(!SceneAssetBinaryCodec().deserializeScene(binary, subsystemHashes, entityHashes, assetEntries))
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

