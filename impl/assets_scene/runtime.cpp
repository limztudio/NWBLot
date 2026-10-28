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


NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_EntityAssetCodecAutoRegistrar, EntityAssetCodec);
NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_ProjectAssetCodecAutoRegistrar, ProjectAssetCodec);
NWB_DEFINE_ASSET_CODEC_REGISTRAR(s_SceneAssetCodecAutoRegistrar, SceneAssetCodec);

inline constexpr TStringView s_ActiveSceneSubsystemsOwner = NWB_TEXT("ActiveSceneSubsystems");
using NameHashSet = HashSet<NameHash, Core::Alloc::ScratchArena, Hasher<NameHash>, EqualTo<NameHash>>;


template<typename TAsset>
static void AssignAssetRefsFromHashes(const Span<const NameHash> hashes, Core::Assets::AssetVector<Core::Assets::AssetRef<TAsset>>& outRefs){
    outRefs.clear();
    outRefs.reserve(hashes.size());
    for(const NameHash& hash : hashes)
        outRefs.push_back(Core::Assets::AssetRef<TAsset>(Name(hash)));
}

template<typename TAsset>
[[nodiscard]] static bool ValidateUniqueAssetRefs(
    const Span<const Core::Assets::AssetRef<TAsset>> references,
    const TStringView contextPrefix,
    const TStringView contextLabel,
    Core::Alloc::ScratchArena& scratchArena
){
    NameHashSet seen(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    seen.reserve(references.size());
    for(usize i = 0u; i < references.size(); ++i){
        const Core::Assets::AssetRef<TAsset>& entry = references[i];
        if(!entry.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: {} entry {} is empty"), contextPrefix, contextLabel, i);
            return false;
        }
        if(!seen.insert(entry.name().hash()).second){
            NWB_LOGGER_ERROR(NWB_TEXT("{} failed: duplicate {} entry at index {}"), contextPrefix, contextLabel, i);
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


bool EntityAsset::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!checkVirtualPath(NWB_TEXT("EntityAsset::validatePayload")))
        return false;
    if(m_components.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("EntityAsset::validatePayload failed: entity must declare at least one component"));
        return false;
    }
    if(!__hidden_scene_runtime::ValidateUniqueAssetRefs(Span<const ComponentRef>(m_components), NWB_TEXT("EntityAsset::validatePayload"), NWB_TEXT("component"), scratchArena))
        return false;
    return true;
}

bool EntityAsset::loadBinary(const Span<const u8> binary){
    Core::Alloc::ScratchArena scratchArena(AssetsSceneArenaScope::s_EntityLoadBinaryArena);
    usize cursor = 0u;
    const auto headerResult = Core::Assets::ReadMagicHeaderPayload<EntityBinaryPayload::EntityHeaderBinary>(
        binary,
        EntityBinaryPayload::s_EntityMagic,
        EntityBinaryPayload::s_EntityLoadBinaryContext,
        EntityBinaryPayload::s_EntityAssetKindLabel,
        cursor
    );
    if(!headerResult)
        return false;
    const EntityBinaryPayload::EntityHeaderBinary& header = *headerResult;
    if(header.version != EntityBinaryPayload::s_EntityVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("EntityAsset::loadBinary failed: unsupported version {}"), header.version);
        return false;
    }

    Vector<NameHash, Core::Alloc::ScratchArena> componentHashes(scratchArena);
    if(!Core::Assets::ReadVectorPayload(binary, header.componentCount, EntityBinaryPayload::s_EntityLoadBinaryContext, EntityBinaryPayload::s_ComponentsPayloadLabel, cursor, componentHashes))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, EntityBinaryPayload::s_EntityLoadBinaryContext))
        return false;

    auto& assetArena = m_components.get_allocator().arena();
    EntityAsset candidate(assetArena, virtualPath());
    __hidden_scene_runtime::AssignAssetRefsFromHashes(Span<const NameHash>(componentHashes), candidate.m_components);

    if(!candidate.validatePayload(scratchArena))
        return false;
    *this = Move(candidate);
    return true;
}


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


