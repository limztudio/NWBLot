// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "arena_names.h"
#include "binary_payload.h"

#include <core/assets/auto_registration.h>
#include <core/assets/binary_payload_io.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_ProjectAssetCodecAutoRegistrar, ProjectAssetCodec);
NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_SceneAssetCodecAutoRegistrar, SceneAssetCodec);

inline constexpr TStringView s_ActiveSceneSubsystemsOwner = NWB_TEXT("ActiveSceneSubsystems");
using NameHashSet = HashSet<NameHash, Core::Alloc::ScratchArena, Hasher<NameHash>, EqualTo<NameHash>>;


template<typename TAsset>
[[nodiscard]] static bool ValidateUniqueAssetRefs(
    const Span<const Core::Assets::AssetRef<TAsset>> references,
    const TStringView contextLabel,
    Core::Alloc::ScratchArena& scratchArena
){
    NameHashSet seen(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    seen.reserve(references.size());
    for(usize i = 0u; i < references.size(); ++i){
        const Core::Assets::AssetRef<TAsset>& entry = references[i];
        if(!entry.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("SceneAsset::validatePayload failed: {} entry {} is empty"), contextLabel, i);
            return false;
        }
        if(!seen.insert(entry.name().hash()).second){
            NWB_LOGGER_ERROR(NWB_TEXT("SceneAsset::validatePayload failed: duplicate {} entry at index {}"), contextLabel, i);
            return false;
        }
    }
    return true;
}

[[nodiscard]] static bool ValidateAssetMap(const SceneAsset::AssetMap& assets){
    for(const auto& [nickname, assetRef] : assets){
        if(!nickname){
            NWB_LOGGER_ERROR(NWB_TEXT("SceneAsset::validatePayload failed: asset entry has an empty nickname"));
            return false;
        }
        if(!assetRef.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("SceneAsset::validatePayload failed: asset nickname '{}' has an empty asset reference"), ConvertString(nickname.resolvedText()));
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ProjectAsset::validatePayload()const{
    if(!checkVirtualPath(NWB_TEXT("ProjectAsset::validatePayload")))
        return false;
    if(!m_startScene.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("ProjectAsset::validatePayload failed: start_scene must not be empty"));
        return false;
    }
    return true;
}

bool ProjectAsset::loadBinary(const Span<const u8> binary){
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<ProjectBinaryPayload::ProjectHeaderBinary>(
        binary,
        ProjectBinaryPayload::s_ProjectMagic,
        ProjectBinaryPayload::s_ProjectLoadBinaryContext,
        ProjectBinaryPayload::s_ProjectAssetKindLabel,
        cursor
    );
    if(!headerResult)
        return false;
    const ProjectBinaryPayload::ProjectHeaderBinary& header = *headerResult;
    if(header.version != ProjectBinaryPayload::s_ProjectVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("ProjectAsset::loadBinary failed: unsupported version {}"), header.version);
        return false;
    }
    if(!Core::Assets::ReadCompletePayload(binary, cursor, ProjectBinaryPayload::s_ProjectLoadBinaryContext))
        return false;

    m_startScene.virtualPath = Name(header.startSceneHash);
    return validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SceneAsset::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!checkVirtualPath(NWB_TEXT("SceneAsset::validatePayload")))
        return false;
    if(m_subsystems.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("SceneAsset::validatePayload failed: scene must declare at least one subsystem"));
        return false;
    }
    if(!__hidden_scene_runtime::ValidateUniqueAssetRefs(Span<const SubsystemRef>(m_subsystems), NWB_TEXT("subsystem"), scratchArena))
        return false;
    if(!__hidden_scene_runtime::ValidateAssetMap(m_assets))
        return false;
    return true;
}

