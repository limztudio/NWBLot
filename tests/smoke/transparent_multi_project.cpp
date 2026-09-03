// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <global/math/frame.h>
#include <core/graphics/module.h>
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
#include <impl/ecs_csg/module.h>
#endif
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material/material_instance.h>

#include "arrow_yaw_input_handler.h"
#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
#include "csg_smoke_helpers.h"
#endif

#include <global/math/constant.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_transparent_multi_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::AddSmokeRenderSystems;
using NWB::Tests::Smoke::ArrowYawInputHandler;
using NWB::Tests::Smoke::CreateSmokeCamera;
using NWB::Tests::Smoke::CreateSmokeWorldOrDie;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::DestroySmokeRenderWorld;
using NWB::Tests::Smoke::ReadSmokeEnvironmentF32;
using NWB::Tests::Smoke::RendererBaselineCaptureFreezeFrame;
using NWB::Tests::Smoke::RendererBaselineFixedDelta;
using NWB::Tests::Smoke::SetSmokeYawWindowTitle;
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
using NWB::Tests::Smoke::AddStaticCsgMeshReceiver;
using NWB::Tests::Smoke::AssignCsgCutterParameters;
using NWB::Tests::Smoke::AssignCsgCutterTransform;
#endif

using TransparentMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;
using TransparentMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;


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
static constexpr TransparentMeshRef s_TransparentShapeMesh{"project/meshes/caustic_sphere"};
#else
// Three DISTINCT spinning glass refractors (left/center/right): a cylinder, an octahedron, and a cone. The cylinder
// + cone have smooth curved silhouettes while the octahedron is faceted, giving the transparent-shadow test a mix of
// curved and hard-edged tinted occlusion without enabling the additive caustic photon pass.
static constexpr TransparentMeshRef s_TransparentLeftMesh{"project/meshes/cylinder"};
static constexpr TransparentMeshRef s_TransparentCenterMesh{"project/meshes/octahedron"};
static constexpr TransparentMeshRef s_TransparentRightMesh{"project/meshes/cone"};
#endif
// Scene rotation. The plain transparent-shadow scene spins for overlap inspection; the caustic sphere stays static so
// its focused photon result is easy to inspect.
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
static constexpr f32 s_TransparentSceneRotationSpeed = 0.0f;
#else
static constexpr f32 s_TransparentSceneRotationSpeed = 0.55f;
#endif
static constexpr TransparentMeshRef s_ShadowPlaneMesh{"project/meshes/shadow_plane"};
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";
static constexpr TransparentMaterialRef s_TransparentSharedMaterial{"project/smoke/transparent_multi/materials/shared"};
static constexpr TransparentMaterialRef s_GroundMaterial{"project/smoke/transparent_multi/materials/ground"};
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
static constexpr Name s_TransparentCsgReceiverGroup("project/smoke/transparent_multi/center_receiver");
#endif

#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
// The target-hardware runner sends F1 only after it observes an accepted history. Keeping the toggle in the smoke
// project makes the current-frame -> re-bootstrap transition deterministic without adding a renderer test control.
class FrameLaggedAsyncLightingToggleInputHandler final : public NWB::Core::IInputEventHandler{
public:
    bool keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods)override{
        static_cast<void>(scancode);
        static_cast<void>(mods);
        if(key != NWB::Core::Key::F1)
            return false;
        if(action == NWB::Core::InputAction::Press)
            m_toggleRequested = true;
        return true;
    }

    [[nodiscard]] bool consumeToggleRequest(){
        const bool requested = m_toggleRequested;
        m_toggleRequested = false;
        return requested;
    }


private:
    bool m_toggleRequested = false;
};

// Windows may deny a harness process foreground ownership even after SetForegroundWindow succeeds locally. The
// target-hardware lifecycle needs actual post-bootstrap submissions, so use Graphics' existing render-pass opt-in
// only in this dedicated smoke executable; production projects retain their normal focus throttling policy.
class FrameLaggedAsyncLightingUnfocusedPass final : public NWB::Core::IRenderPass{
public:
    explicit FrameLaggedAsyncLightingUnfocusedPass(NWB::Core::Graphics& graphics)
        : IRenderPass(graphics)
    {}


public:
    virtual bool shouldRenderUnfocused()override{ return true; }
};
#endif

