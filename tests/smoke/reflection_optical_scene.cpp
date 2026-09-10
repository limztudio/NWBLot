// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_optical_scene.h"

#include <core/assets/manager.h>
#include <global/math/constant.h>
#include <global/math/convert.h>
#include <global/math/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_optical_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Tests::Smoke;
static constexpr SmokeMeshRef s_Plane("project/meshes/shadow_plane");
static constexpr SmokeMeshRef s_Box("project/meshes/cube_hard_edges");
static constexpr SmokeMeshRef s_Disconnected("project/meshes/reflection_disconnected_boxes");
static constexpr SmokeMeshRef s_Overlapping("project/meshes/reflection_overlapping_boxes");
static constexpr SmokeMeshRef s_Torus("project/meshes/refraction_torus");
static constexpr SmokeMeshRef s_Prism("project/meshes/reflection_tir_prism");
static constexpr SmokeMaterialRef s_Opaque("project/smoke/reflection/materials/opaque");
static constexpr SmokeMaterialRef s_Volume("project/smoke/reflection/materials/optical_volume");
static constexpr SmokeMaterialRef s_Pane("project/smoke/reflection/materials/optical_pane");
static constexpr AStringView s_SurfaceInterface = "project/shaders/smoke_surface";
static constexpr AStringView s_OpticalInterface = "project/shaders/reflection_optical";
static constexpr Float4 s_Clear(1.f, 1.f, 1.f, 0.f);
static constexpr Float4 s_Tinted(0.55f, 0.8f, 1.f, 0.f);

static Core::ECS::EntityID CreatePanel(ProjectRuntimeContext& context, Core::ECS::World& world,
    const Float4& color, const Float4& position, const Float4& scale, const f32 f0 = 0.f){
    const auto entity = CreateTintedStaticMeshEntity(world, context.objectArena, s_Plane, s_Opaque, s_SurfaceInterface, color, position, scale);
    if(!entity.valid())
        return entity;
    auto& transform = world.entity(entity).getComponent<Impl::Scene::TransformComponent>();
    StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.f, 0.f), transform.rotation);
    const Half4U packedF0 = MakeHalf4U(f0, f0, f0, 0.f);
    const Half packedRoughness = ConvertFloatToHalf(0.f);
    if(!Impl::SetMaterialMutableParameter(
        world, entity, Name(s_SurfaceInterface), "runtime.specular_f0", Impl::MaterialLayoutFieldType::Half3,
        Impl::PackMaterialInstanceBytes(packedF0.raw, sizeof(Half) * 3u)
    ))
        return Core::ECS::ENTITY_ID_INVALID;
    if(!Impl::SetMaterialMutableParameter(
        world, entity, Name(s_SurfaceInterface), "runtime.perceptual_roughness", Impl::MaterialLayoutFieldType::Half,
        Impl::PackMaterialInstanceBytes(&packedRoughness, sizeof(packedRoughness))
    ))
        return Core::ECS::ENTITY_ID_INVALID;
    return entity;
}

static Core::ECS::EntityID CreateBoundary(ProjectRuntimeContext& context, Core::ECS::World& world,
    const SmokeMeshRef& mesh, const Float4& position, const Float4& scale, const f32 ior, const Float4& transmission,
    const Impl::OpticalBoundaryMode::Enum mode = Impl::OpticalBoundaryMode::ClosedNested,
    const i32 priority = 0, const f32 coverage = 0.f, const bool pane = false){
    const auto entity = CreateTintedStaticMeshEntity(
        world, context.objectArena, mesh, pane ? s_Pane : s_Volume, s_OpticalInterface,
        Float4(0.9f, 0.6f, 0.f, coverage), position, scale
    );
    if(!entity.valid())
        return entity;
    auto& renderer = world.entity(entity).getComponent<Impl::RendererComponent>();
    renderer.opticalBoundaryMode = mode;
    renderer.opticalMediumPriority = priority;
    const Half packedIor = ConvertFloatToHalf(ior);
    const Half4U packedTransmission = MakeHalf4U(transmission.x, transmission.y, transmission.z, 0.f);
    if(!Impl::SetMaterialMutableParameter(
        world, entity, Name(s_OpticalInterface), "runtime.ior", Impl::MaterialLayoutFieldType::Half,
        Impl::PackMaterialInstanceBytes(&packedIor, sizeof(packedIor))
    ))
        return Core::ECS::ENTITY_ID_INVALID;
    if(!Impl::SetMaterialMutableParameter(
        world, entity, Name(s_OpticalInterface), "runtime.unit_transmission", Impl::MaterialLayoutFieldType::Half3,
        Impl::PackMaterialInstanceBytes(packedTransmission.raw, sizeof(Half) * 3u)
    ))
        return Core::ECS::ENTITY_ID_INVALID;
    return entity;
}

