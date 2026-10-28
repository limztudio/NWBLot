// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"
#include "transform_helpers.h"

#include <impl/assets_model/asset.h>
#include <impl/assets_skeleton/asset.h>
#include <impl/ecs_mesh/components.adhd.h>
#include <impl/ecs_skeleton/components.adhd.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/entity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_AssetLoadContext = NWB_TEXT("ModelSystem");

void TagObject(Core::ECS::Entity& entity, const Core::ECS::EntityID owner, const Name model, const Name object, const SkeletonJointMatrix& localTransform, const ModelObjectKind::Enum kind){
    auto& objectComponent = entity.addComponent<ModelObjectComponent>();
    objectComponent.owner = owner;
    objectComponent.model = model;
    objectComponent.object = object;
    objectComponent.localTransform = localTransform;
    objectComponent.kind = kind;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ModelSystem::ModelSystem(Core::Alloc::GlobalArena& arena, Core::ECS::World& world, Core::Assets::AssetManager& assetManager, ModelObjectRendererHooks rendererHooks)
    : Core::ECS::ISystem(arena)
    , m_world(world)
    , m_assetManager(assetManager)
    , m_applyRenderer(Move(rendererHooks.apply))
    , m_scratchJoints(arena)
    , m_attachmentJointQueries(arena)
    , m_attachmentParentOrder(arena)
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

Expected<void, Core::CpuTaskAdmissionError::Enum> ModelSystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);

    const auto transformed = updateModelObjectTransforms();
    if(!transformed)
        return MakeUnexpected(transformed.error());
    updateStaticMeshAttachments();
    return {};
}

void ModelSystem::syncModelRuntimes(){
    Core::Alloc::ScratchArena scratchArena(Name("impl/ecs_model/sync_runtime"));
    clearInactiveModelRuntimes(scratchArena);

    m_world.view<ModelComponent>().each([&](const Core::ECS::EntityID entity, ModelComponent& component){
        ensureModelRuntime(entity, component, scratchArena);
    });
}

void ModelSystem::clearInactiveModelRuntimes(Core::Alloc::ScratchArena& scratchArena){
    const auto objects = m_world.view<ModelObjectComponent>();
    const auto runtimes = m_world.view<ModelRuntimeComponent>();
    const auto objectIsInactive = [&](const ModelObjectComponent& object)noexcept{
        return !m_world.tryGetComponent<ModelRuntimeComponent>(object.owner) || !m_world.tryGetComponent<ModelComponent>(object.owner);
    };
    usize inactiveObjectCount = 0u;
    objects.each([&](const Core::ECS::EntityID entity, const ModelObjectComponent& object)noexcept{
        static_cast<void>(entity);
        if(objectIsInactive(object))
            ++inactiveObjectCount;
    });
    usize inactiveRuntimeCount = 0u;
    runtimes.each([&](const Core::ECS::EntityID entity, const ModelRuntimeComponent& runtime)noexcept{
        static_cast<void>(runtime);
        if(!m_world.tryGetComponent<ModelComponent>(entity))
            ++inactiveRuntimeCount;
    });
    if(inactiveObjectCount == 0u && inactiveRuntimeCount == 0u)
        return;

    Vector<Core::ECS::EntityID, Core::Alloc::ScratchArena> inactiveEntities(scratchArena);
    inactiveEntities.reserve(Max(inactiveObjectCount, inactiveRuntimeCount));
    if(inactiveObjectCount != 0u){
        objects.each([&](const Core::ECS::EntityID entity, const ModelObjectComponent& object)noexcept{
            if(objectIsInactive(object))
                inactiveEntities.push_back(entity);
        });
        for(const Core::ECS::EntityID entity : inactiveEntities)
            m_world.destroyEntity(entity);
    }

    if(inactiveRuntimeCount != 0u){
        // Destroying objects can also remove runtime components, so take a fresh view after that batch completes.
        inactiveEntities.clear();
        m_world.view<ModelRuntimeComponent>().each([&](const Core::ECS::EntityID entity, const ModelRuntimeComponent& runtime)noexcept{
            static_cast<void>(runtime);
            if(!m_world.tryGetComponent<ModelComponent>(entity))
                inactiveEntities.push_back(entity);
        });
        for(const Core::ECS::EntityID entity : inactiveEntities)
            m_world.entity(entity).removeComponent<ModelRuntimeComponent>();
    }
}

