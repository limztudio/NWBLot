// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "transparent_multi_scene.h"
#include "smoke_scene_helpers.h"
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
#include "csg_smoke_helpers.h"
#endif

#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_render/module.h>
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
#include <impl/ecs_csg/module.h>
#endif

#include <core/common/log.h>
#include <core/ecs/world.h>

#include <global/assert.h>
#include <global/simd/frame.h>
#include <global/simd/constant.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_transparent_multi_scene{

using Tests::Smoke::CreateSmokeCamera;
using Tests::Smoke::CreateTintedStaticMeshEntity;
using Tests::Smoke::ReadSmokeEnvironmentF32;
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
using Tests::Smoke::AddStaticCsgMeshReceiver;
using Tests::Smoke::AssignCsgCutterParameters;
using Tests::Smoke::AssignCsgCutterTransform;
#endif

using TransparentMeshRef = Core::Assets::AssetRef<Impl::Mesh>;
using TransparentMaterialRef = Core::Assets::AssetRef<Impl::Material>;

#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
static constexpr TransparentMeshRef s_TransparentShapeMesh{"project/meshes/caustic_sphere"};
#else
static constexpr TransparentMeshRef s_TransparentLeftMesh{"project/meshes/cylinder"};
static constexpr TransparentMeshRef s_TransparentCenterMesh{"project/meshes/octahedron"};
static constexpr TransparentMeshRef s_TransparentRightMesh{"project/meshes/cone"};
#endif
static constexpr TransparentMeshRef s_ShadowPlaneMesh{"project/meshes/shadow_plane"};
static constexpr TransparentMaterialRef s_TransparentSharedMaterial{"project/smoke/transparent_multi/materials/shared"};
static constexpr TransparentMaterialRef s_GroundMaterial{"project/smoke/transparent_multi/materials/ground"};
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
static constexpr TransparentMaterialRef s_TransparentCsgMaterial{"project/smoke/transparent_multi/materials/csg_center"};
static constexpr Name s_TransparentCsgReceiverGroup = NWB::Tests::Smoke::s_SmokeNameProjectSmokeTransparentMultiCenterReceiver;
#endif

#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
[[nodiscard]] static b8 CreateCausticRefractionBackdrop(NWB::Core::ECS::World& world){
    constexpr TransparentMaterialRef s_BackdropMaterial{"project/smoke/refraction/materials/opaque"};
    // Place unlit colored stripes behind the sphere for screen-space refraction.
    for(u32 stripe = 0u; stripe < 24u; ++stripe){
        const f32 value = (stripe & 1u) != 0u ? 0.92f : 0.04f;
        const f32 x = static_cast<f32>(stripe) * 0.2f - 2.3f;
        const auto entity = CreateTintedStaticMeshEntity(s_ShadowPlaneMesh, s_BackdropMaterial, s_SmokeSurfaceMaterialInterface, Float4(value, value, value, 1.0f), Float4(x, TransparentMultiSceneTransforms::s_CameraTargetY, 2.2f, 0.0f), Float4(0.1f, 1.0f, 1.3f, 0.0f), world);
        if(!entity.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("CausticSphereSmokeProject: failed to create screen refraction backdrop"));
            return false;
        }
        auto& transform = world.entity(entity).getComponent<NWB::Impl::Scene::TransformComponent>();
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.0f, 0.0f), transform.rotation);
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CausticSphereSmokeProject: screen refraction striped backdrop created (24 opaque strips)"));
    return true;
}
#endif


[[nodiscard, maybe_unused]] static Name TransparentCenterCsgReceiverGroup()noexcept{
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    return s_TransparentCsgReceiverGroup;
#else
    return s_NameNone;
#endif
}

