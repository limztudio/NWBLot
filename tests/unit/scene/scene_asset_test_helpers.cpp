// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scene_asset_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::SceneAssetTestHelpers{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetBytes MakeProjectBinary(const Name& startScene, AssetArena& arena){
    ProjectHeaderBinary header;
    header.startSceneHash = startScene.hash();

    AssetBytes binary(arena);
    binary.reserve(sizeof(header));
    AppendPOD(binary, header);
    return binary;
}

AssetBytes MakeSceneBinary(const Span<const SceneAsset::SubsystemRef> subsystems, const SceneAsset::AssetMap& assets, const Span<const SceneAsset::EntityRef> entities, AssetArena& arena){
    SceneHeaderBinary header;
    header.subsystemCount = static_cast<u64>(subsystems.size());
    header.entityCount = static_cast<u64>(entities.size());
    header.assetCount = static_cast<u64>(assets.size());

    AssetBytes binary(arena);
    binary.reserve(sizeof(header) + subsystems.size() * sizeof(NameHash) + entities.size() * sizeof(NameHash) + assets.size() * sizeof(SceneAssetEntryBinary));
    AppendPOD(binary, header);
    for(const SceneAsset::SubsystemRef& ref : subsystems)
        AppendPOD(binary, ref.name().hash());
    for(const SceneAsset::EntityRef& ref : entities)
        AppendPOD(binary, ref.name().hash());
    for(const auto& [nickname, ref] : assets){
        SceneAssetEntryBinary entry;
        entry.nicknameHash = nickname.hash();
        entry.assetHash = ref.name().hash();
        AppendPOD(binary, entry);
    }
    return binary;
}

AssetBytes MakeEntityBinary(const Span<const EntityAsset::ComponentRef> components, AssetArena& arena){
    EntityHeaderBinary header;
    header.componentCount = static_cast<u64>(components.size());

    AssetBytes binary(arena);
    binary.reserve(sizeof(header) + components.size() * sizeof(NameHash));
    AppendPOD(binary, header);
    for(const EntityAsset::ComponentRef& ref : components)
        AppendPOD(binary, ref.name().hash());
    return binary;
}

void MapAssetBinarySource::setBinary(const Name& virtualPath, const Span<const u8> binary){
    AssetBytes copy(m_arena);
    copy.assign(binary.begin(), binary.end());
    for(Entry& entry : m_entries){
        if(entry.path == virtualPath){
            entry.binary = Move(copy);
            return;
        }
    }
    m_entries.emplace_back(virtualPath, Move(copy));
}

b8 MapAssetBinarySource::readAssetBinary(const Name& virtualPath, AssetBytes& outBinary)const{
    outBinary.clear();
    for(const Entry& entry : m_entries){
        if(entry.path == virtualPath){
            outBinary.assign(entry.binary.begin(), entry.binary.end());
            return true;
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

