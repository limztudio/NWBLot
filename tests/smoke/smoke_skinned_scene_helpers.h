// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "smoke_scene_helpers.h"

#include <impl/assets_model/asset.h>
#include <impl/assets_skeleton/asset.h>
#include <impl/ecs_mesh/skinning/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_skeleton/runtime_helpers.h>

#include <core/assets/manager.h>
#include <core/ecs/entity.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SmokeModelRef = Core::Assets::AssetRef<Impl::Model>;


struct SmokeTintedModelEntityResult{
    Core::ECS::EntityID entity;
    bool tintApplied = false;
};


inline void AddSmokeSkinnedRenderSystems(Core::ECS::World& world, ProjectRuntimeContext& context, const SmokeRenderQualitySettings& baseSettings = {}){
    const SmokeRenderSystems systems = CreateSmokeRenderSystems(world, context, baseSettings);
    world.addSystem<Impl::ModelSystem>(world, context.assetManager, Impl::CreateModelObjectRendererHooks());
    auto& meshSkinningSystem = world.addSystem<Impl::MeshSkinningSystem>(world, context.graphics, context.assetManager, systems.mesh, context.shaderPathResolver);

    context.graphics.addRenderPassToBack(meshSkinningSystem);
    context.graphics.addRenderPassToBack(systems.renderer);
    context.frameGraphRegistry.registerContributor(systems.renderer);
}

[[nodiscard]] inline NotNullUniquePtr<Core::ECS::World> CreateSmokeSkinnedRenderWorldOrDie(ProjectRuntimeContext& context, const TStringView projectName, const SmokeRenderQualitySettings& baseSettings = {}){
    auto world = CreateSmokeWorldOrDie(context, projectName);
    AddSmokeSkinnedRenderSystems(*world, context, baseSettings);
    return world;
}

[[nodiscard]] inline Expected<Vector<Impl::SkeletonJointMatrix, Core::Alloc::GlobalArena>> LoadSmokeSkeletonBindJoints(
    Core::Alloc::GlobalArena& arena,
    Core::Assets::AssetManager& assetManager,
    const SmokeModelRef& modelRef,
    const TStringView projectName,
    const Name preferredSkeletonObject = Name(),
    const bool requireInvertibleBindPose = false
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
        if(requireInvertibleBindPose && !MatrixIsInvertibleAffine(LoadFloat(joint.localBindPose), Impl::SkeletonRuntime::s_AffineEpsilon, Impl::SkeletonRuntime::s_JointDeterminantEpsilon)){
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
    Core::ECS::World& world,
    Core::Alloc::GlobalArena& arena,
    const SmokeModelRef& model,
    const SmokeMaterialRef& material,
    const NameText<char> materialInterfacePath,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale
){
    const SmokeTintedEntitySetup setup = CreateSmokeTintedEntity(world, arena, material, materialInterfacePath, position, scale);

    auto& modelComponent = world.entity(setup.entity).addComponent<Impl::ModelComponent>();
    modelComponent.model = model;

    return {.entity = setup.entity, .tintApplied = ApplySmokeMaterialTint(world, setup, colorTint)};
}

[[nodiscard]] inline Core::ECS::EntityID FindSpawnedModelObject(Core::ECS::World& world, const Core::ECS::EntityID owner, const Name objectName, const u32 objectKind)noexcept{
    Core::ECS::EntityID result = Core::ECS::s_InvalidEntityId;
    world.view<Impl::ModelObjectComponent>().each([&](const Core::ECS::EntityID entity, Impl::ModelObjectComponent& object)noexcept{
        if(result.valid())
            return;
        if(object.owner == owner && object.object == objectName && object.kind == objectKind)
            result = entity;
    });
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

