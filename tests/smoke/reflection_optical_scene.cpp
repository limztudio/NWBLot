// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_optical_scene.h"
#include "csg_smoke_helpers.h"

#include <impl/ecs_csg/shape_registry.h>

#include <core/assets/manager.h>

#include <global/simd/constant.h>
#include <global/simd/convert.h>
#include <global/simd/frame.h>
#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_optical_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_SpecularParameterPath = "runtime.specular_f0";
constexpr AStringView s_RoughnessParameterPath = "runtime.perceptual_roughness";
constexpr AStringView s_PlaneShapePath = "engine/csg/plane";
constexpr AStringView s_OpticalCsgDenseCase = "optical_csg_dense";
constexpr AStringView s_OpticalReferenceCase = "optical_reference";
constexpr AStringView s_OpticalGroupEntryCase = "optical_group_entry";
constexpr AStringView s_OpticalGroupGapSubUlpCase = "optical_group_gap_sub_ulp";
constexpr AStringView s_OpticalGroupGapCase = "optical_group_gap";
constexpr AStringView s_OpticalSubUlpCase = "optical_sub_ulp";
constexpr AStringView s_OpticalSliverCase = "optical_sliver";
constexpr AStringView s_OpticalCsgCavityCase = "optical_csg_cavity";
constexpr AStringView s_OpticalCsgOpenTailCase = "optical_csg_open_tail";
constexpr AStringView s_OpticalTirCase = "optical_tir";
constexpr AStringView s_OpticalInsideCase = "optical_inside";
constexpr AStringView s_OpticalPriorityTieBCase = "optical_priority_tie_b";
constexpr AStringView s_OpticalPriorityACase = "optical_priority_a";
constexpr AStringView s_OpticalMixedCase = "optical_mixed";
constexpr AStringView s_OpticalNested3Case = "optical_nested3";
constexpr AStringView s_OpticalOverflowCase = "optical_overflow";
constexpr AStringView s_OpticalAlphaBeforeCase = "optical_alpha_before";
constexpr AStringView s_OpticalDuplicateIdenticalCase = "optical_duplicate_identical";
constexpr AStringView s_OpticalDuplicateReverseCase = "optical_duplicate_reverse";
constexpr AStringView s_OpticalClearCase = "optical_clear";
constexpr AStringView s_OpticalTiltedCase = "optical_tilted";
constexpr AStringView s_OpticalMirroredCase = "optical_mirrored";
constexpr AStringView s_OpticalUnspecifiedCase = "optical_unspecified";

using namespace Tests::Smoke;

struct OpticalReceiverEntities{
    Core::ECS::EntityID receiver = Core::ECS::s_InvalidEntityId;
    Core::ECS::EntityID cutter = Core::ECS::s_InvalidEntityId;
};

