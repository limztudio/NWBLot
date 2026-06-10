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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ApplyObjectTransform(Scene::TransformComponent& transform, const SkeletonJointMatrix& matrix){
    SIMDVector scale;
    SIMDVector rotation;
    SIMDVector translation;
    if(!MatrixDecompose(&scale, &rotation, &translation, LoadFloat(matrix)))
        return;

    StoreFloat(VectorSetW(translation, 0.0f), &transform.position);
    StoreFloat(rotation, &transform.rotation);
    StoreFloat(VectorSetW(scale, 0.0f), &transform.scale);
}

void ApplyObjectTransform(Core::ECS::Entity& entity, const SkeletonJointMatrix& matrix){
    auto& transform = entity.addComponent<Scene::TransformComponent>();
    ApplyObjectTransform(transform, matrix);
}

void TagObject(
    Core::ECS::Entity& entity,
    const Core::ECS::EntityID owner,
    const Name model,
    const Name object,
    const ModelObjectKind::Enum kind
){
    auto& objectComponent = entity.addComponent<ModelObjectComponent>();
    objectComponent.owner = owner;
    objectComponent.model = model;
    objectComponent.object = object;
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
    return outSkeleton != nullptr;
}

[[nodiscard]] u32 FindSkeletonJointIndex(const Skeleton& skeleton, const Name jointName){
    const Skeleton::JointVector& joints = skeleton.joints();
    for(usize i = 0u; i < joints.size(); ++i){
        if(joints[i].name == jointName)
            return static_cast<u32>(i);
    }
    return Limit<u32>::s_Max;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ModelSystem::ModelSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Assets::AssetManager& assetManager)
    : Core::ECS::ISystem(arena)
    , m_arena(arena)
    , m_world(world)
    , m_assetManager(assetManager)
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
}

void ModelSystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);

    clearInvalidSpawnedObjects();
    clearRuntimeObjectsWithoutModel();

    m_world.view<ModelComponent>().each(
        [&](const Core::ECS::EntityID entity, ModelComponent& component){
            ensureModelRuntime(entity, component);
        }
    );

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
        ModelObjectKind::Skeleton
    );
    __hidden_model_system::ApplyObjectTransform(entity, object.transform);

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
        ModelObjectKind::StaticMesh
    );
    __hidden_model_system::ApplyObjectTransform(entity, object.transform);

    auto& mesh = entity.addComponent<MeshComponent>();
    mesh.mesh = object.mesh;

    auto& attachment = entity.addComponent<ModelStaticMeshAttachmentComponent>();
    attachment.parentObject = object.parentObject;
    attachment.parentJoint = object.parentJoint;
    attachment.localTransform = object.transform;

    if(object.parentObject){
        attachment.parentEntity = findSpawnedObject(owner, object.parentObject);
        if(!attachment.parentEntity.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: static mesh object '{}' targets missing parent object '{}'")
                , StringConvert(object.name.c_str())
                , StringConvert(object.parentObject.c_str())
            );
            return false;
        }
    }

    if(object.parentJoint){
        const ModelSkeletonComponent* skeletonComponent = m_world.tryGetComponent<ModelSkeletonComponent>(attachment.parentEntity);
        if(!skeletonComponent){
            NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: static mesh object '{}' targets joint '{}' on a non-skeleton object")
                , StringConvert(object.name.c_str())
                , StringConvert(object.parentJoint.c_str())
            );
            return false;
        }

        UniquePtr<Core::Assets::IAsset> loadedAsset;
        const Skeleton* skeleton = nullptr;
        if(!__hidden_model_system::LoadSkeleton(m_assetManager, skeletonComponent->skeleton, loadedAsset, skeleton))
            return false;

        attachment.parentJointIndex = __hidden_model_system::FindSkeletonJointIndex(*skeleton, object.parentJoint);
        if(attachment.parentJointIndex == Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: static mesh object '{}' targets missing joint '{}'")
                , StringConvert(object.name.c_str())
                , StringConvert(object.parentJoint.c_str())
            );
            return false;
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
        ModelObjectKind::SkinnedMesh
    );
    __hidden_model_system::ApplyObjectTransform(entity, object.transform);

    auto& binding = entity.addComponent<SkinnedMeshBindingComponent>();
    binding.mesh = object.mesh;
    binding.skin = object.skin;
    binding.skeletonEntity = skeletonEntity;
    return true;
}

void ModelSystem::updateStaticMeshAttachments(){
    m_world.view<ModelStaticMeshAttachmentComponent, Scene::TransformComponent>().each(
        [&](const Core::ECS::EntityID entity, ModelStaticMeshAttachmentComponent& attachment, Scene::TransformComponent& transform){
            static_cast<void>(entity);
            if(!attachment.parentEntity.valid() || attachment.parentJointIndex == Limit<u32>::s_Max)
                return;

            const SkeletonPoseComponent* pose = m_world.tryGetComponent<SkeletonPoseComponent>(attachment.parentEntity);
            if(!pose)
                return;

            u32 skinningMode = SkeletonSkinningMode::LinearBlend;
            if(!SkeletonRuntime::BuildJointPaletteFromSkeletonPose(*pose, m_scratchJoints, skinningMode))
                return;
            if(attachment.parentJointIndex >= m_scratchJoints.size())
                return;

            SkeletonJointMatrix worldTransform{};
            StoreFloat(
                MatrixMultiply(LoadFloat(m_scratchJoints[attachment.parentJointIndex]), LoadFloat(attachment.localTransform)),
                &worldTransform
            );
            __hidden_model_system::ApplyObjectTransform(transform, worldTransform);
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
