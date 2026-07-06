// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <core/mesh/frame_math.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_skeleton/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_render/model_renderer.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material_instance.h>
#include <impl/ecs_skeleton/runtime_helpers.h>
#include <impl/ecs_mesh/skinning/module.h>

#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_caustic_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::AddSmokeSkinnedRenderSystems;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::CreateTintedModelEntity;
using NWB::Tests::Smoke::DestroySmokeSkinnedRenderWorld;
using NWB::Tests::Smoke::FindSpawnedModelObject;
using NWB::Tests::Smoke::SyncSmokeModelRuntimes;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A SKINNED glass refractor over an opaque ground receiver under one directional light. The skeleton pose is animated
// every frame, so the mesh (and its shading normals) deform continuously. This is the validation scene for the
// per-frame skinned-normal repack (repack_normals_cs / dispatchRepackNormals): the RT shadow + caustic must bend on
// the LIVE deformed normals -- if they read the bind pose instead, the refractive shadow + caustic would not track
// the animation. Reuses the existing refractive glass + ground materials (no new assets).
static constexpr AStringView s_ModelPath = "project/characters/body/model";
static constexpr AStringView s_GlassMaterialPath = "project/smoke/transparent_multi/materials/shared";
static constexpr AStringView s_GroundMaterialPath = "project/smoke/transparent_multi/materials/ground";
static constexpr AStringView s_GroundMeshPath = "project/meshes/shadow_plane";
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";
static constexpr Name s_ModelSkeletonObject("skeleton");

static constexpr f32 s_CameraDistance = 2.6f;
static constexpr f32 s_CameraHeight = 1.15f;
static constexpr f32 s_CharacterLift = 0.35f;
static constexpr f32 s_GroundScale = 4.0f;
static constexpr f32 s_DefaultDirectionalLightPitch = 0.9f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.65f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 2.0f;
static constexpr f32 s_PoseAnimationSpeed = 1.1f;
static constexpr f32 s_PoseAnimationAngle = 0.10f;
static constexpr f32 s_MaxAnimationDelta = 1.0f / 15.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Oscillate each joint around its bind pose so the skinned surface deforms every frame. The root joint (index 0) is
// left at bind so the body stays planted over the ground while the limbs/torso undulate -- enough deformation to move
// the refractive shadow + caustic footprint visibly.
[[nodiscard]] static SIMDMatrix BuildWaveJointMatrix(
    const SIMDMatrix& bindJoint,
    const u32 jointIndex,
    const f32 timeSeconds
){
    if(jointIndex == 0u)
        return bindJoint;

    const f32 phase = static_cast<f32>(jointIndex) * 0.7f;
    const SIMDVector waves = VectorSin(VectorSet(timeSeconds * s_PoseAnimationSpeed + phase, 0.0f, 0.0f, 0.0f));
    const f32 angle = VectorGetX(waves) * s_PoseAnimationAngle;

    const SIMDMatrix rotation = MatrixRotationRollPitchYaw(angle * 0.4f, angle, angle * 0.25f);
    const SIMDMatrix animated = NWB::Impl::SkeletonRuntime::MultiplyJointMatrices(bindJoint, rotation);
    if(!NWB::Impl::SkeletonRuntime::IsInvertibleAffineJointMatrix(animated))
        return bindJoint;

    return animated;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedCausticSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.threadPool);
        if(!world){
            NWB_LOGGER_FATAL(NWB_TEXT("SkinnedCausticSmokeProject initialization failed: ECS world allocation failed"));
            throw RuntimeException("SkinnedCausticSmokeProject initialization failed");
        }
        if(!context.shaderPathResolver){
            NWB_LOGGER_FATAL(NWB_TEXT("SkinnedCausticSmokeProject initialization failed: shader path resolver callback is null"));
            throw RuntimeException("SkinnedCausticSmokeProject initialization failed");
        }

        // Force ray-tracing emulation so the SOFTWARE shadow + caustic path runs even on RT-capable hardware -- the
        // A/B sibling of the hardware ray-traced path. Default OFF: the demo runs the hardware path.