static constexpr SmokeMeshRef s_Plane("project/meshes/shadow_plane");
static constexpr SmokeMeshRef s_Box("project/meshes/cube_hard_edges");
static constexpr SmokeMeshRef s_CsgOpenTail("project/meshes/reflection_csg_open_tail");
static constexpr SmokeMeshRef s_Disconnected("project/meshes/reflection_disconnected_boxes");
static constexpr SmokeMeshRef s_Overlapping("project/meshes/reflection_overlapping_boxes");
static constexpr SmokeMeshRef s_Dense("project/meshes/reflection_dense_boxes");
static constexpr SmokeMeshRef s_GroupGap("project/meshes/reflection_group_gap");
static constexpr SmokeMeshRef s_GroupGapSubUlp("project/meshes/reflection_group_gap_sub_ulp");
static constexpr SmokeMeshRef s_GroupEntry("project/meshes/reflection_group_entry");
static constexpr SmokeMeshRef s_CsgGroupEntry("project/meshes/reflection_csg_group_entry");
static constexpr SmokeMeshRef s_Torus("project/meshes/refraction_torus");
static constexpr SmokeMeshRef s_Prism("project/meshes/reflection_tir_prism");
static constexpr SmokeMaterialRef s_Opaque("project/smoke/reflection/materials/opaque");
static constexpr SmokeMaterialRef s_Volume("project/smoke/reflection/materials/optical_volume");
static constexpr SmokeMaterialRef s_Pane("project/smoke/reflection/materials/optical_pane");
static constexpr AStringView s_SurfaceInterface = "project/shaders/smoke_surface";
static constexpr AStringView s_OpticalInterface = "project/shaders/reflection_optical";
static constexpr Float4 s_Clear(1.f, 1.f, 1.f, 0.f);
static constexpr Float4 s_Tinted(0.55f, 0.8f, 1.f, 0.f);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::ECS::EntityID CreatePanel(ProjectRuntimeContext& context, Core::ECS::World& world, const Float4& color, const Float4& position, const Float4& scale, const f32 f0 = 0.f){
    const auto entity = CreateTintedStaticMeshEntity(world, context.objectArena, s_Plane, s_Opaque, s_SurfaceInterface, color, position, scale);
    if(!entity.valid())
        return entity;
    auto& transform = world.entity(entity).getComponent<Impl::Scene::TransformComponent>();
    StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.f, 0.f), transform.rotation);
    const Half4U packedF0 = MakeHalf4U(f0, f0, f0, 0.f);
    const Half packedRoughness = ConvertFloatToHalf(0.f);
    if(!Impl::SetMaterialMutableParameter(world, entity, Name(s_SurfaceInterface), s_SpecularParameterPath, Impl::MaterialLayoutFieldType::Half3, Impl::PackMaterialInstanceBytes(packedF0.raw, sizeof(Half) * 3u)))
        return Core::ECS::s_InvalidEntityId;
    if(!Impl::SetMaterialMutableParameter(world, entity, Name(s_SurfaceInterface), s_RoughnessParameterPath, Impl::MaterialLayoutFieldType::Half, Impl::PackMaterialInstanceBytes(&packedRoughness, sizeof(packedRoughness))))
        return Core::ECS::s_InvalidEntityId;
    return entity;
}

static Core::ECS::EntityID CreateBoundary(
    ProjectRuntimeContext& context, Core::ECS::World& world,
    const SmokeMeshRef& mesh, const Float4& position, const Float4& scale, const f32 ior, const Float4& transmission,
    const Impl::OpticalBoundaryMode::Enum mode = Impl::OpticalBoundaryMode::ClosedNested,
    const i32 priority = 0, const f32 coverage = 0.f, const bool pane = false
){
    const auto entity = CreateTintedStaticMeshEntity(world, context.objectArena, mesh, pane ? s_Pane : s_Volume, s_OpticalInterface, Float4(0.9f, 0.6f, 0.f, coverage), position, scale);
    if(!entity.valid())
        return entity;
    auto& renderer = world.entity(entity).getComponent<Impl::RendererComponent>();
    renderer.opticalBoundaryMode = mode;
    renderer.opticalMediumPriority = priority;
    const Half packedIor = ConvertFloatToHalf(ior);
    const Half4U packedTransmission = MakeHalf4U(transmission.x, transmission.y, transmission.z, 0.f);
    if(!Impl::SetMaterialMutableParameter(world, entity, Name(s_OpticalInterface), "runtime.ior", Impl::MaterialLayoutFieldType::Half, Impl::PackMaterialInstanceBytes(&packedIor, sizeof(packedIor))))
        return Core::ECS::s_InvalidEntityId;
    if(!Impl::SetMaterialMutableParameter(world, entity, Name(s_OpticalInterface), "runtime.unit_transmission", Impl::MaterialLayoutFieldType::Half3, Impl::PackMaterialInstanceBytes(packedTransmission.raw, sizeof(Half) * 3u)))
        return Core::ECS::s_InvalidEntityId;
    return entity;
}

static Core::ECS::EntityID CreateBox(
    ProjectRuntimeContext& context, Core::ECS::World& world,
    const f32 centerZ, const f32 thickness, const f32 ior, const Float4& transmission,
    const Impl::OpticalBoundaryMode::Enum mode = Impl::OpticalBoundaryMode::ClosedNested, const i32 priority = 0,
    const f32 xyScale = 1.f
){
    Float4 scale;
    StoreFloat(VectorSetZ(VectorScale(VectorSet(24.f, 18.f, 0.f, 0.f), xyScale), thickness), scale);
    return CreateBoundary(context, world, s_Box, Float4(0.f, 1.4f, centerZ, 0.f), scale, ior, transmission, mode, priority);
}

