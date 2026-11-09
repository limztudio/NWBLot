// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "smoke_scene_helpers.h"

#include <impl/assets_model/asset.h>
#include <impl/assets_skeleton/asset.h>
#include <impl/ecs_mesh/skinning/system.h>
#include <impl/ecs_model/system.h>
#include <impl/ecs_model_renderer/model_renderer.h>

#include <core/assets/manager.h>
#include <core/ecs/entity.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SmokeModelRef = Core::Assets::AssetRef<Impl::Model>;

struct SmokeTintedModelEntityResult{
    Core::ECS::EntityID entity;
    b8 tintApplied = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void AddSmokeSkinnedRenderSystems(Core::ECS::World& world, ProjectRuntimeContext& context, const SmokeRenderQualitySettings& baseSettings = {}){
    const SmokeRenderSystems systems = CreateSmokeRenderSystems(world, context, baseSettings);
    world.addSystem<Impl::ModelSystem>(world, context.assetManager, Impl::CreateModelObjectRendererHooks());
    auto& meshSkinningSystem = world.addSystem<Impl::MeshSkinningSystem>(world, context.graphics, context.assetManager, systems.mesh, context.shaderPathResolver);

    context.graphics.addRenderPassToBack(meshSkinningSystem);
    context.graphics.addRenderPassToBack(systems.renderer);
    context.frameGraphRegistry.registerContributor(systems.renderer);
}

[[nodiscard]] inline NotNullUniquePtr<Core::ECS::World> CreateSmokeSkinnedRenderWorldOrDie(const TStringView projectName, const SmokeRenderQualitySettings& baseSettings, ProjectRuntimeContext& context){
    auto world = CreateSmokeWorldOrDie(projectName, context);
    AddSmokeSkinnedRenderSystems(*world, context, baseSettings);
    return world;
}

[[nodiscard]] inline Expected<Vector<Impl::SkeletonJointMatrix, Core::Alloc::GlobalArena>> LoadSmokeSkeletonBindJoints(
    const SmokeModelRef& modelRef,
    const TStringView projectName,
    const Name preferredSkeletonObject,
    const b8 requireInvertibleBindPose,
    Core::Alloc::GlobalArena& arena,
    Core::Assets::AssetManager& assetManager
){
    const auto loadedModelAsset = assetManager.loadSync(Impl::Model::AssetTypeName(), modelRef.name());
    if(!loadedModelAsset){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to load model for skeleton bind joints"), projectName);
        return MakeUnexpected(Failure{});
    }
    NWB_ASSERT(*loadedModelAsset);
    const auto* model = Core::Assets::CastAsset<Impl::Model>(loadedModelAsset->get());
    if(!model){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: loaded model has unexpected type"), projectName);
        return MakeUnexpected(Failure{});
    }

    const Impl::ModelSkeletonObject* skeletonObject = nullptr;
    if(preferredSkeletonObject){
        for(const Impl::ModelSkeletonObject& object : model->skeletonObjects()){
            if(object.name == preferredSkeletonObject){
                skeletonObject = &object;
                break;
            }
        }
    }
    if(!skeletonObject && !model->skeletonObjects().empty())
        skeletonObject = &model->skeletonObjects().front();
    if(!skeletonObject || !skeletonObject->skeleton.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: model has no skeleton object"), projectName);
        return MakeUnexpected(Failure{});
    }

    const auto loadedSkeletonAsset = assetManager.loadSync(Impl::Skeleton::AssetTypeName(), skeletonObject->skeleton.name());
    if(!loadedSkeletonAsset){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to load skeleton for bind joints"), projectName);
        return MakeUnexpected(Failure{});
    }
    NWB_ASSERT(*loadedSkeletonAsset);
    const auto* skeleton = Core::Assets::CastAsset<Impl::Skeleton>(loadedSkeletonAsset->get());
    if(!skeleton){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: loaded skeleton has unexpected type"), projectName);
        return MakeUnexpected(Failure{});
    }
    if(skeleton->joints().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{}: skeleton has no joints"), projectName);
        return MakeUnexpected(Failure{});
    }

    Vector<Impl::SkeletonJointMatrix, Core::Alloc::GlobalArena> bindJoints{arena};
    bindJoints.reserve(skeleton->joints().size());
    for(const Impl::SkeletonJoint& joint : skeleton->joints()){
        if(requireInvertibleBindPose && !MatrixIsInvertibleAffine(LoadFloat(joint.localBindPose), Impl::s_SkeletonAffineEpsilon, Impl::s_SkeletonJointDeterminantEpsilon)){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: skeleton contains an invalid bind pose"), projectName);
            return MakeUnexpected(Failure{});
        }
        bindJoints.push_back(joint.localBindPose);
    }
    return bindJoints;
}

inline void DestroySmokeSkinnedRenderWorld(ProjectRuntimeContext& context, NotNullUniquePtr<Core::ECS::World>& world){
    if(!world.owner())
        return;

    Impl::MeshSkinningSystem& meshSkinningSystem = *MakeNotNull(world->getSystem<Impl::MeshSkinningSystem>());
    context.graphics.removeRenderPass(meshSkinningSystem);

    RemoveSmokeRendererSystem(context, *world);
    FinishDestroyingSmokeWorld(context, world);
}

inline void SyncSmokeModelRuntimes(Core::ECS::World& world){
    Impl::ModelSystem& modelSystem = *MakeNotNull(world.getSystem<Impl::ModelSystem>());
    modelSystem.syncModelRuntimes();
}

[[nodiscard]] inline SmokeTintedModelEntityResult CreateTintedModelEntity(
    const SmokeModelRef& model,
    const SmokeMaterialRef& material,
    const NameText<char> materialInterfacePath,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    Core::ECS::World& world
){
    const SmokeTintedEntitySetup setup = CreateSmokeTintedEntity(material, materialInterfacePath, position, scale, world);

    auto& modelComponent = world.entity(setup.entity).addComponent<Impl::ModelComponent>();
    modelComponent.model = model;

    return { .entity = setup.entity, .tintApplied = ApplySmokeMaterialTint(setup, colorTint, "runtime.color_tint", world) };
}

[[nodiscard]] inline Core::ECS::EntityID FindSpawnedModelObject(const Core::ECS::EntityID owner, const Name objectName, const Impl::ModelObjectKind::Enum objectKind, Core::ECS::World& world)noexcept{
    Core::ECS::EntityID result = Core::ECS::s_InvalidEntityId;
    world.view<Impl::ModelObjectComponent>().each([&](const Core::ECS::EntityID entity, Impl::ModelObjectComponent& object)noexcept{
        if(result.valid())
            return;
        if(object.owner == owner && object.object == objectName && object.kind == objectKind)
            result = entity;
    });
    return result;
}

class SkinningBenchmarkPoseAnimator final{
public:
    static void Animate(Core::ECS::World& world, const Span<const Core::ECS::EntityID> entities, const Span<const Impl::SkeletonJointMatrix> bindJoints, const f32 timeSeconds, const b8 staticPreview)noexcept{
        for(usize entityIndex = 0u; entityIndex < entities.size(); ++entityIndex){
            auto* pose = world.tryGetComponent<NWB::Impl::SkeletonPoseComponent>(entities[entityIndex]);
            if(!pose)
                continue;
            AnimateSkinningBenchmarkPoseJoints(bindJoints, static_cast<u32>(entityIndex), timeSeconds, staticPreview, pose->localJoints);
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
