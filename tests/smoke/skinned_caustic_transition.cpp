// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_caustic_transition.h"
#include "csg_smoke_helpers.h"
#include "smoke_capture_factory.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"

#include <impl/ecs_csg/module.h>
#include <impl/ecs_model/system.h>
#include <impl/ecs_render/module.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/telemetry/frame_graph_contributor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace {
constexpr AStringView s_CsgTransitionEnvironment = "NWB_SKINNED_CAUSTIC_CSG_TRANSITION";
constexpr AStringView s_CsgLaggedEnvironment = "NWB_SKINNED_CAUSTIC_CSG_LAGGED";
constexpr AStringView s_CsgNoPhotonsEnvironment = "NWB_SKINNED_CAUSTIC_CSG_NO_PHOTONS";
constexpr Name s_ModelMeshObject("mesh");
constexpr Name s_TransitionReceiverGroup("project/smoke/skinned_caustic/transition_receiver");
constexpr u64 s_TransitionMinimumFrame = 360u;
constexpr f64 s_TransitionWarmupSeconds = 30.0;
constexpr u64 s_TransitionCaptureInterval = 32u;
constexpr u32 s_TransitionSampleCount = 8u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 SkinnedCausticTransition::ShouldCapture(void* const context, const u64 frame)noexcept{
    const auto& project = *MakeNotNull(static_cast<SkinnedCausticTransition*>(context));
    return project.m_transitionStarted && frame >= project.m_nextCaptureFrame;
}


