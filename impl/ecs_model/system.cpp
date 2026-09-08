// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/entity.h>
#include <core/ecs/world.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_skeleton/asset.h>
#include <impl/ecs_mesh/components.h>
#include <impl/ecs_scene/components.h>
#include <impl/ecs_skeleton/components.h>
#include <impl/ecs_skeleton/runtime_helpers.h>
#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AttachmentJointQuery{
    Core::ECS::EntityID parentEntity;
    u32 parentJointIndex = s_SkeletonInvalidJointIndex;
    SkeletonJointMatrix jointMatrix{};
    bool resolved = false;
};

static void ResolveAttachmentJointQueries(
    Core::ECS::World& world,
    Vector<SkeletonJointMatrix, Core::Alloc::GlobalArena>& jointPalette,
    AttachmentJointQuery* const queries,
    const usize queryCount,
    const usize* const parentOrder){
    const auto queryAt = [&](const usize index) -> AttachmentJointQuery&{
        return queries[parentOrder ? parentOrder[index] : index];
    };
    usize groupBegin = 0u;
    while(groupBegin < queryCount){
        const Core::ECS::EntityID parentEntity = queryAt(groupBegin).parentEntity;
        usize groupEnd = groupBegin + 1u;
        while(groupEnd < queryCount && queryAt(groupEnd).parentEntity == parentEntity)
            ++groupEnd;

        const SkeletonPoseComponent* const pose = world.tryGetComponent<SkeletonPoseComponent>(parentEntity);
        u32 skinningMode = SkeletonSkinningMode::LinearBlend;
        if(pose && SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(*pose, jointPalette, skinningMode)){
            for(usize queryIndex = groupBegin; queryIndex < groupEnd; ++queryIndex){
                AttachmentJointQuery& query = queryAt(queryIndex);
                if(query.parentJointIndex >= jointPalette.size())
                    continue;
                query.jointMatrix = jointPalette[query.parentJointIndex];
                query.resolved = true;
            }
        }
        groupBegin = groupEnd;
    }
}

static constexpr usize s_ParallelModelObjectTransformGrainSize = 256u;

void StoreObjectWorldTransform(
    Core::ECS::World& world,
    const Core::ECS::EntityID owner,
    const SkeletonJointMatrix& localTransform,
    Scene::TransformComponent& transform
){
    const Scene::TransformComponent* ownerTransform = world.tryGetComponent<Scene::TransformComponent>(owner);
    const SIMDMatrix ownerMatrix = ownerTransform
        ? MatrixAffineTransformation(
            LoadFloat(ownerTransform->scale),
            VectorZero(),
            LoadFloat(ownerTransform->rotation),
            LoadFloat(ownerTransform->position)
        )
        : MatrixIdentity()
    ;

    SIMDVector scale;
    SIMDVector rotation;
    SIMDVector translation;
    if(!MatrixDecompose(&scale, &rotation, &translation, MatrixMultiply(ownerMatrix, LoadFloat(localTransform))))
        return;

    StoreFloat(VectorSetW(translation, 0.0f), &transform.position);
    StoreFloat(rotation, &transform.rotation);
    StoreFloat(VectorSetW(scale, 0.0f), &transform.scale);
}

void TagObject(
    Core::ECS::Entity& entity,
    const Core::ECS::EntityID owner,
    const Name model,
    const Name object,
    const SkeletonJointMatrix& localTransform,
    const ModelObjectKind::Enum kind
){
    auto& objectComponent = entity.addComponent<ModelObjectComponent>();
    objectComponent.owner = owner;
    objectComponent.model = model;
    objectComponent.object = object;
    objectComponent.localTransform = localTransform;
    objectComponent.kind = kind;
}

