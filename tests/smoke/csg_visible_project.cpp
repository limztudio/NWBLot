// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <core/mesh/frame_math.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material_instance.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_visible_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SmokeMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;
using SmokeMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_CsgVisibleReceiverGroup("project/smoke/csg_visible/receiver");

static constexpr f32 s_CameraStartDepth = 3.0f;
static constexpr f32 s_CameraTargetY = 0.8f;
static constexpr f32 s_DefaultDirectionalLightPitch = -0.65f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.45f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 3.0f;
static constexpr f32 s_CubeRotationSpeed = 0.75f;
static constexpr f32 s_MaxAnimationDelta = 1.0f / 15.0f;
static constexpr f32 s_CsgCubeRotationPhase = 0.55f;
static constexpr Float4 s_CsgCutterLocalOffset(0.0f, 0.0f, -0.24f, 0.0f);
static constexpr AStringView s_CubeMeshPath = "project/meshes/cube_hard_edges";
static constexpr AStringView s_SolidMaterialPath = "project/smoke/csg_visible/materials/solid";
static constexpr AStringView s_BxdfSurfaceMaterialInterface = "project/shaders/transparent_multi_bxdf_surface";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ParameterT>
static void AssignCsgCutterParameters(NWB::Impl::CsgCutterComponent& cutter, const ParameterT& parameters){
    cutter.parameterBytes.resize(sizeof(ParameterT));
    NWB_MEMCPY(cutter.parameterBytes.data(), cutter.parameterBytes.size(), &parameters, sizeof(ParameterT));
}

