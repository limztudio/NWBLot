// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_caustic_transition.h"
#include "csg_smoke_helpers.h"
#include "framebuffer_capture.h"
#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "smoke_capture_teardown.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"

#include <loader/project_entry.h>

#include <impl/ecs_csg/module.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_skeleton/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_model/system.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_mesh/skinning/system.h>

#include <core/alloc/scratch.h>
#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>
#include <core/telemetry/frame_graph_contributor.h>

#include <global/assert.h>
#include <global/simd/frame.h>
#include <global/simd/vector.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_caustic_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_ProjectName = NWB_TEXT("SkinnedCausticSmokeProject");
constexpr AStringView s_CsgTransitionEnvironment = "NWB_SKINNED_CAUSTIC_CSG_TRANSITION";
constexpr AStringView s_CsgLaggedEnvironment = "NWB_SKINNED_CAUSTIC_CSG_LAGGED";

using NWB::Tests::Smoke::CreateSmokeSkinnedRenderWorldOrDie;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::CreateTintedModelEntity;
using NWB::Tests::Smoke::DestroySmokeSkinnedRenderWorld;
using NWB::Tests::Smoke::FindSpawnedModelObject;
using NWB::Tests::Smoke::LoadSmokeSkeletonBindJoints;
using NWB::Tests::Smoke::SyncSmokeModelRuntimes;
using NWB::Tests::Smoke::TickSmokeWorld;

using SkinnedCausticModelRef = NWB::Core::Assets::AssetRef<NWB::Impl::Model>;
using SkinnedCausticMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;
using SkinnedCausticMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Closed skinned glass keeps shadow and caustic boundaries on live deformed geometry.
static constexpr SkinnedCausticModelRef s_Model{"project/characters/skinned_caustic_prism/model"};
static constexpr SkinnedCausticMaterialRef s_GlassMaterial{"project/smoke/transparent_multi/materials/shared"};
static constexpr SkinnedCausticMaterialRef s_GroundMaterial{"project/smoke/transparent_multi/materials/ground"};
static constexpr SkinnedCausticMeshRef s_GroundMesh{"project/meshes/shadow_plane"};
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";
static constexpr Name s_ModelSkeletonObject("skeleton");
static constexpr Name s_ModelMeshObject("mesh");
static constexpr u64 s_TransitionPhaseFrames = NWB::Tests::Smoke::SkinnedCausticTransition::s_TransitionPhaseFrames;

static constexpr f32 s_CameraDistance = 2.6f;
static constexpr f32 s_CameraHeight = 1.15f;
static constexpr f32 s_CharacterLift = 0.85f;
static constexpr f32 s_GroundScale = 4.0f;
static constexpr f32 s_DefaultDirectionalLightPitch = 0.9f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.65f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 2.0f;
static constexpr f32 s_PoseAnimationSpeed = NWB::Tests::Smoke::SkinnedCausticTransition::s_PoseAnimationSpeed;
static constexpr f32 s_PoseAnimationAngle = 0.10f;
static constexpr f32 s_MaxAnimationDelta = 1.0f / 15.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Keep joint 0 at bind and oscillate child joints so shadows and caustics move each frame.
[[nodiscard]] static SIMDMatrix BuildWaveJointMatrix(const SIMDMatrix& bindJoint, const f32 jointIndex, const f32 timeSeconds)noexcept{
    const f32 angle = Sin(timeSeconds * s_PoseAnimationSpeed + jointIndex * 0.7f) * s_PoseAnimationAngle;
    const SIMDVectorSingle rotationAngles = VectorScale(VectorSet(0.4f, 1.0f, 0.25f, 0.0f), angle);

    const SIMDMatrix rotation = MatrixRotationRollPitchYawFromVector(rotationAngles);
    const SIMDMatrix animated = MatrixMultiply(bindJoint, rotation);
    if(!MatrixIsInvertibleAffine(animated, NWB::Impl::s_SkeletonAffineEpsilon, NWB::Impl::s_SkeletonJointDeterminantEpsilon))
        return bindJoint;

    return animated;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedCausticSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> CreateWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeSkinnedRenderWorldOrDie(s_ProjectName, {}, context);
        if(NWB::Tests::Smoke::ReadSmokeEnvironmentFlag(s_CsgTransitionEnvironment)){
            auto& renderer = *MakeNotNull(world->getSystem<NWB::Impl::RendererSystem>());
            renderer.setFrameLaggedAsyncLightingEnabled(NWB::Tests::Smoke::ReadSmokeEnvironmentFlag(s_CsgLaggedEnvironment));
        }
        return world;
    }




public:
    explicit SkinnedCausticSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateWorldOrDie(context))
        , m_bindJoints(context.objectArena)
        , m_transition(context)
    {}
    virtual ~SkinnedCausticSmokeProject()noexcept override{
        destroyWorld();
    }


