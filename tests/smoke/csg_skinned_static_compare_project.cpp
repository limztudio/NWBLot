// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <core/mesh/frame_math.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_mesh/asset.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_render/material_instance.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_scene/module.h>

#include <cstdlib>

#if !defined(NWB_CSG_SKINNED_STATIC_COMPARE_STATIC_RECEIVER)
#include <impl/assets_mesh/skinned_asset.h>
#include <impl/ecs_skinned_mesh/runtime_helpers.h>
#include <impl/ecs_skinned_mesh_render/module.h>
#endif

#include "csg_smoke_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_skinned_static_compare_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SmokeMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;
using SmokeMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;
using NWB::Tests::Smoke::AssignCsgCutterParameters;
using NWB::Tests::Smoke::AssignCsgCutterTransform;

#if !defined(NWB_CSG_SKINNED_STATIC_COMPARE_STATIC_RECEIVER)
using SmokeSkinnedMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::SkinnedMesh>;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_CSG_SKINNED_STATIC_COMPARE_STATIC_RECEIVER)
static constexpr AStringView s_ReceiverMeshPath = "project/meshes/female_static";
#else
static constexpr AStringView s_ReceiverMeshPath = "project/characters/female";
#endif
static constexpr AStringView s_ReceiverMaterialPath = "project/smoke/csg_visible/materials/solid";
static constexpr AStringView s_SmokeBxdfSurfaceMaterialInterface = "project/shaders/smoke_bxdf_surface";
static constexpr Name s_ReceiverGroup("project/smoke/csg_skinned_static_compare/receiver");
static constexpr f32 s_CameraDistance = 3.25f;
static constexpr f32 s_CameraHeight = 0.92f;
static constexpr f32 s_DefaultDirectionalLightPitch = -0.62f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.54f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 3.0f;
static constexpr f32 s_ReceiverYawSpeed = 0.92f;
static constexpr f32 s_InitialAnimationTime = s_PIDIV2 / s_ReceiverYawSpeed;
static constexpr f32 s_ComparisonAnimationTime = s_InitialAnimationTime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static SIMDVector BuildReceiverRotation(){
    f32 yawOffset = 0.0f;
    if(const char* value = std::getenv("NWB_CSG_FEMALE_VIEW_YAW_OFFSET"))
        yawOffset = static_cast<f32>(std::atof(value));

    return QuaternionRotationRollPitchYaw(0.0f, s_ComparisonAnimationTime * s_ReceiverYawSpeed + yawOffset, 0.0f);
}

[[nodiscard]] static SIMDVector BuildCsgReceiverPosition(){
    return VectorZero();
}

[[nodiscard]] static SIMDVector BuildCutterLocalRotation(){
    const SIMDVector horizontalCapsule = QuaternionRotationRollPitchYaw(s_PIDIV2, s_PIDIV2, 0.0f);
    const SIMDVector gentleRoll = QuaternionRotationRollPitchYaw(0.0f, s_ComparisonAnimationTime * 0.18f, 0.0f);
    return QuaternionNormalize(QuaternionMultiply(horizontalCapsule, gentleRoll));
}

[[nodiscard]] static SIMDVector BuildCutterLocalCenter(){
    const f32 offset = Sin(s_ComparisonAnimationTime) * 0.035f;
    return VectorSet(0.0f, 0.90f + offset, 0.0f, 0.0f);
}

[[nodiscard]] static SIMDVector BuildCutterWorldCenter(const SIMDVector receiverRotation){
    return VectorAdd(BuildCsgReceiverPosition(), Vector3Rotate(BuildCutterLocalCenter(), receiverRotation));
}