SkinnedCausticTransition::SkinnedCausticTransition(ProjectRuntimeContext& context)
    : m_context(context)
    , m_capturePaths(context.objectArena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SkinnedCausticTransition::prepare(){
    m_transitionEnabled = NWB::Tests::Smoke::ReadSmokeEnvironmentFlag(s_CsgTransitionEnvironment);
    if(m_transitionEnabled){
        NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
        m_transitionLagged = NWB::Tests::Smoke::ReadSmokeEnvironmentFlag(s_CsgLaggedEnvironment);
        m_transitionPhotonsEnabled = !NWB::Tests::Smoke::ReadSmokeEnvironmentFlag(s_CsgNoPhotonsEnvironment);
        const auto output = NWB::Tests::Smoke::ReadSmokeEnvironmentText(scratchArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
        if(!output)
            return false;
        const NWB::Tests::Smoke::FramebufferCaptureOptions captureOptions{ .shouldCapture = ShouldCapture, .predicateContext = this, .requiredWidth = 1280u, .requiredHeight = 900u, .quitWhenReady = false };
        auto captureSeries = NWB::Tests::Smoke::SmokeCaptureFactory(m_context).prepareSeries(AStringView(*output), s_TransitionSampleCount, captureOptions);
        if(!captureSeries)
            return false;
        m_capturePaths = Move(captureSeries->paths);
        m_capture = Move(captureSeries->capture);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: setup lagged={} caustics={} minimum_frame=360 warmup_seconds=30 phase_frames=64 samples=8 model=closed_prism")
            , NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_SKINNED_CAUSTIC_CSG_LAGGED") ? 1u : 0u
            , NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_SKINNED_CAUSTIC_CSG_NO_PHOTONS") ? 0u : 1u
        );
    }
    return true;
}

b8 SkinnedCausticTransition::installTransitionCutter(const Core::ECS::EntityID character, Core::ECS::World& world){
    const auto meshEntity = FindSpawnedModelObject(character, s_ModelMeshObject, NWB::Impl::ModelObjectKind::SkinnedMesh, world);
    if(!meshEntity.valid())
        return false;
    NWB::Tests::Smoke::AddSkinnedCsgMeshReceiver(meshEntity, s_TransitionReceiverGroup, false, true, world);
    auto entity = world.createEntity();
    auto& cutter = entity.addComponent<NWB::Impl::CsgCutterComponent>();
    cutter.receiverGroup = s_TransitionReceiverGroup;
    cutter.shapeType = Name("engine/csg/plane");
    cutter.active = false;
    NWB::Impl::CsgPlaneShapeParameters parameters;
    parameters.normalDistance = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    NWB::Tests::Smoke::AssignCsgCutterParameters(parameters, cutter);
    m_cutterEntity = entity.id();
    return true;
}

void SkinnedCausticTransition::foldTransitionTiming(const Name scope, u64& lastPublication, Array<u64, s_TransitionPhaseCount>& phaseSamples)noexcept{
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

void SkinnedCausticTransition::recordTransitionRoute(const u64 frame, Core::ECS::World& world){
    if(!m_transitionPhotonsEnabled || frame <= m_transitionFirstFrame)
        return;
    const u64 sourceFrame = frame - 1u;
    const u64 relativeFrame = sourceFrame - m_transitionFirstFrame;
    const u64 phase = relativeFrame / s_TransitionPhaseFrames;
    if(phase >= s_TransitionPhaseCount || m_phaseRouteRecorded[phase] || relativeFrame % s_TransitionPhaseFrames < 8u || m_phasePhotonSamples[phase] == 0u)
        return;
    auto& renderer = *MakeNotNull(world.getSystem<NWB::Impl::RendererSystem>());
    // The public frame-graph telemetry pairs compiled tasks with accepted native packet queue identities.
    NWB::Core::Telemetry::FrameGraphNodeDescs nodes(m_context.objectArena);
    NWB::Core::Telemetry::FrameGraphEdgeDescs edges(m_context.objectArena);
    NWB::Core::Telemetry::FrameGraphPendingNameEdges pendingEdges(m_context.objectArena);
    NWB::Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords queueStatistics(m_context.objectArena);
    NWB::Core::Telemetry::FrameGraphPacketSubmissionStatisticsRecords packetStatistics(m_context.objectArena);
    NWB::Core::Alloc::ScratchArena scratchArena(Name("tests/smoke/skinned_caustic_frame_graph"));
    NWB::Core::Telemetry::FrameGraphBuilder builder(scratchArena, nodes, edges, pendingEdges, queueStatistics, packetStatistics, sourceFrame);
    if(!renderer.appendFrameGraph(builder)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticTransition: could not capture public accepted-queue telemetry"));
        m_context.requestQuit();
        return;
    }
    const auto primaryGraphics = m_context.graphics.getDevice().getPrimaryPhysicalQueue(NWB::Core::CommandQueue::Graphics);
    for(const auto& node : nodes){
        if(
            node.name != Name("render.hardware_caustics.photons") || !node.queueAssignment.present
            || !node.compiledTask.present || !node.queueAssignment.acceptedQueue.valid()
            || node.queueAssignment.acceptance == NWB::Core::Telemetry::FrameGraphQueueAssignmentAcceptance::NotAccepted
        )
            continue;
        for(const auto& packet : packetStatistics){
            if(
                packet.packetIndex != node.compiledTask.packetIndex
                || packet.packetGeneration != node.compiledTask.planGeneration
                || packet.queue != node.queueAssignment.acceptedQueue || packet.taskCount == 0u
                || packet.commandListCount == 0u || packet.recoverySubmission
            )
                continue;
            const b8 offGraphics = packet.queue.index != primaryGraphics.index || packet.queue.deviceGeneration != primaryGraphics.deviceGeneration;
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

void SkinnedCausticTransition::updateTransitionState(Core::ECS::World& world, f64& animationTime){
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
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: warmup complete elapsed_seconds={:.6f} first_frame={}"), elapsed, frame);
    }
    const u64 relativeFrame = frame - m_transitionFirstFrame;
    const u32 phase = static_cast<u32>(Min(relativeFrame / s_TransitionPhaseFrames, static_cast<u64>(s_TransitionPhaseCount - 1u)));
    const f64 phaseAngle = static_cast<f64>(relativeFrame % s_TransitionPhaseFrames) * static_cast<f64>(s_PI * 2.0f);
    animationTime = phaseAngle / (static_cast<f64>(s_TransitionPhaseFrames) * s_PoseAnimationSpeed);
    if(phase != m_transitionPhase){
        auto& cutter = *MakeNotNull(world.tryGetComponent<NWB::Impl::CsgCutterComponent>(m_cutterEntity));
        cutter.active = (phase & 1u) != 0u;
        m_transitionPhase = phase;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: phase {} csg={} graphics frame {}"), phase, cutter.active ? 1u : 0u, frame);
    }
    foldTransitionTiming(Name("render.caustic_photons"), m_lastPhotonPublication, m_phasePhotonSamples);
    foldTransitionTiming(Name("mesh_skinning.skinning"), m_lastSkinningPublication, m_phaseSkinningSamples);
    recordTransitionRoute(frame, world);
}

void SkinnedCausticTransition::updateTransitionCapture(){
    if(!m_capture || !m_transitionStarted || m_transitionSample == s_TransitionSampleCount)
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
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: sample {} graphics frame {} phase {} pose_frame {}"), m_transitionSample, sourceFrame, m_transitionSample / 2u, (sourceFrame - m_transitionFirstFrame) % s_TransitionPhaseFrames);
    ++m_transitionSample;
    if(m_transitionSample == s_TransitionSampleCount){
        for(u32 phase = 0u; phase < s_TransitionPhaseCount; ++phase)
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: completed GPU phase {} photon_samples={} skinning_samples={}"), phase, m_phasePhotonSamples[phase], m_phaseSkinningSamples[phase]);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SkinnedCausticTransition: complete samples=8"));
        m_capture->finish();
        return;
    }
    m_nextCaptureFrame += s_TransitionCaptureInterval;
    if(!m_capture->rearm(Move(m_capturePaths[m_transitionSample]))){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedCausticTransition: failed to advance framebuffer capture"));
        m_context.requestQuit();
    }
}


void SkinnedCausticTransition::stop(){
    if(m_capture){
        m_capture->stop();
        m_capture.reset();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

