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
    const Core::Assets::AssetVector<Core::Assets::AssetRef<TAsset>>& references,
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

[[nodiscard]] static bool ValidateUniqueNames(
    const SceneAsset::NameVector& names,
    const TStringView contextLabel,
    Core::Alloc::ScratchArena& scratchArena
){
    NameHashSet seen(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    seen.reserve(names.size());
    for(usize i = 0u; i < names.size(); ++i){
        const Name& entry = names[i];
        if(!entry){
            NWB_LOGGER_ERROR(NWB_TEXT("SceneAsset::validatePayload failed: {} entry {} is empty"), contextLabel, i);
            return false;
        }
        if(!seen.insert(entry.hash()).second){
            NWB_LOGGER_ERROR(NWB_TEXT("SceneAsset::validatePayload failed: duplicate {} entry at index {}"), contextLabel, i);
            return false;
        }
    }
    return true;
}

static void AppendUniqueName(ActiveSceneSubsystems::NameVector& list, const Name& name){
    if(!name)
        return;
    for(const Name& existing : list){
        if(existing == name)
            return;
    }
    list.push_back(name);
}

template<typename TAsset>
static void AppendUniqueAssetRef(Vector<Core::Assets::AssetRef<TAsset>, Core::Alloc::GlobalArena>& list, const Core::Assets::AssetRef<TAsset>& ref){
    if(!ref.valid())
        return;
    for(const Core::Assets::AssetRef<TAsset>& existing : list){
        if(existing == ref)
            return;
    }
    list.push_back(ref);
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

bool ProjectAsset::loadBinary(const Core::Assets::AssetBytes& binary){
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
    if(!__hidden_scene_runtime::ValidateUniqueAssetRefs(m_subsystems, NWB_TEXT("subsystem"), scratchArena))
        return false;
    if(!__hidden_scene_runtime::ValidateUniqueNames(m_enabledFeatures, NWB_TEXT("enabled feature"), scratchArena))
        return false;
    if(!__hidden_scene_runtime::ValidateUniqueAssetRefs(m_assets, NWB_TEXT("referenced asset"), scratchArena))
        return false;
    return true;
}

bool SceneAsset::loadBinary(const Core::Assets::AssetBytes& binary){
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
    Vector<NameHash, Core::Alloc::ScratchArena> enabledFeatureHashes(scratchArena);
    Vector<NameHash, Core::Alloc::ScratchArena> assetHashes(scratchArena);

    if(!Core::Assets::ReadVectorPayload(binary, header.subsystemCount, SceneBinaryPayload::s_SceneLoadBinaryContext, SceneBinaryPayload::s_SubsystemsPayloadLabel, cursor, subsystemHashes))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.enabledFeatureCount, SceneBinaryPayload::s_SceneLoadBinaryContext, SceneBinaryPayload::s_EnabledFeaturesPayloadLabel, cursor, enabledFeatureHashes))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.assetCount, SceneBinaryPayload::s_SceneLoadBinaryContext, SceneBinaryPayload::s_AssetsPayloadLabel, cursor, assetHashes))
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

    candidate.m_enabledFeatures.reserve(enabledFeatureHashes.size());
    for(const NameHash& hash : enabledFeatureHashes)
        candidate.m_enabledFeatures.push_back(Name(hash));

    candidate.m_assets.reserve(assetHashes.size());
    for(const NameHash& hash : assetHashes){
        AssetRef ref;
        ref.virtualPath = Name(hash);
        candidate.m_assets.push_back(ref);
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
    m_project = project;
    return loadScene(assetManager, (*projectAsset)->startScene());
}

bool ActiveSceneSubsystems::loadScene(Core::Assets::AssetManager& assetManager, const SceneRef& scene){
    auto sceneAssetResult = assetManager.loadTypedSync<SceneAsset>(scene.name(), __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner, "scene");
    if(!sceneAssetResult)
        return false;
    const SceneAsset& sceneAsset = **sceneAssetResult;

    m_scene = scene;
    m_subsystems.clear();
    m_enabledFeatures.clear();
    m_resolvedAssets.clear();

    for(const Name& feature : sceneAsset.enabledFeatures())
        __hidden_scene_runtime::AppendUniqueName(m_enabledFeatures, feature);
    for(const AssetRef& asset : sceneAsset.assets())
        __hidden_scene_runtime::AppendUniqueAssetRef(m_resolvedAssets, asset);

    Core::Alloc::ScratchArena scratchArena(AssetsSceneArenaScope::s_RuntimeSubsystemLoadArena);
    Vector<SubsystemRef, Core::Alloc::ScratchArena> worklist(scratchArena);
    for(const SubsystemRef& subsystem : sceneAsset.subsystems()){
        __hidden_scene_runtime::AppendUniqueAssetRef(m_subsystems, subsystem);
        worklist.push_back(subsystem);
    }

    for(usize index = 0u; index < worklist.size(); ++index){
        const SubsystemRef currentSubsystem = worklist[index];
        auto subsystemResult = assetManager.loadTypedSync<Subsystem>(currentSubsystem.name(), __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner, "subsystem");
        if(!subsystemResult)
            return false;
        const Subsystem& subsystem = **subsystemResult;

        for(const SubsystemRef& childSubsystem : subsystem.subsystems()){
            const usize previousSize = m_subsystems.size();
            __hidden_scene_runtime::AppendUniqueAssetRef(m_subsystems, childSubsystem);
            if(m_subsystems.size() != previousSize)
                worklist.push_back(childSubsystem);
        }
        for(const AssetRef& unconditionalAsset : subsystem.assets())
            __hidden_scene_runtime::AppendUniqueAssetRef(m_resolvedAssets, unconditionalAsset);
        for(const SubsystemFeature& feature : subsystem.features()){
            if(!isFeatureEnabled(feature.name))
                continue;
            for(const AssetRef& featureAsset : feature.assets)
                __hidden_scene_runtime::AppendUniqueAssetRef(m_resolvedAssets, featureAsset);
        }
    }

    return true;
}

void ActiveSceneSubsystems::enableFeature(const Name& featureName){
    __hidden_scene_runtime::AppendUniqueName(m_enabledFeatures, featureName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