[[nodiscard]] static const tchar* TransparentMultiFpsLabel(){
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
    return NWB_TEXT("FrameLaggedAsyncLightingSmokeProject");
#elif defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
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
    const TransparentMeshRef& mesh,
    const TransparentMaterialRef& material,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    const Name csgReceiverGroup = NAME_NONE
){
    const NWB::Core::ECS::EntityID entity = CreateTintedStaticMeshEntity(
        world,
        arena,
        mesh,
        material,
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
    [[nodiscard]] static u32 rendererBaselineCaptureFreezeFrame(){
        return RendererBaselineCaptureFreezeFrame();
    }

    [[nodiscard]] static f32 rendererBaselineFixedDelta(){
        return RendererBaselineFixedDelta();
    }


    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(context, NWB_TEXT("TransparentMultiSmokeProject"));

        const bool rayQueryHardwareAvailable =
            context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct)
            && context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery)
        ;
        if(rayQueryHardwareAvailable){
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("TransparentMultiSmokeProject: natural hybrid shadow route selected on RayQuery-capable hardware")
            );
        }else{
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("TransparentMultiSmokeProject: natural software-only shadow route selected because RayQuery-capable hardware is unavailable")
            );
        }

#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
        auto& rendererSystem = AddSmokeRenderSystems(*world, context);
        rendererSystem.setFrameLaggedAsyncLightingEnabled(true);
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("FrameLaggedAsyncLightingSmoke: requested frame-lagged async lighting; F1 toggles the current-frame path")
        );
#else
        AddSmokeRenderSystems(*world, context);
#endif

        return world;
    }

    void destroyWorld(){
        DestroySmokeRenderWorld(m_context, m_world);
    }

#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
    void removeFrameLaggedAsyncLightingUnfocusedPass(){
        if(!m_frameLaggedAsyncLightingUnfocusedPassRegistered)
            return;

        m_context.graphics.removeRenderPass(m_frameLaggedAsyncLightingUnfocusedPass);
        m_frameLaggedAsyncLightingUnfocusedPassRegistered = false;
    }
#endif


public:
    explicit TransparentMultiSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
    {}

    virtual ~TransparentMultiSmokeProject()override{
        m_context.input.removeHandler(m_arrowYawInput); // idempotent backstop if onShutdown was skipped (dispatcher outlives us)
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
        m_context.input.removeHandler(m_frameLaggedAsyncLightingToggleInput);
        removeFrameLaggedAsyncLightingUnfocusedPass();
#endif
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
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
        m_context.graphics.addRenderPassToBack(m_frameLaggedAsyncLightingUnfocusedPass);
        m_frameLaggedAsyncLightingUnfocusedPassRegistered = true;
        // The target-hardware harness presses F1 only after an accepted history is observed. It then proves the
        // normal current-frame path and the following bootstrap without exposing a renderer-only test switch.
        m_context.input.addHandlerToBack(m_frameLaggedAsyncLightingToggleInput);
#endif

        const NWB::Core::ECS::EntityID activeCamera = CreateSmokeCamera(*m_world, s_CameraTargetY, s_CameraStartDepth, 0.0f);
        const auto lightEntity = NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DefaultDirectionalLightPitch,
            s_DefaultDirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DefaultDirectionalLightIntensity
        );
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
        if(auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(lightEntity))
            light->enableCaustics = true;
#endif