static void AssignCsgCutterTransform(
    NWB::Impl::CsgCutterComponent& cutter,
    const SIMDVector center,
    const SIMDVector rotation
){
    const SIMDMatrix shapeToWorld = MatrixAffineTransformation(s_SIMDOne, VectorZero(), rotation, center);
    SIMDVector determinant;
    const SIMDMatrix worldToShape = MatrixInverse(&determinant, shapeToWorld);
    StoreFloat(worldToShape, &cutter.worldToShape);
    StoreFloat(shapeToWorld, &cutter.shapeToWorld);
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateSolidCubeEntity(
    NWB::Core::ECS::World& world,
    NWB::Core::Alloc::GlobalArena& arena,
    const Float4& colorTint,
    const Float4& position,
    const Float4& scale,
    const bool csgReceiver
){
    SmokeMeshRef mesh;
    mesh.virtualPath = Name(s_CubeMeshPath);
    SmokeMaterialRef material;
    material.virtualPath = Name(s_SolidMaterialPath);

    auto entity = world.createEntity();
    auto& transform = entity.addComponent<NWB::Impl::Scene::TransformComponent>();
    transform.position = position;
    transform.scale = scale;

    auto& meshComponent = entity.addComponent<NWB::Impl::MeshComponent>();
    meshComponent.mesh = mesh;

    auto& renderer = entity.addComponent<NWB::Impl::RendererComponent>();
    renderer.material = material;

    const Name materialInterface(s_BxdfSurfaceMaterialInterface);
    entity.addComponent<NWB::Impl::MaterialInstanceComponent>(arena, materialInterface);
    if(!NWB::Impl::SetMaterialMutableFloat4(
        world,
        entity.id(),
        materialInterface,
        "runtime.color_tint",
        colorTint
    ))
        return NWB::Core::ECS::ENTITY_ID_INVALID;

    if(csgReceiver){
        auto& receiver = entity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
        receiver.receiverGroup = s_CsgVisibleReceiverGroup;
        receiver.generateCaps = true;
        receiver.affectOpaquePass = true;
        receiver.affectTransparentPass = false;
    }

    return entity.id();
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

static void ApplyCapsuleCutterTransform(
    NWB::Core::ECS::World& world,
    const NWB::Core::ECS::EntityID cutterEntity,
    const SIMDVector receiverCenter,
    const SIMDVector receiverRotation
){
    auto* cutter = world.tryGetComponent<NWB::Impl::CsgCutterComponent>(cutterEntity);
    if(!cutter)
        return;

    const SIMDVector localOffset = LoadFloat(s_CsgCutterLocalOffset);
    const SIMDVector cutterCenter = VectorAdd(receiverCenter, Vector3Rotate(localOffset, receiverRotation));
    AssignCsgCutterTransform(*cutter, cutterCenter, receiverRotation);
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateCapsuleCutter(
    NWB::Core::ECS::World& world,
    NWB::Core::Alloc::GlobalArena& arena,
    const Float4& center
){
    auto cutterEntity = world.createEntity();
    auto& cutter = cutterEntity.addComponent<NWB::Impl::CsgCutterComponent>(arena);
    cutter.receiverGroup = s_CsgVisibleReceiverGroup;
    cutter.shapeType = NWB::Impl::s_CsgCapsuleShapeName;
    cutter.operation = NWB::Impl::CsgOperation::Subtract;
    cutter.active = true;
    AssignCsgCutterTransform(cutter, LoadFloat(center), QuaternionIdentity());

    NWB::Impl::CsgCapsuleShapeParameters parameters;
    parameters.radiusHalfHeight = Float4(0.24f, 0.34f, 0.0f, 0.0f);
    AssignCsgCutterParameters(cutter, parameters);
    return cutterEntity.id();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgVisibleSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.threadPool);
        if(!world){
            NWB_LOGGER_FATAL(NWB_TEXT("CsgVisibleSmokeProject initialization failed: ECS world allocation failed"));
            throw RuntimeException("CsgVisibleSmokeProject initialization failed");
        }
        if(!context.shaderPathResolver){
            NWB_LOGGER_FATAL(NWB_TEXT("CsgVisibleSmokeProject initialization failed: shader path resolver callback is null"));
            throw RuntimeException("CsgVisibleSmokeProject initialization failed");
        }

        world->addSystem<NWB::Impl::MeshSystem>(*world);
        if(!world->getSystem<NWB::Impl::MeshSystem>()){
            NWB_LOGGER_FATAL(NWB_TEXT("CsgVisibleSmokeProject initialization failed: mesh system is missing"));
            throw RuntimeException("CsgVisibleSmokeProject initialization failed");
        }

        auto& rendererSystem = world->addSystem<NWB::Impl::RendererSystem>(
            *world,
            context.graphics,
            context.assetManager,
            context.shaderPathResolver
        );
        if(!world->getSystem<NWB::Impl::RendererSystem>()){
            NWB_LOGGER_FATAL(NWB_TEXT("CsgVisibleSmokeProject initialization failed: renderer system is missing"));
            throw RuntimeException("CsgVisibleSmokeProject initialization failed");
        }

        context.graphics.addRenderPassToBack(rendererSystem);
        return MakeNotNullUnique(Move(world));
    }

    void destroyWorld(){
        if(!m_world.owner())
            return;

        auto* rendererSystem = m_world->getSystem<NWB::Impl::RendererSystem>();
        if(rendererSystem)
            m_context.graphics.removeRenderPass(*rendererSystem);

        m_context.graphics.waitAllJobs();
        if(auto* device = m_context.graphics.getDevice())
            device->waitForIdle();

        m_world->clear();
        m_world.owner().reset();
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
            Float4(0.0f, s_CameraTargetY, -s_CameraStartDepth)
        );
        NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DefaultDirectionalLightPitch,
            s_DefaultDirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DefaultDirectionalLightIntensity
        );

        const Float4 referencePosition(-0.7f, s_CameraTargetY, 0.0f, 0.0f);
        const Float4 receiverPosition(0.7f, s_CameraTargetY, 0.0f, 0.0f);
        m_referenceCube = CreateSolidCubeEntity(
            *m_world,
            m_context.objectArena,
            Float4(0.32f, 0.75f, 1.0f, 1.0f),
            referencePosition,
            Float4(0.78f, 0.78f, 0.78f, 0.0f),
            false
        );
        m_csgCube = CreateSolidCubeEntity(
            *m_world,
            m_context.objectArena,
            Float4(1.0f, 0.42f, 0.58f, 1.0f),
            receiverPosition,
            Float4(0.78f, 0.78f, 0.78f, 0.0f),
            true
        );
        m_csgCubeCenter = receiverPosition;
        const Float4 cutterPosition(
            receiverPosition.x + s_CsgCutterLocalOffset.x,
            receiverPosition.y + s_CsgCutterLocalOffset.y,
            receiverPosition.z + s_CsgCutterLocalOffset.z,
            0.0f
        );
        m_cutter = CreateCapsuleCutter(*m_world, m_context.objectArena, cutterPosition);

        NWB_FATAL_ASSERT_MSG(
            activeCamera.camera.valid() && m_referenceCube.valid() && m_csgCube.valid() && m_cutter.valid(),
            NWB_TEXT("CsgVisibleSmokeProject failed to create all scene entities")
        );

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: visible CSG capsule receiver scene created"));
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_animationTime += Min(safeDelta, s_MaxAnimationDelta) * s_CubeRotationSpeed;
        const SIMDVector referenceRotation = BuildCubeRotation(m_animationTime, 0.0f);
        const SIMDVector csgRotation = BuildCubeRotation(m_animationTime, s_CsgCubeRotationPhase);
        ApplyCubeRotation(*m_world, m_referenceCube, referenceRotation);
        ApplyCubeRotation(*m_world, m_csgCube, csgRotation);
        ApplyCapsuleCutterTransform(*m_world, m_cutter, LoadFloat(m_csgCubeCenter), csgRotation);

        m_world->tick(safeDelta);
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Core::ECS::EntityID m_referenceCube = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_csgCube = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_cutter = NWB::Core::ECS::ENTITY_ID_INVALID;
    Float4 m_csgCubeCenter = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    f32 m_animationTime = 0.0f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_csg_visible_smoke::CsgVisibleSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
