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
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material_instance.h>

#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "smoke_scene_helpers.h"
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
#include "csg_smoke_helpers.h"
#endif

#include <global/environment.h> // ReadEnvironmentVariableBuffer -- the NWB_TRANSPARENT_MULTI_SPIN_SPEED diagnostic override
#include <cstdlib>               // std::atof


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_transparent_multi_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::AddSmokeRenderSystems;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::DestroySmokeRenderWorld;
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
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
static constexpr AStringView s_TransparentShapeMeshPath = "project/meshes/caustic_sphere";
#else
// Three DISTINCT spinning glass refractors (left/center/right): a cylinder, an octahedron, and a cone. The cylinder
// + cone have SMOOTH curved-surface normals (a cylinder focuses light into a line caustic, a cone into a curved
// caustic), while the octahedron is faceted (8 flat prism faces) -- a variety of caustic behaviours vs the old
// identical tetrahedra. Generated procedurally (scratchpad gen_shapes.py) into tests/smoke/assets/meshes.
static constexpr AStringView s_TransparentLeftMeshPath = "project/meshes/cylinder";
static constexpr AStringView s_TransparentCenterMeshPath = "project/meshes/octahedron";
static constexpr AStringView s_TransparentRightMeshPath = "project/meshes/cone";
#endif
// Scene rotation. Tetrahedra (plain + CSG) SPIN; the glass sphere is STATIC (its lens focus converges via the EMA into
// the clean money-shot). NOTE on the spinning tetra caustic: a flat tetra is a PRISM -- spinning sweeps it through
// steep orientations that DEVIATE the caustic away from the shadow (a prism separates caustic+shadow; a lens keeps them
// together). This residual displacement is correct prism PHYSICS and cannot be removed without freezing the prism.
// Two SAMPLING artifacts on top of it were diagnosed + fixed: (1) the diagonal-stripe artifact (dense-photon sampling)
// via the hash-decorrelated per-target emission in caustic_photon_sw_cs.slang; (2) the far bright patches FLUNG across
// the receiver, caused by photons chaining through the 3 overlapping tetrahedra (each crossing compounds the prism
// deviation) -- bounded by the caustic specular-depth cap (NWB_CAUSTIC_MAX_BOUNCES in caustic_trace.slangi), which
// keeps each tetra's caustic local. Kept spinning for inspection; freeze (rotation 0) for the cleanest converged look.
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

[[nodiscard]] static SIMDVector BuildTransparentSceneRotation(const f32 time){
    return QuaternionRotationRollPitchYaw(0.0f, time, 0.0f);
}

[[nodiscard]] static SIMDVector RotateTransparentBasePosition(const SIMDVector basePosition, const SIMDVector sceneRotation){
    return Vector3Rotate(basePosition, sceneRotation);
}

static void ApplyTransparentSceneTransform(
    NWB::Core::ECS::World& world,
    const NWB::Core::ECS::EntityID entity,
    const Float4& basePosition,
    const SIMDVector sceneRotation,
    const SIMDVector localRotation
){
    auto* transform = world.tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
    if(!transform)
        return;

    StoreFloat(RotateTransparentBasePosition(LoadFloat(basePosition), sceneRotation), &transform->position);
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
    const SIMDVector sceneRotation,
    const SIMDVector localRotation
){
    const SIMDVector receiverPosition = RotateTransparentBasePosition(LoadFloat(TransparentCenterShapeBasePosition()), sceneRotation);
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
    AssignCsgCutterTransform(cutter, LoadFloat(Float4(0.0f, s_CameraTargetY, 0.0f, 0.0f)), QuaternionIdentity());
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
        auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.threadPool);
        if(!world){
            NWB_LOGGER_FATAL(NWB_TEXT("TransparentMultiSmokeProject initialization failed: ECS world allocation failed"));
            throw RuntimeException("TransparentMultiSmokeProject initialization failed");
        }
        if(!context.shaderPathResolver){
            NWB_LOGGER_FATAL(NWB_TEXT("TransparentMultiSmokeProject initialization failed: shader path resolver callback is null"));
            throw RuntimeException("TransparentMultiSmokeProject initialization failed");
        }

        // Force ray-tracing emulation so the SOFTWARE caustic producer (P3) runs even on RT-capable hardware -- the
        // knob for A/B-ing the SW path against the hardware ray-traced producer (P4). Default OFF: the demo runs the
        // HW caustic producer (the real-hardware path). Define NWB_TRANSPARENT_MULTI_FORCE_RT_EMULATION to force SW.