[[nodiscard]] static SIMDVector BuildCutterWorldRotation(const SIMDVector receiverRotation){
    return QuaternionNormalize(QuaternionMultiply(receiverRotation, BuildCutterLocalRotation()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgSkinnedStaticCompareSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.threadPool);
        if(!world){
            NWB_LOGGER_FATAL(NWB_TEXT("CsgSkinnedStaticCompareSmokeProject initialization failed: ECS world allocation failed"));
            throw RuntimeException("CsgSkinnedStaticCompareSmokeProject initialization failed");
        }
        if(!context.shaderPathResolver){
            NWB_LOGGER_FATAL(NWB_TEXT("CsgSkinnedStaticCompareSmokeProject initialization failed: shader path resolver callback is null"));
            throw RuntimeException("CsgSkinnedStaticCompareSmokeProject initialization failed");
        }

        auto& meshSystem = world->addSystem<NWB::Impl::MeshSystem>(*world);
        auto& rendererSystem = world->addSystem<NWB::Impl::RendererSystem>(
            *world,
            context.graphics,
            context.assetManager,
            context.shaderPathResolver
        );

#if !defined(NWB_CSG_SKINNED_STATIC_COMPARE_STATIC_RECEIVER)
        auto& skinnedMeshSystem = world->addSystem<NWB::Impl::SkinnedMeshSystem>(
            *world,
            context.graphics,
            context.assetManager,
            meshSystem,
            context.shaderPathResolver
        );
        context.graphics.addRenderPassToBack(skinnedMeshSystem);
#else
        static_cast<void>(meshSystem);
#endif

        context.graphics.addRenderPassToBack(rendererSystem);
        return MakeNotNullUnique(Move(world));
    }

    void destroyWorld(){
        if(!m_world.owner())
            return;

#if !defined(NWB_CSG_SKINNED_STATIC_COMPARE_STATIC_RECEIVER)
        auto* skinnedMeshSystem = m_world->getSystem<NWB::Impl::SkinnedMeshSystem>();
        if(skinnedMeshSystem)
            m_context.graphics.removeRenderPass(*skinnedMeshSystem);
#endif

        auto* rendererSystem = m_world->getSystem<NWB::Impl::RendererSystem>();
        if(rendererSystem)
            m_context.graphics.removeRenderPass(*rendererSystem);

        m_context.graphics.waitAllJobs();
        if(auto* device = m_context.graphics.getDevice())
            device->waitForIdle();

        m_world->clear();
        m_world.owner().reset();
    }

#if !defined(NWB_CSG_SKINNED_STATIC_COMPARE_STATIC_RECEIVER)
    void initializeRestPose(NWB::Impl::SkinnedMeshSkeletonPoseComponent& pose){
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        if(!m_context.assetManager.loadSync(NWB::Impl::SkinnedMesh::AssetTypeName(), Name(s_ReceiverMeshPath), loadedAsset)){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgSkinnedStaticCompareSmokeProject: failed to load skinned receiver rest pose"));
            return;
        }
        if(!loadedAsset || loadedAsset->assetType() != NWB::Impl::SkinnedMesh::AssetTypeName()){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgSkinnedStaticCompareSmokeProject: receiver asset loaded with an unexpected type"));
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
#endif

    void createReceiver(){
        SmokeMaterialRef material;
        material.virtualPath = Name(s_ReceiverMaterialPath);

        auto entity = m_world->createEntity();
        auto& transform = entity.addComponent<NWB::Impl::Scene::TransformComponent>();
        StoreFloat(BuildReceiverRotation(), &transform.rotation);
        StoreFloat(BuildCsgReceiverPosition(), &transform.position);
        transform.scale = Float4(1.0f, 1.0f, 1.0f, 0.0f);

#if defined(NWB_CSG_SKINNED_STATIC_COMPARE_STATIC_RECEIVER)
        SmokeMeshRef mesh;
        mesh.virtualPath = Name(s_ReceiverMeshPath);
        auto& meshComponent = entity.addComponent<NWB::Impl::MeshComponent>();
        meshComponent.mesh = mesh;
#else
        SmokeSkinnedMeshRef mesh;
        mesh.virtualPath = Name(s_ReceiverMeshPath);
        auto& skinnedMesh = entity.addComponent<NWB::Impl::SkinnedMeshComponent>();
        skinnedMesh.skinnedMesh = mesh;
        auto& pose = entity.addComponent<NWB::Impl::SkinnedMeshSkeletonPoseComponent>(m_context.objectArena);
        initializeRestPose(pose);
#endif

        auto& renderer = entity.addComponent<NWB::Impl::RendererComponent>();
        renderer.material = material;

        const Name materialInterface(s_SmokeBxdfSurfaceMaterialInterface);
        entity.addComponent<NWB::Impl::MaterialInstanceComponent>(m_context.objectArena, materialInterface);
        if(!NWB::Impl::SetMaterialMutableFloat4(
            *m_world,
            entity.id(),
            materialInterface,
            "runtime.color_tint",
            Float4(0.85f, 0.72f, 1.0f, 1.0f)
        ))
            NWB_LOGGER_ERROR(NWB_TEXT("CsgSkinnedStaticCompareSmokeProject: failed to set receiver material tint"));

#if defined(NWB_CSG_SKINNED_STATIC_COMPARE_STATIC_RECEIVER)
        auto& receiver = entity.addComponent<NWB::Impl::StaticCsgMeshComponent>();
#else
        auto& receiver = entity.addComponent<NWB::Impl::SkinnedCsgMeshComponent>();
#endif
        receiver.receiverGroup = s_ReceiverGroup;
        receiver.affectOpaquePass = true;
        receiver.affectTransparentPass = false;

        m_receiver = entity.id();
    }

    void createCutter(){
        auto entity = m_world->createEntity();
        auto& cutter = entity.addComponent<NWB::Impl::CsgCutterComponent>(m_context.objectArena);
        cutter.receiverGroup = s_ReceiverGroup;
        cutter.shapeType = Name("engine/csg/capsule");

        NWB::Impl::CsgCapsuleShapeParameters parameters;
        parameters.radiusHalfHeight = Float4(0.105f, 0.27f, 0.0f, 0.0f);
        AssignCsgCutterParameters(cutter, parameters);

        const SIMDVector receiverRotation = BuildReceiverRotation();
        AssignCsgCutterTransform(
            cutter,
            BuildCutterWorldCenter(receiverRotation),
            BuildCutterWorldRotation(receiverRotation)
        );

        m_cutter = entity.id();
    }


public:
    explicit CsgSkinnedStaticCompareSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
    {}

    virtual ~CsgSkinnedStaticCompareSmokeProject()override{
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
            activeCamera.camera.valid() && m_receiver.valid() && m_cutter.valid(),
            NWB_TEXT("CsgSkinnedStaticCompareSmokeProject failed to create all scene entities")
        );

#if defined(NWB_CSG_SKINNED_STATIC_COMPARE_STATIC_RECEIVER)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgSkinnedStaticCompareSmokeProject: static converted female receiver scene created"));
#else
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgSkinnedStaticCompareSmokeProject: skinned female receiver scene created"));
#endif
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgSkinnedStaticCompareSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_world->tick(safeDelta);
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Core::ECS::EntityID m_receiver = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_cutter = NWB::Core::ECS::ENTITY_ID_INVALID;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


const tchar* NWB::QueryProjectWindowTitle(){
#if defined(NWB_CSG_SKINNED_STATIC_COMPARE_STATIC_RECEIVER)
    return NWB_TEXT("NWB Static Female CSG Compare Smoke");
#else
    return NWB_TEXT("NWB Skinned Female CSG Compare Smoke");
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_csg_skinned_static_compare_smoke::CsgSkinnedStaticCompareSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