bool LoadSkeleton(
    Core::Assets::AssetManager& assetManager,
    const Core::Assets::AssetRef<Skeleton>& skeletonRef,
    UniquePtr<Core::Assets::IAsset>& outAsset,
    const Skeleton*& outSkeleton
){
    outAsset.reset();
    outSkeleton = nullptr;

    const Name skeletonName = skeletonRef.name();
    if(!skeletonName)
        return false;

    if(!assetManager.loadSync(Skeleton::AssetTypeName(), skeletonName, outAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: failed to load skeleton '{}'"), StringConvert(skeletonName.c_str()));
        return false;
    }
    if(!outAsset || outAsset->assetType() != Skeleton::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: asset '{}' is not a skeleton"), StringConvert(skeletonName.c_str()));
        outAsset.reset();
        return false;
    }

    outSkeleton = checked_cast<const Skeleton*>(outAsset.get());
    NWB_ASSERT(outSkeleton != nullptr);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ModelSystem::ModelSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Assets::AssetManager& assetManager,
    ModelObjectRendererHooks rendererHooks)
    : Core::ECS::ISystem(arena)
    , m_arena(arena)
    , m_world(world)
    , m_assetManager(assetManager)
    , m_applyRenderer(Move(rendererHooks.apply))
    , m_scratchJoints(arena)
{
    readAccess<ModelComponent>();
    writeAccess<ModelRuntimeComponent>();
    writeAccess<ModelObjectComponent>();
    writeAccess<ModelSkeletonComponent>();
    writeAccess<ModelStaticMeshAttachmentComponent>();
    writeAccess<MeshComponent>();
    writeAccess<SkinnedMeshBindingComponent>();
    writeAccess<Scene::TransformComponent>();
    writeAccess<SkeletonPoseComponent>();

    if(rendererHooks.accesses){
        for(usize i = 0u; i < rendererHooks.accessCount; ++i)
            registerAccess(rendererHooks.accesses[i].typeId, rendererHooks.accesses[i].mode);
    }
}

void ModelSystem::prepare(Core::ECS::World& world){
    static_cast<void>(world);

    syncModelRuntimes();
}

void ModelSystem::syncModelRuntimes(){
    Core::Alloc::ScratchArena scratchArena(Name("impl/ecs_model/sync_runtime"));
    clearInactiveModelRuntimes(scratchArena);

    m_world.view<ModelComponent>().each(
        [&](const Core::ECS::EntityID entity, ModelComponent& component){
            ensureModelRuntime(entity, component, scratchArena);
        }
    );
}

void ModelSystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);

    updateModelObjectTransforms();
    updateStaticMeshAttachments();
}

void ModelSystem::clearInactiveModelRuntimes(Core::Alloc::ScratchArena& scratchArena){
    const auto objects = m_world.view<ModelObjectComponent>();
    const auto runtimes = m_world.view<ModelRuntimeComponent>();
    const auto objectIsInactive = [&](const ModelObjectComponent& object){
        return !m_world.tryGetComponent<ModelRuntimeComponent>(object.owner)
            || !m_world.tryGetComponent<ModelComponent>(object.owner);
    };
    usize inactiveObjectCount = 0u;
    objects.each(
        [&](const Core::ECS::EntityID entity, const ModelObjectComponent& object){
            static_cast<void>(entity);
            if(objectIsInactive(object))
                ++inactiveObjectCount;
        }
    );
    usize inactiveRuntimeCount = 0u;
    runtimes.each(
        [&](const Core::ECS::EntityID entity, const ModelRuntimeComponent& runtime){
            static_cast<void>(runtime);
            if(!m_world.tryGetComponent<ModelComponent>(entity))
                ++inactiveRuntimeCount;
        }
    );
    if(inactiveObjectCount == 0u && inactiveRuntimeCount == 0u)
        return;

    Vector<Core::ECS::EntityID, Core::Alloc::ScratchArena> inactiveEntities(scratchArena);
    inactiveEntities.reserve(Max(inactiveObjectCount, inactiveRuntimeCount));
    if(inactiveObjectCount != 0u){
        objects.each(
            [&](const Core::ECS::EntityID entity, const ModelObjectComponent& object){
                if(objectIsInactive(object))
                    inactiveEntities.push_back(entity);
            }
        );
        for(const Core::ECS::EntityID entity : inactiveEntities)
            m_world.destroyEntity(entity);
    }

    if(inactiveRuntimeCount != 0u){
        // Destroying objects can also remove runtime components, so take a fresh view after that batch completes.
        inactiveEntities.clear();
        m_world.view<ModelRuntimeComponent>().each(
            [&](const Core::ECS::EntityID entity, const ModelRuntimeComponent& runtime){
                static_cast<void>(runtime);
                if(!m_world.tryGetComponent<ModelComponent>(entity))
                    inactiveEntities.push_back(entity);
            }
        );
        for(const Core::ECS::EntityID entity : inactiveEntities)
            m_world.entity(entity).removeComponent<ModelRuntimeComponent>();
    }
}

