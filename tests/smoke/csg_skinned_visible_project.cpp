// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <core/mesh/frame_math.h>
#include <impl/assets_mesh/skinned_asset.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material_instance.h>
#include <impl/ecs_skinned_mesh/runtime_helpers.h>
#include <impl/ecs_skinned_mesh_render/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_skinned_visible_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SmokeSkinnedMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::SkinnedMesh>;
using SmokeMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_SkinnedMeshPath = "project/characters/skinned_cone_female";
#if defined(NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER)
static constexpr AStringView s_ReceiverMaterialPath = "project/smoke/transparent_multi/materials/shared";
#else
static constexpr AStringView s_ReceiverMaterialPath = "project/smoke/csg_visible/materials/solid";
#endif
static constexpr AStringView s_SmokeBxdfSurfaceMaterialInterface = "project/shaders/smoke_bxdf_surface";
static constexpr Name s_ReceiverGroup("project/smoke/csg_skinned_visible/female_receiver");
static constexpr f32 s_CameraDistance = 3.25f;
static constexpr f32 s_CameraHeight = 0.92f;
static constexpr f32 s_DefaultDirectionalLightPitch = -0.62f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.54f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 3.0f;
static constexpr f32 s_CutterAnimationSpeed = 0.18f;
static constexpr f32 s_ReceiverYawSpeed = 0.92f;
static constexpr f32 s_MaxAnimationDelta = 1.0f / 15.0f;
static constexpr f32 s_InitialAnimationTime = s_PIDIV2 / s_ReceiverYawSpeed;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static f32 ReceiverAlpha(){
#if defined(NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER)
    return 0.44f;
#else
    return 1.0f;
#endif
}


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

[[nodiscard]] static SIMDVector BuildReceiverRotation(const f32 animationTime){
    return QuaternionRotationRollPitchYaw(0.0f, animationTime * s_ReceiverYawSpeed, 0.0f);
}

[[nodiscard]] static SIMDVector BuildCsgReceiverPosition(){
    return VectorSet(0.48f, 0.0f, 0.0f, 0.0f);
}

[[nodiscard]] static SIMDVector BuildCutterLocalRotation(const f32 animationTime){
    const SIMDVector horizontalCapsule = QuaternionRotationRollPitchYaw(s_PIDIV2, s_PIDIV2, 0.0f);
    const SIMDVector gentleRoll = QuaternionRotationRollPitchYaw(0.0f, animationTime * 0.18f, 0.0f);
    return QuaternionNormalize(QuaternionMultiply(horizontalCapsule, gentleRoll));
}

[[nodiscard]] static SIMDVector BuildCutterLocalCenter(const f32 animationTime){
    const f32 offset = Sin(animationTime) * 0.035f;
    return VectorSet(0.0f, 0.90f + offset, 0.0f, 0.0f);
}

[[nodiscard]] static SIMDVector BuildCutterWorldCenter(const f32 animationTime, const SIMDVector receiverRotation){
    return VectorAdd(BuildCsgReceiverPosition(), Vector3Rotate(BuildCutterLocalCenter(animationTime), receiverRotation));
}

[[nodiscard]] static SIMDVector BuildCutterWorldRotation(const f32 animationTime, const SIMDVector receiverRotation){
    return QuaternionNormalize(QuaternionMultiply(receiverRotation, BuildCutterLocalRotation(animationTime)));
}


class CsgSkinnedVisibleSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.threadPool);
        if(!world){
            NWB_LOGGER_FATAL(NWB_TEXT("CsgSkinnedVisibleSmokeProject initialization failed: ECS world allocation failed"));
            throw RuntimeException("CsgSkinnedVisibleSmokeProject initialization failed");
        }
        if(!context.shaderPathResolver){
            NWB_LOGGER_FATAL(NWB_TEXT("CsgSkinnedVisibleSmokeProject initialization failed: shader path resolver callback is null"));
            throw RuntimeException("CsgSkinnedVisibleSmokeProject initialization failed");
        }

        auto& meshSystem = world->addSystem<NWB::Impl::MeshSystem>(*world);
        auto& rendererSystem = world->addSystem<NWB::Impl::RendererSystem>(
            *world,
            context.graphics,
            context.assetManager,
            context.shaderPathResolver
        );
        auto& skinnedMeshSystem = world->addSystem<NWB::Impl::SkinnedMeshSystem>(
            *world,
            context.graphics,
            context.assetManager,
            meshSystem,
            context.shaderPathResolver
        );

        context.graphics.addRenderPassToBack(skinnedMeshSystem);
        context.graphics.addRenderPassToBack(rendererSystem);
        return MakeNotNullUnique(Move(world));
    }

    void destroyWorld(){
        if(!m_world.owner())
            return;

        auto* skinnedMeshSystem = m_world->getSystem<NWB::Impl::SkinnedMeshSystem>();
        if(skinnedMeshSystem)
            m_context.graphics.removeRenderPass(*skinnedMeshSystem);

        auto* rendererSystem = m_world->getSystem<NWB::Impl::RendererSystem>();
        if(rendererSystem)
            m_context.graphics.removeRenderPass(*rendererSystem);

        m_context.graphics.waitAllJobs();
        if(auto* device = m_context.graphics.getDevice())
            device->waitForIdle();

        m_world->clear();
        m_world.owner().reset();
    }

    void initializeRestPose(NWB::Impl::SkinnedMeshSkeletonPoseComponent& pose){
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        if(!m_context.assetManager.loadSync(NWB::Impl::SkinnedMesh::AssetTypeName(), Name(s_SkinnedMeshPath), loadedAsset)){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgSkinnedVisibleSmokeProject: failed to load skinned receiver rest pose"));
            return;
        }
        if(!loadedAsset || loadedAsset->assetType() != NWB::Impl::SkinnedMesh::AssetTypeName()){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgSkinnedVisibleSmokeProject: receiver asset loaded with an unexpected type"));
            return;
        }

        const auto* mesh = static_cast<const NWB::Impl::SkinnedMesh*>(loadedAsset.get());
        pose.parentJoints.resize(mesh->skeletonJointCount(), NWB::Impl::s_SkinnedMeshSkeletonRootParent);
        pose.localJoints.clear();
        pose.localJoints.reserve(mesh->inverseBindMatrices().size());
        for(const NWB::Impl::SkinnedMeshJointMatrix& inverseBind : mesh->inverseBindMatrices()){
            SIMDVector determinant = VectorZero();
            const SIMDMatrix bindJoint = MatrixInverse(&determinant, LoadFloat(inverseBind));
            NWB::Impl::SkinnedMeshJointMatrix storedBind{};
            StoreFloat(bindJoint, &storedBind);
            pose.localJoints.push_back(storedBind);
        }
    }

    NWB::Core::ECS::EntityID createSkinnedReceiverInstance(
        const Float4 position,
        const Float4 colorTint,
        const bool enableCsg
    ){
        SmokeSkinnedMeshRef mesh;
        mesh.virtualPath = Name(s_SkinnedMeshPath);
        SmokeMaterialRef material;
        material.virtualPath = Name(s_ReceiverMaterialPath);

        auto entity = m_world->createEntity();
        auto& transform = entity.addComponent<NWB::Impl::Scene::TransformComponent>();
        transform.position = position;
        transform.scale = Float4(1.0f, 1.0f, 1.0f, 0.0f);

        auto& skinnedMesh = entity.addComponent<NWB::Impl::SkinnedMeshComponent>();
        skinnedMesh.skinnedMesh = mesh;
        auto& pose = entity.addComponent<NWB::Impl::SkinnedMeshSkeletonPoseComponent>(m_context.objectArena);
        initializeRestPose(pose);

        auto& renderer = entity.addComponent<NWB::Impl::RendererComponent>();
        renderer.material = material;

        const Name materialInterface(s_SmokeBxdfSurfaceMaterialInterface);
        entity.addComponent<NWB::Impl::MaterialInstanceComponent>(m_context.objectArena, materialInterface);
        if(!NWB::Impl::SetMaterialMutableFloat4(
            *m_world,
            entity.id(),
            materialInterface,
            "runtime.color_tint",
            colorTint
        ))
            NWB_LOGGER_ERROR(NWB_TEXT("CsgSkinnedVisibleSmokeProject: failed to set receiver material tint"));

        if(enableCsg){
            auto& receiver = entity.addComponent<NWB::Impl::SkinnedCsgMeshComponent>();
            receiver.receiverGroup = s_ReceiverGroup;
#if defined(NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER)
            receiver.affectOpaquePass = false;
            receiver.affectTransparentPass = true;
#else
            receiver.affectOpaquePass = true;
            receiver.affectTransparentPass = false;
#endif
        }

        return entity.id();
    }

    void createReceiver(){
        m_plainReceiver = createSkinnedReceiverInstance(
            Float4(-0.48f, 0.0f, 0.0f, 0.0f),
            Float4(0.86f, 0.88f, 0.90f, ReceiverAlpha()),
            false
        );
        m_receiver = createSkinnedReceiverInstance(
            Float4(0.48f, 0.0f, 0.0f, 0.0f),
            Float4(0.85f, 0.72f, 1.0f, ReceiverAlpha()),
            true
        );
    }

    void createCutter(){
        auto entity = m_world->createEntity();
        auto& cutter = entity.addComponent<NWB::Impl::CsgCutterComponent>(m_context.objectArena);
        cutter.receiverGroup = s_ReceiverGroup;
#if defined(NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER)
        cutter.shapeType = Name("engine/csg/sphere");
#else
        cutter.shapeType = Name("engine/csg/capsule");
#endif
        cutter.operation = NWB::Impl::CsgOperation::Subtract;
        cutter.active = true;

#if defined(NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER)
        NWB::Impl::CsgSphereShapeParameters parameters;
        parameters.radius = Float4(0.18f, 0.0f, 0.0f, 0.0f);
#else
        NWB::Impl::CsgCapsuleShapeParameters parameters;
        parameters.radiusHalfHeight = Float4(0.105f, 0.27f, 0.0f, 0.0f);
#endif
        AssignCsgCutterParameters(cutter, parameters);
        const SIMDVector receiverRotation = BuildReceiverRotation(0.0f);
        AssignCsgCutterTransform(
            cutter,
            BuildCutterWorldCenter(0.0f, receiverRotation),
            BuildCutterWorldRotation(0.0f, receiverRotation)
        );

        m_cutter = entity.id();
    }

    void updateReceiverRotation(const NWB::Core::ECS::EntityID entity, const SIMDVector receiverRotation){
        auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        if(!transform)
            return;

        StoreFloat(receiverRotation, &transform->rotation);
    }

    void updateReceiverTransforms(const SIMDVector receiverRotation){
        updateReceiverRotation(m_plainReceiver, receiverRotation);
        updateReceiverRotation(m_receiver, receiverRotation);
    }

    void updateCutterTransform(const SIMDVector receiverRotation){
        auto* cutter = m_world->tryGetComponent<NWB::Impl::CsgCutterComponent>(m_cutter);
        if(!cutter)
            return;

        AssignCsgCutterTransform(
            *cutter,
            BuildCutterWorldCenter(m_animationTime, receiverRotation),
            BuildCutterWorldRotation(m_animationTime, receiverRotation)
        );
    }