bool SceneAsset::loadBinary(const Span<const u8> binary){
    Core::Alloc::ScratchArena scratchArena(AssetsSceneArenaScope::s_SceneLoadBinaryArena);
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<SceneBinaryPayload::SceneHeaderBinary>(
        binary,
        SceneBinaryPayload::s_SceneMagic,
        SceneBinaryPayload::s_SceneLoadBinaryContext,
        SceneBinaryPayload::s_SceneAssetKindLabel,
        cursor
    );
    if(!headerResult)
        return false;
    const SceneBinaryPayload::SceneHeaderBinary& header = *headerResult;
    if(header.version != SceneBinaryPayload::s_SceneVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("SceneAsset::loadBinary failed: unsupported version {}"), header.version);
        return false;
    }

    Vector<NameHash, Core::Alloc::ScratchArena> subsystemHashes(scratchArena);
    Vector<SceneBinaryPayload::SceneAssetEntryBinary, Core::Alloc::ScratchArena> assetEntries(scratchArena);

    if(!Core::Assets::ReadVectorPayload(binary, header.subsystemCount, SceneBinaryPayload::s_SceneLoadBinaryContext, SceneBinaryPayload::s_SubsystemsPayloadLabel, cursor, subsystemHashes))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.assetCount, SceneBinaryPayload::s_SceneLoadBinaryContext, SceneBinaryPayload::s_AssetsPayloadLabel, cursor, assetEntries))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, SceneBinaryPayload::s_SceneLoadBinaryContext))
        return false;

    auto& assetArena = m_subsystems.get_allocator().arena();
    SceneAsset candidate(assetArena, virtualPath());

    candidate.m_subsystems.reserve(subsystemHashes.size());
    for(const NameHash& hash : subsystemHashes){
        SubsystemRef ref;
        ref.virtualPath = Name(hash);
        candidate.m_subsystems.push_back(ref);
    }

    candidate.m_assets.reserve(assetEntries.size());
    for(usize i = 0u; i < assetEntries.size(); ++i){
        const SceneBinaryPayload::SceneAssetEntryBinary& entry = assetEntries[i];
        const Name nickname(entry.nicknameHash);
        AssetRef ref;
        ref.virtualPath = Name(entry.assetHash);
        if(!nickname || !ref.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("SceneAsset::loadBinary failed: invalid asset entry at index {}"), i);
            return false;
        }
        if(!candidate.m_assets.emplace(nickname, ref).second){
            NWB_LOGGER_ERROR(NWB_TEXT("SceneAsset::loadBinary failed: duplicate asset nickname at index {}"), i);
            return false;
        }
    }

    if(!candidate.validatePayload(scratchArena))
        return false;
    *this = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ActiveSceneSubsystems::loadProjectStartScene(Core::Assets::AssetManager& assetManager, const ProjectRef& project){
    auto projectAsset = assetManager.loadTypedSync<ProjectAsset>(project.name(), __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner, "project manifest");
    if(!projectAsset)
        return false;
    if(!loadScene(assetManager, (*projectAsset)->startScene()))
        return false;
    m_project = project;
    return true;
}

bool ActiveSceneSubsystems::loadScene(Core::Assets::AssetManager& assetManager, const SceneRef& scene){
    auto sceneAssetResult = assetManager.loadTypedSync<SceneAsset>(scene.name(), __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner, "scene");
    if(!sceneAssetResult)
        return false;
    const SceneAsset& sceneAsset = **sceneAssetResult;

    AssetMap nextSceneAssets(0, Hasher<Name>(), EqualTo<Name>(), m_arena);
    LoadedSubsystemVector nextLoadedSubsystems(m_arena);

    nextSceneAssets.reserve(sceneAsset.assets().size());
    for(const auto& [nickname, asset] : sceneAsset.assets())
        nextSceneAssets.emplace(nickname, asset);

    nextLoadedSubsystems.reserve(sceneAsset.subsystems().size());
    for(const SubsystemRef& currentSubsystem : sceneAsset.subsystems()){
        auto loadedAsset = assetManager.loadSync(ISubsystem::AssetTypeName(), currentSubsystem.name());
        if(!loadedAsset){
            NWB_LOGGER_ERROR(
                NWB_TEXT("{}: failed to load subsystem asset '{}'"),
                __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner,
                ConvertString(currentSubsystem.name().resolvedText())
            );
            return false;
        }
        const ISubsystem* const subsystemPtr = CastSubsystem(loadedAsset->get());
        if(!subsystemPtr){
            NWB_LOGGER_ERROR(
                NWB_TEXT("{}: asset '{}' is not a subsystem"),
                __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner,
                ConvertString(currentSubsystem.name().resolvedText())
            );
            return false;
        }
        for(const UniquePtr<ISubsystem>& existing : nextLoadedSubsystems){
            if(existing && existing->assetType() == subsystemPtr->assetType()){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("{}: duplicate subsystem type '{}' in scene '{}'"),
                    __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner,
                    ConvertString(subsystemPtr->assetType().resolvedText()),
                    ConvertString(scene.name().resolvedText())
                );
                return false;
            }
        }
        nextLoadedSubsystems.push_back(UniquePtr<ISubsystem>(checked_cast<ISubsystem*>(loadedAsset->release())));
    }

    m_scene = scene;
    m_sceneAssets = Move(nextSceneAssets);
    m_loadedSubsystems = Move(nextLoadedSubsystems);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
