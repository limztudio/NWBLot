// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_model/components.adhd.h>

#include <core/assets/ref.h>
#include <core/ecs/system.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Entity;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(IsStandardLayout_V<ModelComponent>, "ModelComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<ModelComponent>, "ModelComponent must stay cheap to move in dense ECS storage");
static_assert(IsStandardLayout_V<ModelRuntimeComponent>, "ModelRuntimeComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<ModelRuntimeComponent>, "ModelRuntimeComponent must stay cheap to move in dense ECS storage");
static_assert(IsStandardLayout_V<ModelObjectComponent>, "ModelObjectComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<ModelObjectComponent>, "ModelObjectComponent must stay cheap to move in dense ECS storage");
static_assert(IsStandardLayout_V<ModelSkeletonComponent>, "ModelSkeletonComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<ModelSkeletonComponent>, "ModelSkeletonComponent must stay cheap to move in dense ECS storage");
static_assert(IsStandardLayout_V<ModelStaticMeshAttachmentComponent>, "ModelStaticMeshAttachmentComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<ModelStaticMeshAttachmentComponent>, "ModelStaticMeshAttachmentComponent must stay cheap to move in dense ECS storage");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Material;
class Model;
class ModelSkeletonObject;
class ModelStaticMeshObject;
class ModelSkinnedMeshObject;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ModelObjectRendererCallback = Function<void(Core::ECS::World& world, Core::ECS::EntityID owner, const Core::Assets::AssetRef<Material>& material, Core::ECS::Entity& entity)>;

struct ModelObjectRendererHooks{
    ModelObjectRendererCallback apply;
    const Core::ECS::ComponentAccess* accesses = nullptr;
    usize accessCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ModelSystem final : public Core::ECS::ISystem{
public:
    ModelSystem(Core::Alloc::GlobalArena& arena, Core::ECS::World& world, Core::Assets::AssetManager& assetManager, ModelObjectRendererHooks rendererHooks = {});
    virtual ~ModelSystem()noexcept override = default;


public:
    virtual void prepare(Core::ECS::World& world)override;
    [[nodiscard]] virtual Expected<void, Core::CpuTaskAdmissionError::Enum> update(Core::ECS::World& world, f32 delta)override;
    void syncModelRuntimes();


private:
    void clearInactiveModelRuntimes(Core::Alloc::ScratchArena& scratchArena);
    void ensureModelRuntime(Core::ECS::EntityID entity, const ModelComponent& component, Core::Alloc::ScratchArena& scratchArena);
    void clearModelRuntime(Core::ECS::EntityID entity, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] b8 expandModel(Core::ECS::EntityID owner, const Model& model, ModelRuntimeComponent& runtime);
    [[nodiscard]] b8 spawnSkeletonObject(Core::ECS::EntityID owner, const ModelSkeletonObject& object);
    [[nodiscard]] b8 spawnStaticMeshObject(Core::ECS::EntityID owner, const ModelStaticMeshObject& object);
    [[nodiscard]] b8 spawnSkinnedMeshObject(Core::ECS::EntityID owner, const ModelSkinnedMeshObject& object);
    [[nodiscard]] Expected<void, Core::CpuTaskAdmissionError::Enum> updateModelObjectTransforms();
    void updateStaticMeshAttachments();
    [[nodiscard]] Core::ECS::EntityID findSpawnedObject(Core::ECS::EntityID owner, Name objectName)const noexcept;


private:
    Core::ECS::World& m_world;
    Core::Assets::AssetManager& m_assetManager;
    ModelObjectRendererCallback m_applyRenderer;
    Vector<SkeletonJointMatrix, Core::Alloc::GlobalArena> m_scratchJoints;
    // Reused across updateStaticMeshAttachments() calls so per-frame updates perform no allocations.
    Vector<ModelAttachmentJointQuery, Core::Alloc::GlobalArena> m_attachmentJointQueries;
    Vector<usize, Core::Alloc::GlobalArena> m_attachmentParentOrder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

