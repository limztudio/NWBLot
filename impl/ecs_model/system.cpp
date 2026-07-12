
#include "system.h"

#include <global/core/assets/manager.h>
#include <global/core/common/log.h>
#include <global/core/ecs/entity.h>
#include <global/core/ecs/world.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_skeleton/asset.h>
#include <impl/ecs_mesh/components.h>
#include <impl/ecs_scene/components.h>
#include <impl/ecs_skeleton/components.h>
#include <impl/ecs_skeleton/runtime_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_ParallelModelObjectTransformGrainSize = 256u;

[[nodiscard]] bool ResolveObjectTransformVectors(
    const SIMDMatrix& matrix,
    SIMDVector& outScale,
    SIMDVector& outRotation,
    SIMDVector& outTranslation
){
    SIMDVector scale;
    SIMDVector rotation;
    SIMDVector translation;
    if(!MatrixDecompose(&scale, &rotation, &translation, matrix))
        return false;

    outScale = VectorSetW(scale, 0.0f);
    outRotation = rotation;
    outTranslation = VectorSetW(translation, 0.0f);
    return true;
}

SIMDMatrix MakeTransformMatrix(
    const SIMDVector scale,
    const SIMDVector rotation,
    const SIMDVector translation
){
    return MatrixAffineTransformation(
        scale,
        VectorZero(),
        rotation,
        translation
    );
}

SIMDMatrix MakeWorldTransform(
    const SIMDMatrix& parentTransform,
    const SIMDMatrix& localTransform
){
    return MatrixMultiply(parentTransform, localTransform);
}

SIMDMatrix MakeStaticAttachmentWorldTransform(
    const SIMDMatrix& parentTransform,
    const SIMDMatrix* jointTransform,
    const SIMDMatrix& localTransform
){
    SIMDMatrix worldTransform = parentTransform;
    if(jointTransform)
        worldTransform = MatrixMultiply(worldTransform, *jointTransform);
    return MatrixMultiply(worldTransform, localTransform);
}

void StoreResolvedTransform(
    Scene::TransformComponent& transform,
    const SIMDMatrix& worldTransform
){
    SIMDVector scale;
    SIMDVector rotation;
    SIMDVector translation;
    if(!ResolveObjectTransformVectors(worldTransform, scale, rotation, translation))
        return;

    StoreFloat(translation, &transform.position);
    StoreFloat(rotation, &transform.rotation);
    StoreFloat(scale, &transform.scale);
}

void StoreObjectWorldTransform(
    Core::ECS::World& world,
    const Core::ECS::EntityID owner,
    const SkeletonJointMatrix& localTransform,
    Scene::TransformComponent& transform
){
    const Scene::TransformComponent* ownerTransform = world.tryGetComponent<Scene::TransformComponent>(owner);
    const SIMDMatrix ownerMatrix = ownerTransform
        ? MakeTransformMatrix(
            LoadFloat(ownerTransform->scale),
            LoadFloat(ownerTransform->rotation),
            LoadFloat(ownerTransform->position)
        )
        : MatrixIdentity()
    ;

    StoreResolvedTransform(
        transform,
        MakeWorldTransform(
            ownerMatrix,
            LoadFloat(localTransform)
        )
    );
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
    , m_scratchEntities(arena)
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
    clearInvalidSpawnedObjects();
    clearRuntimeObjectsWithoutModel();

    m_world.view<ModelComponent>().each(
        [&](const Core::ECS::EntityID entity, ModelComponent& component){
            ensureModelRuntime(entity, component);
        }
    );
}

void ModelSystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);

    updateModelObjectTransforms();
    updateStaticMeshAttachments();
}

void ModelSystem::clearInvalidSpawnedObjects(){
    m_scratchEntities.clear();
    m_world.view<ModelObjectComponent>().each(
        [&](const Core::ECS::EntityID entity, ModelObjectComponent& object){
            if(!m_world.tryGetComponent<ModelRuntimeComponent>(object.owner))
                m_scratchEntities.push_back(entity);
        }
    );

    for(const Core::ECS::EntityID entity : m_scratchEntities)
        m_world.destroyEntity(entity);
}

