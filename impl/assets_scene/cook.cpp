// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"


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

    return SerializeEntityAssetBinary(Span<const EntityAsset::ComponentRef>(entity.components()), outBinary);
}

b8 ProjectAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, s_ProjectSerializeContext))
        return false;

    const ProjectAsset& project = *MakeNotNull(checked_cast<const ProjectAsset*>(&asset));
    if(!project.validatePayload())
        return false;

    return SerializeProjectAssetBinary(project.startScene().name().hash(), outBinary);
}

b8 SceneAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(!checkSerializeAssetType(asset, s_SceneSerializeContext))
        return false;

    const SceneAsset& scene = *MakeNotNull(checked_cast<const SceneAsset*>(&asset));
    Core::Alloc::ScratchArena scratchArena(s_SceneSerializeArena);
    if(!scene.validatePayload(scratchArena))
        return false;

    Vector<SceneAssetEntryBinary, Core::Alloc::ScratchArena> assetEntries(scratchArena);
    return SerializeSceneAssetBinary(
        Span<const SceneAsset::SubsystemRef>(scene.subsystems()),
        Span<const SceneAsset::EntityRef>(scene.entities()),
        scene.assets(),
        assetEntries,
        outBinary
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