void ModelSystem::ensureModelRuntime(
    const Core::ECS::EntityID entity,
    const ModelComponent& component,
    Core::Alloc::ScratchArena& scratchArena){
    if(!component.model.valid()){
        clearModelRuntime(entity, scratchArena);
        m_world.entity(entity).removeComponent<ModelRuntimeComponent>();
        return;
    }

    auto& runtime = m_world.entity(entity).addComponent<ModelRuntimeComponent>();
    if(runtime.model == component.model.name())
        return;

    clearModelRuntime(entity, scratchArena);

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    const Name modelName = component.model.name();
    if(!m_assetManager.loadSync(Model::AssetTypeName(), modelName, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: failed to load model '{}'"), StringConvert(modelName.c_str()));
        runtime = ModelRuntimeComponent{};
        return;
    }
    if(!loadedAsset || loadedAsset->assetType() != Model::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: asset '{}' is not a model"), StringConvert(modelName.c_str()));
        runtime = ModelRuntimeComponent{};
        return;
    }

    const Model& model = *checked_cast<const Model*>(loadedAsset.get());
    if(!expandModel(entity, model, runtime))
        clearModelRuntime(entity, scratchArena);
}

void ModelSystem::clearModelRuntime(const Core::ECS::EntityID entity, Core::Alloc::ScratchArena& scratchArena){
    const auto objects = m_world.view<ModelObjectComponent>();
    usize ownedCount = 0u;
    objects.each(
        [&](const Core::ECS::EntityID objectEntity, const ModelObjectComponent& object){
            static_cast<void>(objectEntity);
            if(object.owner == entity)
                ++ownedCount;
        }
    );
    if(ownedCount != 0u){
        Vector<Core::ECS::EntityID, Core::Alloc::ScratchArena> ownedEntities(scratchArena);
        ownedEntities.reserve(ownedCount);
        objects.each(
            [&](const Core::ECS::EntityID objectEntity, const ModelObjectComponent& object){
                if(object.owner == entity)
                    ownedEntities.push_back(objectEntity);
            }
        );
        for(const Core::ECS::EntityID objectEntity : ownedEntities)
            m_world.destroyEntity(objectEntity);
    }

    if(auto* runtime = m_world.tryGetComponent<ModelRuntimeComponent>(entity))
        *runtime = ModelRuntimeComponent{};
}

bool ModelSystem::expandModel(
    const Core::ECS::EntityID owner,
    const Model& model,
    ModelRuntimeComponent& runtime
){
    runtime.model = model.virtualPath();
    runtime.objectCount = 0u;

    bool complete = true;
    for(const ModelSkeletonObject& object : model.skeletonObjects()){
        if(spawnSkeletonObject(owner, object))
            ++runtime.objectCount;
        else
            complete = false;
    }
    for(const ModelStaticMeshObject& object : model.staticMeshObjects()){
        if(spawnStaticMeshObject(owner, object))
            ++runtime.objectCount;
        else
            complete = false;
    }
    for(const ModelSkinnedMeshObject& object : model.skinnedMeshObjects()){
        if(spawnSkinnedMeshObject(owner, object))
            ++runtime.objectCount;
        else
            complete = false;
    }

    return complete;
}

bool ModelSystem::spawnSkeletonObject(const Core::ECS::EntityID owner, const ModelSkeletonObject& object){
    UniquePtr<Core::Assets::IAsset> loadedAsset;
    const Skeleton* skeleton = nullptr;
    if(!__hidden_model_system::LoadSkeleton(m_assetManager, object.skeleton, loadedAsset, skeleton))
        return false;

    Core::ECS::Entity entity = m_world.createEntity();
    __hidden_model_system::TagObject(
        entity,
        owner,
        m_world.entity(owner).getComponent<ModelRuntimeComponent>().model,
        object.name,
        object.transform,
        ModelObjectKind::Skeleton
    );
    auto& transform = entity.addComponent<Scene::TransformComponent>();
    __hidden_model_system::StoreObjectWorldTransform(m_world, owner, object.transform, transform);

    auto& pose = entity.addComponent<SkeletonPoseComponent>(m_arena);
    pose.parentJoints.clear();
    pose.localJoints.clear();
    pose.parentJoints.reserve(skeleton->joints().size());
    pose.localJoints.reserve(skeleton->joints().size());
    for(const SkeletonJoint& joint : skeleton->joints()){
        pose.parentJoints.push_back(joint.parentIndex);
        pose.localJoints.push_back(joint.localBindPose);
    }

    auto& skeletonComponent = entity.addComponent<ModelSkeletonComponent>();
    skeletonComponent.skeleton = object.skeleton;

    return true;
}

bool ModelSystem::spawnStaticMeshObject(const Core::ECS::EntityID owner, const ModelStaticMeshObject& object){
    Core::ECS::Entity entity = m_world.createEntity();
    __hidden_model_system::TagObject(
        entity,
        owner,
        m_world.entity(owner).getComponent<ModelRuntimeComponent>().model,
        object.name,
        object.transform,
        ModelObjectKind::StaticMesh
    );
    auto& transform = entity.addComponent<Scene::TransformComponent>();
    __hidden_model_system::StoreObjectWorldTransform(m_world, owner, object.transform, transform);
    if(m_applyRenderer)
        m_applyRenderer(m_world, m_arena, entity, owner, object.material);

    auto& mesh = entity.addComponent<MeshComponent>();
    mesh.mesh = object.mesh;

    auto& attachment = entity.addComponent<ModelStaticMeshAttachmentComponent>();
    attachment.parentObject = object.parentObject;
    attachment.parentJoint = object.parentJoint;
    attachment.localTransform = object.transform;

    if(!object.parentObject && object.parentJoint){
        NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: static mesh object '{}' uses parent_joint '{}' without parent_object")
            , StringConvert(object.name.c_str())
            , StringConvert(object.parentJoint.c_str())
        );
        return false;
    }

    if(object.parentObject){
        attachment.parentEntity = findSpawnedObject(owner, object.parentObject);
        if(!attachment.parentEntity.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: static mesh object '{}' targets missing parent object '{}'")
                , StringConvert(object.name.c_str())
                , StringConvert(object.parentObject.c_str())
            );
            return false;
        }

        const ModelSkeletonComponent* skeletonComponent = m_world.tryGetComponent<ModelSkeletonComponent>(attachment.parentEntity);
        if(!skeletonComponent){
            NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: static mesh object '{}' parent_object '{}' is not a skeleton object")
                , StringConvert(object.name.c_str())
                , StringConvert(object.parentObject.c_str())
            );
            return false;
        }

        if(object.parentJoint){
            UniquePtr<Core::Assets::IAsset> loadedAsset;
            const Skeleton* skeleton = nullptr;
            if(!__hidden_model_system::LoadSkeleton(m_assetManager, skeletonComponent->skeleton, loadedAsset, skeleton))
                return false;

            attachment.parentJointIndex = skeleton->findJointIndex(object.parentJoint);
            if(attachment.parentJointIndex == s_SkeletonInvalidJointIndex){
                NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: static mesh object '{}' targets missing joint '{}' on skeleton object '{}'")
                    , StringConvert(object.name.c_str())
                    , StringConvert(object.parentJoint.c_str())
                    , StringConvert(object.parentObject.c_str())
                );
                return false;
            }
        }
    }

    return true;
}

