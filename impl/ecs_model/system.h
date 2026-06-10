// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/ecs/system.h>
#include <impl/assets_model/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ModelSystem final : public Core::ECS::ISystem{
public:
    ModelSystem(Core::Alloc::GlobalArena& arena, Core::ECS::World& world, Core::Assets::AssetManager& assetManager);
    virtual ~ModelSystem()override = default;


public:
    virtual void update(Core::ECS::World& world, f32 delta)override;


private:
    void clearInvalidSpawnedObjects();
    void clearRuntimeObjectsWithoutModel();
    void ensureModelRuntime(Core::ECS::EntityID entity, const ModelComponent& component);
    void clearModelRuntime(Core::ECS::EntityID entity);
    [[nodiscard]] bool expandModel(Core::ECS::EntityID owner, const Model& model, ModelRuntimeComponent& runtime);
    [[nodiscard]] bool spawnSkeletonObject(Core::ECS::EntityID owner, const ModelSkeletonObject& object);
    [[nodiscard]] bool spawnStaticMeshObject(Core::ECS::EntityID owner, const ModelStaticMeshObject& object);
    [[nodiscard]] bool spawnSkinnedMeshObject(Core::ECS::EntityID owner, const ModelSkinnedMeshObject& object);
    void updateStaticMeshAttachments();
    [[nodiscard]] Core::ECS::EntityID findSpawnedObject(Core::ECS::EntityID owner, Name objectName)const;


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::Assets::AssetManager& m_assetManager;
    Vector<Core::ECS::EntityID, Core::Alloc::GlobalArena> m_scratchEntities;
    Vector<SkeletonJointMatrix, Core::Alloc::GlobalArena> m_scratchJoints;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
