// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "smoke_capture_factory.h"
#include "framebuffer_capture.h"
#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "reflection_csg_context_probe.h"
#include "reflection_smoke_configuration.h"
#include "reflection_cpu_timing_probe.h"
#include "reflection_feedback_scene.h"
#include "reflection_optical_scene.h"
#include "reflection_roughness_scene.h"
#include "reflection_reference_scene.h"
#include "reflection_spatial_owner_probe.h"
#include "reflection_statistics_reporter.h"
#include "reflection_timing_render_pass.h"
#include "smoke_capture_teardown.h"
#include "smoke_environment.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"

#include <loader/project_entry.h>

#include <impl/assets/graphics/reflection/depth_constants.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/reflection/task_graph_reflection.h>
#include <impl/ecs_scene/module.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/entity.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/render_pass.h>
#include <core/graphics/runtime/runtime.h>
#include <core/telemetry/frame_graph_contributor.h>

#include <global/simd/constant.h>
#include <global/simd/convert.h>
#include <global/simd/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_SmokeCaseEnvironment = "NWB_REFLECTION_SMOKE_CASE";
constexpr TStringView s_ProjectName = NWB_TEXT("ReflectionSmokeProject");
constexpr AStringView s_OffscreenCase = "offscreen";
constexpr AStringView s_RoughCase = "rough";
constexpr AStringView s_CapturePathEnvironment = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH";
constexpr AStringView s_OnscreenMovedCase = "onscreen_moved";
constexpr AStringView s_BoundaryCase = "boundary";
constexpr AStringView s_FloorCase = "floor";
constexpr AStringView s_MovedCase = "moved";
constexpr AStringView s_OpaqueGlassCase = "opaque_glass";

