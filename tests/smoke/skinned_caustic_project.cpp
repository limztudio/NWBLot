// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>
#include <core/telemetry/frame_graph_contributor.h>
#include <global/assert.h>
#include <global/math/frame.h>
#include <global/timer.h>
#include <impl/ecs_csg/module.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_skeleton/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_skeleton/runtime_helpers.h>
#include <impl/ecs_mesh/skinning/module.h>

#include "csg_smoke_helpers.h"
#include "framebuffer_capture.h"
#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "smoke_project_helpers.h"
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

using SkinnedCausticModelRef = NWB::Core::Assets::AssetRef<NWB::Impl::Model>;
using SkinnedCausticMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;
using SkinnedCausticMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// SKINNED glass refractor over ground: per-frame pose validates the skinned-normal repack (shadow + caustic must
// bend on live deformed normals, not bind pose). Reuses glass + ground materials.
static constexpr SkinnedCausticModelRef s_Model{"project/characters/body/model"};
static constexpr SkinnedCausticModelRef s_TransitionModel{"project/characters/skinned_caustic_prism/model"};
static constexpr SkinnedCausticMaterialRef s_GlassMaterial{"project/smoke/transparent_multi/materials/shared"};
static constexpr SkinnedCausticMaterialRef s_GroundMaterial{"project/smoke/transparent_multi/materials/ground"};
static constexpr SkinnedCausticMeshRef s_GroundMesh{"project/meshes/shadow_plane"};
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";
static constexpr Name s_ModelSkeletonObject("skeleton");
static constexpr Name s_ModelMeshObject("mesh");
static constexpr Name s_TransitionReceiverGroup("project/smoke/skinned_caustic/transition_receiver");
static constexpr u64 s_TransitionMinimumFrame = 360u;
static constexpr f64 s_TransitionWarmupSeconds = 30.0;
static constexpr u64 s_TransitionPhaseFrames = 64u;
static constexpr u64 s_TransitionCaptureInterval = 32u;
static constexpr u32 s_TransitionPhaseCount = 4u;
static constexpr u32 s_TransitionSampleCount = 8u;

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
    const SIMDVector angle = VectorScale(waves, s_PoseAnimationAngle);
    const SIMDVector rotationAngles = VectorMergeX(
        VectorScale(angle, 0.4f),
        angle,
        VectorScale(angle, 0.25f),
        VectorZero()
    );

    const SIMDMatrix rotation = MatrixRotationRollPitchYawFromVector(rotationAngles);
    const SIMDMatrix animated = MatrixMultiply(bindJoint, rotation);
    if(!MatrixIsInvertibleAffine(
        animated,
        NWB::Impl::SkeletonRuntime::s_AffineEpsilon,
        NWB::Impl::SkeletonRuntime::s_JointDeterminantEpsilon
    ))
        return bindJoint;

    return animated;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedCausticSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> CreateWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = NWB::Tests::Smoke::CreateSmokeWorldOrDie(context, NWB_TEXT("SkinnedCausticSmokeProject"));

        AddSmokeSkinnedRenderSystems(*world, context);
        if(NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_SKINNED_CAUSTIC_CSG_TRANSITION")){
            auto* renderer = world->getSystem<NWB::Impl::RendererSystem>();
            NWB_ASSERT(renderer);
            renderer->setFrameLaggedAsyncLightingEnabled(
                NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_SKINNED_CAUSTIC_CSG_LAGGED")
            );
        }
        return world;
    }

    [[nodiscard]] static bool ShouldCapture(void* const context, const u64 frame)noexcept{
        return frame >= static_cast<SkinnedCausticSmokeProject*>(context)->m_nextCaptureFrame;
    }