public:
    explicit CsgSkinnedVisibleSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
    {}

    virtual ~CsgSkinnedVisibleSmokeProject()override{
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(
            *m_world,
            Float4(0.0f, s_CameraHeight, -s_CameraDistance, 0.0f)
        );
        NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DefaultDirectionalLightPitch,
            s_DefaultDirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DefaultDirectionalLightIntensity
        );

        createReceiver();
        createCutter();
        NWB_FATAL_ASSERT_MSG(
            activeCamera.camera.valid()
                && m_plainReceiver.valid()
                && m_receiver.valid()
                && m_cutter.valid(),
            NWB_TEXT("CsgSkinnedVisibleSmokeProject failed to create all scene entities")
        );

#if defined(NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER) && defined(NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgSkinnedVisibleSmokeProject: transparent skinned CSG receiver scene with sphere cutter and non-CSG control created"));
#elif defined(NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgSkinnedVisibleSmokeProject: transparent skinned CSG receiver scene with non-CSG control created"));
#elif defined(NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgSkinnedVisibleSmokeProject: skinned CSG receiver scene with sphere cutter and non-CSG control created"));
#else
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgSkinnedVisibleSmokeProject: skinned CSG receiver scene with non-CSG control created"));
#endif
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgSkinnedVisibleSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_animationTime += Min(safeDelta, s_MaxAnimationDelta) * s_CutterAnimationSpeed;
        const SIMDVector receiverRotation = BuildReceiverRotation(m_animationTime);
        updateReceiverTransforms(receiverRotation);
        updateCutterTransform(receiverRotation);

        m_world->tick(safeDelta);
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Core::ECS::EntityID m_plainReceiver = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_receiver = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_cutter = NWB::Core::ECS::ENTITY_ID_INVALID;
    f32 m_animationTime = s_InitialAnimationTime;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_csg_skinned_visible_smoke::CsgSkinnedVisibleSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

