// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>
#include <core/perf/report.h>
#include <global/math/frame.h>
#include <global/timer.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material/material_instance.h>

#include "csg_smoke_helpers.h"
#include "framebuffer_capture.h"
#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
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

using CsgVisibleMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;
using CsgVisibleMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_CameraTargetY = 0.75f;
static constexpr f32 s_CameraStartDepth = 6.5f;
static constexpr Float2U s_CameraStart = Float2U(0.0f, s_CameraTargetY);
static constexpr f32 s_DefaultDirectionalLightPitch = -0.65f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.45f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 3.0f;
static constexpr f32 s_CubeRotationSpeed = 0.25f;
static constexpr f32 s_MaxAnimationDelta = 1.0f / 15.0f;
static constexpr f32 s_ShapeGridCenterY = s_CameraTargetY;
static constexpr Float2U s_ShapeGridHalfSpacing = Float2U(1.55f, 1.05f);
static constexpr f32 s_ReceiverBaseScale = 0.72f;
static constexpr f32 s_ReceiverScale = 1.08f;
static constexpr f32 s_CutterScale = s_ReceiverScale / s_ReceiverBaseScale;
static constexpr usize s_CsgVisibleShapeCount = 4u;
static constexpr CsgVisibleMeshRef s_CubeMesh{"project/meshes/cube_hard_edges"};
static constexpr CsgVisibleMaterialRef s_SolidMaterial{"project/smoke/csg_visible/materials/solid"};
static constexpr CsgVisibleMaterialRef s_IndirectMaterial{"project/smoke/csg_visible/materials/indirect"};
static constexpr CsgVisibleMaterialRef s_DirectMaterial{"project/smoke/csg_visible/materials/direct"};
static constexpr CsgVisibleMaterialRef s_NormalMaterial{"project/smoke/csg_visible/materials/normal"};
static constexpr CsgVisibleMaterialRef s_PositionMaterial{"project/smoke/csg_visible/materials/position"};
static constexpr CsgVisibleMaterialRef s_CapStateMaterial{"project/smoke/csg_visible/materials/cap_state"};
static constexpr CsgVisibleMaterialRef s_IntervalStateMaterial{"project/smoke/csg_visible/materials/interval_state"};
static constexpr CsgVisibleMaterialRef s_EventStateMaterial{"project/smoke/csg_visible/materials/event_state"};
static constexpr CsgVisibleMaterialRef s_EventDataMaterial{"project/smoke/csg_visible/materials/event_data"};
static constexpr CsgVisibleMaterialRef s_EventOrderMaterial{"project/smoke/csg_visible/materials/event_order"};
static constexpr CsgVisibleMaterialRef s_SpanStateMaterial{"project/smoke/csg_visible/materials/span_state"};
static constexpr f32 s_OverlappingReceiverDepthOffset = 0.125f;
static constexpr u32 s_TemporalSampleCount = 32u;
static constexpr u64 s_TemporalMinimumFrame = 360u;
static constexpr f64 s_TemporalWarmupSeconds = 30.0;
static constexpr u64 s_TemporalFrameInterval = 8u;
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