private:
    void destroyWorld(){
        if(m_capture){
            m_capture->stop();
            m_capture.reset();
        }
        DestroySmokeSkinnedRenderWorld(m_context, m_world);
    }

    [[nodiscard]] bool installTransitionCutter(){
        const auto meshEntity = FindSpawnedModelObject(
            *m_world, m_character, s_ModelMeshObject, NWB::Impl::ModelObjectKind::SkinnedMesh
        );
        if(!meshEntity.valid())
            return false;
        NWB::Tests::Smoke::AddSkinnedCsgMeshReceiver(*m_world, meshEntity, s_TransitionReceiverGroup, false, true);
        auto entity = m_world->createEntity();
        auto& cutter = entity.addComponent<NWB::Impl::CsgCutterComponent>(m_context.objectArena);
        cutter.receiverGroup = s_TransitionReceiverGroup;
        cutter.shapeType = Name("engine/csg/plane");
        cutter.active = false;
        NWB::Impl::CsgPlaneShapeParameters parameters;
        parameters.normalDistance = Float4(1.0f, 0.0f, 0.0f, 0.0f);
        NWB::Tests::Smoke::AssignCsgCutterParameters(cutter, parameters);
        m_cutterEntity = entity.id();
        return true;
    }

    [[nodiscard]] bool startTransitionCapture(){
        const auto path = m_transitionSample + 1u == s_TransitionSampleCount
            ? NWB::Tests::Smoke::SmokeEnvironmentString(m_transitionOutput, m_context.objectArena)
            : StringFormat(m_context.objectArena, "{}.{}.bmp", m_transitionOutput, m_transitionSample)
        ;
        const NWB::Tests::Smoke::FramebufferCaptureOptions options{
            .shouldCapture = ShouldCapture,
            .predicateContext = this,
            .requiredWidth = 1280u,
            .requiredHeight = 900u,
            .quitWhenReady = false,
        };
        m_capture = MakeUnique<NWB::Tests::Smoke::FramebufferCapture>(m_context, AStringView(path), 1u, options);
        return m_capture && m_capture->start();
    }

    void foldTransitionTiming(const Name scope, u64& lastPublication, u64 (&phaseSamples)[s_TransitionPhaseCount])noexcept{
        const auto& stats = m_context.gpuTimingView().stats(scope);
        if(!stats.valid() || stats.publishFrameIndex == lastPublication)
            return;
        lastPublication = stats.publishFrameIndex;
        if(stats.firstSampleFrameIndex < m_transitionFirstFrame)
            return;
        const u64 firstPhase = (stats.firstSampleFrameIndex - m_transitionFirstFrame) / s_TransitionPhaseFrames;
        const u64 lastPhase = (stats.lastSampleFrameIndex - m_transitionFirstFrame) / s_TransitionPhaseFrames;
        if(firstPhase == lastPhase && firstPhase < s_TransitionPhaseCount)
            phaseSamples[firstPhase] += stats.sampleCount;
    }

    void recordTransitionRoute(const u64 frame){
        if(!m_transitionPhotonsEnabled || frame <= m_transitionFirstFrame)
            return;
        const u64 sourceFrame = frame - 1u;
        const u64 relativeFrame = sourceFrame - m_transitionFirstFrame;
        const u64 phase = relativeFrame / s_TransitionPhaseFrames;
        if(phase >= s_TransitionPhaseCount || m_phaseRouteRecorded[phase]
            || relativeFrame % s_TransitionPhaseFrames < 8u || m_phasePhotonSamples[phase] == 0u)
            return;
        auto* renderer = m_world->getSystem<NWB::Impl::RendererSystem>();
        NWB_ASSERT(renderer);
        // The public frame-graph telemetry pairs compiled tasks with accepted native packet queue identities.
        NWB::Core::Telemetry::FrameGraphNodeDescs nodes(m_context.objectArena);
        NWB::Core::Telemetry::FrameGraphEdgeDescs edges(m_context.objectArena);
        NWB::Core::Telemetry::FrameGraphPendingNameEdges pendingEdges(m_context.objectArena);
        NWB::Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords queueStatistics(m_context.objectArena);
        NWB::Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetStatistics(m_context.objectArena);
        NWB::Core::Telemetry::FrameGraphBuilder builder(
            nodes, edges, pendingEdges, queueStatistics, packetStatistics, sourceFrame
        );
        if(!renderer->appendFrameGraph(builder)){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticTransition: could not capture public accepted-queue telemetry"));
            m_context.requestQuit();
            return;
        }
        const auto primaryGraphics = m_context.graphics.getDevice().getPrimaryPhysicalQueue(NWB::Core::CommandQueue::Graphics);
        for(const auto& node : nodes){
            if(node.name != Name("render.hardware_caustics.photons") || !node.queueAssignment.present
                || !node.compiledTask.present || !node.queueAssignment.acceptedQueue.valid()
                || node.queueAssignment.acceptance == NWB::Core::Telemetry::FrameGraphQueueAssignmentAcceptance::NotAccepted)
                continue;
            for(const auto& packet : packetStatistics){
                if(packet.packetIndex != node.compiledTask.packetIndex
                    || packet.packetGeneration != node.compiledTask.planGeneration
                    || packet.queue != node.queueAssignment.acceptedQueue || packet.taskCount == 0u
                    || packet.commandListCount == 0u || packet.recoverySubmission)
                    continue;
                const bool offGraphics = packet.queue.index != primaryGraphics.index
                    || packet.queue.deviceGeneration != primaryGraphics.deviceGeneration;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: accepted photon route phase {} source_frame={} queue={} graphics_queue={} generation={} off_graphics={}")
                    , phase
                    , sourceFrame
                    , packet.queue.index
                    , primaryGraphics.index
                    , packet.queue.deviceGeneration
                    , offGraphics ? 1u : 0u
                );
                m_phaseRouteRecorded[phase] = true;
                if(m_transitionLagged && !offGraphics){
                    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: photon reader remains on primary Graphics; asynchronous hazard route unavailable"));
                    m_context.requestQuit();
                }
                return;
            }
        }
    }

    void updateTransitionState(){
        const u64 frame = m_context.graphics.getFrameIndex();
        if(!m_transitionWarmupStarted){
            if(frame < s_TransitionMinimumFrame)
                return;
            m_transitionWarmupStart = TimerNow();
            m_transitionWarmupStarted = true;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: warmup started graphics frame {}"), frame);
        }
        if(!m_transitionStarted){
            const f64 elapsed = DurationInSeconds<f64>(TimerNow(), m_transitionWarmupStart);
            if(elapsed < s_TransitionWarmupSeconds)
                return;
            m_transitionFirstFrame = frame;
            m_nextCaptureFrame = frame + s_TransitionCaptureInterval - 1u;
            m_transitionStarted = true;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: warmup complete elapsed_seconds={:.6f} first_frame={}")
                , elapsed
                , frame
            );
            if(!startTransitionCapture()){
                NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticTransition: failed to start framebuffer capture"));
                m_context.requestQuit();
                return;
            }
        }
        const u64 relativeFrame = frame - m_transitionFirstFrame;
        const u32 phase = static_cast<u32>(Min(relativeFrame / s_TransitionPhaseFrames, static_cast<u64>(s_TransitionPhaseCount - 1u)));
        m_animationTime = static_cast<f64>(relativeFrame % s_TransitionPhaseFrames)
            * static_cast<f64>(s_PI * 2.0f) / (static_cast<f64>(s_TransitionPhaseFrames) * s_PoseAnimationSpeed);
        if(phase != m_transitionPhase){
            auto* cutter = m_world->tryGetComponent<NWB::Impl::CsgCutterComponent>(m_cutterEntity);
            NWB_ASSERT(cutter);
            cutter->active = (phase & 1u) != 0u;
            m_transitionPhase = phase;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: phase {} csg={} graphics frame {}")
                , phase
                , cutter->active ? 1u : 0u
                , frame
            );
        }
        foldTransitionTiming(Name("render.caustic_photons"), m_lastPhotonPublication, m_phasePhotonSamples);
        foldTransitionTiming(Name("mesh_skinning.skinning"), m_lastSkinningPublication, m_phaseSkinningSamples);
        recordTransitionRoute(frame);
    }

    void updateTransitionCapture(){
        if(!m_capture || m_transitionSample == s_TransitionSampleCount)
            return;
        // Poll accepted readbacks without a device join, preserving overlap with next-frame pose writes.
        m_capture->update();
        if(!m_capture->captureReady())
            return;
        const u64 sourceFrame = m_capture->capturedGraphicsFrameIndex();
        if(sourceFrame != m_nextCaptureFrame){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticTransition: missed source frame {} (captured {})"), m_nextCaptureFrame, sourceFrame);
            m_context.requestQuit();
            return;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: sample {} graphics frame {} phase {} pose_frame {}")
            , m_transitionSample
            , sourceFrame
            , m_transitionSample / 2u
            , (sourceFrame - m_transitionFirstFrame) % s_TransitionPhaseFrames
        );
        ++m_transitionSample;
        if(m_transitionSample == s_TransitionSampleCount){
            for(u32 phase = 0u; phase < s_TransitionPhaseCount; ++phase){
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: completed GPU phase {} photon_samples={} skinning_samples={}")
                    , phase
                    , m_phasePhotonSamples[phase]
                    , m_phaseSkinningSamples[phase]
                );
            }
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: complete samples=8"));
            m_capture->finish();
            return;
        }
        m_capture->stop();
        m_capture.reset();
        m_nextCaptureFrame += s_TransitionCaptureInterval;
        if(!startTransitionCapture()){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticTransition: failed to advance framebuffer capture"));
            m_context.requestQuit();
        }
    }

    [[nodiscard]] bool loadSkeletonBindJoints(){
        const auto modelAsset = m_context.assetManager.loadSync(NWB::Impl::Model::AssetTypeName(), (m_transitionEnabled ? s_TransitionModel : s_Model).name());
        if(!modelAsset){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticSmokeProject: failed to load model for skeleton bind joints"));
            return false;
        }
        NWB_ASSERT(*modelAsset);
        const auto* model = NWB::Core::Assets::CastAsset<NWB::Impl::Model>(modelAsset->get());
        if(!model){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticSmokeProject: loaded model has unexpected type"));
            return false;
        }
        if(model->skeletonObjects().empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticSmokeProject: model has no skeleton object"));
            return false;
        }

        const auto skeletonAsset = m_context.assetManager.loadSync(NWB::Impl::Skeleton::AssetTypeName(), model->skeletonObjects().front().skeleton.name());
        if(!skeletonAsset){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticSmokeProject: failed to load skeleton for bind joints"));
            return false;
        }
        NWB_ASSERT(*skeletonAsset);
        const auto* skeleton = NWB::Core::Assets::CastAsset<NWB::Impl::Skeleton>(skeletonAsset->get());
        if(!skeleton){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticSmokeProject: loaded skeleton has unexpected type"));
            return false;
        }
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
            m_transitionEnabled ? s_TransitionModel : s_Model,
            s_GlassMaterial,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.72f, 0.86f, 1.0f, 0.42f),
            Float4(0.0f, m_transitionEnabled ? 0.85f : s_CharacterLift, 0.0f, 0.0f),
            Float4(1.0f, 1.0f, 1.0f, 0.0f),
            tintApplied
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
            StoreFloat(animatedJoint, pose->localJoints[jointIndex]);
        }
    }


