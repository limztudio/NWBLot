// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/mesh/frame_math.h>
#include <core/graphics/module.h>
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
#include <impl/ecs_csg/module.h>
#endif
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/kernel/module.h>
#include <impl/ecs_render/material/material_instance.h>

#include "arrow_yaw_input_handler.h"
#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
#include "csg_smoke_helpers.h"
#endif

#include <global/environment.h>   // ReadEnvironmentVariableBuffer -- the NWB_TRANSPARENT_MULTI_SPIN_SPEED diagnostic override
#include <global/math/constant.h> // s_2PI -- normalising the displayed yaw into [0, 2pi) for the title bar
#include <global/simplemath.h>    // IsFinite -- env override validation
#include <global/text_utils.h>    // ParseF32FromChars -- env float diagnostics


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_transparent_multi_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::AddSmokeRenderSystems;
using NWB::Tests::Smoke::ArrowYawInputHandler;
using NWB::Tests::Smoke::CreateSmokeCamera;
using NWB::Tests::Smoke::CreateSmokeWorldOrDie;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::DestroySmokeRenderWorld;
using NWB::Tests::Smoke::SetSmokeYawWindowTitle;
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
using NWB::Tests::Smoke::AddStaticCsgMeshReceiver;
using NWB::Tests::Smoke::AssignCsgCutterParameters;
using NWB::Tests::Smoke::AssignCsgCutterTransform;
#endif


static constexpr f32 s_CameraStartDepth = 2.2f;
static constexpr f32 s_CameraTargetY = 0.85f;
static constexpr f32 s_DefaultDirectionalLightPitch = 0.9f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.65f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 2.0f;
static constexpr f32 s_MaxAnimationDelta = 1.0f / 30.0f;
// Manual arrow-key scrub speed (radians/second). Slow enough that a brief tap nudges the yaw finely, while holding a
// key still sweeps a full turn in a few seconds -- enough control to park on the exact angle an artifact appears at.
static constexpr f32 s_ManualYawSpeed = 0.6f;
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
static constexpr AStringView s_TransparentShapeMeshPath = "project/meshes/caustic_sphere";
#else
// Three DISTINCT spinning glass refractors (left/center/right): a cylinder, an octahedron, and a cone. The cylinder
// + cone have smooth curved silhouettes while the octahedron is faceted, giving the transparent-shadow test a mix of
// curved and hard-edged tinted occlusion without enabling the additive caustic photon pass.
static constexpr AStringView s_TransparentLeftMeshPath = "project/meshes/cylinder";
static constexpr AStringView s_TransparentCenterMeshPath = "project/meshes/octahedron";
static constexpr AStringView s_TransparentRightMeshPath = "project/meshes/cone";
#endif
// Scene rotation. The plain transparent-shadow scene spins for overlap inspection; the caustic sphere stays static so
// its focused photon result is easy to inspect.
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
static constexpr f32 s_TransparentSceneRotationSpeed = 0.0f;
#else
static constexpr f32 s_TransparentSceneRotationSpeed = 0.55f;
#endif
static constexpr AStringView s_ShadowPlaneMeshPath = "project/meshes/shadow_plane";
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";
static constexpr AStringView s_TransparentSharedMaterialPath = "project/smoke/transparent_multi/materials/shared";
static constexpr AStringView s_GroundMaterialPath = "project/smoke/transparent_multi/materials/ground";
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
static constexpr Name s_TransparentCsgReceiverGroup("project/smoke/transparent_multi/center_receiver");
#endif

[[nodiscard]] static const tchar* TransparentMultiFpsLabel(){
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    return NWB_TEXT("TransparentCsgSmokeProject");
#elif defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
    return NWB_TEXT("CausticSphereSmokeProject");
#else
    return NWB_TEXT("TransparentMultiSmokeProject");
#endif
}


[[nodiscard, maybe_unused]] static Name TransparentCenterCsgReceiverGroup(){
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    return s_TransparentCsgReceiverGroup;
#else
    return NAME_NONE;
#endif
}


[[nodiscard]] static Float4 TransparentLeftShapeBasePosition(){
    return Float4(-0.68f, s_CameraTargetY, 0.02f, 0.0f);
}

[[nodiscard]] static Float4 TransparentCenterShapeBasePosition(){
    return Float4(0.0f, s_CameraTargetY, 0.0f, 0.0f);
}

[[nodiscard]] static Float4 TransparentRightShapeBasePosition(){
    return Float4(0.68f, s_CameraTargetY, 0.04f, 0.0f);
}