[[nodiscard]] static TStringView CsgVisibleFpsLabel(){
    return NWB_TEXT("CsgVisibleSmokeProject");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static NWB::Core::ECS::EntityID CreateSolidCubeEntity(
    NWB::Core::ECS::World& world,
    NWB::Core::Alloc::GlobalArena& arena,
    const Name receiverGroup,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    const bool csgReceiver,
    const CsgVisibleMaterialRef& material
){
    const NWB::Core::ECS::EntityID entity = CreateTintedStaticMeshEntity(
        world,
        arena,
        s_CubeMesh,
        material,
        s_SmokeSurfaceMaterialInterface,
        colorTint,
        position,
        scale
    );
    if(!entity.valid())
        return NWB::Core::ECS::s_InvalidEntityId;

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

    StoreFloat(rotation, transform->rotation);
}

[[nodiscard]] static Float4 CsgVisibleShapePosition(const usize shapeSlot){
    switch(shapeSlot){
    case CsgVisibleShapeSlot::Plane: return Float4(-s_ShapeGridHalfSpacing.x, s_ShapeGridCenterY + s_ShapeGridHalfSpacing.y, 0.0f, 0.0f);
    case CsgVisibleShapeSlot::Box: return Float4(s_ShapeGridHalfSpacing.x, s_ShapeGridCenterY + s_ShapeGridHalfSpacing.y, 0.0f, 0.0f);
    case CsgVisibleShapeSlot::Sphere: return Float4(-s_ShapeGridHalfSpacing.x, s_ShapeGridCenterY - s_ShapeGridHalfSpacing.y, 0.0f, 0.0f);
    case CsgVisibleShapeSlot::Capsule: return Float4(s_ShapeGridHalfSpacing.x, s_ShapeGridCenterY - s_ShapeGridHalfSpacing.y, 0.0f, 0.0f);
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

[[nodiscard]] static SIMDVector CsgVisibleCutterLocalOffset(const usize shapeSlot){
    switch(shapeSlot){
    case CsgVisibleShapeSlot::Plane: return VectorZero();
    case CsgVisibleShapeSlot::Box: return VectorSet(0.0f, 0.0f, -0.30f * s_CutterScale, 0.0f);
    case CsgVisibleShapeSlot::Sphere: return VectorSet(0.0f, 0.0f, -0.32f * s_CutterScale, 0.0f);
    case CsgVisibleShapeSlot::Capsule: return VectorSet(0.0f, 0.0f, -0.32f * s_CutterScale, 0.0f);
    default: return VectorZero();
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
    static NotNullUniquePtr<NWB::Core::ECS::World> CreateWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.cpuTasks);
        if(!world){
            NWB_LOGGER_FATAL(NWB_TEXT("CsgVisibleSmokeProject initialization failed: ECS world allocation failed"));
            throw RuntimeException("CsgVisibleSmokeProject initialization failed");
        }
        if(!context.shaderPathResolver){
            NWB_LOGGER_FATAL(NWB_TEXT("CsgVisibleSmokeProject initialization failed: shader path resolver callback is null"));
            throw RuntimeException("CsgVisibleSmokeProject initialization failed");
        }

        if(context.graphics.queryFeatureSupport(NWB::Core::Feature::Meshlets)){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: natural native mesh-shader route selected"));
        }else{
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("CsgVisibleSmokeProject: natural indexed route selected because Meshlets are unavailable")
            );
        }

        auto& renderer = AddSmokeRenderSystems(*world, context);
        if(NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_CSG_GI_TEMPORAL")){
            NWB::Impl::ReflectionSettings reflection;
            reflection.traceMode = NWB::Impl::ReflectionTraceMode::Disabled;
            NWB_FATAL_ASSERT_MSG(renderer.setReflectionSettings(reflection), NWB_TEXT("Invalid temporal reflection settings"));
            renderer.setFrameLaggedAsyncLightingEnabled(
                NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_CSG_GI_TEMPORAL_LAGGED")
            );
        }

        return MakeNotNullUnique(Move(world));
    }

    [[nodiscard]] static bool ShouldCapture(void* const context, const u64 frame)noexcept{
        return frame >= static_cast<CsgVisibleSmokeProject*>(context)->m_nextCaptureFrame;
    }


private:
    void destroyWorld(){
        if(m_capture){
            m_capture->stop();
            m_capture.reset();
        }
        DestroySmokeRenderWorld(m_context, m_world);
    }

    [[nodiscard]] bool startTemporalCapture(){
        const auto path = m_temporalSample + 1u == s_TemporalSampleCount
            ? NWB::Tests::Smoke::SmokeEnvironmentString(m_temporalOutput, m_context.objectArena)
            : StringFormat(m_context.objectArena, "{}.{}.bmp", m_temporalOutput, m_temporalSample)
        ;
        const NWB::Tests::Smoke::FramebufferCaptureOptions options{
            .shouldCapture = ShouldCapture,
            .predicateContext = this,
            .requiredWidth = 1280u,
            .requiredHeight = 900u,
            .quitWhenReady = false,
        };
        m_capture = MakeUnique<NWB::Tests::Smoke::FramebufferCapture>(m_context, AStringView(path), 1u, options);
        return m_capture && m_capture->start();
    }

    void updateTemporalCapture(){
        if(m_temporalSample == s_TemporalSampleCount)
            return;
        if(!m_temporalWarmupStarted){
            const u64 frame = m_context.graphics.getFrameIndex();
            if(frame < s_TemporalMinimumFrame)
                return;
            m_temporalWarmupStart = TimerNow();
            m_temporalWarmupStarted = true;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: temporal warmup started graphics frame {}"), frame);
            return;
        }
        if(!m_capture){
            const f64 elapsed = DurationInSeconds<f64>(TimerNow(), m_temporalWarmupStart);
            if(elapsed < s_TemporalWarmupSeconds)
                return;
            m_nextCaptureFrame = m_context.graphics.getFrameIndex();
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: temporal warmup complete elapsed_seconds={:.6f} first_frame={}")
                , elapsed
                , m_nextCaptureFrame
            );
            if(!startTemporalCapture())
                m_context.requestQuit();
            return;
        }
        // Consecutive captures complete the previous readback before reusing the single observer.
        if(m_temporalFrameInterval == 1u && m_capture->capturedGraphicsFrameIndex() != Limit<u64>::s_Max){
            if(!m_context.graphics.waitForIdle()){
                NWB_LOGGER_ERROR(NWB_TEXT("CsgVisibleSmokeProject: consecutive capture completion failed"));
                m_context.requestQuit();
                return;
            }
        }
        m_capture->update();
        if(!m_capture->captureReady())
            return;
        const u64 sourceFrame = m_capture->capturedGraphicsFrameIndex();
        if(sourceFrame != m_nextCaptureFrame){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgVisibleSmokeProject: temporal sample missed frame {} (captured {})"), m_nextCaptureFrame, sourceFrame);
            m_context.requestQuit();
            return;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: temporal sample {} graphics frame {}"), m_temporalSample, sourceFrame);
        ++m_temporalSample;
        if(m_temporalSample == s_TemporalSampleCount){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: temporal series complete samples=32"));
            m_capture->finish();
            return;
        }
        m_capture->stop();
        m_capture.reset();
        m_nextCaptureFrame += m_temporalFrameInterval;
        if(!startTemporalCapture())
            m_context.requestQuit();
    }


public:
    explicit CsgVisibleSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateWorldOrDie(context))
        , m_temporalOutput(context.objectArena)
    {}

    virtual ~CsgVisibleSmokeProject()override{
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        m_temporalEnabled = NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_CSG_GI_TEMPORAL");
        const bool overlapping = m_temporalEnabled
            && NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_CSG_GI_TEMPORAL_OVERLAPPING");
        CsgVisibleMaterialRef material = s_SolidMaterial;
        if(m_temporalEnabled){
            m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());
            const auto output = NWB::Tests::Smoke::ReadSmokeEnvironmentText(m_context.objectArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
            const auto view = NWB::Tests::Smoke::ReadSmokeEnvironmentText(m_context.objectArena, "NWB_CSG_GI_TEMPORAL_VIEW");
            if(!output || !view)
                return false;
            if(*view == "indirect")
                material = s_IndirectMaterial;
            else if(*view == "direct")
                material = s_DirectMaterial;
            else if(*view == "normal")
                material = s_NormalMaterial;
            else if(*view == "position")
                material = s_PositionMaterial;
            else if(*view == "cap_state")
                material = s_CapStateMaterial;
            else if(*view == "interval_state")
                material = s_IntervalStateMaterial;
            else if(*view == "event_state")
                material = s_EventStateMaterial;
            else if(*view == "event_data"){
                material = s_EventDataMaterial;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: event-data red stores missing-span flags divided by 33"));
            }
            else if(*view == "event_order"){
                material = s_EventOrderMaterial;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: event-order RGB stores span/flags and validated raw FRONT=1 BACK=0.5"));
            }
            else if(*view == "span_state"){
                material = s_SpanStateMaterial;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: span-state RGB stores span count, flags, raw event count divided by 33"));
            }
            else if(*view != "full")
                return false;
            m_temporalOutput = *output;
            m_temporalAnimated = NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_CSG_GI_TEMPORAL_ANIMATED");
            if(overlapping && m_temporalAnimated){
                NWB_LOGGER_ERROR(NWB_TEXT("CsgVisibleSmokeProject: overlapping receiver regression requires a static scene"));
                return false;
            }
            const auto intervalText = NWB::Tests::Smoke::ReadSmokeEnvironmentText(m_context.objectArena, "NWB_CSG_GI_TEMPORAL_INTERVAL");
            if(intervalText){
                const auto interval = ParseU64(AStringView(intervalText->data(), intervalText->size()));
                if(!interval || *interval == 0u || *interval > 64u){
                    NWB_LOGGER_ERROR(NWB_TEXT("CsgVisibleSmokeProject: temporal interval must be between 1 and 64"));
                    return false;
                }
                m_temporalFrameInterval = *interval;
            }
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: temporal scene animated={} view={} minimum_frame=360 warmup_seconds=30 interval={} samples=32")
                , static_cast<u32>(m_temporalAnimated)
                , StringConvert(*view)
                , m_temporalFrameInterval
            );
            if(m_temporalAnimated)
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: front-facing triangle pose from graphics frame"));
        }
        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(
            *m_world,
            Float4(s_CameraStart.x, s_CameraStart.y, -s_CameraStartDepth)
        );
        const auto directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(
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
            const SIMDVector cutterLocalOffset = CsgVisibleCutterLocalOffset(shapeSlot);
            const SIMDVector cutterPosition = VectorSetW(VectorAdd(receiverPositionVector, cutterLocalOffset), 0.0f);
            m_receivers[shapeSlot] = CreateSolidCubeEntity(
                *m_world,
                m_context.objectArena,
                s_CsgVisibleReceiverGroups[shapeSlot],
                CsgVisibleShapeColor(shapeSlot),
                receiverPosition,
                Float4(s_ReceiverScale, s_ReceiverScale, s_ReceiverScale, 0.0f),
                true,
                material
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

        bool allEntitiesValid = activeCamera.camera.valid() && directionalLight.valid();
        if(overlapping){
            Float4 receiverPosition = CsgVisibleShapePosition(CsgVisibleShapeSlot::Plane);
            receiverPosition.z += s_OverlappingReceiverDepthOffset;
            const auto receiver = CreateSolidCubeEntity(
                *m_world,
                m_context.objectArena,
                s_CsgVisibleReceiverGroups[CsgVisibleShapeSlot::Plane],
                CsgVisibleShapeColor(CsgVisibleShapeSlot::Plane),
                receiverPosition,
                Float4(s_ReceiverScale, s_ReceiverScale, s_ReceiverScale, 0.0f),
                true,
                material
            );
            allEntitiesValid = allEntitiesValid && receiver.valid();
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: overlapping plane receiver created z_offset=0.125"));
        }
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
        if(m_temporalEnabled){
            m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
            const u32 posePhase = static_cast<u32>(m_context.graphics.getFrameIndex() % 240u);
            m_animationTime = m_temporalAnimated
                ? static_cast<f32>(Min(posePhase, 240u - posePhase)) * (0.3f / 120.0f)
                : 0.0f
            ;
            updateTemporalCapture();
        }else
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
                CsgVisibleCutterLocalOffset(shapeSlot)
            );
        }

        m_world->tick(m_temporalEnabled ? 1.0f / 60.0f : safeDelta);
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Core::ECS::EntityID m_receivers[s_CsgVisibleShapeCount] = {};
    NWB::Core::ECS::EntityID m_cutters[s_CsgVisibleShapeCount] = {};
    f32 m_animationTime = 0.0f;
    Float4 m_receiverCenters[s_CsgVisibleShapeCount] = {};
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ CsgVisibleFpsLabel() };
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ CsgVisibleFpsLabel() };
    NWB::Tests::Smoke::SmokeEnvironmentString m_temporalOutput;
    UniquePtr<NWB::Tests::Smoke::FramebufferCapture> m_capture;
    Timer m_temporalWarmupStart;
    u64 m_nextCaptureFrame = 0u;
    u64 m_temporalFrameInterval = s_TemporalFrameInterval;
    u32 m_temporalSample = 0u;
    bool m_temporalEnabled = false;
    bool m_temporalAnimated = false;
    bool m_temporalWarmupStarted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


TStringView NWB::QueryProjectWindowTitle(){
    return NWB_TEXT("NWB CSG Visible Smoke");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_csg_visible_smoke::CsgVisibleSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