static Expected<OpticalReceiverEntities> CreateDenseReceiver(ProjectRuntimeContext& context, Core::ECS::World& world, const bool csg){
    // One opaque receiver has eighteen distinct closed crossings; an admitted cutter leaves its nearest surface intact.
    const auto receiver = CreateTintedStaticMeshEntity(world, context.objectArena, s_Dense, s_Opaque, s_SurfaceInterface, Float4(0.9f, 0.2f, 0.05f, 1.f), Float4(0.f, 1.4f, -8.5f, 0.f), Float4(1.f, 1.f, 1.f, 0.f));
    if(!receiver.valid())
        return MakeUnexpected(Failure{});
    const Half4U packedF0 = MakeHalf4U(0.f, 0.f, 0.f, 0.f);
    const Half packedRoughness = ConvertFloatToHalf(1.f);
    if(!Impl::SetMaterialMutableParameter(world, receiver, Name(s_SurfaceInterface), s_SpecularParameterPath, Impl::MaterialLayoutFieldType::Half3, Impl::PackMaterialInstanceBytes(packedF0.raw, sizeof(Half) * 3u)))
        return MakeUnexpected(Failure{});
    if(!Impl::SetMaterialMutableParameter(world, receiver, Name(s_SurfaceInterface), s_RoughnessParameterPath, Impl::MaterialLayoutFieldType::Half, Impl::PackMaterialInstanceBytes(&packedRoughness, sizeof(packedRoughness))))
        return MakeUnexpected(Failure{});
    if(!csg)
        return OpticalReceiverEntities{ .receiver = receiver };
    const Name group("smoke/reflection/csg_dense");
    AddStaticCsgMeshReceiver(world, receiver, group, true, false);
    auto cutterEntity = world.createEntity();
    auto& cutter = cutterEntity.addComponent<Impl::CsgCutterComponent>(context.objectArena);
    cutter.receiverGroup = group;
    cutter.shapeType = Name(s_PlaneShapePath);
    Impl::CsgPlaneShapeParameters parameters;
    parameters.normalDistance = Float4(-1.f, 0.f, 0.f, 11.f);
    AssignCsgCutterParameters(cutter, parameters);
    AssignCsgCutterTransform(cutter, VectorZero(), QuaternionIdentity());
    return OpticalReceiverEntities{ .receiver = receiver, .cutter = cutterEntity.id() };
}

static Expected<OpticalReceiverEntities> CreateCsgBoundaryReceiver(ProjectRuntimeContext& context, Core::ECS::World& world, const SmokeMeshRef& mesh, const bool cavity, const f32 ior, const Float4& transmission, const Impl::OpticalBoundaryMode::Enum mode){
    // Both retain [-10,-9]; the cavity case starts the reflected ray inside the removed receiver volume.
    const auto receiver = CreateBoundary(context, world, mesh, Float4(0.f, 1.4f, cavity ? -3.f : -9.f, 0.f), Float4(24.f, 18.f, cavity ? 14.f : 2.f, 0.f), ior, transmission, mode);
    if(!receiver.valid())
        return MakeUnexpected(Failure{});
    const Name group("smoke/reflection/csg_optics");
    AddStaticCsgMeshReceiver(world, receiver, group, false, true);
    auto cutterEntity = world.createEntity();
    auto& cutter = cutterEntity.addComponent<Impl::CsgCutterComponent>(context.objectArena);
    cutter.receiverGroup = group;
    cutter.shapeType = Name(s_PlaneShapePath);
    Impl::CsgPlaneShapeParameters parameters;
    parameters.normalDistance = Float4(0.f, 0.f, -1.f, -9.f);
    AssignCsgCutterParameters(cutter, parameters);
    AssignCsgCutterTransform(cutter, VectorZero(), QuaternionIdentity());
    return OpticalReceiverEntities{ .receiver = receiver, .cutter = cutterEntity.id() };
}