void SceneAsset::setContents(SubsystemRefVector&& subsystems, AssetMap&& assets, EntityRefVector&& entities)noexcept{
    m_subsystems = Move(subsystems);
    SubsystemAssetRuntime::DeduplicateSubsystemRefs(m_subsystems);
    m_entities = Move(entities);
    m_assets = Move(assets);
}

bool SceneAsset::validatePayload(Core::Alloc::ScratchArena& scratchArena)const{
    if(!checkVirtualPath(NWB_TEXT("SceneAsset::validatePayload")))
        return false;
    if(m_subsystems.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("SceneAsset::validatePayload failed: scene must declare at least one subsystem"));
        return false;
    }
    if(!__hidden_scene_runtime::ValidateUniqueAssetRefs(Span<const SubsystemRef>(m_subsystems), NWB_TEXT("SceneAsset::validatePayload"), NWB_TEXT("subsystem"), scratchArena))
        return false;
    if(!__hidden_scene_runtime::ValidateUniqueAssetRefs(Span<const EntityRef>(m_entities), NWB_TEXT("SceneAsset::validatePayload"), NWB_TEXT("entity"), scratchArena))
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
    Vector<NameHash, Core::Alloc::ScratchArena> entityHashes(scratchArena);
    Vector<SceneBinaryPayload::SceneAssetEntryBinary, Core::Alloc::ScratchArena> assetEntries(scratchArena);

    if(!Core::Assets::ReadVectorPayload(binary, header.subsystemCount, SceneBinaryPayload::s_SceneLoadBinaryContext, SceneBinaryPayload::s_SubsystemsPayloadLabel, cursor, subsystemHashes))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.entityCount, SceneBinaryPayload::s_SceneLoadBinaryContext, SceneBinaryPayload::s_EntitiesPayloadLabel, cursor, entityHashes))
        return false;
    if(!Core::Assets::ReadVectorPayload(binary, header.assetCount, SceneBinaryPayload::s_SceneLoadBinaryContext, SceneBinaryPayload::s_AssetsPayloadLabel, cursor, assetEntries))
        return false;
    if(!Core::Assets::ReadCompletePayload(binary, cursor, SceneBinaryPayload::s_SceneLoadBinaryContext))
        return false;

    auto& assetArena = m_subsystems.get_allocator().arena();
    SceneAsset candidate(assetArena, virtualPath());

    __hidden_scene_runtime::AssignAssetRefsFromHashes(Span<const NameHash>(subsystemHashes), candidate.m_subsystems);
    SubsystemAssetRuntime::DeduplicateSubsystemRefs(candidate.m_subsystems);

    __hidden_scene_runtime::AssignAssetRefsFromHashes(Span<const NameHash>(entityHashes), candidate.m_entities);

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
    auto projectAsset = assetManager.loadTypedSync<ProjectAsset>(project.name(), __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner);
    if(!projectAsset)
        return false;
    return loadScene(assetManager, (*projectAsset)->startScene());
}