public:
    virtual b8 onStartup()override{
        // Enable per-pass GPU timing when a timingprobe sink is active.
        m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());

        if(!m_transition.prepare())
            return false;
        auto bindJoints = LoadSmokeSkeletonBindJoints(s_Model, s_ProjectName, Name(), false, m_context.objectArena, m_context.assetManager);
        if(!bindJoints)
            return false;
        m_bindJoints = Move(*bindJoints);

        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(*m_world, Float4(0.0f, s_CameraHeight, -s_CameraDistance, 0.0f));
        const auto lightEntity = NWB::Impl::Scene::CreateDirectionalLightEntity(*m_world, s_DefaultDirectionalLightPitch, s_DefaultDirectionalLightYaw, 0.0f, Float4(1.0f, 0.96f, 0.88f), s_DefaultDirectionalLightIntensity);
        if(auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(lightEntity))
            light->enableCaustics = !m_transition.enabled() || !NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_SKINNED_CAUSTIC_CSG_NO_PHOTONS");

        m_groundEntity = CreateTintedStaticMeshEntity(s_GroundMesh, s_GroundMaterial, s_SmokeSurfaceMaterialInterface, Float4(0.82f, 0.82f, 0.85f, 1.0f), Float4(0.0f, 0.0f, 0.0f, 0.0f), Float4(s_GroundScale, 1.0f, s_GroundScale, 0.0f), *m_world);

        m_character = createGlassCharacter();
        SyncSmokeModelRuntimes(*m_world);
        const auto meshEntity = FindSpawnedModelObject(m_character, s_ModelMeshObject, NWB::Impl::ModelObjectKind::SkinnedMesh, *m_world);
        auto* const renderer = m_world->tryGetComponent<NWB::Impl::RendererComponent>(meshEntity);
        if(!renderer){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticSmokeProject: closed refractor renderer is unavailable"));
            return false;
        }
        renderer->opticalBoundaryMode = NWB::Impl::OpticalBoundaryMode::ClosedNested;

        m_skeletonEntity = FindSpawnedModelObject(m_character, s_ModelSkeletonObject, NWB::Impl::ModelObjectKind::Skeleton, *m_world);

        NWB_FATAL_ASSERT_MSG(activeCamera.camera.valid() && m_groundEntity.valid() && m_character.valid() && m_skeletonEntity.valid(), NWB_TEXT("SkinnedCausticSmokeProject failed to create all scene entities"));

        if(m_transition.enabled() && !m_transition.installTransitionCutter(m_character, *m_world)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticTransition: could not install the skinned CSG receiver"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticSmokeProject: skinned glass refractor over ground created ({} joints)"), static_cast<u32>(m_bindJoints.size()));
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticSmokeProject: shutdown"));
    }

    virtual b8 onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_fpsProbe.recordFrame(safeDelta);
        m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
        if(m_transition.enabled()){
            const f64 phaseAngle = static_cast<f64>(m_context.graphics.getFrameIndex() % s_TransitionPhaseFrames) * static_cast<f64>(s_PI * 2.0f);
            m_animationTime = phaseAngle / (static_cast<f64>(s_TransitionPhaseFrames) * s_PoseAnimationSpeed);
            m_transition.updateTransitionState(*m_world, m_animationTime);
        }
        else{
            const f32 animationStep = Min(safeDelta, s_MaxAnimationDelta) * s_PoseAnimationSpeed;
            m_animationTime += static_cast<f64>(animationStep);
        }
        animatePoses();
        if(!TickSmokeWorld(m_transition.enabled() ? 1.0f / 60.0f : safeDelta, *m_world))
            return false;
        if(m_transition.enabled())
            m_transition.updateTransitionCapture();
        return true;
    }


private:
    void destroyWorld(){
        m_transition.stop();
        DestroySmokeSkinnedRenderWorld(m_context, m_world);
    }

    NWB::Core::ECS::EntityID createGlassCharacter(){
        const auto modelResult = CreateTintedModelEntity(s_Model, s_GlassMaterial, s_SmokeSurfaceMaterialInterface, Float4(0.72f, 0.86f, 1.0f, 0.42f), Float4(0.0f, s_CharacterLift, 0.0f, 0.0f), Float4(1.0f, 1.0f, 1.0f, 0.0f), *m_world);
        const NWB::Core::ECS::EntityID entity = modelResult.entity;
        if(!modelResult.tintApplied)
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticSmokeProject: failed to set glass character tint"));

        return entity;
    }

    void animatePoses()noexcept{
        auto* pose = m_world->tryGetComponent<NWB::Impl::SkeletonPoseComponent>(m_skeletonEntity);
        if(!pose)
            return;

        const f32 timeSeconds = static_cast<f32>(m_animationTime);
        for(u32 jointIndex = 0u; jointIndex < pose->localJoints.size() && jointIndex < m_bindJoints.size(); ++jointIndex){
            if(jointIndex == 0u){
                pose->localJoints[jointIndex] = m_bindJoints[jointIndex];
                continue;
            }
            const SIMDMatrix animatedJoint = BuildWaveJointMatrix(LoadFloat(m_bindJoints[jointIndex]), static_cast<f32>(jointIndex), timeSeconds);
            StoreFloat(animatedJoint, pose->localJoints[jointIndex]);
        }
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    Vector<NWB::Impl::SkeletonJointMatrix, NWB::Core::Alloc::GlobalArena> m_bindJoints;
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ s_ProjectName };
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ s_ProjectName };
    f64 m_animationTime = 0.0;
    NWB::Tests::Smoke::SkinnedCausticTransition m_transition;
    NWB::Core::ECS::EntityID m_groundEntity = NWB::Core::ECS::s_InvalidEntityId;
    NWB::Core::ECS::EntityID m_character = NWB::Core::ECS::s_InvalidEntityId;
    NWB::Core::ECS::EntityID m_skeletonEntity = NWB::Core::ECS::s_InvalidEntityId;

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{
    return NWB_TEXT("NWB Skinned Caustic Smoke");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_skinned_caustic_smoke::SkinnedCausticSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