bool ModelSystem::spawnSkinnedMeshObject(const Core::ECS::EntityID owner, const ModelSkinnedMeshObject& object){
    const Core::ECS::EntityID skeletonEntity = findSpawnedObject(owner, object.skeletonObject);
    if(!skeletonEntity.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: skinned mesh object '{}' targets missing skeleton object")
            , StringConvert(object.name.c_str())
        );
        return false;
    }

    Core::ECS::Entity entity = m_world.createEntity();
    __hidden_model_system::TagObject(
        entity,
        owner,
        m_world.entity(owner).getComponent<ModelRuntimeComponent>().model,
        object.name,
        object.transform,
        ModelObjectKind::SkinnedMesh
    );
    auto& transform = entity.addComponent<Scene::TransformComponent>();
    __hidden_model_system::StoreObjectWorldTransform(m_world, owner, object.transform, transform);
    if(m_applyRenderer)
        m_applyRenderer(m_world, m_arena, entity, owner, object.material);

    auto& binding = entity.addComponent<SkinnedMeshBindingComponent>();
    binding.mesh = object.mesh;
    binding.skin = object.skin;
    binding.skeletonEntity = skeletonEntity;
    return true;
}

void ModelSystem::updateModelObjectTransforms(){
    m_world.view<ModelObjectComponent, Scene::TransformComponent>().parallelEach(
        m_world.taskScope(),
        __hidden_model_system::s_ParallelModelObjectTransformGrainSize,
        [&](const Core::ECS::EntityID entity, ModelObjectComponent& object, Scene::TransformComponent& transform){
            static_cast<void>(entity);
            if(object.kind == ModelObjectKind::StaticMesh)
                return;

            __hidden_model_system::StoreObjectWorldTransform(m_world, object.owner, object.localTransform, transform);
        }
    );
}