// Two static-scale OPAQUE occluders, interleaved between the transparent shapes; they orbit with the same scene
// rotation so their HARD (hardware) shadows sweep across -- and overlap -- the colored transparent shadows.
[[nodiscard]] static Float4 OpaqueLeftShapeBasePosition(){
    return Float4(-0.34f, s_CameraTargetY, 0.30f, 0.0f);
}

[[nodiscard]] static Float4 OpaqueRightShapeBasePosition(){
    return Float4(0.34f, s_CameraTargetY, 0.30f, 0.0f);
}

[[nodiscard]] static SIMDVector BuildTransparentSceneRotation(const f32 time){
    return QuaternionRotationRollPitchYaw(0.0f, time, 0.0f);
}

[[nodiscard]] static SIMDVector RotateTransparentBasePosition(const SIMDVector basePosition, const SIMDVector sceneRotation){
    return Vector3Rotate(basePosition, sceneRotation);
}

static void ApplyTransparentSceneTransform(
    NWB::Core::ECS::World& world,
    const NWB::Core::ECS::EntityID entity,
    const SIMDVector basePosition,
    const SIMDVector sceneRotation,
    const SIMDVector localRotation
){
    auto* transform = world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
    if(!transform)
        return;

    StoreFloat(RotateTransparentBasePosition(basePosition, sceneRotation), &transform->position);
    StoreFloat(QuaternionNormalize(QuaternionMultiply(sceneRotation, localRotation)), &transform->rotation);
}


#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
[[nodiscard]] static SIMDVector BuildTransparentCsgRotation(const f32 time){
    return QuaternionRotationRollPitchYaw(time * 0.32f, time, time * 0.16f);
}

static void ApplyTransparentCsgSceneTransform(
    NWB::Core::ECS::World& world,
    const NWB::Core::ECS::EntityID receiverEntity,
    const NWB::Core::ECS::EntityID cutterEntity,
    const SIMDVector receiverBasePosition,
    const SIMDVector sceneRotation,
    const SIMDVector localRotation
){
    const SIMDVector receiverPosition = RotateTransparentBasePosition(receiverBasePosition, sceneRotation);
    const SIMDVector receiverRotation = QuaternionNormalize(QuaternionMultiply(sceneRotation, localRotation));

    if(auto* transform = world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(receiverEntity)){
        StoreFloat(receiverPosition, &transform->position);
        StoreFloat(receiverRotation, &transform->rotation);
    }

    if(auto* cutter = world.tryGetComponent<NWB::Impl::CsgCutterComponent>(cutterEntity))
        AssignCsgCutterTransform(*cutter, receiverPosition, receiverRotation);
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateTransparentCsgPlaneCutter(
    NWB::Core::ECS::World& world,
    NWB::Core::Alloc::GlobalArena& arena
){
    auto cutterEntity = world.createEntity();
    auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(arena);
    cutter.receiverGroup = s_TransparentCsgReceiverGroup;
    cutter.shapeType = Name("engine/csg/plane");

    NWB::Impl::CsgPlaneShapeParameters parameters;
    parameters.normalDistance = Float4(0.0f, -1.0f, 0.0f, 0.0f);
    AssignCsgCutterParameters(cutter, parameters);
    AssignCsgCutterTransform(cutter, VectorSet(0.0f, s_CameraTargetY, 0.0f, 0.0f), QuaternionIdentity());
    return cutterEntity.id();
}
#endif


[[nodiscard]] static NWB::Core::ECS::EntityID CreateTransparentStaticMeshEntity(
    NWB::Core::ECS::World& world,
    NWB::Core::Alloc::GlobalArena& arena,
    const AStringView meshPath,
    const AStringView materialPath,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    const Name csgReceiverGroup = NAME_NONE
){
    const NWB::Core::ECS::EntityID entity = CreateTintedStaticMeshEntity(
        world,
        arena,
        meshPath,
        materialPath,
        s_SmokeSurfaceMaterialInterface,
        colorTint,
        position,
        scale
    );
    if(!entity.valid())
        return NWB::Core::ECS::ENTITY_ID_INVALID;

#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    if(csgReceiverGroup)
        AddStaticCsgMeshReceiver(world, entity, csgReceiverGroup, false, true);
#else
    static_cast<void>(csgReceiverGroup);
#endif

    return entity;
}


class TransparentMultiSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(context, NWB_TEXT("TransparentMultiSmokeProject"));

        // Force ray-tracing emulation so the software shadow path runs even on RT-capable hardware; for caustic-focused
        // builds this also A/Bs the SW caustic producer against the hardware ray-traced producer.
#if defined(NWB_TRANSPARENT_MULTI_FORCE_RT_EMULATION) && !defined(NWB_FINAL)
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayTracingAccelStruct, true);
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayTracingPipeline, true);
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayQuery, true);
#endif

        AddSmokeRenderSystems(*world, context);
        if(!world->getSystem<NWB::Impl::MeshSystem>()){
            NWB_LOGGER_FATAL(NWB_TEXT("TransparentMultiSmokeProject initialization failed: mesh system is missing"));
            throw RuntimeException("TransparentMultiSmokeProject initialization failed");
        }
        if(!world->getSystem<NWB::Impl::RendererSystem>()){
            NWB_LOGGER_FATAL(NWB_TEXT("TransparentMultiSmokeProject initialization failed: renderer system is missing"));
            throw RuntimeException("TransparentMultiSmokeProject initialization failed");
        }

        return world;
    }

    void destroyWorld(){
        DestroySmokeRenderWorld(m_context, m_world);
    }


