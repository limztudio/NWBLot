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
#include "smoke_scene_helpers.h"
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
#include "csg_smoke_helpers.h"
#endif


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
static constexpr f32 s_TransparentSceneRotationSpeed = 0.55f;
static constexpr AStringView s_CubeMeshPath = "project/meshes/cube";
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
#else
    return NWB_TEXT("TransparentMultiSmokeProject");
#endif
}


[[nodiscard]] static Name TransparentCenterCsgReceiverGroup(){
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    return s_TransparentCsgReceiverGroup;
#else
    return NAME_NONE;
#endif
}


[[nodiscard]] static Float4 TransparentLeftCubeBasePosition(){
    return Float4(-0.68f, s_CameraTargetY, 0.02f, 0.0f);
}

[[nodiscard]] static Float4 TransparentCenterCubeBasePosition(){
    return Float4(0.0f, s_CameraTargetY, 0.0f, 0.0f);
}

[[nodiscard]] static Float4 TransparentRightCubeBasePosition(){
    return Float4(0.68f, s_CameraTargetY, 0.04f, 0.0f);
}

[[nodiscard]] static SIMDVector BuildTransparentSceneRotation(const f32 time){
    return QuaternionRotationRollPitchYaw(0.0f, time, 0.0f);
}

[[nodiscard]] static SIMDVector RotateTransparentBasePosition(const Float4& basePosition, const SIMDVector sceneRotation){
    return Vector3Rotate(LoadFloat(basePosition), sceneRotation);
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
    const SIMDVector sceneRotation,
    const SIMDVector localRotation
){
    const SIMDVector receiverPosition = RotateTransparentBasePosition(TransparentCenterCubeBasePosition(), sceneRotation);
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
        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(
            *m_world,
            Float4(0.0f, s_CameraTargetY, -s_CameraStartDepth)
        );
        // Shadow-check key light: a single directional source makes the transparent CSG cubes cast a readable
        // tinted transmittance shadow onto the receiver plane below.
        NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DefaultDirectionalLightPitch,
            s_DefaultDirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DefaultDirectionalLightIntensity
        );

        const auto cubeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_CubeMeshPath,
            s_TransparentSharedMaterialPath,
            Float4(1.0f, 0.42f, 0.20f, 0.42f),
            TransparentLeftCubeBasePosition(),
            Float4(0.62f, 0.62f, 0.62f)
        );
        const auto centerCubeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_CubeMeshPath,
            s_TransparentSharedMaterialPath,
            Float4(0.10f, 1.0f, 0.45f, 0.42f),
            TransparentCenterCubeBasePosition(),
            Float4(0.78f, 0.78f, 0.78f),
            TransparentCenterCsgReceiverGroup()
        );
        const auto rightCubeEntity = CreateTransparentStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_CubeMeshPath,
            s_TransparentSharedMaterialPath,
            Float4(0.12f, 0.44f, 1.0f, 0.42f),
            TransparentRightCubeBasePosition(),
            Float4(0.68f, 0.68f, 0.68f)
        );

        // Opaque ground-plane receiver beneath the transparent cubes. The colored transmittance each transparent
        // cube casts toward the directional light lands here as a tinted
        // shadow -- the visible payoff of Phase 6 colored/transmittance shadows.
        const auto shadowPlaneEntity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_ShadowPlaneMeshPath,
            s_GroundMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            Float4(1.0f, 1.0f, 1.0f, 1.0f),
            Float4(0.0f, -0.08f, 0.08f),
            Float4(1.75f, 1.0f, 1.55f)
        );
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
        const auto cutterEntity = CreateTransparentCsgPlaneCutter(*m_world, m_context.objectArena);
        const bool csgEntitiesValid = cutterEntity.valid();
        m_csgReceiver = centerCubeEntity;
        m_csgCutter = cutterEntity;
#else
        const bool csgEntitiesValid = true;
#endif
        m_leftCube = cubeEntity;
        m_centerCube = centerCubeEntity;
        m_rightCube = rightCubeEntity;
        updateTransparentSceneTransforms();
        NWB_FATAL_ASSERT_MSG(
            activeCamera.camera.valid() && cubeEntity.valid() && centerCubeEntity.valid() && rightCubeEntity.valid() && shadowPlaneEntity.valid() && csgEntitiesValid,
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
        m_animationTime += Min(safeDelta, s_MaxAnimationDelta) * s_TransparentSceneRotationSpeed;
        updateTransparentSceneTransforms();
        m_world->tick(safeDelta);
        return true;
    }


private:
    void updateTransparentSceneTransforms(){
        const SIMDVector sceneRotation = BuildTransparentSceneRotation(m_animationTime);
        ApplyTransparentSceneTransform(*m_world, m_leftCube, TransparentLeftCubeBasePosition(), sceneRotation, QuaternionIdentity());
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
        ApplyTransparentCsgSceneTransform(*m_world, m_csgReceiver, m_csgCutter, sceneRotation, BuildTransparentCsgRotation(m_animationTime));
#else
        ApplyTransparentSceneTransform(*m_world, m_centerCube, TransparentCenterCubeBasePosition(), sceneRotation, QuaternionIdentity());
#endif
        ApplyTransparentSceneTransform(*m_world, m_rightCube, TransparentRightCubeBasePosition(), sceneRotation, QuaternionIdentity());
    }

    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ TransparentMultiFpsLabel() };
    NWB::Core::ECS::EntityID m_leftCube = {};
    NWB::Core::ECS::EntityID m_centerCube = {};
    NWB::Core::ECS::EntityID m_rightCube = {};
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
#else
    return NWB_TEXT("NWB Transparent Multi Smoke");
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_transparent_multi_smoke::TransparentMultiSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

