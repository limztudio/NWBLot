// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "smoke_scene_helpers.h"

#include <impl/assets_model/asset.h>
#include <impl/assets_skeleton/asset.h>
#include <impl/ecs_mesh/skinning/system.h>
#include <impl/ecs_model/system.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_skeleton/runtime_helpers.h>

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


void AddSmokeSkinnedRenderSystems(Core::ECS::World& world, ProjectRuntimeContext& context, const SmokeRenderQualitySettings& baseSettings = {});
[[nodiscard]] NotNullUniquePtr<Core::ECS::World> CreateSmokeSkinnedRenderWorldOrDie(TStringView projectName, const SmokeRenderQualitySettings& baseSettings, ProjectRuntimeContext& context);
[[nodiscard]] Expected<Vector<Impl::SkeletonJointMatrix, Core::Alloc::GlobalArena>> LoadSmokeSkeletonBindJoints(
    const SmokeModelRef& modelRef,
    TStringView projectName,
    Name preferredSkeletonObject,
    b8 requireInvertibleBindPose,
    Core::Alloc::GlobalArena& arena,
    Core::Assets::AssetManager& assetManager
);
void DestroySmokeSkinnedRenderWorld(ProjectRuntimeContext& context, NotNullUniquePtr<Core::ECS::World>& world);
void SyncSmokeModelRuntimes(Core::ECS::World& world);
[[nodiscard]] SmokeTintedModelEntityResult CreateTintedModelEntity(
    const SmokeModelRef& model,
    const SmokeMaterialRef& material,
    NameText<char> materialInterfacePath,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    Core::ECS::World& world
);
[[nodiscard]] Core::ECS::EntityID FindSpawnedModelObject(Core::ECS::EntityID owner, Name objectName, Impl::ModelObjectKind::Enum objectKind, Core::ECS::World& world)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

