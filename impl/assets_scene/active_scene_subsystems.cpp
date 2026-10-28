// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_active_scene_subsystems{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_ActiveSceneSubsystemsOwner = NWB_TEXT("ActiveSceneSubsystems");
using NameHashSet = HashSet<NameHash, Core::Alloc::ScratchArena, Hasher<NameHash>, EqualTo<NameHash>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ActiveSceneSubsystems::loadProjectStartScene(Core::Assets::AssetManager& assetManager, const ProjectRef& project){
    auto projectAsset = assetManager.loadTypedSync<ProjectAsset>(project.name(), __hidden_active_scene_subsystems::s_ActiveSceneSubsystemsOwner);
    if(!projectAsset)
        return false;
    return loadScene(assetManager, (*projectAsset)->startScene());
}

bool ActiveSceneSubsystems::loadScene(Core::Assets::AssetManager& assetManager, const SceneRef& scene){
    auto sceneAssetResult = assetManager.loadTypedSync<SceneAsset>(scene.name(), __hidden_active_scene_subsystems::s_ActiveSceneSubsystemsOwner);
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
    __hidden_active_scene_subsystems::NameHashSet seenSubsystems(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);
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
                __hidden_active_scene_subsystems::s_ActiveSceneSubsystemsOwner,
                ConvertString(currentSubsystem.name().resolvedText())
            );
            return false;
        }
        const ISubsystem* const subsystemPtr = CastSubsystem(loadedAsset->get());
        if(!subsystemPtr){
            NWB_LOGGER_ERROR(
                NWB_TEXT("{}: asset '{}' is not a subsystem"),
                __hidden_active_scene_subsystems::s_ActiveSceneSubsystemsOwner,
                ConvertString(currentSubsystem.name().resolvedText())
            );
            return false;
        }
        for(const UniquePtr<ISubsystem>& existing : nextLoadedSubsystems){
            if(existing && existing->assetType() == subsystemPtr->assetType()){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("{}: duplicate subsystem type '{}' in scene '{}'"),
                    __hidden_active_scene_subsystems::s_ActiveSceneSubsystemsOwner,
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
        auto entityAssetResult = assetManager.loadTypedSync<EntityAsset>(currentEntity.name(), __hidden_active_scene_subsystems::s_ActiveSceneSubsystemsOwner);
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
                    __hidden_active_scene_subsystems::s_ActiveSceneSubsystemsOwner,
                    ConvertString(currentComponent.name().resolvedText()),
                    ConvertString(currentEntity.name().resolvedText())
                );
                return false;
            }
            const IComponent* const componentPtr = CastComponent(loadedComponentAsset->get());
            if(!componentPtr){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("{}: asset '{}' is not a component"),
                    __hidden_active_scene_subsystems::s_ActiveSceneSubsystemsOwner,
                    ConvertString(currentComponent.name().resolvedText())
                );
                return false;
            }
            for(const UniquePtr<IComponent>& existing : loadedComponents){
                if(existing && existing->assetType() == componentPtr->assetType()){
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("{}: duplicate component type '{}' on entity '{}'"),
                        __hidden_active_scene_subsystems::s_ActiveSceneSubsystemsOwner,
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
        NWB_LOGGER_ERROR(NWB_TEXT("{}: cannot instantiate entities before loading a valid scene"), __hidden_active_scene_subsystems::s_ActiveSceneSubsystemsOwner);
        return false;
    }

    for(const LoadedEntity& loadedEntity : m_loadedEntities){
        if(loadedEntity.components.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("{}: loaded entity '{}' has no components"),
                __hidden_active_scene_subsystems::s_ActiveSceneSubsystemsOwner,
                ConvertString(loadedEntity.entity.name().resolvedText())
            );
            return false;
        }
        Core::ECS::Entity entity = world.createEntity();
        if(!entity.id().valid()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("{}: failed to create ECS entity for '{}'"),
                __hidden_active_scene_subsystems::s_ActiveSceneSubsystemsOwner,
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