public:
    explicit SkinnedCausticSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateWorldOrDie(context))
        , m_bindJoints(context.objectArena)
        , m_transitionOutput(context.objectArena)
    {}

    virtual ~SkinnedCausticSmokeProject()override{
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        // Opt into per-pass GPU timing so m_gpuPassTimingProbe can report the caustic photon/resolve + shadow +
        // skinning pass GPU times each interval (flips the GPU-timing double gate via the Frame).
        m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());

        m_transitionEnabled = NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_SKINNED_CAUSTIC_CSG_TRANSITION");
        if(m_transitionEnabled){
            m_transitionLagged = NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_SKINNED_CAUSTIC_CSG_LAGGED");
            m_transitionPhotonsEnabled = !NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_SKINNED_CAUSTIC_CSG_NO_PHOTONS");
            const auto output = NWB::Tests::Smoke::ReadSmokeEnvironmentText(m_context.objectArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
            if(!output)
                return false;
            m_transitionOutput = *output;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: setup lagged={} caustics={} minimum_frame=360 warmup_seconds=30 phase_frames=64 samples=8 model=closed_prism")
                , NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_SKINNED_CAUSTIC_CSG_LAGGED") ? 1u : 0u
                , NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_SKINNED_CAUSTIC_CSG_NO_PHOTONS") ? 0u : 1u
            );
        }
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
            light->enableCaustics = !m_transitionEnabled
                || !NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_SKINNED_CAUSTIC_CSG_NO_PHOTONS");

        m_groundEntity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_GroundMesh,
            s_GroundMaterial,
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

        if(m_transitionEnabled && !installTransitionCutter()){
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

    virtual bool onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_fpsProbe.recordFrame(safeDelta);
        m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
        if(m_transitionEnabled){
            m_animationTime = static_cast<f64>(m_context.graphics.getFrameIndex() % s_TransitionPhaseFrames)
                * static_cast<f64>(s_PI * 2.0f) / (static_cast<f64>(s_TransitionPhaseFrames) * s_PoseAnimationSpeed);
            updateTransitionState();
        }else{
            m_animationTime += Min(safeDelta, s_MaxAnimationDelta) * s_PoseAnimationSpeed;
        }
        animatePoses();
        m_world->tick(m_transitionEnabled ? 1.0f / 60.0f : safeDelta);
        if(m_transitionEnabled)
            updateTransitionCapture();
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    Vector<NWB::Impl::SkeletonJointMatrix, NWB::Core::Alloc::GlobalArena> m_bindJoints;
    NWB::Core::ECS::EntityID m_groundEntity = NWB::Core::ECS::s_InvalidEntityId;
    NWB::Core::ECS::EntityID m_character = NWB::Core::ECS::s_InvalidEntityId;
    NWB::Core::ECS::EntityID m_skeletonEntity = NWB::Core::ECS::s_InvalidEntityId;
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ NWB_TEXT("SkinnedCausticSmokeProject") };
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ NWB_TEXT("SkinnedCausticSmokeProject") };
    f64 m_animationTime = 0.0;
    NWB::Tests::Smoke::SmokeEnvironmentString m_transitionOutput;
    UniquePtr<NWB::Tests::Smoke::FramebufferCapture> m_capture;
    NWB::Core::ECS::EntityID m_cutterEntity = NWB::Core::ECS::s_InvalidEntityId;
    Timer m_transitionWarmupStart;
    u64 m_transitionFirstFrame = 0u;
    u64 m_nextCaptureFrame = 0u;
    u64 m_lastPhotonPublication = 0u;
    u64 m_lastSkinningPublication = 0u;
    u64 m_phasePhotonSamples[s_TransitionPhaseCount] = {};
    u64 m_phaseSkinningSamples[s_TransitionPhaseCount] = {};
    u32 m_transitionPhase = Limit<u32>::s_Max;
    u32 m_transitionSample = 0u;
    bool m_phaseRouteRecorded[s_TransitionPhaseCount] = {};
    bool m_transitionEnabled = false;
    bool m_transitionLagged = false;
    bool m_transitionPhotonsEnabled = false;
    bool m_transitionWarmupStarted = false;
    bool m_transitionStarted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


TStringView NWB::QueryProjectWindowTitle(){
    return NWB_TEXT("NWB Skinned Caustic Smoke");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_skinned_caustic_smoke::SkinnedCausticSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