void ModelSystem::updateStaticMeshAttachments(){
    const auto attachments = m_world.view<ModelObjectComponent, ModelStaticMeshAttachmentComponent, Scene::TransformComponent>();
    usize queryCount = 0u;
    if(attachments.candidateCount() > 1u){
        attachments.each(
            [&](const Core::ECS::EntityID entity, const ModelObjectComponent& object,
                const ModelStaticMeshAttachmentComponent& attachment, const Scene::TransformComponent& transform){
                static_cast<void>(entity);
                static_cast<void>(object);
                static_cast<void>(transform);
                if(attachment.parentEntity.valid() && attachment.parentJointIndex != s_SkeletonInvalidJointIndex)
                    ++queryCount;
            }
        );
    }

    // This operation owns grouping storage; zero or one joint query needs neither an index nor a scratch arena.
    Optional<Core::Alloc::ScratchArena> scratchArena;
    Optional<Vector<__hidden_model_system::AttachmentJointQuery, Core::Alloc::ScratchArena>> jointQueries;
    if(queryCount > 1u){
        scratchArena.emplace(Name("impl/ecs_model/attachment_joints"));
        jointQueries.emplace(*scratchArena);
        jointQueries->reserve(queryCount);
        bool sharedParent = true;
        attachments.each(
            [&](const Core::ECS::EntityID entity, const ModelObjectComponent& object,
                const ModelStaticMeshAttachmentComponent& attachment, const Scene::TransformComponent& transform){
                static_cast<void>(entity);
                static_cast<void>(object);
                static_cast<void>(transform);
                if(!attachment.parentEntity.valid() || attachment.parentJointIndex == s_SkeletonInvalidJointIndex)
                    return;
                if(!jointQueries->empty() && jointQueries->front().parentEntity != attachment.parentEntity)
                    sharedParent = false;
                jointQueries->push_back(__hidden_model_system::AttachmentJointQuery{
                    .parentEntity = attachment.parentEntity,
                    .parentJointIndex = attachment.parentJointIndex,
                });
            }
        );
        auto* const queries = jointQueries->data();
        if(sharedParent){
            __hidden_model_system::ResolveAttachmentJointQueries(m_world, m_scratchJoints, queries, queryCount, nullptr);
        }
        else{
            Vector<usize, Core::Alloc::ScratchArena> parentOrder(*scratchArena);
            parentOrder.reserve(queryCount);
            for(usize queryIndex = 0u; queryIndex < queryCount; ++queryIndex)
                parentOrder.push_back(queryIndex);
            Sort(parentOrder.begin(), parentOrder.end(), [&](const usize lhs, const usize rhs){
                return (*jointQueries)[lhs].parentEntity < (*jointQueries)[rhs].parentEntity;
            });
            __hidden_model_system::ResolveAttachmentJointQueries(
                m_world, m_scratchJoints, queries, queryCount, parentOrder.data()
            );
        }
    }

    usize nextQuery = 0u;
    attachments.each(
        [&](const Core::ECS::EntityID entity, ModelObjectComponent& object, ModelStaticMeshAttachmentComponent& attachment, Scene::TransformComponent& transform){
            static_cast<void>(entity);

            const Scene::TransformComponent* ownerTransform = m_world.tryGetComponent<Scene::TransformComponent>(object.owner);
            const Scene::TransformComponent* parentTransform = attachment.parentEntity.valid()
                ? m_world.tryGetComponent<Scene::TransformComponent>(attachment.parentEntity)
                : ownerTransform;
            const SIMDMatrix ownerMatrix = ownerTransform
                ? MatrixAffineTransformation(
                    LoadFloat(ownerTransform->scale),
                    VectorZero(),
                    LoadFloat(ownerTransform->rotation),
                    LoadFloat(ownerTransform->position)
                )
                : MatrixIdentity()
            ;
            const SIMDMatrix parentMatrix = parentTransform
                ? MatrixAffineTransformation(
                    LoadFloat(parentTransform->scale),
                    VectorZero(),
                    LoadFloat(parentTransform->rotation),
                    LoadFloat(parentTransform->position)
                )
                : ownerMatrix
            ;
            const SIMDMatrix localMatrix = LoadFloat(attachment.localTransform);

            SIMDMatrix worldTransform{};
            if(!attachment.parentEntity.valid() || attachment.parentJointIndex == Limit<u32>::s_Max){
                worldTransform = MatrixMultiply(parentMatrix, localMatrix);
            }
            else{
                SIMDMatrix jointMatrix{};
                if(jointQueries){
                    const __hidden_model_system::AttachmentJointQuery& query = (*jointQueries)[nextQuery++];
                    if(!query.resolved)
                        return;
                    jointMatrix = LoadFloat(query.jointMatrix);
                }
                else{
                    const SkeletonPoseComponent* const pose = m_world.tryGetComponent<SkeletonPoseComponent>(attachment.parentEntity);
                    u32 skinningMode = SkeletonSkinningMode::LinearBlend;
                    if(
                        !pose
                        || !SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(*pose, m_scratchJoints, skinningMode)
                        || attachment.parentJointIndex >= m_scratchJoints.size()
                    )
                        return;
                    jointMatrix = LoadFloat(m_scratchJoints[attachment.parentJointIndex]);
                }
                worldTransform = MatrixMultiply(MatrixMultiply(parentMatrix, jointMatrix), localMatrix);
            }

            SIMDVector scale;
            SIMDVector rotation;
            SIMDVector translation;
            if(!MatrixDecompose(&scale, &rotation, &translation, worldTransform))
                return;

            StoreFloat(VectorSetW(translation, 0.0f), &transform.position);
            StoreFloat(rotation, &transform.rotation);
            StoreFloat(VectorSetW(scale, 0.0f), &transform.scale);
        }
    );
}

Core::ECS::EntityID ModelSystem::findSpawnedObject(const Core::ECS::EntityID owner, const Name objectName)const{
    Core::ECS::EntityID result = Core::ECS::ENTITY_ID_INVALID;
    m_world.view<ModelObjectComponent>().each(
        [&](const Core::ECS::EntityID entity, ModelObjectComponent& object){
            if(result.valid())
                return;
            if(object.owner == owner && object.object == objectName)
                result = entity;
        }
    );
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