void ModelSystem::ensureModelRuntime(const Core::ECS::EntityID entity, const ModelComponent& component, Core::Alloc::ScratchArena& scratchArena){
    if(!component.model.valid()){
        clearModelRuntime(entity, scratchArena);
        m_world.entity(entity).removeComponent<ModelRuntimeComponent>();
        return;
    }

    ModelRuntimeComponent* const existing = m_world.tryGetComponent<ModelRuntimeComponent>(entity);
    if(existing && existing->model == component.model.name())
        return;
    auto& runtime = existing ? *existing : m_world.entity(entity).addComponent<ModelRuntimeComponent>();

    clearModelRuntime(entity, scratchArena);

    const Name modelName = component.model.name();
    auto loadedModel = m_assetManager.loadTypedSync<Model>(modelName, __hidden_model_system::s_AssetLoadContext);
    if(!loadedModel){
        runtime = ModelRuntimeComponent{};
        return;
    }

    const Model& model = **loadedModel;
    if(!expandModel(entity, model, runtime))
        clearModelRuntime(entity, scratchArena);
}

void ModelSystem::clearModelRuntime(const Core::ECS::EntityID entity, Core::Alloc::ScratchArena& scratchArena){
    const auto objects = m_world.view<ModelObjectComponent>();
    usize ownedCount = 0u;
    objects.each([&](const Core::ECS::EntityID objectEntity, const ModelObjectComponent& object)noexcept{
        static_cast<void>(objectEntity);
        if(object.owner == entity)
            ++ownedCount;
    });
    if(ownedCount != 0u){
        Vector<Core::ECS::EntityID, Core::Alloc::ScratchArena> ownedEntities(scratchArena);
        ownedEntities.reserve(ownedCount);
        objects.each([&](const Core::ECS::EntityID objectEntity, const ModelObjectComponent& object)noexcept{
            if(object.owner == entity)
                ownedEntities.push_back(objectEntity);
        });
        for(const Core::ECS::EntityID objectEntity : ownedEntities)
            m_world.destroyEntity(objectEntity);
    }

    if(auto* runtime = m_world.tryGetComponent<ModelRuntimeComponent>(entity))
        *runtime = ModelRuntimeComponent{};
}

bool ModelSystem::expandModel(const Core::ECS::EntityID owner, const Model& model, ModelRuntimeComponent& runtime){
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
    const Name skeletonName = object.skeleton.name();
    NWB_ASSERT(skeletonName);
    auto skeletonAsset = m_assetManager.loadTypedSync<Skeleton>(skeletonName, __hidden_model_system::s_AssetLoadContext);
    if(!skeletonAsset)
        return false;
    const Skeleton& skeleton = **skeletonAsset;

    Core::ECS::Entity entity = m_world.createEntity();
    __hidden_model_system::TagObject(entity, owner, m_world.entity(owner).getComponent<ModelRuntimeComponent>().model, object.name, object.transform, ModelObjectKind::Skeleton);
    auto& transform = entity.addComponent<Scene::TransformComponent>();
    if(const auto decomposition = MatrixDecompose(MatrixMultiply(ModelTransformHelpers::LoadOwnerWorldMatrix(m_world, owner), LoadFloat(object.transform))))
        ModelTransformHelpers::StoreDecomposedTransform(*decomposition, transform);

    auto& pose = entity.addComponent<SkeletonPoseComponent>();
    pose.parentJoints.clear();
    pose.localJoints.clear();
    pose.parentJoints.reserve(skeleton.joints().size());
    pose.localJoints.reserve(skeleton.joints().size());
    for(const SkeletonJoint& joint : skeleton.joints()){
        pose.parentJoints.push_back(joint.parentIndex);
        pose.localJoints.push_back(joint.localBindPose);
    }

    auto& skeletonComponent = entity.addComponent<ModelSkeletonComponent>();
    skeletonComponent.skeleton = object.skeleton;

    return true;
}