public:
    explicit TransparentMultiSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
    {}

    virtual ~TransparentMultiSmokeProject()override{
        m_context.input.removeHandler(m_arrowYawInput); // idempotent backstop if onShutdown was skipped (dispatcher outlives us)
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        // Opt into per-pass GPU timing: flips the GPU-timing double gate (perf-session sink + graphics query
        // recorder) so m_gpuPassTimingProbe can read each pass's GPU time from the timing view every frame.
        m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());

        // Arrow keys (Left/Right) drive a manual yaw scrub; the live angle is shown in the title bar so the exact
        // orientation an artifact appears at can be read off and reproduced (via NWB_TRANSPARENT_MULTI_SPIN_ANGLE).
        // The dispatcher visits handlers back-to-front, so addHandlerToBack gives this diagnostic scrubber first crack
        // at the arrow keys; it consumes only Left/Right and passes everything else through.
        m_context.input.addHandlerToBack(m_arrowYawInput);

        const NWB::Core::ECS::EntityID activeCamera = CreateSmokeCamera(*m_world, s_CameraTargetY, s_CameraStartDepth, 0.0f);
        // Shadow-check key light: a single directional source makes the transparent CSG tetrahedra cast a readable
        // tinted transmittance shadow onto the receiver plane below.
        const auto lightEntity = NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DefaultDirectionalLightPitch,
            s_DefaultDirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DefaultDirectionalLightIntensity
        );
        static_cast<void>(lightEntity);
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
        if(auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(lightEntity))
            light->enableCaustics = true;
#endif

#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
        // Single STATIC glass sphere centered above the ground. A sphere lens CONVERGES the directional light into a
        // focused caustic on the receiver (a faceted tetrahedron only deviates light), and a static scene lets the
        // temporal accumulator (P5b) average the jittered photon splat into a smooth crescent instead of a grid of dots.
        const auto centerShapeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_TransparentShapeMeshPath,
            s_TransparentSharedMaterialPath,
            Float4(0.55f, 0.78f, 1.0f, 0.30f),
            TransparentCenterShapeBasePosition(),
            Float4(0.70f, 0.70f, 0.70f)
        );
        m_centerShape = centerShapeEntity;
        const bool shapesValid = centerShapeEntity.valid();