bool ActiveSceneSubsystems::loadScene(Core::Assets::AssetManager& assetManager, const SceneRef& scene){
    auto sceneAssetResult = assetManager.loadTypedSync<SceneAsset>(scene.name(), __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner);
    if(!sceneAssetResult)
        return false;
    const SceneAsset& sceneAsset = **sceneAssetResult;

    AssetMap nextSceneAssets(0, Hasher<Name>(), EqualTo<Name>(), m_arena);
    LoadedSubsystemVector nextLoadedSubsystems(m_arena);
    LoadedEntityVector nextLoadedEntities(m_arena);

    nextSceneAssets.reserve(sceneAsset.assets().size());
    for(const auto& [nickname, asset] : sceneAsset.assets())
        nextSceneAssets.emplace(nickname, asset);

    Core::Alloc::ScratchArena scratchArena(AssetsSceneArenaScope::s_ActiveSceneLoadArena);
    __hidden_scene_runtime::NameHashSet seenSubsystems(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
    Vector<SubsystemRef, Core::Alloc::ScratchArena> pendingSubsystems(scratchArena);
    pendingSubsystems.reserve(sceneAsset.subsystems().size());

    const auto enqueueSubsystem = [&](const SubsystemRef& subsystemRef){
        if(!subsystemRef.valid())
            return;
        if(seenSubsystems.insert(subsystemRef.name().hash()).second)
            pendingSubsystems.push_back(subsystemRef);
    };

    for(const SubsystemRef& subsystemRef : sceneAsset.subsystems())
        enqueueSubsystem(subsystemRef);

    nextLoadedSubsystems.reserve(pendingSubsystems.size());
    for(usize queueIndex = 0u; queueIndex < pendingSubsystems.size(); ++queueIndex){
        const SubsystemRef currentSubsystem = pendingSubsystems[queueIndex];
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
        for(const SubsystemRef& nestedSubsystem : subsystemPtr->subsystems)
            enqueueSubsystem(nestedSubsystem);

        nextLoadedSubsystems.push_back(UniquePtr<ISubsystem>(checked_cast<ISubsystem*>(loadedAsset->release())));
    }

    nextLoadedEntities.reserve(sceneAsset.entities().size());
    for(const EntityRef& currentEntity : sceneAsset.entities()){
        auto entityAssetResult = assetManager.loadTypedSync<EntityAsset>(currentEntity.name(), __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner);
        if(!entityAssetResult)
            return false;
        const EntityAsset& entityAsset = **entityAssetResult;

        LoadedComponentVector loadedComponents(m_arena);
        loadedComponents.reserve(entityAsset.components().size());
        for(const ComponentRef& currentComponent : entityAsset.components()){
            auto loadedComponentAsset = assetManager.loadSync(IComponent::AssetTypeName(), currentComponent.name());
            if(!loadedComponentAsset){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("{}: failed to load component asset '{}' for entity '{}'"),
                    __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner,
                    ConvertString(currentComponent.name().resolvedText()),
                    ConvertString(currentEntity.name().resolvedText())
                );
                return false;
            }
            const IComponent* const componentPtr = CastComponent(loadedComponentAsset->get());
            if(!componentPtr){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("{}: asset '{}' is not a component"),
                    __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner,
                    ConvertString(currentComponent.name().resolvedText())
                );
                return false;
            }
            for(const UniquePtr<IComponent>& existing : loadedComponents){
                if(existing && existing->assetType() == componentPtr->assetType()){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("{}: duplicate component type '{}' on entity '{}'"),
                        __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner,
                        ConvertString(componentPtr->assetType().resolvedText()),
                        ConvertString(currentEntity.name().resolvedText())
                    );
                    return false;
                }
            }
            loadedComponents.push_back(UniquePtr<IComponent>(checked_cast<IComponent*>(loadedComponentAsset->release())));
        }

        nextLoadedEntities.emplace_back(currentEntity, Move(loadedComponents));
    }

    m_scene = scene;
    m_sceneAssets = Move(nextSceneAssets);
    m_loadedSubsystems = Move(nextLoadedSubsystems);
    m_loadedEntities = Move(nextLoadedEntities);
    return true;
}

bool ActiveSceneSubsystems::instantiateEntities(Core::ECS::World& world)const{
    if(!valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: cannot instantiate entities before loading a valid scene"), __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner);
        return false;
    }

    for(const LoadedEntity& loadedEntity : m_loadedEntities){
        if(loadedEntity.components.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("{}: loaded entity '{}' has no components"),
                __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner,
                ConvertString(loadedEntity.entity.name().resolvedText())
            );
            return false;
        }
        Core::ECS::Entity entity = world.createEntity();
        if(!entity.id().valid()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("{}: failed to create ECS entity for '{}'"),
                __hidden_scene_runtime::s_ActiveSceneSubsystemsOwner,
                ConvertString(loadedEntity.entity.name().resolvedText())
            );
            return false;
        }
        for(const UniquePtr<IComponent>& component : loadedEntity.components){
            if(!component)
                return false;
            component->applyToEntity(entity);
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