bool ModelSystem::spawnStaticMeshObject(const Core::ECS::EntityID owner, const ModelStaticMeshObject& object){
    Core::ECS::Entity entity = m_world.createEntity();
    __hidden_model_system::TagObject(entity, owner, m_world.entity(owner).getComponent<ModelRuntimeComponent>().model, object.name, object.transform, ModelObjectKind::StaticMesh);
    auto& transform = entity.addComponent<Scene::TransformComponent>();
    if(const auto decomposition = MatrixDecompose(MatrixMultiply(ModelTransformHelpers::LoadOwnerWorldMatrix(m_world, owner), LoadFloat(object.transform))))
        ModelTransformHelpers::StoreDecomposedTransform(*decomposition, transform);
    if(m_applyRenderer)
        m_applyRenderer(m_world, owner, object.material, entity);

    auto& mesh = entity.addComponent<MeshComponent>();
    mesh.mesh = object.mesh;

    auto& attachment = entity.addComponent<ModelStaticMeshAttachmentComponent>();
    attachment.parentObject = object.parentObject;
    attachment.parentJoint = object.parentJoint;
    attachment.localTransform = object.transform;

    NWB_ASSERT(object.parentObject || !object.parentJoint);
    if(object.parentObject){
        attachment.parentEntity = findSpawnedObject(owner, object.parentObject);
        if(!attachment.parentEntity.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: static mesh object '{}' targets missing parent object '{}'"), ConvertString(object.name.resolvedText()), ConvertString(object.parentObject.resolvedText()));
            return false;
        }

        const ModelSkeletonComponent* skeletonComponent = m_world.tryGetComponent<ModelSkeletonComponent>(attachment.parentEntity);
        if(!skeletonComponent){
            NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: static mesh object '{}' parent_object '{}' is not a skeleton object"), ConvertString(object.name.resolvedText()), ConvertString(object.parentObject.resolvedText()));
            return false;
        }

        if(object.parentJoint){
            const Name skeletonName = skeletonComponent->skeleton.name();
            if(!skeletonName)
                return false;
            auto skeletonAsset = m_assetManager.loadTypedSync<Skeleton>(skeletonName, __hidden_model_system::s_AssetLoadContext);
            if(!skeletonAsset)
                return false;
            const Skeleton& skeleton = **skeletonAsset;

            attachment.parentJointIndex = skeleton.findJointIndex(object.parentJoint);
            if(attachment.parentJointIndex == s_SkeletonInvalidJointIndex){
                NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: static mesh object '{}' targets missing joint '{}' on skeleton object '{}'")
                    , ConvertString(object.name.resolvedText())
                    , ConvertString(object.parentJoint.resolvedText())
                    , ConvertString(object.parentObject.resolvedText())
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
        NWB_LOGGER_ERROR(NWB_TEXT("ModelSystem: skinned mesh object '{}' targets missing skeleton object"), ConvertString(object.name.resolvedText()));
        return false;
    }

    Core::ECS::Entity entity = m_world.createEntity();
    __hidden_model_system::TagObject(entity, owner, m_world.entity(owner).getComponent<ModelRuntimeComponent>().model, object.name, object.transform, ModelObjectKind::SkinnedMesh);
    auto& transform = entity.addComponent<Scene::TransformComponent>();
    if(const auto decomposition = MatrixDecompose(MatrixMultiply(ModelTransformHelpers::LoadOwnerWorldMatrix(m_world, owner), LoadFloat(object.transform))))
        ModelTransformHelpers::StoreDecomposedTransform(*decomposition, transform);
    if(m_applyRenderer)
        m_applyRenderer(m_world, owner, object.material, entity);

    auto& binding = entity.addComponent<SkinnedMeshBindingComponent>();
    binding.skin = object.skin;
    binding.skeletonEntity = skeletonEntity;
    return true;
}

Core::ECS::EntityID ModelSystem::findSpawnedObject(const Core::ECS::EntityID owner, const Name objectName)const noexcept{
    const auto objects = m_world.view<ModelObjectComponent>();
    for(auto it = objects.begin(); it != objects.end(); ++it){
        auto&& [entity, object] = *it;
        if(object.owner == owner && object.object == objectName)
            return entity;
    }
    return Core::ECS::s_InvalidEntityId;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
