// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <global/core/common/log.h>
#include <global/core/ecs/module.h>
#include <global/core/graphics/module.h>
#include <global/core/mesh/frame_math.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/kernel/module.h>
#include <impl/ecs_render/material/material_instance.h>

#include "csg_smoke_helpers.h"
#include "fps_probe.h"
#include "smoke_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_visible_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::AddSmokeRenderSystems;
using NWB::Tests::Smoke::AddStaticCsgMeshReceiver;
using NWB::Tests::Smoke::AssignCsgCutterParameters;
using NWB::Tests::Smoke::AssignCsgCutterTransform;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::DestroySmokeRenderWorld;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_CameraTargetY = 0.75f;
static constexpr f32 s_CameraStartDepth = 6.5f;
static constexpr f32 s_CameraStartX = 0.0f;
static constexpr f32 s_CameraStartY = s_CameraTargetY;
static constexpr f32 s_DefaultDirectionalLightPitch = -0.65f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.45f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 3.0f;
static constexpr f32 s_CubeRotationSpeed = 0.25f;
static constexpr f32 s_MaxAnimationDelta = 1.0f / 15.0f;
static constexpr f32 s_ShapeGridCenterY = s_CameraTargetY;
static constexpr f32 s_ShapeGridHalfSpacingX = 1.55f;
static constexpr f32 s_ShapeGridHalfSpacingY = 1.05f;
static constexpr f32 s_ReceiverBaseScale = 0.72f;
static constexpr f32 s_ReceiverScale = 1.08f;
static constexpr f32 s_CutterScale = s_ReceiverScale / s_ReceiverBaseScale;
static constexpr usize s_CsgVisibleShapeCount = 4u;
static constexpr AStringView s_CubeMeshPath = "project/meshes/cube_hard_edges";
static constexpr AStringView s_SolidMaterialPath = "project/smoke/csg_visible/materials/solid";
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";

namespace CsgVisibleShapeSlot{
enum Enum : usize{
    Plane,
    Box,
    Sphere,
    Capsule,
    Count
};
};

static_assert(CsgVisibleShapeSlot::Count == s_CsgVisibleShapeCount, "CSG visible shape table size must stay in sync");

inline constexpr Name s_CsgVisibleReceiverGroups[s_CsgVisibleShapeCount] = {
    Name("project/smoke/csg_visible/plane_receiver"),
    Name("project/smoke/csg_visible/box_receiver"),
    Name("project/smoke/csg_visible/sphere_receiver"),
    Name("project/smoke/csg_visible/capsule_receiver"),
};

