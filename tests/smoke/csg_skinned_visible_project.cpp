#include <loader/project_entry.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <global/math/frame.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_skeleton/asset.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_render/mesh/model_renderer.h>
#include <impl/ecs_render/kernel/module.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_mesh/skinning/module.h>

#include "csg_smoke_helpers.h"
#include "fps_probe.h"
#include "smoke_skinned_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_skinned_visible_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::AddSkinnedCsgMeshReceiver;
using NWB::Tests::Smoke::AddSmokeSkinnedRenderSystems;
using NWB::Tests::Smoke::AssignCsgCutterParameters;
using NWB::Tests::Smoke::AssignCsgCutterTransform;
using NWB::Tests::Smoke::CreateTintedModelEntity;
using NWB::Tests::Smoke::DestroySmokeSkinnedRenderWorld;
using NWB::Tests::Smoke::FindSpawnedModelObject;
using NWB::Tests::Smoke::SyncSmokeModelRuntimes;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_ModelPath = "project/characters/body/model";
#if defined(NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER)
static constexpr AStringView s_ReceiverMaterialPath = "project/smoke/transparent_multi/materials/shared";
#else
static constexpr AStringView s_ReceiverMaterialPath = "project/smoke/csg_visible/materials/solid";
#endif
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";
static constexpr Name s_ReceiverGroup("project/smoke/csg_skinned_visible/female_receiver");
static constexpr Name s_ModelMeshObject("mesh");
static constexpr Name s_CutterAnchorBoneName("hip");
static constexpr f32 s_CameraDistance = 1.75f;
static constexpr f32 s_CameraHeight = 0.92f;
static constexpr f32 s_DefaultDirectionalLightPitch = -0.62f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.54f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 3.0f;
static constexpr f32 s_CutterAnimationSpeed = 0.18f;
static constexpr f32 s_ReceiverYawSpeed = 0.92f;
static constexpr f32 s_MaxAnimationDelta = 1.0f / 15.0f;
static constexpr f32 s_InitialAnimationTime = s_PIDIV2 / s_ReceiverYawSpeed;
static constexpr u16 s_FrameClientWidth = 1280u;
static constexpr u16 s_FrameClientHeight = 900u;
#if defined(NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER)
static constexpr f32 s_TransparentReceiverAlpha = 0.44f;
#endif
static constexpr f32 s_ReceiverOffsetX = 0.48f;
static constexpr f32 s_CutterGentleRollSpeed = 0.18f;
static constexpr f32 s_CutterAnchorFallbackY = 0.90f;
#if defined(NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER)
static constexpr f32 s_SphereCutterRadius = 0.18f;
#else
static constexpr f32 s_CapsuleCutterRadius = 0.105f;
static constexpr f32 s_CapsuleCutterHalfHeight = 0.27f;
#endif
static constexpr Float4 s_PlainReceiverTint = Float4(0.86f, 0.88f, 0.90f, 1.0f);
static constexpr Float4 s_CsgReceiverTint = Float4(0.85f, 0.72f, 1.0f, 1.0f);
static constexpr Float4 s_WarmDirectionalLightColor = Float4(1.0f, 0.96f, 0.88f);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static const tchar* CsgSkinnedVisibleFpsLabel(){
#if defined(NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER) && defined(NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER)
    return NWB_TEXT("CsgSkinnedTransparentSphereVisibleSmokeProject");
#elif defined(NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER)
    return NWB_TEXT("CsgSkinnedTransparentVisibleSmokeProject");
#elif defined(NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER)
    return NWB_TEXT("CsgSkinnedSphereVisibleSmokeProject");
#else
    return NWB_TEXT("CsgSkinnedVisibleSmokeProject");
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static f32 ReceiverAlpha(){
#if defined(NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER)
    return s_TransparentReceiverAlpha;
#else
    return 1.0f;
#endif
}


[[nodiscard]] static SIMDVector BuildReceiverRotation(const f32 animationTime){
    return QuaternionRotationRollPitchYaw(0.0f, animationTime * s_ReceiverYawSpeed, 0.0f);
}

[[nodiscard]] static SIMDVector BuildCsgReceiverPosition(){
    return VectorSet(s_ReceiverOffsetX, 0.0f, 0.0f, 0.0f);
}

[[nodiscard]] static SIMDVector BuildCutterLocalRotation(const f32 animationTime){
    const SIMDVector horizontalCapsule = QuaternionRotationRollPitchYaw(s_PIDIV2, s_PIDIV2, 0.0f);
    const SIMDVector gentleRoll = QuaternionRotationRollPitchYaw(0.0f, animationTime * s_CutterGentleRollSpeed, 0.0f);
    return QuaternionNormalize(QuaternionMultiply(horizontalCapsule, gentleRoll));
}

[[nodiscard]] static SIMDVector BuildCutterWorldCenter(const SIMDVector cutterLocalCenter, const SIMDVector receiverRotation){
    return VectorAdd(BuildCsgReceiverPosition(), Vector3Rotate(cutterLocalCenter, receiverRotation));
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

        AddSmokeSkinnedRenderSystems(*world, context);
        return MakeNotNullUnique(Move(world));
    }

    void destroyWorld(){
        DestroySmokeSkinnedRenderWorld(m_context, m_world);
    }

    NWB::Core::ECS::EntityID createSkinnedReceiverInstance(
        const Float4 position,
        const Float4 colorTint,
        const bool enableCsg
    ){
        static_cast<void>(enableCsg);

        bool tintApplied = false;
        const NWB::Core::ECS::EntityID entity = CreateTintedModelEntity(
            *m_world,
            m_context.objectArena,
            s_ModelPath,
            s_ReceiverMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            colorTint,
            position,
            Float4(1.0f, 1.0f, 1.0f, 0.0f),
            &tintApplied
        );
        if(!tintApplied)
            NWB_LOGGER_ERROR(NWB_TEXT("CsgSkinnedVisibleSmokeProject: failed to set receiver material tint"));

        return entity;
    }

    [[nodiscard]] bool installCsgReceiverOnSpawnedModelObject(){
        m_receiverObject = FindSpawnedModelObject(
            *m_world,
            m_receiver,
            s_ModelMeshObject,
            NWB::Impl::ModelObjectKind::SkinnedMesh
        );
        if(!m_receiverObject.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgSkinnedVisibleSmokeProject: failed to find spawned receiver model object"));
            return false;
        }

#if defined(NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER)
        AddSkinnedCsgMeshReceiver(*m_world, m_receiverObject, s_ReceiverGroup, false, true);
#else
        AddSkinnedCsgMeshReceiver(*m_world, m_receiverObject, s_ReceiverGroup, true, false);
#endif
        return true;
    }

    void createReceiver(){
        m_plainReceiver = createSkinnedReceiverInstance(
            Float4(-s_ReceiverOffsetX, 0.0f, 0.0f, 0.0f),
            Float4(s_PlainReceiverTint.x, s_PlainReceiverTint.y, s_PlainReceiverTint.z, ReceiverAlpha()),
            false
        );
        m_receiver = createSkinnedReceiverInstance(
            Float4(s_ReceiverOffsetX, 0.0f, 0.0f, 0.0f),
            Float4(s_CsgReceiverTint.x, s_CsgReceiverTint.y, s_CsgReceiverTint.z, ReceiverAlpha()),
            true
        );
    }

    [[nodiscard]] SIMDVector resolveCutterAnchorLocalCenter()const{
        const SIMDVector fallback = VectorSet(0.0f, s_CutterAnchorFallbackY, 0.0f, 0.0f);

        UniquePtr<NWB::Core::Assets::IAsset> modelAsset;
        if(!m_context.assetManager.loadSync(NWB::Impl::Model::AssetTypeName(), Name(s_ModelPath), modelAsset) || !modelAsset){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgSkinnedVisibleSmokeProject: failed to load model for cutter anchor"));
            return fallback;
        }
        const auto* model = checked_cast<const NWB::Impl::Model*>(modelAsset.get());
        if(model->skeletonObjects().empty())
            return fallback;

        const Name skeletonPath = model->skeletonObjects().front().skeleton.name();
        UniquePtr<NWB::Core::Assets::IAsset> skeletonAsset;
        if(!m_context.assetManager.loadSync(NWB::Impl::Skeleton::AssetTypeName(), skeletonPath, skeletonAsset) || !skeletonAsset){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgSkinnedVisibleSmokeProject: failed to load skeleton for cutter anchor"));
            return fallback;
        }
        const auto* skeleton = checked_cast<const NWB::Impl::Skeleton*>(skeletonAsset.get());

        const u32 jointCount = skeleton->jointCount();
        const u32 anchorIndex = skeleton->findJointIndex(s_CutterAnchorBoneName);
        if(anchorIndex == NWB::Impl::s_SkeletonInvalidJointIndex || anchorIndex >= jointCount){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgSkinnedVisibleSmokeProject: skeleton has no '{}' bone for cutter anchor"), StringConvert(s_CutterAnchorBoneName.c_str()));
            return fallback;
        }

        // Accumulate the anchor bone's model-space bind transform by walking up the parent chain.
        SIMDMatrix boneToModel = LoadFloat(skeleton->joints()[anchorIndex].localBindPose);
        for(
            u32 parentIndex = skeleton->joints()[anchorIndex].parentIndex;
            parentIndex != NWB::Impl::s_SkeletonInvalidJointIndex && parentIndex < jointCount;
            parentIndex = skeleton->joints()[parentIndex].parentIndex
        )
            boneToModel = MatrixMultiply(LoadFloat(skeleton->joints()[parentIndex].localBindPose), boneToModel);

        // The bone origin in model space is the translation column (M*v convention); receiver scale is 1.
        return VectorSet(
            VectorGetW(boneToModel.v[0]),
            VectorGetW(boneToModel.v[1]),
            VectorGetW(boneToModel.v[2]),
            0.0f
        );
    }

    void createCutter(const SIMDVector cutterLocalCenter){
        auto entity = m_world->createEntity();
        auto& cutter = entity.addComponent<NWB::Impl::CsgCutterComponent>(m_context.objectArena);
        cutter.receiverGroup = s_ReceiverGroup;
#if defined(NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER)
        cutter.shapeType = Name("engine/csg/sphere");
#else
        cutter.shapeType = Name("engine/csg/capsule");
#endif

#if defined(NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER)
        NWB::Impl::CsgSphereShapeParameters parameters;
        parameters.radius = Float4(s_SphereCutterRadius, 0.0f, 0.0f, 0.0f);
#else
        NWB::Impl::CsgCapsuleShapeParameters parameters;
        parameters.radiusHalfHeight = Float4(s_CapsuleCutterRadius, s_CapsuleCutterHalfHeight, 0.0f, 0.0f);
#endif
        AssignCsgCutterParameters(cutter, parameters);
        const SIMDVector receiverRotation = BuildReceiverRotation(0.0f);
        AssignCsgCutterTransform(
            cutter,
            BuildCutterWorldCenter(cutterLocalCenter, receiverRotation),
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

    void updateCutterTransform(const SIMDVector receiverRotation, const SIMDVector cutterLocalCenter){
        auto* cutter = m_world->tryGetComponent<NWB::Impl::CsgCutterComponent>(m_cutter);
        if(!cutter)
            return;

        AssignCsgCutterTransform(
            *cutter,
            BuildCutterWorldCenter(cutterLocalCenter, receiverRotation),
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
            s_WarmDirectionalLightColor,
            s_DefaultDirectionalLightIntensity
        );

        createReceiver();
        SyncSmokeModelRuntimes(*m_world);
        const bool receiverReady = installCsgReceiverOnSpawnedModelObject();
        const SIMDVector cutterLocalCenter = resolveCutterAnchorLocalCenter();
        StoreFloat(cutterLocalCenter, &m_cutterLocalCenter);
        createCutter(cutterLocalCenter);
        NWB_FATAL_ASSERT_MSG(
            activeCamera.camera.valid()
                && m_plainReceiver.valid()
                && m_receiver.valid()
                && m_receiverObject.valid()
                && m_cutter.valid(),
            NWB_TEXT("CsgSkinnedVisibleSmokeProject failed to create all scene entities")
        );
        NWB_FATAL_ASSERT_MSG(receiverReady, NWB_TEXT("CsgSkinnedVisibleSmokeProject failed to bind CSG receiver to spawned model object"));

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
        m_fpsProbe.recordFrame(safeDelta);
        m_animationTime += Min(safeDelta, s_MaxAnimationDelta) * s_CutterAnimationSpeed;
        const SIMDVector receiverRotation = BuildReceiverRotation(m_animationTime);
        updateReceiverTransforms(receiverRotation);
        updateCutterTransform(receiverRotation, LoadFloat(m_cutterLocalCenter));

        m_world->tick(safeDelta);
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Core::ECS::EntityID m_plainReceiver = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_receiver = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_receiverObject = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_cutter = NWB::Core::ECS::ENTITY_ID_INVALID;
    f32 m_animationTime = s_InitialAnimationTime;
    Float4 m_cutterLocalCenter = Float4(0.0f, s_CutterAnchorFallbackY, 0.0f, 0.0f);
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ CsgSkinnedVisibleFpsLabel() };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return {
        __hidden_csg_skinned_visible_smoke::s_FrameClientWidth,
        __hidden_csg_skinned_visible_smoke::s_FrameClientHeight
    };
}


const tchar* NWB::QueryProjectWindowTitle(){
#if defined(NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER) && defined(NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER)
    return NWB_TEXT("NWB Transparent Skinned Sphere CSG Smoke");
#elif defined(NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER)
    return NWB_TEXT("NWB Transparent Skinned CSG Smoke");
#elif defined(NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER)
    return NWB_TEXT("NWB Skinned Sphere CSG Smoke");
#else
    return NWB_TEXT("NWB Skinned CSG Smoke");
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_csg_skinned_visible_smoke::CsgSkinnedVisibleSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

