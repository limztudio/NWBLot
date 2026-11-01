// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "render_quality_settings.h"
#include "smoke_environment.h"
#include "smoke_project_helpers.h"

#include <loader/project_entry.h>

#include <impl/assets_material/asset.h>
#include <impl/assets_mesh/asset.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_scene/module.h>

#include <core/alloc/scratch.h>
#include <core/assets/ref.h>
#include <core/ecs/entity.h>
#include <core/ecs/world.h>
#include <core/graphics/runtime/runtime.h>
#include <core/telemetry/frame_graph_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SmokeMaterialRef = Core::Assets::AssetRef<Impl::Material>;
using SmokeMeshRef = Core::Assets::AssetRef<Impl::Mesh>;

struct SmokeTintedEntitySetup{
    Name materialInterface;
    Core::ECS::EntityID entity;
};

struct SmokeRenderSystems{
    Impl::MeshSystem& mesh;
    Impl::RendererSystem& renderer;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SmokeTintedEntitySetup CreateSmokeTintedEntity(
    const SmokeMaterialRef& material,
    NameText<char> materialInterfacePath,
    const Float4& position,
    const Float4& scale,
    Core::ECS::World& world
);
[[nodiscard]] b8 ApplySmokeMaterialTint(
    const SmokeTintedEntitySetup& setup,
    const Float4& colorTint,
    AStringView tintParameterPath,
    Core::ECS::World& world
);
[[nodiscard]] Core::ECS::EntityID CreateTintedStaticMeshEntity(
    const SmokeMeshRef& mesh,
    const SmokeMaterialRef& material,
    NameText<char> materialInterfacePath,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    AStringView tintParameterPath,
    Core::ECS::World& world
);
[[nodiscard]] Core::ECS::EntityID CreateTintedStaticMeshEntity(
    const SmokeMeshRef& mesh,
    const SmokeMaterialRef& material,
    NameText<char> materialInterfacePath,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    Core::ECS::World& world
);
[[nodiscard]] SmokeRenderSystems CreateSmokeRenderSystems(Core::ECS::World& world, ProjectRuntimeContext& context, const SmokeRenderQualitySettings& baseSettings = {});
Impl::RendererSystem& AddSmokeRenderSystems(Core::ECS::World& world, ProjectRuntimeContext& context);
[[nodiscard]] NotNullUniquePtr<Core::ECS::World> CreateSmokeRenderWorldOrDie(TStringView projectName, ProjectRuntimeContext& context);
void FinishDestroyingSmokeWorld(ProjectRuntimeContext& context, NotNullUniquePtr<Core::ECS::World>& world);
void RemoveSmokeRendererSystem(ProjectRuntimeContext& context, Core::ECS::World& world);
void DestroySmokeRenderWorld(ProjectRuntimeContext& context, NotNullUniquePtr<Core::ECS::World>& world);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