#if defined(NWB_SKINNED_CAUSTIC_FORCE_RT_EMULATION) && !defined(NWB_FINAL)
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayTracingAccelStruct, true);
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayTracingPipeline, true);
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayQuery, true);
#endif

        AddSmokeSkinnedRenderSystems(*world, context);
        return MakeNotNullUnique(Move(world));
    }

    void destroyWorld(){
        DestroySmokeSkinnedRenderWorld(m_context, m_world);
    }

    [[nodiscard]] bool loadSkeletonBindJoints(){
        UniquePtr<NWB::Core::Assets::IAsset> modelAsset;
        if(!m_context.assetManager.loadSync(NWB::Impl::Model::AssetTypeName(), Name(s_ModelPath), modelAsset) || !modelAsset){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticSmokeProject: failed to load model for skeleton bind joints"));
            return false;
        }
        const auto* model = checked_cast<const NWB::Impl::Model*>(modelAsset.get());
        if(model->skeletonObjects().empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticSmokeProject: model has no skeleton object"));
            return false;
        }

        const Name skeletonPath = model->skeletonObjects().front().skeleton.name();
        UniquePtr<NWB::Core::Assets::IAsset> skeletonAsset;
        if(!m_context.assetManager.loadSync(NWB::Impl::Skeleton::AssetTypeName(), skeletonPath, skeletonAsset) || !skeletonAsset){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticSmokeProject: failed to load skeleton for bind joints"));
            return false;
        }
        const auto* skeleton = checked_cast<const NWB::Impl::Skeleton*>(skeletonAsset.get());
        if(skeleton->joints().empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticSmokeProject: skeleton has no joints"));
            return false;
        }

        m_bindJoints.clear();
        m_bindJoints.reserve(skeleton->joints().size());
        for(const NWB::Impl::SkeletonJoint& joint : skeleton->joints())
            m_bindJoints.push_back(joint.localBindPose);
        return !m_bindJoints.empty();
    }

    NWB::Core::ECS::EntityID createGlassCharacter(){
        bool tintApplied = false;
        const NWB::Core::ECS::EntityID entity = CreateTintedModelEntity(
            *m_world,
            m_context.objectArena,
            s_ModelPath,
            s_GlassMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.72f, 0.86f, 1.0f, 0.42f),
            Float4(0.0f, s_CharacterLift, 0.0f, 0.0f),
            Float4(1.0f, 1.0f, 1.0f, 0.0f),
            &tintApplied
        );
        if(!tintApplied)
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticSmokeProject: failed to set glass character tint"));

        return entity;
    }

    void animatePoses(){
        auto* pose = m_world->tryGetComponent<NWB::Impl::SkeletonPoseComponent>(m_skeletonEntity);
        if(!pose)
            return;

        const f32 timeSeconds = static_cast<f32>(m_animationTime);
        for(u32 jointIndex = 0u; jointIndex < pose->localJoints.size() && jointIndex < m_bindJoints.size(); ++jointIndex){
            const SIMDMatrix animatedJoint = BuildWaveJointMatrix(LoadFloat(m_bindJoints[jointIndex]), jointIndex, timeSeconds);
            StoreFloat(animatedJoint, &pose->localJoints[jointIndex]);
        }
    }


public:
    explicit SkinnedCausticSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
        , m_bindJoints(context.objectArena)
    {}

    virtual ~SkinnedCausticSmokeProject()override{
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        // Opt into per-pass GPU timing so m_gpuPassTimingProbe can report the caustic photon/resolve + shadow +
        // skinning pass GPU times each interval (flips the GPU-timing double gate via the Frame).
        m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());

        if(!loadSkeletonBindJoints())
            return false;

        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(
            *m_world,
            Float4(0.0f, s_CameraHeight, -s_CameraDistance, 0.0f)
        );
        const auto lightEntity = NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DefaultDirectionalLightPitch,
            s_DefaultDirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DefaultDirectionalLightIntensity
        );
        if(auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(lightEntity))
            light->enableCaustics = true;

        m_groundEntity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_GroundMeshPath,
            s_GroundMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.82f, 0.82f, 0.85f, 1.0f),
            Float4(0.0f, 0.0f, 0.0f, 0.0f),
            Float4(s_GroundScale, 1.0f, s_GroundScale, 0.0f)
        );

        m_character = createGlassCharacter();
        SyncSmokeModelRuntimes(*m_world);

        m_skeletonEntity = FindSpawnedModelObject(
            *m_world,
            m_character,
            s_ModelSkeletonObject,
            NWB::Impl::ModelObjectKind::Skeleton
        );

        NWB_FATAL_ASSERT_MSG(
            activeCamera.camera.valid()
                && m_groundEntity.valid()
                && m_character.valid()
                && m_skeletonEntity.valid(),
            NWB_TEXT("SkinnedCausticSmokeProject failed to create all scene entities")
        );

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticSmokeProject: skinned glass refractor over ground created ({} joints)"), static_cast<u32>(m_bindJoints.size()));
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_fpsProbe.recordFrame(safeDelta);
        m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
        m_animationTime += Min(safeDelta, s_MaxAnimationDelta) * s_PoseAnimationSpeed;
        animatePoses();
        m_world->tick(safeDelta);
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    Vector<NWB::Impl::SkeletonJointMatrix, NWB::Core::Alloc::GlobalArena> m_bindJoints;
    NWB::Core::ECS::EntityID m_groundEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_character = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_skeletonEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ NWB_TEXT("SkinnedCausticSmokeProject") };
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ NWB_TEXT("SkinnedCausticSmokeProject") };
    f64 m_animationTime = 0.0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


const tchar* NWB::QueryProjectWindowTitle(){
    return NWB_TEXT("NWB Skinned Caustic Smoke");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_skinned_caustic_smoke::SkinnedCausticSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