static bool CreateOpticalObjects(ProjectRuntimeContext& context, Core::ECS::World& world, const AStringView caseName){
    if(caseName == "optical_dense" || caseName == s_OpticalCsgDenseCase)
        return CreateDenseReceiver(context, world, caseName == s_OpticalCsgDenseCase).has_value();
    if(caseName == s_OpticalReferenceCase)
        return true;
    if(caseName == "optical_csg_reference")
        return CreateBox(context, world, -9.5f, 1.f, 1.5f, s_Tinted).valid();
    if(caseName == "optical_unspecified_reference")
        return CreateBox(context, world, -9.5f, 1.f, 1.f, s_Clear, Impl::OpticalBoundaryMode::Unspecified).valid();
    if(caseName == s_OpticalGroupEntryCase || caseName == "optical_csg_group_entry"){
        // The cutter replaces the final grouped entry with a cap while the preceding positive thin solid remains retained.
        const bool ordinary = caseName == s_OpticalGroupEntryCase;
        const auto receiver = CreateBoundary(context, world, ordinary ? s_GroupEntry : s_CsgGroupEntry, Float4(0.f, 1.4f, -9.f, 0.f), Float4(1.f, 1.f, 1.f, 0.f), 1.5f, s_Tinted);
        if(!receiver.valid())
            return false;
        if(ordinary)
            return true;
        const Name group("smoke/reflection/csg_group_entry");
        AddStaticCsgMeshReceiver(world, receiver, group, false, true);
        auto cutterEntity = world.createEntity();
        auto& cutter = cutterEntity.addComponent<Impl::CsgCutterComponent>(context.objectArena);
        cutter.receiverGroup = group;
        cutter.shapeType = Name("engine/csg/box");
        Impl::CsgBoxShapeParameters parameters;
        parameters.halfExtents = Float4(13.f, 10.f, 3.5e-6f, 0.f);
        AssignCsgCutterParameters(cutter, parameters);
        AssignCsgCutterTransform(cutter, VectorSet(0.f, 1.4f, -9.0000085f, 0.f), QuaternionIdentity());
        return true;
    }
    const bool groupGapSubUlp = caseName == s_OpticalGroupGapSubUlpCase || caseName == "optical_csg_group_gap_sub_ulp";
    if(groupGapSubUlp || caseName == s_OpticalGroupGapCase || caseName == "optical_csg_group_gap"){
        // Three closed components hide a positive gap inside a grouped 1 -> 0 -> 1 -> 2 winding sequence.
        const auto receiver = CreateBoundary(context, world, groupGapSubUlp ? s_GroupGapSubUlp : s_GroupGap, Float4(0.f, 1.4f, -9.f, 0.f), Float4(1.f, 1.f, 1.f, 0.f), 1.5f, s_Tinted);
        if(!receiver.valid())
            return false;
        if(caseName == s_OpticalGroupGapCase || caseName == s_OpticalGroupGapSubUlpCase)
            return true;
        const Name group(groupGapSubUlp ? "smoke/reflection/csg_group_gap_sub_ulp" : "smoke/reflection/csg_group_gap");
        AddStaticCsgMeshReceiver(world, receiver, group, false, true);
        auto cutterEntity = world.createEntity();
        auto& cutter = cutterEntity.addComponent<Impl::CsgCutterComponent>(context.objectArena);
        cutter.receiverGroup = group;
        cutter.shapeType = Name(s_PlaneShapePath);
        Impl::CsgPlaneShapeParameters parameters;
        parameters.normalDistance = Float4(-1.f, 0.f, 0.f, 10.f);
        AssignCsgCutterParameters(cutter, parameters);
        AssignCsgCutterTransform(cutter, VectorZero(), QuaternionIdentity());
        return true;
    }
    const bool subUlp = caseName == s_OpticalSubUlpCase || caseName == "optical_csg_sub_ulp";
    if(subUlp || caseName == s_OpticalSliverCase || caseName == "optical_csg_sliver"){
        // Positive sub-ULP thickness can round nonincident interfaces to identical ray parameters; Fresnel loss must not disappear.
        const auto receiver = CreateBox(context, world, -9.f, subUlp ? 4e-7f : 8e-6f, 1.5f, s_Tinted);
        if(!receiver.valid())
            return false;
        if(caseName == s_OpticalSliverCase || caseName == s_OpticalSubUlpCase)
            return true;
        const Name group(subUlp ? "smoke/reflection/csg_sub_ulp" : "smoke/reflection/csg_sliver");
        AddStaticCsgMeshReceiver(world, receiver, group, false, true);
        auto cutterEntity = world.createEntity();
        auto& cutter = cutterEntity.addComponent<Impl::CsgCutterComponent>(context.objectArena);
        cutter.receiverGroup = group;
        cutter.shapeType = Name(s_PlaneShapePath);
        Impl::CsgPlaneShapeParameters parameters;
        parameters.normalDistance = Float4(-1.f, 0.f, 0.f, 10.f);
        AssignCsgCutterParameters(cutter, parameters);
        AssignCsgCutterTransform(cutter, VectorZero(), QuaternionIdentity());
        return true;
    }
    const bool unspecifiedCap = caseName == "optical_csg_unspecified_cap";
    if(caseName == "optical_csg_cap" || caseName == s_OpticalCsgCavityCase || caseName == s_OpticalCsgOpenTailCase || unspecifiedCap)
        return CreateCsgBoundaryReceiver(
            context, world, caseName == s_OpticalCsgOpenTailCase ? s_CsgOpenTail : s_Box, caseName == s_OpticalCsgCavityCase,
            unspecifiedCap ? 1.f : 1.5f, unspecifiedCap ? s_Clear : s_Tinted,
            unspecifiedCap ? Impl::OpticalBoundaryMode::Unspecified : Impl::OpticalBoundaryMode::ClosedNested
        ).has_value();
    if(caseName == s_OpticalTirCase)
        return CreateBoundary(context, world, s_Prism, Float4(0.f, 1.4f, 0.f, 0.f), Float4(1.f, 1.f, 1.f, 0.f), 1.5f, s_Clear).valid();
    if(caseName == s_OpticalInsideCase || caseName == "optical_inside_nested"){
        if(!CreateBox(context, world, -3.f, 14.f, 1.5f, s_Clear).valid())
            return false;
        if(caseName == s_OpticalInsideCase)
            return true;
        return CreateBox(context, world, -3.f, 10.f, 1.33f, s_Clear, Impl::OpticalBoundaryMode::ClosedNested, 0, 0.7f).valid();
    }
    if(caseName == "optical_same_mesh")
        return CreateBoundary(context, world, s_Disconnected, Float4(0.f, 1.4f, -9.f, 0.f), Float4(1.f, 1.f, 1.f, 0.f), 1.5f, s_Tinted).valid();
    if(caseName == "optical_union_same_mesh")
        return CreateBoundary(context, world, s_Overlapping, Float4(0.f, 1.4f, -9.f, 0.f), Float4(1.f, 1.f, 1.f, 0.f), 1.5f, s_Tinted).valid();
    if(caseName == "optical_union_single")
        return CreateBox(context, world, -9.f, 0.8f, 1.5f, s_Tinted).valid();
    if(caseName == "optical_disconnected"){
        return CreateBox(context, world, -8.f, 0.5f, 1.5f, s_Tinted).valid() && CreateBox(context, world, -10.f, 0.5f, 1.5f, s_Tinted).valid();
    }
    if(caseName == "optical_torus"){
        const auto entity = CreateBoundary(context, world, s_Torus, Float4(0.f, 1.4f, -9.5f, 0.f), Float4(2.f, 2.f, 2.f, 0.f), 1.5f, s_Tinted);
        if(!entity.valid())
            return false;
        auto& transform = world.entity(entity).getComponent<Impl::Scene::TransformComponent>();
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.f, 0.f), transform.rotation);
        return true;
    }
    if(caseName == "optical_priority_tie_a" || caseName == s_OpticalPriorityTieBCase){
        const bool reverse = caseName == s_OpticalPriorityTieBCase;
        for(u32 index = 0u; index < 2u; ++index){
            const bool firstMedium = reverse ? index == 1u : index == 0u;
            if(!CreateBox(context, world, firstMedium ? -8.5f : -9.5f, 2.f, firstMedium ? 1.5f : 1.33f, firstMedium ? s_Tinted : Float4(1.f, 0.7f, 0.45f, 0.f), Impl::OpticalBoundaryMode::ClosedPriority, 10).valid())
                return false;
        }
        return true;
    }
    if(caseName == s_OpticalPriorityACase || caseName == "optical_priority_b" || caseName == s_OpticalMixedCase){
        const bool firstWins = caseName == s_OpticalPriorityACase;
        return
            CreateBox(context, world, -8.5f, 2.f, 1.5f, s_Tinted, Impl::OpticalBoundaryMode::ClosedPriority, firstWins ? 20 : 10).valid()
            && CreateBox(context, world, -9.5f, 2.f, 1.33f, Float4(1.f, 0.7f, 0.45f, 0.f), caseName == s_OpticalMixedCase ? Impl::OpticalBoundaryMode::ClosedNested : Impl::OpticalBoundaryMode::ClosedPriority, firstWins ? 10 : 20).valid()
        ;
    }
    if(caseName == "optical_nested2" || caseName == s_OpticalNested3Case || caseName == s_OpticalOverflowCase){
        const u32 count = caseName == s_OpticalOverflowCase ? 5u : caseName == s_OpticalNested3Case ? 3u : 2u;
        for(u32 index = 0u; index < count; ++index){
            // Lane x is the stepped thickness; lane y is the layer opacity.
            const SIMDVectorSingle layerFade = VectorNegativeMultiplySubtractExpression(VectorSet(0.6f, 0.1f, 0.f, 0.f), VectorReplicate(static_cast<f32>(index)), VectorSet(4.f, 1.f, 0.f, 0.f));
            const f32 thickness = count == 5u ? VectorGetX(layerFade) : VectorGetX(VectorDivide(VectorReplicate(3.f), VectorReplicate(static_cast<f32>(1u << index))));
            const f32 ior = index == 0u ? 1.5f : index == 1u ? 1.33f : VectorGetX(VectorMultiplyAddExpression(VectorReplicate(0.02f), VectorReplicate(static_cast<f32>(index - 2u)), VectorReplicate(1.1f)));
            const Float4 transmission = index == 0u ? s_Tinted : Float4(1.f, 0.7f, 0.45f, 0.f);
            if(!CreateBox(context, world, -9.f, thickness, ior, transmission, Impl::OpticalBoundaryMode::ClosedNested, 0, VectorGetY(layerFade)).valid())
                return false;
        }
        return true;
    }
    if(caseName == s_OpticalAlphaBeforeCase || caseName == "optical_alpha_after"){
        if(!CreateBox(context, world, -9.f, 2.f, 1.5f, s_Tinted).valid())
            return false;
        for(u32 index = 0u; index < 3u; ++index){
            const f32 x = index == 0u ? -7.f : index == 1u ? 0.f : 7.f;
            const f32 halfWidth = index == 1u ? 2.f : 5.f;
            const auto entity = CreateBoundary(
                context, world, s_Plane, Float4(x, 1.4f, caseName == s_OpticalAlphaBeforeCase ? -7.f : -11.f, 0.f),
                Float4(halfWidth, 1.f, 9.f, 0.f), 1.f, s_Clear, Impl::OpticalBoundaryMode::Unspecified,
                0, VectorGetX(VectorScale(VectorReplicate(static_cast<f32>(index)), 0.5f)), true
            );
            if(!entity.valid())
                return false;
            auto& transform = world.entity(entity).getComponent<Impl::Scene::TransformComponent>();
            StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.f, 0.f), transform.rotation);
        }
        return true;
    }
    const bool duplicate = caseName == s_OpticalDuplicateIdenticalCase || caseName == "optical_duplicate_group" || caseName == s_OpticalDuplicateReverseCase;
    const bool independent = caseName == "optical_coincident_independent";
    if(!duplicate && !independent && caseName != s_OpticalClearCase && caseName != "optical_tinted" && caseName != s_OpticalTiltedCase && caseName != s_OpticalMirroredCase && caseName != s_OpticalUnspecifiedCase)
        return false;
    const u32 count = duplicate || independent ? 2u : 1u;
    for(u32 index = 0u; index < count; ++index){
        const bool sameInputs = caseName == s_OpticalDuplicateIdenticalCase;
        const bool selected = caseName == s_OpticalDuplicateReverseCase ? index == 1u : index == 0u;
        const Float4 transmission = caseName == s_OpticalClearCase ? s_Clear : !duplicate || sameInputs || selected ? s_Tinted : Float4(1.f, 0.5f, 0.2f, 0.f);
        const auto entity = CreateBox(context, world, -9.f, 2.f, 1.5f, transmission, caseName == s_OpticalUnspecifiedCase ? Impl::OpticalBoundaryMode::Unspecified : Impl::OpticalBoundaryMode::ClosedNested);
        if(!entity.valid())
            return false;
        auto& transform = world.entity(entity).getComponent<Impl::Scene::TransformComponent>();
        if(caseName == s_OpticalMirroredCase)
            transform.scale.x = -24.f;
        if(caseName == s_OpticalTiltedCase)
            StoreFloat(QuaternionRotationRollPitchYaw(0.f, 0.2f, 0.f), transform.rotation);
        if(duplicate){
            auto& renderer = world.entity(entity).getComponent<Impl::RendererComponent>();
            renderer.opticalVolumeCoincidence = sameInputs ? Impl::OpticalVolumeCoincidence::IdenticalMaterial : Impl::OpticalVolumeCoincidence::SharedGroup;
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


Expected<ReflectionCsgContextEntities> CreateReflectionCsgContextScene(ProjectRuntimeContext& context, Core::ECS::World& world){
    using namespace __hidden_reflection_optical_scene;
    if(!CreateReflectionOpticalScene(context, world, s_OpticalReferenceCase))
        return MakeUnexpected(Failure{});
    const auto small = CreateCsgBoundaryReceiver(context, world, s_Box, false, 1.5f, s_Tinted, Impl::OpticalBoundaryMode::ClosedNested);
    if(!small)
        return MakeUnexpected(small.error());
    const auto dense = CreateDenseReceiver(context, world, true);
    if(!dense)
        return MakeUnexpected(dense.error());
    return ReflectionCsgContextEntities{ .smallReceiver = small->receiver, .denseReceiver = dense->receiver, .smallCutter = small->cutter, .denseCutter = dense->cutter, };
}

bool CreateReflectionOpticalScene(ProjectRuntimeContext& context, Core::ECS::World& world, const AStringView caseName){
    using namespace __hidden_reflection_optical_scene;
    if(!CreatePanel(context, world, Float4(0.12f, 0.12f, 0.12f, 1.f), Float4(0.f, 1.4f, 2.f, 0.f), Float4(6.f, 1.f, 4.5f, 0.f)).valid())
        return false;
    if(!CreatePanel(context, world, Float4(0.f, 0.f, 0.f, 1.f), Float4(0.f, 1.4f, 0.f, 0.f), Float4(2.8f, 1.f, 1.8f, 0.f), 0.95f).valid())
        return false;
    if(caseName != s_OpticalTirCase){
        for(u32 stripe = 0u; stripe < 31u; ++stripe){
            const Float4 color = stripe == 15u ? Float4(0.9f, 0.9f, 0.9f, 1.f) : Float4(stripe % 3u == 0u ? 0.9f : 0.f, stripe % 3u == 1u ? 0.9f : 0.f, stripe % 3u == 2u ? 0.9f : 0.f, 1.f);
            const SIMDVectorSingle stripeOffset = VectorScale(VectorSubtract(VectorSet(static_cast<f32>(stripe), 0.f, 0.f, 0.f), VectorSet(15.f, 0.f, 0.f, 0.f)), 0.8f);
            Float4 position;
            StoreFloat(VectorSetZ(VectorSetY(stripeOffset, 1.4f), -14.f), position);
            if(!CreatePanel(context, world, color, position, Float4(0.4f, 1.f, 8.f, 0.f)).valid())
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