using namespace NWB::Tests::Smoke;




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB::Impl::RendererSystem& CreateReflectionRenderer(NWB::Core::ECS::World& world, NWB::ProjectRuntimeContext& context){
    NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
    const auto caseText = ReadSmokeEnvironmentText(scratchArena, s_SmokeCaseEnvironment);
    const b8 hasCase = caseText.has_value();
    const AStringView caseName = hasCase ? AStringView(caseText->data(), caseText->size()) : AStringView{};
    if(hasCase && (caseName == "temporal_deform" || caseName == "rough_deform")){
        AddSmokeSkinnedRenderSystems(world, context);
        return *MakeNotNull(world.getSystem<NWB::Impl::RendererSystem>());
    }
    return AddSmokeRenderSystems(world, context);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ReflectionSmokeProject final : public NWB::IProjectEntryCallbacks{
public:
    explicit ReflectionSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateSmokeWorldOrDie(s_ProjectName, context))
        , m_renderer(CreateReflectionRenderer(*m_world, context)){}
    virtual ~ReflectionSmokeProject()noexcept override{ destroyWorld(); }


public:
    virtual b8 onStartup()override{
        NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
        const auto caseText = ReadSmokeEnvironmentText(scratchArena, s_SmokeCaseEnvironment);
        const b8 hasCase = caseText.has_value();
        const AStringView caseName = hasCase ? AStringView(caseText->data(), caseText->size()) : AStringView(s_OffscreenCase);
        m_feedbackCase = caseName.starts_with("feedback_");
        m_opticalCase = caseName.starts_with("optical_");
        m_csgOpticalCase = caseName.starts_with("optical_csg_") && caseName != "optical_csg_reference";
        m_extendedCase = m_feedbackCase || m_opticalCase || caseName.starts_with(s_RoughCase) || caseName.starts_with("temporal_");
        m_opticalTir = caseName == "optical_tir";
        m_furnace = caseName == "rough_furnace";
        m_glassRoughnessCase = caseName == "rough_glass";
        if(!m_configuration.configure(m_renderer, m_context.graphics, m_extendedCase, m_feedbackCase, m_opticalCase, m_furnace, m_opticalTir, scratchArena))
            return false;
        b8 cpuDiagnostics = false;
        if(!ReflectionSmokeConfiguration::ReadFlag("NWB_REFLECTION_SMOKE_CPU_DIAGNOSTICS", scratchArena, cpuDiagnostics))
            return false;
        const auto cpuTimingPath = ReadSmokeEnvironmentText(scratchArena, "NWB_REFLECTION_CPU_GPU_TIMING_FILE");
        if(cpuTimingPath.has_value() != cpuDiagnostics){
            NWB_LOGGER_ERROR(NWB_TEXT("ReflectionCpuTiming: diagnostic request and explicit output path must agree"));
            return false;
        }
        if(cpuDiagnostics){
            if(
                m_configuration.settings().diagnosticsEnabled || m_configuration.settings().temporalEnabled
                || m_configuration.settings().spatialFilterEnabled || m_configuration.settings().screenFeedbackEnabled
                || m_configuration.settings().traceMode != NWB::Impl::ReflectionTraceMode::Hardware
                || ReadSmokeEnvironmentText(scratchArena, s_CapturePathEnvironment)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("ReflectionCpuTiming: requires raw hardware reflection without capture or path diagnostics"));
                return false;
            }
            m_cpuTimingProbe = MakeUnique<ReflectionCpuTimingProbe>(m_context);
            if(!m_cpuTimingProbe->initialize(caseName, scratchArena))
                return false;
        }
        const auto spatialOwnerSelection = ReadSmokeEnvironmentText(scratchArena, "NWB_REFLECTION_SMOKE_SPATIAL_OWNER");
        if(spatialOwnerSelection){
            if(caseName != s_RoughCase || m_configuration.authoredRoughness() != 0.4f){
                NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSpatialOwner: requires the unchanged roughness0.4 scene"));
                return false;
            }
            m_spatialOwnerProbe = MakeUnique<ReflectionSpatialOwnerProbe>(m_context, m_renderer);
            if(!m_spatialOwnerProbe || !m_spatialOwnerProbe->configure(AStringView(spatialOwnerSelection->data(), spatialOwnerSelection->size()), m_configuration.settings()) || !m_renderer.setReflectionSettings(m_configuration.settings()))
                return false;
        }
        const b8 screenCase = caseName == "onscreen" || caseName == s_OnscreenMovedCase || caseName == s_BoundaryCase || caseName == s_FloorCase;
        if(caseName != s_OffscreenCase && caseName != s_MovedCase && caseName != s_OpaqueGlassCase && !screenCase && !m_extendedCase){
            NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: unknown case '{}'"), ConvertString(caseName));
            return false;
        }
        const auto cameraId = CreateSmokeCamera(1.4f, 6.0f, 0.0f, *m_world);
        auto& camera = m_world->entity(cameraId).getComponent<NWB::Impl::Scene::CameraComponent>();
        camera.setVerticalFovRadians(s_PI / 3.0f);
        const auto extent = NWB::QueryProjectFrameClientSize();
        camera.setAspectRatio(static_cast<f32>(extent.width) / static_cast<f32>(extent.height));
        m_csgSliceRayCapacity = Min(m_configuration.settings().maxHardwareRaysPerFrame, 2u * extent.width * extent.height);
        const auto light = NWB::Impl::Scene::CreateDirectionalLightEntity(*m_world, 0.6f, 0.4f, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f);
        NWB_FATAL_ASSERT_MSG(cameraId.valid() && light.valid(), NWB_TEXT("ReflectionSmokeProject: camera/light creation failed"));
        if(m_opticalCase)
            m_world->entity(light).getComponent<NWB::Impl::Scene::LightComponent>().enableCaustics = false;

        ReflectionReferenceScene referenceScene(*m_world, m_glassRoughnessCase, m_configuration.authoredRoughness());
        if(m_feedbackCase){
            m_feedbackScene = MakeUnique<ReflectionFeedbackScene>(*m_world);
            if(!m_feedbackScene->create(caseName, m_configuration.freshFinalState()))
                return false;
        }
        else if(caseName == "optical_csg_context_transition"){
            m_csgContextProbe = MakeUnique<ReflectionCsgContextProbe>(m_csgSliceRayCapacity, m_context, *m_world, m_renderer);
            if(!m_csgContextProbe || !m_csgContextProbe->create(scratchArena))
                return false;
        }
        else if(m_opticalCase){
            if(!CreateReflectionOpticalScene(caseName, *m_world))
                return false;
        }
        else if(m_extendedCase && !m_glassRoughnessCase){
            m_roughnessScene = MakeUnique<ReflectionRoughnessScene>(cameraId, light, m_configuration.authoredRoughness(), m_context, *m_world);
            if(!m_roughnessScene->create(caseName, m_configuration.freshFinalState()))
                return false;
            if(m_roughnessScene->mutationCase() && !m_roughnessScene->deforming() && m_configuration.settings().temporalEnabled && (m_configuration.targetSamples() < 32u || m_configuration.postResetSamples() > m_configuration.targetSamples())){
                NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: mutation history cap must cover warm-up and post-reset samples"));
                return false;
            }
        }
        else if(caseName == s_OpaqueGlassCase || m_glassRoughnessCase)
            referenceScene.createOpaqueGlassScene();
        else if(caseName == s_FloorCase)
            referenceScene.createFloorMirrorScene();
        else if(screenCase)
            referenceScene.createOnscreenMirrorScene(caseName == s_OnscreenMovedCase, caseName == s_BoundaryCase);
        else
            referenceScene.createMirrorScene(caseName == s_MovedCase);
        if(!configureFramebufferCapture(scratchArena))
            return false;
        if(m_spatialOwnerProbe && !m_framebufferCapture){
            NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSpatialOwner: application framebuffer capture is required"));
            return false;
        }
        if(ReadSmokeEnvironmentFlag("NWB_REFLECTION_SMOKE_TIMING") || m_spatialOwnerProbe || m_cpuTimingProbe){
            auto captureOptions = NWB::Core::Perf::CaptureOptions::GpuTimingOnly();
            captureOptions.cpuTiming = m_cpuTimingProbe != nullptr;
            m_context.setPerfCapture(captureOptions);
            if(!m_timingRenderPass.prepareQueries(extent.width, extent.height))
                return false;
            m_context.graphics.addRenderPassToBack(m_timingRenderPass);
            m_timingRenderPassRegistered = true;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: timing render unfocused 1"));
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: case {} created"), ConvertString(caseName));
        return true;
    }

    virtual void onShutdown()override{
        if(m_cpuTimingProbe)
            m_cpuTimingProbe->finish();
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: shutdown"));
    }

    virtual b8 onUpdate(const f32 delta)override{
        if(m_cpuTimingProbe && !m_cpuTimingProbe->update())
            return false;
        if(m_csgContextProbe && !m_csgContextProbe->sampleSynchronizedFrame(m_statisticsReporter.latest()))
            return false;
        if(m_framebufferCapture)
            m_framebufferCapture->update();
        if(m_spatialOwnerProbe && !m_spatialOwnerProbe->update(m_statisticsReporter.latest(), m_context.gpuTimingView(), m_configuration.settings()))
            return false;
        reportCsgSlicePackets();
        updateRoughnessScene();
        updateFeedbackScene();
        if(!m_cpuTimingProbe){
            m_fpsProbe.recordFrame(delta);
            m_gpuPassTimingProbe.recordFrame(delta, m_context.gpuTimingView());
        }
        const f32 fixedDelta = RendererBaselineFixedDelta();
        if(!TickSmokeWorld(fixedDelta > 0.0f ? fixedDelta : delta, *m_world))
            return false;
        reportReflectionStatistics();
        return true;
    }


private:
    void updateRoughnessScene(){
        if(!m_roughnessScene || !m_roughnessScene->mutationCase() || m_mutationApplied || m_statisticsReporter.latest().sequence == 0u)
            return;
        const b8 warmed = m_roughnessScene->deforming() || !m_configuration.settings().temporalEnabled ? m_statisticsReporter.latest().frameIndex >= 32u : m_statisticsReporter.latest().historySampleCount >= 32u;
        if(!warmed)
            return;
        m_mutationGraphicsFrame = m_context.graphics.getFrameIndex();
        if(m_configuration.freshFinalState()){
            m_configuration.settings().samplingSeed = m_configuration.requestedSeed();
            NWB_FATAL_ASSERT_MSG(m_renderer.setReflectionSettings(m_configuration.settings()), NWB_TEXT("ReflectionSmokeProject: fresh seed reset failed"));
        }
        else
            NWB_FATAL_ASSERT_MSG(m_roughnessScene->applyMutation(), NWB_TEXT("ReflectionSmokeProject: scene mutation failed"));
        m_mutationApplied = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeMutation: graphics_frame={} fresh_final={} seed={}"), m_mutationGraphicsFrame, m_configuration.freshFinalState() ? 1u : 0u, m_configuration.requestedSeed());
    }

    void updateFeedbackScene(){
        if(!m_feedbackScene || !m_feedbackScene->mutationCase() || m_mutationApplied)
            return;
        if(m_statisticsReporter.latest().feedbackSequence < m_configuration.targetSamples() || (!m_configuration.freshFinalState() && !m_statisticsReporter.feedbackObservedBypass()))
            return;
        m_mutationGraphicsFrame = m_context.graphics.getFrameIndex();
        if(m_configuration.freshFinalState()){
            m_configuration.settings().samplingSeed = m_configuration.requestedSeed();
            NWB_FATAL_ASSERT_MSG(m_renderer.setReflectionSettings(m_configuration.settings()), NWB_TEXT("ReflectionSmokeProject: fresh feedback reset failed"));
        }
        else
            NWB_FATAL_ASSERT_MSG(m_feedbackScene->applyMutation(), NWB_TEXT("ReflectionSmokeProject: feedback mutation failed"));
        m_mutationApplied = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeFeedbackMutation: graphics_frame={} fresh_final={}"), m_mutationGraphicsFrame, m_configuration.freshFinalState() ? 1u : 0u);
    }

    b8 shouldCapture(const u64 graphicsFrame)const noexcept{
        if(m_spatialOwnerProbe)
            return m_spatialOwnerProbe->shouldCapture(graphicsFrame);
        if(m_feedbackScene && m_feedbackScene->mutationCase())
            return m_mutationApplied && graphicsFrame == m_mutationGraphicsFrame;
        if(m_configuration.feedbackCapture())
            return m_statisticsReporter.latest().feedbackSequence >= m_configuration.targetSamples();
        if(m_roughnessScene && m_roughnessScene->mutationCase()){
            if(!m_mutationApplied)
                return false;
            if(m_configuration.postResetSamples() == 1u)
                return graphicsFrame == m_mutationGraphicsFrame;
            return m_statisticsReporter.latest().historyStartGraphicsFrame == m_mutationGraphicsFrame && m_statisticsReporter.latest().historySampleCount >= m_configuration.postResetSamples();
        }
        return m_configuration.settings().temporalEnabled && !(m_roughnessScene && m_roughnessScene->deforming()) ? m_statisticsReporter.latest().historySampleCount >= m_configuration.targetSamples() : m_statisticsReporter.latest().frameIndex >= Max(m_configuration.targetSamples(), 3u);
    }

    void reportCsgSlicePackets(){
        if(!m_csgOpticalCase || !m_configuration.settings().diagnosticsEnabled || m_csgContextProbe)
            return;
        const u64 frame = m_context.graphics.getFrameIndex();
        if(frame < 3u || frame - 1u == m_slicePacketSourceFrame)
            return;
        m_slicePacketSourceFrame = frame - 1u;
        ReportReflectionSlicePackets(m_csgSliceRayCapacity, m_slicePacketSourceFrame, true, m_context, m_renderer);
    }

    void reportReflectionStatistics(){
        if(!m_statisticsReporter.poll(m_renderer))
            return;
        const NWB::Impl::ReflectionStatistics& statistics = m_statisticsReporter.latest();
        if(
            (m_extendedCase || m_configuration.feedbackCapture()) && m_framebufferCapture && m_framebufferCapture->captureReady()
            && statistics.graphicsFrameIndex >= m_framebufferCapture->capturedGraphicsFrameIndex()
            && (!m_spatialOwnerProbe || m_spatialOwnerProbe->canFinish(statistics, m_framebufferCapture->capturedGraphicsFrameIndex()))
        )
            m_framebufferCapture->finish();
    }

    b8 configureFramebufferCapture(NWB::Core::Alloc::ScratchArena& scratchArena){
        if(m_csgContextProbe)
            return true;
        FramebufferCaptureOptions options;
        if(m_extendedCase || m_configuration.feedbackCapture()){
            if(!m_configuration.settings().diagnosticsEnabled){
                // Timing-only extended scenes do not request completed-state capture.
                const auto capturePath = ReadSmokeEnvironmentText(scratchArena, s_CapturePathEnvironment);
                if(capturePath){
                    NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: controlled completed-state capture requires diagnostics"));
                    return false;
                }
            }
            options.quitWhenReady = false;
            options.predicateContext = this;
            options.shouldCapture = [](void* owner, const u64 graphicsFrame)noexcept{
                return MakeNotNull(static_cast<ReflectionSmokeProject*>(owner))->shouldCapture(graphicsFrame);
            };
        }
        constexpr u32 s_DefaultCaptureFrameCount = 16u;
        auto capture = NWB::Tests::Smoke::SmokeCaptureFactory(m_context).configure(s_ProjectName, s_DefaultCaptureFrameCount, options, scratchArena);
        if(!capture)
            return false;
        m_framebufferCapture = Move(*capture);
        return true;
    }

    void destroyWorld(){
        m_csgContextProbe.reset();
        if(m_timingRenderPassRegistered){
            m_context.graphics.removeRenderPass(m_timingRenderPass);
            m_timingRenderPassRegistered = false;
        }
        if(m_world.owner() && m_world->getSystem<NWB::Impl::MeshSkinningSystem>())
            DestroySmokeSkinnedRenderWorld(m_context, m_world);
        else
            DestroySmokeRenderWorld(m_context, m_world);
        if(m_framebufferCapture){
            m_framebufferCapture->stop();
            m_framebufferCapture.reset();
        }
        m_spatialOwnerProbe.reset();
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Impl::RendererSystem& m_renderer;
    ReflectionTimingRenderPass m_timingRenderPass{ m_context.graphics };
    UniquePtr<FramebufferCapture> m_framebufferCapture;
    FpsProbe m_fpsProbe{ s_ProjectName };
    GpuPassTimingProbe m_gpuPassTimingProbe{ s_ProjectName };
    ReflectionStatisticsReporter m_statisticsReporter;
    ReflectionSmokeConfiguration m_configuration;
    u64 m_slicePacketSourceFrame = Limit<u64>::s_Max;
    UniquePtr<ReflectionRoughnessScene> m_roughnessScene;
    UniquePtr<ReflectionFeedbackScene> m_feedbackScene;
    UniquePtr<ReflectionSpatialOwnerProbe> m_spatialOwnerProbe;
    UniquePtr<ReflectionCsgContextProbe> m_csgContextProbe;
    UniquePtr<ReflectionCpuTimingProbe> m_cpuTimingProbe;
    u64 m_mutationGraphicsFrame = Limit<u64>::s_Max;
    u32 m_csgSliceRayCapacity = 0u;
    b8 m_extendedCase = false;
    b8 m_feedbackCase = false;
    b8 m_opticalCase = false;
    b8 m_csgOpticalCase = false;
    b8 m_opticalTir = false;
    b8 m_furnace = false;
    b8 m_glassRoughnessCase = false;
    b8 m_mutationApplied = false;
    b8 m_timingRenderPassRegistered = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    NWB::Core::Alloc::ScratchArena arena(Tests::Smoke::s_SmokeEnvironmentArena);
    const auto extent = Tests::Smoke::ReadSmokeEnvironmentText(arena, "NWB_REFLECTION_SMOKE_EXTENT");
    if(!extent || *extent == "native")
        return { 960, 720 };
    NWB_FATAL_ASSERT_MSG(*extent == "npot", NWB_TEXT("ReflectionSmokeProject: extent must be native or npot"));
    return { 953, 713 };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{ return NWB_TEXT("NWB Reflection Smoke"); }

UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_reflection_smoke::ReflectionSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