void ModelSystem::clearRuntimeObjectsWithoutModel(){
    m_scratchEntities.clear();
    m_world.view<ModelRuntimeComponent>().each(
        [&](const Core::ECS::EntityID entity, ModelRuntimeComponent& runtime){
            static_cast<void>(runtime);
            if(!m_world.tryGetComponent<ModelComponent>(entity))
                m_scratchEntities.push_back(entity);
        }
    );

    for(const Core::ECS::EntityID entity : m_scratchEntities){
        clearModelRuntime(entity);
        m_world.removeComponent<ModelRuntimeComponent>(entity);
    }
}

void ModelSystem::ensureModelRuntime(const Core::ECS::EntityID entity, const ModelComponent& component){
    if(!component.model.valid()){
        clearModelRuntime(entity);
        m_world.removeComponent<ModelRuntimeComponent>(entity);
        return;
    }

    auto& runtime = m_world.addComponent<ModelRuntimeComponent>(entity);
    if(runtime.model == component.model.name())
        return;

    clearModelRuntime(entity);

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
        clearModelRuntime(entity);
}

void ModelSystem::clearModelRuntime(const Core::ECS::EntityID entity){
    m_scratchEntities.clear();
    m_world.view<ModelObjectComponent>().each(
        [&](const Core::ECS::EntityID objectEntity, ModelObjectComponent& object){
            if(object.owner == entity)
                m_scratchEntities.push_back(objectEntity);
        }
    );

    for(const Core::ECS::EntityID objectEntity : m_scratchEntities)
        m_world.destroyEntity(objectEntity);

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
        m_world.getComponent<ModelRuntimeComponent>(owner).model,
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
        m_world.getComponent<ModelRuntimeComponent>(owner).model,
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
        m_world.getComponent<ModelRuntimeComponent>(owner).model,
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
        m_world.taskPool(),
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
    m_world.view<ModelObjectComponent, ModelStaticMeshAttachmentComponent, Scene::TransformComponent>().each(
        [&](const Core::ECS::EntityID entity, ModelObjectComponent& object, ModelStaticMeshAttachmentComponent& attachment, Scene::TransformComponent& transform){
            static_cast<void>(entity);

            const Scene::TransformComponent* ownerTransform = m_world.tryGetComponent<Scene::TransformComponent>(object.owner);
            const Scene::TransformComponent* parentTransform = attachment.parentEntity.valid()
                ? m_world.tryGetComponent<Scene::TransformComponent>(attachment.parentEntity)
                : ownerTransform;
            const SIMDMatrix ownerMatrix = ownerTransform
                ? __hidden_model_system::MakeTransformMatrix(
                    LoadFloat(ownerTransform->scale),
                    LoadFloat(ownerTransform->rotation),
                    LoadFloat(ownerTransform->position)
                )
                : MatrixIdentity()
            ;
            const SIMDMatrix parentMatrix = parentTransform
                ? __hidden_model_system::MakeTransformMatrix(
                    LoadFloat(parentTransform->scale),
                    LoadFloat(parentTransform->rotation),
                    LoadFloat(parentTransform->position)
                )
                : ownerMatrix
            ;
            const SIMDMatrix localMatrix = LoadFloat(attachment.localTransform);

            if(!attachment.parentEntity.valid() || attachment.parentJointIndex == Limit<u32>::s_Max){
                __hidden_model_system::StoreResolvedTransform(
                    transform,
                    __hidden_model_system::MakeStaticAttachmentWorldTransform(
                        parentMatrix,
                        nullptr,
                        localMatrix
                    )
                );
                return;
            }

            const SkeletonPoseComponent* pose = m_world.tryGetComponent<SkeletonPoseComponent>(attachment.parentEntity);
            if(!pose)
                return;

            u32 skinningMode = SkeletonSkinningMode::LinearBlend;
            if(!SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(*pose, m_scratchJoints, skinningMode))
                return;

            NWB_ASSERT(attachment.parentJointIndex < m_scratchJoints.size());
            const SIMDMatrix jointMatrix = LoadFloat(m_scratchJoints[attachment.parentJointIndex]);
            __hidden_model_system::StoreResolvedTransform(
                transform,
                __hidden_model_system::MakeStaticAttachmentWorldTransform(
                    parentMatrix,
                    &jointMatrix,
                    localMatrix
                )
            );
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