#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
        // Single STATIC glass sphere centered above the ground. A sphere lens CONVERGES the directional light into a
        // focused caustic on the receiver (a faceted tetrahedron only deviates light), and a static scene lets the
        // temporal accumulator average the jittered photon splat into a smooth crescent instead of a grid of dots.
        const auto centerShapeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_TransparentShapeMesh,
            s_TransparentSharedMaterial,
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
            s_TransparentLeftMesh,
            s_TransparentSharedMaterial,
            Float4(1.0f, 0.42f, 0.20f, 0.42f),
            TransparentLeftShapeBasePosition(),
            Float4(0.62f, 0.62f, 0.62f)
        );
        const auto centerShapeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_TransparentCenterMesh,
            s_TransparentSharedMaterial,
            Float4(0.10f, 1.0f, 0.45f, 0.42f),
            TransparentCenterShapeBasePosition(),
            Float4(0.78f, 0.78f, 0.78f),
            TransparentCenterCsgReceiverGroup()
        );
        const auto rightShapeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_TransparentRightMesh,
            s_TransparentSharedMaterial,
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
            s_TransparentCenterMesh, // octahedron mesh, OPAQUE ground material
            s_GroundMaterial,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.66f, 0.66f, 0.70f, 1.0f),
            OpaqueLeftShapeBasePosition(),
            Float4(0.26f, 0.26f, 0.26f)
        );
        const auto opaqueRightEntity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_TransparentRightMesh, // cone mesh, OPAQUE ground material
            s_GroundMaterial,
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
            s_ShadowPlaneMesh,
            s_GroundMaterial,
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
            activeCamera.valid() && lightEntity.valid() && shapesValid && shadowPlaneEntity.valid() && csgEntitiesValid,
            NWB_TEXT("TransparentMultiSmokeProject failed to create all scene entities")
        );

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentMultiSmokeProject: shared transparent material with three mutable instance overrides created"));
        return true;
    }

    virtual void onShutdown()override{
        m_context.graphics.setFrameSubmissionSuspended(false);
        m_context.input.removeHandler(m_arrowYawInput);
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
        m_context.input.removeHandler(m_frameLaggedAsyncLightingToggleInput);
        removeFrameLaggedAsyncLightingUnfocusedPass();
#endif
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentMultiSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const u32 captureFreezeFrame = rendererBaselineCaptureFreezeFrame();
        if(captureFreezeFrame != 0u && m_rendererBaselineRenderedFrameCount >= captureFreezeFrame){
            if(!m_rendererBaselineCapturePaused){
                // The harness captures only after the last requested render submission has completed. Keeping the
                // event loop alive while submissions are suspended pins AVBOIT's temporal phase for a later image
                // comparison without adding a renderer/runtime test feature.
                m_context.graphics.setFrameSubmissionSuspended(true);
                m_rendererBaselineCapturePaused = true;
                NWB_LOGGER_ESSENTIAL_INFO(
                    NWB_TEXT("TransparentMultiSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"),
                    m_rendererBaselineRenderedFrameCount
                );
            }
            return true;
        }

        const f32 fixedDelta = rendererBaselineFixedDelta();
        const f32 safeDelta = fixedDelta > 0.0f ? fixedDelta : (IsFinite(delta) ? Max(delta, 0.0f) : 0.0f);
        m_fpsProbe.recordFrame(safeDelta);
        m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
        if(m_frameLaggedAsyncLightingToggleInput.consumeToggleRequest()){
            auto* const rendererSystem = m_world->getSystem<NWB::Impl::RendererSystem>();
            NWB_FATAL_ASSERT_MSG(rendererSystem, NWB_TEXT("FrameLaggedAsyncLightingSmokeProject renderer system disappeared"));
            m_frameLaggedAsyncLightingEnabled = !m_frameLaggedAsyncLightingEnabled;
            rendererSystem->setFrameLaggedAsyncLightingEnabled(m_frameLaggedAsyncLightingEnabled);
            if(m_frameLaggedAsyncLightingEnabled)
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("FrameLaggedAsyncLightingSmoke: F1 re-enabled frame-lagged async lighting"));
            else
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("FrameLaggedAsyncLightingSmoke: F1 requested current-frame path"));
        }
#endif
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
        ++m_rendererBaselineRenderedFrameCount;
        return true;
    }

    // Diagnostic override (read once): NWB_TRANSPARENT_MULTI_SPIN_SPEED replaces the compile-time rotation speed, so a
    // verification harness can sweep static (0) / slow / fast spin from a single build (e.g. to A/B the caustic motion-
    // vector reprojection across a rotation). Unset / unparseable keeps s_TransparentSceneRotationSpeed.
    static f32 effectiveRotationSpeed(){
        static const f32 s_speed = [](){
            f32 parsed = 0.0f;
            if(!ReadSmokeEnvironmentF32("NWB_TRANSPARENT_MULTI_SPIN_SPEED", parsed))
                return s_TransparentSceneRotationSpeed;
            return IsFinite(parsed) && (parsed >= 0.0f) ? parsed : s_TransparentSceneRotationSpeed;
        }();
        return s_speed;
    }

    // Diagnostic override (read once): NWB_TRANSPARENT_MULTI_SPIN_ANGLE pins the scene rotation to a fixed yaw (radians)
    // for deterministic frame-exact A/B captures. Returns a non-finite sentinel when unset so the normal spin runs.
    static f32 effectiveFrozenAngle(){
        static const f32 s_angle = [](){
            f32 parsed = 0.0f;
            if(!ReadSmokeEnvironmentF32("NWB_TRANSPARENT_MULTI_SPIN_ANGLE", parsed))
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
    u32 m_rendererBaselineRenderedFrameCount = 0u;
    bool m_rendererBaselineCapturePaused = false;
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
    FrameLaggedAsyncLightingToggleInputHandler m_frameLaggedAsyncLightingToggleInput;
    FrameLaggedAsyncLightingUnfocusedPass m_frameLaggedAsyncLightingUnfocusedPass{ m_context.graphics };
    bool m_frameLaggedAsyncLightingUnfocusedPassRegistered = false;
    bool m_frameLaggedAsyncLightingEnabled = true;
#endif
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
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
    return NWB_TEXT("NWB Frame Lagged Async Lighting Smoke");
#elif defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
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