static Core::ECS::EntityID CreateBox(ProjectRuntimeContext& context, Core::ECS::World& world,
    const f32 centerZ, const f32 thickness, const f32 ior, const Float4& transmission,
    const Impl::OpticalBoundaryMode::Enum mode = Impl::OpticalBoundaryMode::ClosedNested, const i32 priority = 0,
    const f32 xyScale = 1.f){
    return CreateBoundary(
        context, world, s_Box, Float4(0.f, 1.4f, centerZ, 0.f), Float4(24.f * xyScale, 18.f * xyScale, thickness, 0.f),
        ior, transmission, mode, priority
    );
}

static bool CreateOpticalObjects(ProjectRuntimeContext& context, Core::ECS::World& world, const AStringView caseName){
    if(caseName == "optical_reference")
        return true;
    if(caseName == "optical_tir")
        return CreateBoundary(context, world, s_Prism, Float4(0.f, 1.4f, 0.f, 0.f), Float4(1.f, 1.f, 1.f, 0.f), 1.5f, s_Clear).valid();
    if(caseName == "optical_inside" || caseName == "optical_inside_nested"){
        if(!CreateBox(context, world, -3.f, 14.f, 1.5f, s_Clear).valid())
            return false;
        if(caseName == "optical_inside")
            return true;
        return CreateBox(
            context, world, -3.f, 10.f, 1.33f, s_Clear, Impl::OpticalBoundaryMode::ClosedNested, 0, 0.7f
        ).valid();
    }
    if(caseName == "optical_same_mesh")
        return CreateBoundary(context, world, s_Disconnected, Float4(0.f, 1.4f, -9.f, 0.f), Float4(1.f, 1.f, 1.f, 0.f), 1.5f, s_Tinted).valid();
    if(caseName == "optical_union_same_mesh")
        return CreateBoundary(context, world, s_Overlapping, Float4(0.f, 1.4f, -9.f, 0.f), Float4(1.f, 1.f, 1.f, 0.f), 1.5f, s_Tinted).valid();
    if(caseName == "optical_union_single")
        return CreateBox(context, world, -9.f, 0.8f, 1.5f, s_Tinted).valid();
    if(caseName == "optical_disconnected"){
        return
            CreateBox(context, world, -8.f, 0.5f, 1.5f, s_Tinted).valid()
            && CreateBox(context, world, -10.f, 0.5f, 1.5f, s_Tinted).valid()
        ;
    }
    if(caseName == "optical_torus"){
        const auto entity = CreateBoundary(
            context, world, s_Torus, Float4(0.f, 1.4f, -9.5f, 0.f), Float4(2.f, 2.f, 2.f, 0.f), 1.5f, s_Tinted
        );
        if(!entity.valid())
            return false;
        auto& transform = world.entity(entity).getComponent<Impl::Scene::TransformComponent>();
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.f, 0.f), transform.rotation);
        return true;
    }
    if(caseName == "optical_priority_tie_a" || caseName == "optical_priority_tie_b"){
        const bool reverse = caseName == "optical_priority_tie_b";
        for(u32 index = 0u; index < 2u; ++index){
            const bool firstMedium = reverse ? index == 1u : index == 0u;
            if(!CreateBox(
                context, world, firstMedium ? -8.5f : -9.5f, 2.f, firstMedium ? 1.5f : 1.33f,
                firstMedium ? s_Tinted : Float4(1.f, 0.7f, 0.45f, 0.f), Impl::OpticalBoundaryMode::ClosedPriority, 10
            ).valid())
                return false;
        }
        return true;
    }
    if(caseName == "optical_priority_a" || caseName == "optical_priority_b" || caseName == "optical_mixed"){
        const bool firstWins = caseName == "optical_priority_a";
        return
            CreateBox(context, world, -8.5f, 2.f, 1.5f, s_Tinted, Impl::OpticalBoundaryMode::ClosedPriority, firstWins ? 20 : 10).valid()
            && CreateBox(
                context, world, -9.5f, 2.f, 1.33f, Float4(1.f, 0.7f, 0.45f, 0.f),
                caseName == "optical_mixed" ? Impl::OpticalBoundaryMode::ClosedNested : Impl::OpticalBoundaryMode::ClosedPriority,
                firstWins ? 10 : 20
            ).valid()
        ;
    }
    if(caseName == "optical_nested2" || caseName == "optical_nested3" || caseName == "optical_overflow"){
        const u32 count = caseName == "optical_overflow" ? 5u : caseName == "optical_nested3" ? 3u : 2u;
        for(u32 index = 0u; index < count; ++index){
            const f32 thickness = count == 5u ? 4.f - 0.6f * static_cast<f32>(index) : 3.f / static_cast<f32>(1u << index);
            const f32 ior = index == 0u ? 1.5f : index == 1u ? 1.33f : 1.1f + 0.02f * static_cast<f32>(index - 2u);
            const Float4 transmission = index == 0u ? s_Tinted : Float4(1.f, 0.7f, 0.45f, 0.f);
            if(!CreateBox(
                context, world, -9.f, thickness, ior, transmission, Impl::OpticalBoundaryMode::ClosedNested,
                0, 1.f - 0.1f * static_cast<f32>(index)
            ).valid())
                return false;
        }
        return true;
    }
    if(caseName == "optical_alpha_before" || caseName == "optical_alpha_after"){
        if(!CreateBox(context, world, -9.f, 2.f, 1.5f, s_Tinted).valid())
            return false;
        for(u32 index = 0u; index < 3u; ++index){
            const f32 x = index == 0u ? -7.f : index == 1u ? 0.f : 7.f;
            const f32 halfWidth = index == 1u ? 2.f : 5.f;
            const auto entity = CreateBoundary(
                context, world, s_Plane, Float4(x, 1.4f, caseName == "optical_alpha_before" ? -7.f : -11.f, 0.f),
                Float4(halfWidth, 1.f, 9.f, 0.f), 1.f, s_Clear, Impl::OpticalBoundaryMode::Unspecified,
                0, static_cast<f32>(index) * 0.5f, true
            );
            if(!entity.valid())
                return false;
            auto& transform = world.entity(entity).getComponent<Impl::Scene::TransformComponent>();
            StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.f, 0.f), transform.rotation);
        }
        return true;
    }
    const bool duplicate = caseName == "optical_duplicate_identical" || caseName == "optical_duplicate_group"
        || caseName == "optical_duplicate_reverse";
    const bool independent = caseName == "optical_coincident_independent";
    if(
        !duplicate && !independent && caseName != "optical_clear" && caseName != "optical_tinted" && caseName != "optical_tilted"
        && caseName != "optical_mirrored" && caseName != "optical_unspecified"
    )
        return false;
    const u32 count = duplicate || independent ? 2u : 1u;
    for(u32 index = 0u; index < count; ++index){
        const bool sameInputs = caseName == "optical_duplicate_identical";
        const bool selected = caseName == "optical_duplicate_reverse" ? index == 1u : index == 0u;
        const Float4 transmission = caseName == "optical_clear" ? s_Clear
            : !duplicate || sameInputs || selected ? s_Tinted : Float4(1.f, 0.5f, 0.2f, 0.f);
        const auto entity = CreateBox(
            context, world, -9.f, 2.f, 1.5f, transmission,
            caseName == "optical_unspecified" ? Impl::OpticalBoundaryMode::Unspecified : Impl::OpticalBoundaryMode::ClosedNested
        );
        if(!entity.valid())
            return false;
        auto& transform = world.entity(entity).getComponent<Impl::Scene::TransformComponent>();
        if(caseName == "optical_mirrored")
            transform.scale.x = -24.f;
        if(caseName == "optical_tilted")
            StoreFloat(QuaternionRotationRollPitchYaw(0.f, 0.2f, 0.f), transform.rotation);
        if(duplicate){
            auto& renderer = world.entity(entity).getComponent<Impl::RendererComponent>();
            renderer.opticalVolumeCoincidence = sameInputs ? Impl::OpticalVolumeCoincidence::IdenticalMaterial
                : Impl::OpticalVolumeCoincidence::SharedGroup;
            renderer.opticalVolumeGroup = Name("smoke/reflection/optical_duplicate");
            renderer.opticalVolumePriority = selected ? 20 : 10;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CreateReflectionOpticalScene(ProjectRuntimeContext& context, Core::ECS::World& world, const AStringView caseName){
    using namespace __hidden_reflection_optical_scene;
    if(!CreatePanel(context, world, Float4(0.12f, 0.12f, 0.12f, 1.f), Float4(0.f, 1.4f, 2.f, 0.f), Float4(6.f, 1.f, 4.5f, 0.f)).valid())
        return false;
    if(!CreatePanel(context, world, Float4(0.f, 0.f, 0.f, 1.f), Float4(0.f, 1.4f, 0.f, 0.f), Float4(2.8f, 1.f, 1.8f, 0.f), 0.95f).valid())
        return false;
    if(caseName != "optical_tir"){
        for(u32 stripe = 0u; stripe < 31u; ++stripe){
            const Float4 color = stripe == 15u ? Float4(0.9f, 0.9f, 0.9f, 1.f)
                : Float4(stripe % 3u == 0u ? 0.9f : 0.f, stripe % 3u == 1u ? 0.9f : 0.f, stripe % 3u == 2u ? 0.9f : 0.f, 1.f);
            if(!CreatePanel(
                context, world, color, Float4((static_cast<f32>(stripe) - 15.f) * 0.8f, 1.4f, -14.f, 0.f),
                Float4(0.4f, 1.f, 8.f, 0.f)
            ).valid())
                return false;
        }
    }
    return CreateOpticalObjects(context, world, caseName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