#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
[[nodiscard]] static NWB::Core::ECS::EntityID CreateTransparentCsgPlaneCutter(NWB::Core::ECS::World& world){
    auto cutterEntity = world.createEntity();
    auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>();
    cutter.receiverGroup = s_TransparentCsgReceiverGroup;
    cutter.shapeType = Name(NWB::Impl::s_CsgPlaneShapeName);
    cutter.active = !NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_TRANSPARENT_CSG_DISABLE_CUTTER");
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentCsgSmokeProject: cutter active {}"), cutter.active);

    NWB::Impl::CsgPlaneShapeParameters parameters;
    parameters.normalDistance = Float4(0.0f, -1.0f, 0.0f, 0.0f);
    AssignCsgCutterParameters(parameters, cutter);
    AssignCsgCutterTransform(VectorSet(0.0f, TransparentMultiSceneTransforms::s_CameraTargetY, 0.0f, 0.0f), QuaternionIdentity(), cutter);
    return cutterEntity.id();
}
#endif

[[nodiscard]] static NWB::Core::ECS::EntityID CreateTransparentStaticMeshEntity(
    const TransparentMeshRef& mesh,
    const TransparentMaterialRef& material,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    NWB::Core::ECS::World& world,
    [[maybe_unused]] const Name csgReceiverGroup = s_NameNone
){
    const NWB::Core::ECS::EntityID entity = CreateTintedStaticMeshEntity(mesh, material, s_SmokeSurfaceMaterialInterface, colorTint, position, scale, world);
    if(!entity.valid())
        return NWB::Core::ECS::s_InvalidEntityId;

#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    if(csgReceiverGroup)
        AddStaticCsgMeshReceiver(entity, csgReceiverGroup, false, true, world);
#endif

    return entity;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TransparentMultiScene::Populate(const f32 cameraDistance, [[maybe_unused]] const b8 causticTiming, const f32 yaw, Core::ECS::World& world, TransparentMultiSceneTransforms& transforms){
    using namespace __hidden_transparent_multi_scene;
    const NWB::Core::ECS::EntityID activeCamera = CreateSmokeCamera(TransparentMultiSceneTransforms::s_CameraTargetY, cameraDistance, 0.0f, world);
    const auto lightEntity = NWB::Impl::Scene::CreateDirectionalLightEntity(world, s_DefaultDirectionalLightPitch, s_DefaultDirectionalLightYaw, 0.0f, Float4(1.0f, 0.96f, 0.88f), s_DefaultDirectionalLightIntensity);
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
    const auto causticSetting = ReadSmokeEnvironmentF32("NWB_CAUSTIC_SMOKE_ENABLED");
    const b8 causticsEnabled = !causticSetting || *causticSetting != 0.0f;
    world.entity(lightEntity).getComponent<NWB::Impl::Scene::LightComponent>().enableCaustics = causticsEnabled;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CausticSphereSmokeProject: caustics {}"), causticsEnabled ? NWB_TEXT("enabled") : NWB_TEXT("disabled"));
#else
    world.entity(lightEntity).getComponent<NWB::Impl::Scene::LightComponent>().enableCaustics = false;
#endif

#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
    // A static sphere lens lets temporal photon accumulation converge without motion artifacts.
    const auto centerShapeEntity = CreateTransparentStaticMeshEntity(
        s_TransparentShapeMesh,
        s_TransparentSharedMaterial,
        Float4(0.55f, 0.78f, 1.0f, 0.30f),
        TransparentMultiSceneTransforms::s_TransparentCenterShapeBasePosition,
        Float4(s_CausticSphereScale, s_CausticSphereScale, s_CausticSphereScale),
        world
    );
    transforms.centerShape = centerShapeEntity;
    const b8 shapesValid = centerShapeEntity.valid();
    if(shapesValid)
        world.entity(centerShapeEntity).getComponent<NWB::Impl::RendererComponent>().opticalBoundaryMode = NWB::Impl::OpticalBoundaryMode::ClosedNested;
#else
    const auto shapeEntity = CreateTransparentStaticMeshEntity(s_TransparentLeftMesh, s_TransparentSharedMaterial, Float4(1.0f, 0.42f, 0.20f, 0.42f), TransparentMultiSceneTransforms::s_TransparentLeftShapeBasePosition, Float4(0.62f, 0.62f, 0.62f), world);
    const auto centerShapeEntity = CreateTransparentStaticMeshEntity(
        s_TransparentCenterMesh,
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
        // Unlit saturated color keeps clipping visible independently of facet lighting while retaining transparency.
        s_TransparentCsgMaterial,
        Float4(0.02f, 1.0f, 0.05f, 0.60f),
#else
        s_TransparentSharedMaterial,
        Float4(0.10f, 1.0f, 0.45f, 0.42f),
#endif
        TransparentMultiSceneTransforms::s_TransparentCenterShapeBasePosition, Float4(0.78f, 0.78f, 0.78f), world, TransparentCenterCsgReceiverGroup()
    );
    const auto rightShapeEntity = CreateTransparentStaticMeshEntity(s_TransparentRightMesh, s_TransparentSharedMaterial, Float4(0.12f, 0.44f, 1.0f, 0.42f), TransparentMultiSceneTransforms::s_TransparentRightShapeBasePosition, Float4(0.68f, 0.68f, 0.68f), world);
    transforms.leftShape = shapeEntity;
    transforms.centerShape = centerShapeEntity;
    transforms.rightShape = rightShapeEntity;

    // Static opaque occluders cast hard shadows across the sweeping tinted shadows.
    const auto opaqueLeftEntity = CreateTintedStaticMeshEntity(s_TransparentCenterMesh, s_GroundMaterial, s_SmokeSurfaceMaterialInterface, Float4(0.66f, 0.66f, 0.70f, 1.0f), TransparentMultiSceneTransforms::s_OpaqueLeftShapeBasePosition, Float4(0.26f, 0.26f, 0.26f), world);
    const auto opaqueRightEntity = CreateTintedStaticMeshEntity(s_TransparentRightMesh, s_GroundMaterial, s_SmokeSurfaceMaterialInterface, Float4(0.72f, 0.68f, 0.62f, 1.0f), TransparentMultiSceneTransforms::s_OpaqueRightShapeBasePosition, Float4(0.26f, 0.26f, 0.26f), world);
    transforms.opaqueLeftShape = opaqueLeftEntity;
    transforms.opaqueRightShape = opaqueRightEntity;

    const b8 shapesValid = shapeEntity.valid() && centerShapeEntity.valid() && rightShapeEntity.valid() && opaqueLeftEntity.valid() && opaqueRightEntity.valid();
#endif

    // The ground receives tinted shadows and, in caustic builds, additive photon irradiance.
    const auto shadowPlaneEntity = CreateTintedStaticMeshEntity(
        s_ShadowPlaneMesh,
        s_GroundMaterial,
        s_SmokeSurfaceMaterialInterface,
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
        Float4(1.0f, 1.0f, 1.0f, 1.0f), // Light ground preserves the caustic baseline.
#else
        Float4(0.08f, 0.08f, 0.08f, 1.0f), // dark ground keeps the overlapping colored transparent shadows readable
#endif
        Float4(0.0f, -0.08f, 0.08f), Float4(1.75f, 1.0f, 1.55f), world
    );
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
    if(NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_CAUSTIC_SMOKE_SCREEN_REFRACTION_BACKDROP") && !CreateCausticRefractionBackdrop(world))
        return false;
#endif
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    const auto cutterEntity = CreateTransparentCsgPlaneCutter(world);
    const b8 csgEntitiesValid = cutterEntity.valid();
    transforms.csgReceiver = centerShapeEntity;
    transforms.csgCutter = cutterEntity;
#else
    const b8 csgEntitiesValid = true;
#endif
    transforms.update(yaw, world);
    NWB_FATAL_ASSERT_MSG(activeCamera.valid() && lightEntity.valid() && shapesValid && shadowPlaneEntity.valid() && csgEntitiesValid, NWB_TEXT("TransparentMultiSmokeProject failed to create all scene entities"));

#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
    if(causticTiming){
        const auto& camera = world.entity(activeCamera).getComponent<NWB::Impl::Scene::CameraComponent>();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CausticTimingProbe: vertical FOV radians {}"), camera.verticalFovRadians());
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CausticTimingProbe: scene single-static-sphere-ground-v1"));
    }
#endif
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentMultiSmokeProject: shared transparent material with three mutable instance overrides created"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