#else
        const auto shapeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_TransparentLeftMeshPath, // cylinder
            s_TransparentSharedMaterialPath,
            Float4(1.0f, 0.42f, 0.20f, 0.42f),
            TransparentLeftShapeBasePosition(),
            Float4(0.62f, 0.62f, 0.62f)
        );
        const auto centerShapeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_TransparentCenterMeshPath, // octahedron
            s_TransparentSharedMaterialPath,
            Float4(0.10f, 1.0f, 0.45f, 0.42f),
            TransparentCenterShapeBasePosition(),
            Float4(0.78f, 0.78f, 0.78f),
            TransparentCenterCsgReceiverGroup()
        );
        const auto rightShapeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_TransparentRightMeshPath, // cone
            s_TransparentSharedMaterialPath,
            Float4(0.12f, 0.44f, 1.0f, 0.42f),
            TransparentRightShapeBasePosition(),
            Float4(0.68f, 0.68f, 0.68f)
        );
        m_leftShape = shapeEntity;
        m_centerShape = centerShapeEntity;
        m_rightShape = rightShapeEntity;

        // Two STATIC OPAQUE occluders (the octahedron + cone meshes with the OPAQUE ground material) to exercise the
        // hybrid opaque-shadow path: opaque occluders cast a HARD (binary) shadow via the hardware RayQuery pass, while
        // the spinning transparent shapes cast their colored shadow via the software pass. Placed between the
        // transparent shapes so their fixed hard shadows OVERLAP the sweeping colored shadows -- verifying the
        // multiplicative combine (opaque fully blocks: the receiver is black even under a colored tint).
        const auto opaqueLeftEntity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_TransparentCenterMeshPath, // octahedron mesh, OPAQUE ground material
            s_GroundMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.66f, 0.66f, 0.70f, 1.0f),
            OpaqueLeftShapeBasePosition(),
            Float4(0.26f, 0.26f, 0.26f)
        );
        const auto opaqueRightEntity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_TransparentRightMeshPath, // cone mesh, OPAQUE ground material
            s_GroundMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.72f, 0.68f, 0.62f, 1.0f),
            OpaqueRightShapeBasePosition(),
            Float4(0.26f, 0.26f, 0.26f)
        );
        m_opaqueLeftShape = opaqueLeftEntity;
        m_opaqueRightShape = opaqueRightEntity;

        const bool shapesValid =
            shapeEntity.valid() && centerShapeEntity.valid() && rightShapeEntity.valid()
            && opaqueLeftEntity.valid() && opaqueRightEntity.valid();
#endif

        // Opaque ground-plane receiver beneath the transparent shape(s). The colored transmittance each transparent
        // shape casts toward the directional light lands here as a tinted shadow; caustic-focused builds opt in to the
        // additive photon pass and land that result here too.
        const auto shadowPlaneEntity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_ShadowPlaneMeshPath,
            s_GroundMaterialPath,
            s_SmokeSurfaceMaterialInterface,
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
            Float4(1.0f, 1.0f, 1.0f, 1.0f),    // sphere money-shot keeps the original light ground (the validated look)
#else
            Float4(0.08f, 0.08f, 0.08f, 1.0f), // dark ground keeps the overlapping colored transparent shadows readable
#endif
            Float4(0.0f, -0.08f, 0.08f),
            Float4(1.75f, 1.0f, 1.55f)
        );
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
        const auto cutterEntity = CreateTransparentCsgPlaneCutter(*m_world, m_context.objectArena);
        const bool csgEntitiesValid = cutterEntity.valid();
        m_csgReceiver = centerShapeEntity;
        m_csgCutter = cutterEntity;
#else
        const bool csgEntitiesValid = true;
#endif
        updateTransparentSceneTransforms();
        NWB_FATAL_ASSERT_MSG(
            activeCamera.valid() && shapesValid && shadowPlaneEntity.valid() && csgEntitiesValid,
            NWB_TEXT("TransparentMultiSmokeProject failed to create all scene entities")
        );

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentMultiSmokeProject: shared transparent material with three mutable instance overrides created"));
        return true;
    }

    virtual void onShutdown()override{
        m_context.input.removeHandler(m_arrowYawInput);
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentMultiSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_fpsProbe.recordFrame(safeDelta);
        m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
        // Yaw selection, in priority order:
        //  1. NWB_TRANSPARENT_MULTI_SPIN_ANGLE env freeze -- pins one orientation for deterministic A/B captures.
        //  2. Manual arrow-key scrub -- the moment Left/Right is touched, auto-spin latches off so the user can park
        //     the scene on a precise angle (read off the title bar) to report exactly where an artifact appears.
        //  3. Auto-spin -- the default continuous rotation.
        const f32 frozenAngle = effectiveFrozenAngle();
        m_sceneYaw.update(safeDelta, frozenAngle, IsFinite(frozenAngle), m_arrowYawInput, s_ManualYawSpeed, effectiveRotationSpeed(), s_MaxAnimationDelta);
        updateTransparentSceneTransforms();
        SetSmokeYawWindowTitle(m_context, m_sceneYaw.yaw(), m_sceneYaw.manualControl(), s_2PI);
        m_world->tick(safeDelta);
        return true;
    }

    // Diagnostic override (read once): NWB_TRANSPARENT_MULTI_SPIN_SPEED replaces the compile-time rotation speed, so a
    // verification harness can sweep static (0) / slow / fast spin from a single build (e.g. to A/B the caustic motion-
    // vector reprojection across a rotation). Unset / unparseable keeps s_TransparentSceneRotationSpeed.
    static f32 effectiveRotationSpeed(){
        static const f32 s_speed = [](){
            char value[32] = {};
            if(!ReadEnvironmentVariableBuffer("NWB_TRANSPARENT_MULTI_SPIN_SPEED", value, sizeof(value)))
                return s_TransparentSceneRotationSpeed;
            f32 parsed = 0.0f;
            if(!ParseF32FromChars(value, value + NWB_STRLEN(value), parsed))
                return s_TransparentSceneRotationSpeed;
            return IsFinite(parsed) && (parsed >= 0.0f) ? parsed : s_TransparentSceneRotationSpeed;
        }();
        return s_speed;
    }

    // Diagnostic override (read once): NWB_TRANSPARENT_MULTI_SPIN_ANGLE pins the scene rotation to a fixed yaw (radians)
    // for deterministic frame-exact A/B captures. Returns a non-finite sentinel when unset so the normal spin runs.
    static f32 effectiveFrozenAngle(){
        static const f32 s_angle = [](){
            char value[32] = {};
            if(!ReadEnvironmentVariableBuffer("NWB_TRANSPARENT_MULTI_SPIN_ANGLE", value, sizeof(value)))
                return Limit<f32>::s_QuietNaN;
            f32 parsed = 0.0f;
            if(!ParseF32FromChars(value, value + NWB_STRLEN(value), parsed))
                return Limit<f32>::s_QuietNaN;
            return IsFinite(parsed) ? parsed : Limit<f32>::s_QuietNaN;
        }();
        return s_angle;
    }