[[nodiscard]] static const tchar* CsgVisibleFpsLabel(){
#if defined(NWB_CSG_VISIBLE_FORCE_MESHLET_EMULATION)
    return NWB_TEXT("CsgVisibleSmokeProject[compute-emulation]");
#else
    return NWB_TEXT("CsgVisibleSmokeProject[mesh-shader]");
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static NWB::Core::ECS::EntityID CreateSolidCubeEntity(
    NWB::Core::ECS::World& world,
    NWB::Core::Alloc::GlobalArena& arena,
    const Name receiverGroup,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    const bool csgReceiver
){
    const NWB::Core::ECS::EntityID entity = CreateTintedStaticMeshEntity(
        world,
        arena,
        s_CubeMeshPath,
        s_SolidMaterialPath,
        s_SmokeSurfaceMaterialInterface,
        colorTint,
        position,
        scale
    );
    if(!entity.valid())
        return NWB::Core::ECS::ENTITY_ID_INVALID;

    if(csgReceiver)
        AddStaticCsgMeshReceiver(world, entity, receiverGroup, true, false);

    return entity;
}

[[nodiscard]] static SIMDVector BuildCubeRotation(const f32 time, const f32 phase){
    return QuaternionRotationRollPitchYaw(time * 0.35f, time + phase, time * 0.18f);
}

static void ApplyCubeRotation(
    NWB::Core::ECS::World& world,
    const NWB::Core::ECS::EntityID entity,
    const SIMDVector rotation
){
    auto* transform = world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
    if(!transform)
        return;

    StoreFloat(rotation, &transform->rotation);
}

[[nodiscard]] static Float4 CsgVisibleShapePosition(const usize shapeSlot){
    switch(shapeSlot){
    case CsgVisibleShapeSlot::Plane: return Float4(-s_ShapeGridHalfSpacingX, s_ShapeGridCenterY + s_ShapeGridHalfSpacingY, 0.0f, 0.0f);
    case CsgVisibleShapeSlot::Box: return Float4(s_ShapeGridHalfSpacingX, s_ShapeGridCenterY + s_ShapeGridHalfSpacingY, 0.0f, 0.0f);
    case CsgVisibleShapeSlot::Sphere: return Float4(-s_ShapeGridHalfSpacingX, s_ShapeGridCenterY - s_ShapeGridHalfSpacingY, 0.0f, 0.0f);
    case CsgVisibleShapeSlot::Capsule: return Float4(s_ShapeGridHalfSpacingX, s_ShapeGridCenterY - s_ShapeGridHalfSpacingY, 0.0f, 0.0f);
    default: return Float4(0.0f, s_CameraTargetY, 0.0f, 0.0f);
    }
}

[[nodiscard]] static Float4 CsgVisibleShapeColor(const usize shapeSlot){
    switch(shapeSlot){
    case CsgVisibleShapeSlot::Plane: return Float4(0.24f, 0.56f, 0.86f, 1.0f);
    case CsgVisibleShapeSlot::Box: return Float4(0.72f, 0.40f, 0.20f, 1.0f);
    case CsgVisibleShapeSlot::Sphere: return Float4(0.34f, 0.70f, 0.42f, 1.0f);
    case CsgVisibleShapeSlot::Capsule: return Float4(0.84f, 0.28f, 0.44f, 1.0f);
    default: return Float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

[[nodiscard]] static Float4 CsgVisibleCutterLocalOffset(const usize shapeSlot){
    switch(shapeSlot){
    case CsgVisibleShapeSlot::Plane: return Float4(0.0f, 0.0f, 0.0f, 0.0f);
    case CsgVisibleShapeSlot::Box: return Float4(0.0f, 0.0f, -0.30f * s_CutterScale, 0.0f);
    case CsgVisibleShapeSlot::Sphere: return Float4(0.0f, 0.0f, -0.32f * s_CutterScale, 0.0f);
    case CsgVisibleShapeSlot::Capsule: return Float4(0.0f, 0.0f, -0.32f * s_CutterScale, 0.0f);
    default: return Float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
}

static void ApplyCutterTransform(
    NWB::Core::ECS::World& world,
    const NWB::Core::ECS::EntityID cutterEntity,
    const SIMDVector receiverCenter,
    const SIMDVector receiverRotation,
    const SIMDVector cutterLocalOffset
){
    auto* cutter = world.tryGetComponent<NWB::Impl::CsgCutterComponent>(cutterEntity);
    if(!cutter)
        return;

    const SIMDVector cutterCenter = VectorAdd(receiverCenter, Vector3Rotate(cutterLocalOffset, receiverRotation));
    AssignCsgCutterTransform(*cutter, cutterCenter, receiverRotation);
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateCutter(
    NWB::Core::ECS::World& world,
    NWB::Core::Alloc::GlobalArena& arena,
    const usize shapeSlot,
    const Name receiverGroup,
    const SIMDVector center
){
    auto cutterEntity = world.createEntity();
    auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(arena);
    cutter.receiverGroup = receiverGroup;
    AssignCsgCutterTransform(cutter, center, QuaternionIdentity());

    switch(shapeSlot){
    case CsgVisibleShapeSlot::Plane:{
        cutter.shapeType = Name("engine/csg/plane");
        NWB::Impl::CsgPlaneShapeParameters parameters;
        parameters.normalDistance = Float4(0.0f, 0.0f, 1.0f, 0.0f);
        AssignCsgCutterParameters(cutter, parameters);
        break;
    }
    case CsgVisibleShapeSlot::Box:{
        cutter.shapeType = Name("engine/csg/box");
        NWB::Impl::CsgBoxShapeParameters parameters;
        parameters.halfExtents = Float4(0.30f * s_CutterScale, 0.30f * s_CutterScale, 0.56f * s_CutterScale, 0.0f);
        AssignCsgCutterParameters(cutter, parameters);
        break;
    }
    case CsgVisibleShapeSlot::Sphere:{
        cutter.shapeType = Name("engine/csg/sphere");
        NWB::Impl::CsgSphereShapeParameters parameters;
        parameters.radius = Float4(0.34f * s_CutterScale, 0.0f, 0.0f, 0.0f);
        AssignCsgCutterParameters(cutter, parameters);
        break;
    }
    case CsgVisibleShapeSlot::Capsule:{
        cutter.shapeType = Name("engine/csg/capsule");
        NWB::Impl::CsgCapsuleShapeParameters parameters;
        parameters.radiusHalfHeight = Float4(0.22f * s_CutterScale, 0.28f * s_CutterScale, 0.0f, 0.0f);
        AssignCsgCutterParameters(cutter, parameters);
        break;
    }
    default:
        cutter.active = false;
        break;
    }
    return cutterEntity.id();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgVisibleSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static void applyFeatureOverrides(NWB::ProjectRuntimeContext& context){
#if defined(NWB_CSG_VISIBLE_FORCE_MESHLET_EMULATION) && !defined(NWB_FINAL)
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::Meshlets, true);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: forced Meshlets feature off for compute-emulation smoke"));
#else
        static_cast<void>(context);
#endif
    }

    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        applyFeatureOverrides(context);

        auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.threadPool);
        if(!world){
            NWB_LOGGER_FATAL(NWB_TEXT("CsgVisibleSmokeProject initialization failed: ECS world allocation failed"));
            throw RuntimeException("CsgVisibleSmokeProject initialization failed");
        }
        if(!context.shaderPathResolver){
            NWB_LOGGER_FATAL(NWB_TEXT("CsgVisibleSmokeProject initialization failed: shader path resolver callback is null"));
            throw RuntimeException("CsgVisibleSmokeProject initialization failed");
        }

        AddSmokeRenderSystems(*world, context);
        if(!world->getSystem<NWB::Impl::MeshSystem>()){
            NWB_LOGGER_FATAL(NWB_TEXT("CsgVisibleSmokeProject initialization failed: mesh system is missing"));
            throw RuntimeException("CsgVisibleSmokeProject initialization failed");
        }
        if(!world->getSystem<NWB::Impl::RendererSystem>()){
            NWB_LOGGER_FATAL(NWB_TEXT("CsgVisibleSmokeProject initialization failed: renderer system is missing"));
            throw RuntimeException("CsgVisibleSmokeProject initialization failed");
        }

        return MakeNotNullUnique(Move(world));
    }

    void destroyWorld(){
        DestroySmokeRenderWorld(m_context, m_world);
#if defined(NWB_CSG_VISIBLE_FORCE_MESHLET_EMULATION) && !defined(NWB_FINAL)
        m_context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::Meshlets, false);
#endif
    }


public:
    explicit CsgVisibleSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
    {}

    virtual ~CsgVisibleSmokeProject()override{
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(
            *m_world,
            Float4(s_CameraStartX, s_CameraStartY, -s_CameraStartDepth)
        );
        NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DefaultDirectionalLightPitch,
            s_DefaultDirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DefaultDirectionalLightIntensity
        );

        for(usize shapeSlot = 0u; shapeSlot < s_CsgVisibleShapeCount; ++shapeSlot){
            const Float4 receiverPosition = CsgVisibleShapePosition(shapeSlot);
            const SIMDVector receiverPositionVector = LoadFloat(receiverPosition);
            const SIMDVector cutterLocalOffset = LoadFloat(CsgVisibleCutterLocalOffset(shapeSlot));
            const SIMDVector cutterPosition = VectorSetW(VectorAdd(receiverPositionVector, cutterLocalOffset), 0.0f);
            m_receivers[shapeSlot] = CreateSolidCubeEntity(
                *m_world,
                m_context.objectArena,
                s_CsgVisibleReceiverGroups[shapeSlot],
                CsgVisibleShapeColor(shapeSlot),
                receiverPosition,
                Float4(s_ReceiverScale, s_ReceiverScale, s_ReceiverScale, 0.0f),
                true
            );
            m_cutters[shapeSlot] = CreateCutter(
                *m_world,
                m_context.objectArena,
                shapeSlot,
                s_CsgVisibleReceiverGroups[shapeSlot],
                cutterPosition
            );
            m_receiverCenters[shapeSlot] = receiverPosition;
        }

        bool allEntitiesValid = activeCamera.camera.valid();
        for(usize shapeSlot = 0u; shapeSlot < s_CsgVisibleShapeCount; ++shapeSlot)
            allEntitiesValid = allEntitiesValid && m_receivers[shapeSlot].valid() && m_cutters[shapeSlot].valid();
        NWB_FATAL_ASSERT_MSG(
            allEntitiesValid,
            NWB_TEXT("CsgVisibleSmokeProject failed to create all scene entities")
        );

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: visible CSG interval receiver scene created"));
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_fpsProbe.recordFrame(safeDelta);
        m_animationTime += Min(safeDelta, s_MaxAnimationDelta) * s_CubeRotationSpeed;
        for(usize shapeSlot = 0u; shapeSlot < s_CsgVisibleShapeCount; ++shapeSlot){
            const f32 rotationPhase = static_cast<f32>(shapeSlot) * 0.18f;
            const SIMDVector rotation = BuildCubeRotation(m_animationTime, rotationPhase);
            ApplyCubeRotation(*m_world, m_receivers[shapeSlot], rotation);
            ApplyCutterTransform(
                *m_world,
                m_cutters[shapeSlot],
                LoadFloat(m_receiverCenters[shapeSlot]),
                rotation,
                LoadFloat(CsgVisibleCutterLocalOffset(shapeSlot))
            );
        }

        m_world->tick(safeDelta);
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Core::ECS::EntityID m_receivers[s_CsgVisibleShapeCount] = {};
    NWB::Core::ECS::EntityID m_cutters[s_CsgVisibleShapeCount] = {};
    Float4 m_receiverCenters[s_CsgVisibleShapeCount] = {};
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ CsgVisibleFpsLabel() };
    f32 m_animationTime = 0.0f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


const tchar* NWB::QueryProjectWindowTitle(){
#if defined(NWB_CSG_VISIBLE_FORCE_MESHLET_EMULATION)
    return NWB_TEXT("NWB CSG Visible Compute Emulation Smoke");
#else
    return NWB_TEXT("NWB CSG Visible Smoke");
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_csg_visible_smoke::CsgVisibleSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