#if defined(NWB_TRANSPARENT_MULTI_FORCE_RT_EMULATION)
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

        return MakeNotNullUnique(Move(world));
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
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        // Opt into per-pass GPU timing: flips the GPU-timing double gate (perf-session sink + graphics query
        // recorder) so m_gpuPassTimingProbe can read each pass's GPU time from the timing view every frame.
        m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());

        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(
            *m_world,
            Float4(0.0f, s_CameraTargetY, -s_CameraStartDepth)
        );
        // Shadow-check key light: a single directional source makes the transparent CSG tetrahedra cast a readable
        // tinted transmittance shadow onto the receiver plane below.
        NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DefaultDirectionalLightPitch,
            s_DefaultDirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DefaultDirectionalLightIntensity
        );

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
        const bool shapesValid = shapeEntity.valid() && centerShapeEntity.valid() && rightShapeEntity.valid();
#endif

        // Opaque ground-plane receiver beneath the transparent shape(s). The colored transmittance each transparent
        // shape casts toward the directional light lands here as a tinted shadow (Phase 6 colored/transmittance
        // shadows); the additive refracted caustic lands here too (the producer's payoff).
        const auto shadowPlaneEntity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_ShadowPlaneMeshPath,
            s_GroundMaterialPath,
            s_SmokeSurfaceMaterialInterface,
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
            Float4(1.0f, 1.0f, 1.0f, 1.0f),    // sphere money-shot keeps the original light ground (the validated look)
#else
            Float4(0.08f, 0.08f, 0.08f, 1.0f), // dark ground for the tetra so the caustic + any stray sparkles read clearly (caustic is *baseColor in lighting.slangi, so the Reinhard tonemap separates sparkle from a dark ground better)
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
            activeCamera.camera.valid() && shapesValid && shadowPlaneEntity.valid() && csgEntitiesValid,
            NWB_TEXT("TransparentMultiSmokeProject failed to create all scene entities")
        );

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentMultiSmokeProject: shared transparent material with three mutable instance overrides created"));
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentMultiSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_fpsProbe.recordFrame(safeDelta);
        m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
        m_animationTime += Min(safeDelta, s_MaxAnimationDelta) * effectiveRotationSpeed();
        updateTransparentSceneTransforms();
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
            const f32 parsed = static_cast<f32>(::std::atof(value));
            return IsFinite(parsed) && (parsed >= 0.0f) ? parsed : s_TransparentSceneRotationSpeed;
        }();
        return s_speed;
    }


private:
    void updateTransparentSceneTransforms(){
        const SIMDVector sceneRotation = BuildTransparentSceneRotation(m_animationTime);
        ApplyTransparentSceneTransform(*m_world, m_leftShape, TransparentLeftShapeBasePosition(), sceneRotation, QuaternionIdentity());
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
        ApplyTransparentCsgSceneTransform(*m_world, m_csgReceiver, m_csgCutter, sceneRotation, BuildTransparentCsgRotation(m_animationTime));
#else
        ApplyTransparentSceneTransform(*m_world, m_centerShape, TransparentCenterShapeBasePosition(), sceneRotation, QuaternionIdentity());
#endif
        ApplyTransparentSceneTransform(*m_world, m_rightShape, TransparentRightShapeBasePosition(), sceneRotation, QuaternionIdentity());
    }

    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ TransparentMultiFpsLabel() };
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ TransparentMultiFpsLabel() };
    NWB::Core::ECS::EntityID m_leftShape = {};
    NWB::Core::ECS::EntityID m_centerShape = {};
    NWB::Core::ECS::EntityID m_rightShape = {};
    f32 m_animationTime = 0.0f;
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