private:
    void updateTransparentSceneTransforms(){
        const f32 sceneYaw = m_sceneYaw.yaw();
        const SIMDVector sceneRotation = BuildTransparentSceneRotation(sceneYaw);
        ApplyTransparentSceneTransform(*m_world, m_leftShape, LoadFloat(TransparentLeftShapeBasePosition()), sceneRotation, QuaternionIdentity());
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
        ApplyTransparentCsgSceneTransform(
            *m_world,
            m_csgReceiver,
            m_csgCutter,
            LoadFloat(TransparentCenterShapeBasePosition()),
            sceneRotation,
            BuildTransparentCsgRotation(sceneYaw)
        );
#else
        ApplyTransparentSceneTransform(*m_world, m_centerShape, LoadFloat(TransparentCenterShapeBasePosition()), sceneRotation, QuaternionIdentity());
#endif
        ApplyTransparentSceneTransform(*m_world, m_rightShape, LoadFloat(TransparentRightShapeBasePosition()), sceneRotation, QuaternionIdentity());
        // Opaque occluders orbit with the same scene rotation (no-op when invalid, e.g. the caustic-sphere build); their
        // hard hardware shadows sweep across the colored transparent shadows so the multiplicative combine is exercised
        // continuously, not just at one static overlap.
        ApplyTransparentSceneTransform(*m_world, m_opaqueLeftShape, LoadFloat(OpaqueLeftShapeBasePosition()), sceneRotation, QuaternionIdentity());
        ApplyTransparentSceneTransform(*m_world, m_opaqueRightShape, LoadFloat(OpaqueRightShapeBasePosition()), sceneRotation, QuaternionIdentity());
    }

    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ TransparentMultiFpsLabel() };
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ TransparentMultiFpsLabel() };
    NWB::Core::ECS::EntityID m_leftShape = {};
    NWB::Core::ECS::EntityID m_centerShape = {};
    NWB::Core::ECS::EntityID m_rightShape = {};
    NWB::Core::ECS::EntityID m_opaqueLeftShape = {};
    NWB::Core::ECS::EntityID m_opaqueRightShape = {};
    NWB::Tests::Smoke::YawSpinController m_sceneYaw;
    ArrowYawInputHandler m_arrowYawInput;
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    NWB::Core::ECS::EntityID m_csgReceiver = {};
    NWB::Core::ECS::EntityID m_csgCutter = {};
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


const tchar* NWB::QueryProjectWindowTitle(){
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    return NWB_TEXT("NWB Transparent CSG Smoke");
#elif defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
    return NWB_TEXT("NWB Caustic Sphere Smoke");
#else
    return NWB_TEXT("NWB Transparent Multi Smoke");
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_transparent_multi_smoke::TransparentMultiSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

