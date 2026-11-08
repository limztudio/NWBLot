// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "arrow_yaw_input_handler.h"
#include "avboit_timing_render_pass.h"
#include "gpu_pass_timing_probe.h"
#include "smoke_capture_teardown.h"
#include "smoke_cpu_gpu_timing_probe.h"
#if defined(NWB_ASYNC_SHADOW_M4_BENCHMARK)
#include "stress_frame_graph_snapshot.h"
#endif
#include "stress_crowd_scene.h"
#include "presentation_fps_probe.h"
#include "presentation_pacing_ring.h"
#include "render_quality_settings.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"

#include <loader/project_entry.h>

#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_model/system.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_mesh/skinning/system.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#if defined(NWB_ASYNC_SHADOW_M4_BENCHMARK)
#include <core/graphics/backend_selection/backend.h>
#endif
#include <core/graphics/runtime/runtime.h>
#include <core/input/module.h>

#include <global/simd/frame.h>
#include <global/simd/constant.h>
#include <global/simd/quaternion.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_stress_test_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_ProjectName = NWB_TEXT("StressTestSmokeProject");

using NWB::Tests::Smoke::AddSmokeSkinnedRenderSystems;
using NWB::Tests::Smoke::ArrowYawInputHandler;
using NWB::Tests::Smoke::CreateSmokeWorldOrDie;
using NWB::Tests::Smoke::DestroySmokeSkinnedRenderWorld;
using NWB::Tests::Smoke::ReadSmokeEnvironmentF32;
using NWB::Tests::Smoke::RendererBaselineCaptureFreezeFrame;
using NWB::Tests::Smoke::RendererBaselineFixedDelta;
using NWB::Tests::Smoke::ReadSmokeEnvironmentFlag;
using NWB::Tests::Smoke::ReadSmokeFrozenYawCached;
using NWB::Tests::Smoke::SetSmokeYawWindowTitle;
using NWB::Tests::Smoke::TickSmokeWorld;

static constexpr u32 s_DefaultCharactersPerClass = 10u;
static constexpr u32 s_ComparisonCharactersPerClass = 5u;

static constexpr f32 s_SpinSpeed = 0.8f;
static constexpr f32 s_ManualYawSpeed = 0.6f;
static constexpr f32 s_MaxSpinDelta = 1.0f / 15.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class StressTestSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    [[nodiscard]] static b8 TimingEnabled(){
        static const b8 s_Enabled = ReadSmokeEnvironmentFlag("NWB_STRESS_SMOKE_TIMING");
        return s_Enabled;
    }

    [[nodiscard]] static u32 M4PixelCaptureFreezeFrame(){
#if defined(NWB_ASYNC_SHADOW_M4_BENCHMARK)
        static const u32 s_CaptureFrame = [](){
            const auto configuredFrame = ReadSmokeEnvironmentF32("NWB_M4_PIXEL_CAPTURE_FREEZE_FRAME");
            if(!configuredFrame || !IsFinite(*configuredFrame) || *configuredFrame < 1.0f)
                return 0u;
            return static_cast<u32>(Min(*configuredFrame, 1000000.0f));
        }();
        return s_CaptureFrame;
#else
        return 0u;
#endif
    }

    // The benchmark compares hardware transparent shadows against an opaque-only scene.
    [[nodiscard]] static b8 HardwareShadowOpaqueBaseline(){
#if defined(NWB_HARDWARE_SHADOW_BOUNDARY_BENCHMARK)
        static const b8 s_Enabled = ReadSmokeEnvironmentFlag("NWB_HARDWARE_SHADOW_BOUNDARY_OPAQUE_BASELINE");
        return s_Enabled;
#else
        return false;
#endif
    }

    [[nodiscard]] static Expected<u32> ReadCharactersPerClass(){
        NWB::Core::Alloc::GlobalArena arena(Name("tests/stress/workload"));
        const auto value = ReadEnvironmentVariable(arena, "NWB_STRESS_CHARACTERS_PER_CLASS");
        if(!value){
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: failed to read workload environment: {}"), ConvertString(value.error().message()));
            return MakeUnexpected(Failure{});
        }
        if(!*value)
            return s_DefaultCharactersPerClass;
        const AStringView text((*value)->data(), (*value)->size());
        if(text == "5")
            return s_ComparisonCharactersPerClass;
        if(text != "10"){
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: characters per class must be exactly 5 or 10"));
            return MakeUnexpected(Failure{});
        }
        return s_DefaultCharactersPerClass;
    }

    static NotNullUniquePtr<NWB::Core::ECS::World> CreateWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(s_ProjectName, context);

        NWB::Tests::Smoke::SmokeRenderQualitySettings settings;
        settings.shadow.transparentSampling = NWB::Impl::TransparentShadowSampling::TemporalOne;
        settings.shadow.receiverResolution = NWB::Impl::ShadowReceiverResolution::Quarter;
        settings.surfelGi.resolveResolution = NWB::Impl::SurfelGiResolveResolution::Quarter;
        settings.softwareShadow.blockerSearch = NWB::Impl::SoftwareShadowBlockerSearch::Center1;
        settings.softwareShadow.coverage = NWB::Impl::SoftwareShadowCoverage::FittedVolume;
        settings.softwareShadow.captureCadence = NWB::Impl::SoftwareShadowCaptureCadence::ReuseTwoFrames;
        settings.caustic.photonGridDivisor = context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery) ? 2u : 4u;
        AddSmokeSkinnedRenderSystems(*world, context, settings);
        return world;
    }

    // Spin bodies with staggered Y phases unless NWB_STRESS_TEST_SPIN_ANGLE pins yaw.
    static f32 FrozenYaw(){
        return ReadSmokeFrozenYawCached("NWB_STRESS_TEST_SPIN_ANGLE");
    }


public:
    explicit StressTestSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateWorldOrDie(context))
        , m_renderer(*MakeNotNull(m_world->getSystem<NWB::Impl::RendererSystem>()))
        , m_scene(context, *m_world)
    {}
    virtual ~StressTestSmokeProject()noexcept override{
        m_context.input.removeHandler(m_arrowYawInput);
        m_timingRenderPass.stop();
        destroyWorld();
    }


public:
    virtual b8 onStartup()override{
        NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
#if defined(NWB_ASYNC_SHADOW_M4_BENCHMARK)
#if defined(NWB_SMOKE_DISABLE_ASYNC_COMPUTE)
        constexpr b8 s_AsyncRequested = false;
#else
        constexpr b8 s_AsyncRequested = true;
#endif
        const auto& device = m_context.graphics.getDevice();
        const auto graphicsQueue = device.getPrimaryPhysicalQueue(NWB::Core::CommandQueue::Graphics);
        const auto computeQueue = device.getPrimaryPhysicalQueue(NWB::Core::CommandQueue::Compute);
        const auto* const graphicsInfo = device.getPhysicalQueueInfo(graphicsQueue);
        const auto* const computeInfo = device.getPhysicalQueueInfo(computeQueue);
        const auto topology = device.getPhysicalQueueTopology();
        const u16 deviceGeneration = device.getDeviceGeneration();
        b8 graphicsRegistered = false;
        b8 computeRegistered = false;
        b8 topologyValid = topology.queues && topology.queueCount != 0u;
        if(topologyValid){
            for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
                const auto& queue = topology.queues[queueIndex];
                topologyValid = topologyValid && queue.id.valid() && queue.id.deviceGeneration == deviceGeneration;
                graphicsRegistered = graphicsRegistered || queue.id == graphicsQueue;
                computeRegistered = computeRegistered || queue.id == computeQueue;
            }
        }
        if(
            !topologyValid || !graphicsQueue.valid() || graphicsQueue.deviceGeneration != deviceGeneration
            || !graphicsInfo || graphicsInfo->id != graphicsQueue || !graphicsRegistered
            || (computeQueue.valid() ? !computeInfo || computeInfo->id != computeQueue || !computeRegistered || computeQueue.deviceGeneration != deviceGeneration : computeQueue != NWB::Core::GpuPhysicalQueueId{} || computeInfo != nullptr)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: M4 selected queue topology is invalid"));
            return false;
        }
        const b8 computeDedicated = computeInfo && computeInfo->dedicated;
        const b8 computeSupported = computeInfo && (computeInfo->capabilities & NWB::Core::GpuQueueCapability::Compute) != NWB::Core::GpuQueueCapability::None;
        const b8 computeGraphics = computeInfo && (computeInfo->capabilities & NWB::Core::GpuQueueCapability::Graphics) != NWB::Core::GpuQueueCapability::None;
        const b8 asyncEffective = s_AsyncRequested && computeQueue != graphicsQueue && computeDedicated && computeSupported && !computeGraphics;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: M4 async compute requested={} effective={} graphicsQueue={}:{} computeQueue={}:{} computeDedicated={} computeSupported={} computeGraphics={}")
            , s_AsyncRequested ? 1u : 0u
            , asyncEffective ? 1u : 0u
            , graphicsQueue.index
            , graphicsQueue.deviceGeneration
            , computeQueue.index
            , computeQueue.deviceGeneration
            , computeDedicated ? 1u : 0u
            , computeSupported ? 1u : 0u
            , computeGraphics ? 1u : 0u
        );
#endif

#if defined(NWB_ASYNC_SHADOW_M4_BENCHMARK)
        auto snapshotPath = NWB::Tests::Smoke::ReadSmokeEnvironmentText(m_context.objectArena, "NWB_STRESS_FRAME_GRAPH_FILE");
        if(snapshotPath)
            m_frameGraphSnapshotPath = Move(*snapshotPath);
        else
            m_frameGraphSnapshotPath.clear();
        if(snapshotPath && (m_timingEnabled || M4PixelCaptureFreezeFrame() == 0u)){
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: frame graph snapshot requires pixel freeze and excludes presentation measurement"));
            return false;
        }
#endif

        const auto charactersPerClass = ReadCharactersPerClass();
        if(!charactersPerClass)
            return false;
        m_charactersPerClass = *charactersPerClass;
        NWB::Impl::ReflectionSettings reflectionSettings;
        reflectionSettings.diagnosticsEnabled = m_reflectionDiagnosticsEnabled;
        if(!NWB::Tests::Smoke::ApplyReflectionQualitySmokeSettings(reflectionSettings, m_renderer, scratchArena))
            return false;
        if(m_reflectionDiagnosticsEnabled)
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: reflection diagnostics enabled"));
        // Report FPS from accepted presentations alongside sampled GPU pass durations.
        const auto cpuTimingPath = NWB::Tests::Smoke::ReadSmokeEnvironmentText(scratchArena, "NWB_STRESS_CPU_TIMING_FILE");
        if(!m_cpuTimingProbe.initialize(m_cpuDiagnosticsEnabled, m_timingEnabled, cpuTimingPath ? AStringView(cpuTimingPath->data(), cpuTimingPath->size()) : AStringView{}))
            return false;
        NWB::Core::Perf::CaptureOptions capture = NWB::Core::Perf::CaptureOptions::GpuTimingOnly();
        capture.cpuTiming = m_cpuDiagnosticsEnabled;
        m_context.setPerfCapture(capture);
        if(m_timingEnabled && (M4PixelCaptureFreezeFrame() != 0u || RendererBaselineCaptureFreezeFrame() != 0u || !m_context.requestQuit)){
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: timing requires continuous submissions and a quit callback"));
            return false;
        }
#if defined(NWB_ASYNC_SHADOW_M4_BENCHMARK) || defined(NWB_HARDWARE_SHADOW_BOUNDARY_BENCHMARK)
        constexpr b8 s_ContinuousBenchmarkRendering = true;
#else
        constexpr b8 s_ContinuousBenchmarkRendering = false;
#endif
        if((m_timingEnabled || s_ContinuousBenchmarkRendering) && !m_timingRenderPass.start(true))
            return false;
        if(m_timingEnabled){
            m_pacingRing.reset();
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: presentation timing warmup_seconds=5 measure_seconds=30 clock=steady accepted_native_present=1"));
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: device capability meshlets={} rayquery={} raypipeline={} accelstruct={} wavelanes={} renderer={}")
                , m_context.graphics.queryFeatureSupport(NWB::Core::Feature::Meshlets) ? 1u : 0u
                , m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery) ? 1u : 0u
                , m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingPipeline) ? 1u : 0u
                , m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct) ? 1u : 0u
                , m_context.graphics.queryWaveLaneCount()
                , m_context.graphics.getRendererString()
            );
        }

        // Register arrow yaw scrubbing at the front of the input chain.
        m_context.input.addHandlerToBack(m_arrowYawInput);

        if(!m_scene.initialize(m_context, m_charactersPerClass, m_charactersPerClass == s_ComparisonCharactersPerClass, HardwareShadowOpaqueBaseline(), scratchArena))
            return false;
        return true;
    }

    virtual void onShutdown()override{
        m_context.graphics.setFrameSubmissionSuspended(false);
        m_context.input.removeHandler(m_arrowYawInput);
        m_timingRenderPass.stop();
        if(m_timingEnabled && !m_timingComplete)
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: presentation measurement incomplete"));
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: shutdown"));
    }

    virtual b8 onUpdate(const f32 delta)override{
        const u32 m4CaptureFreezeFrame = M4PixelCaptureFreezeFrame();
        if(m4CaptureFreezeFrame != 0u && m_m4RenderedFrameCount >= m4CaptureFreezeFrame){
            if(!m_m4PixelCapturePaused){
#if defined(NWB_ASYNC_SHADOW_M4_BENCHMARK)
                if(!m_frameGraphSnapshotPath.empty() && !NWB::Tests::Smoke::CaptureStressFrameGraphSnapshot(m_frameGraphSnapshotPath, m_m4RenderedFrameCount, m_context))
                    return false;
#endif
                // Suspend rendering before logging the held-frame marker so temporal history stops advancing.
                m_context.graphics.setFrameSubmissionSuspended(true);
                m_m4PixelCapturePaused = true;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: M4 pixel capture ready after {} rendered frames; render submission suspended"), m_m4RenderedFrameCount);
            }
            return true;
        }

        const u32 baselineCaptureFreezeFrame = RendererBaselineCaptureFreezeFrame();
        if(baselineCaptureFreezeFrame != 0u && m_m4RenderedFrameCount >= baselineCaptureFreezeFrame){
            if(!m_rendererBaselineCapturePaused){
                // Log the separate baseline capture marker after the requested frame count.
                m_context.graphics.setFrameSubmissionSuspended(true);
                m_rendererBaselineCapturePaused = true;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"), m_m4RenderedFrameCount);
            }
            return true;
        }

        const f32 fixedDelta = RendererBaselineFixedDelta();
        const f32 safeDelta = fixedDelta > 0.0f ? fixedDelta : (IsFinite(delta) ? Max(delta, 0.0f) : 0.0f);
        if(!samplePresentationFps())
            return false;
        m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
        // Yaw priority: fixed override, manual scrub, then automatic spin.
        const f32 frozen = FrozenYaw();
        m_yaw.update(safeDelta, frozen, frozen >= 0.0f, m_arrowYawInput, s_ManualYawSpeed, s_SpinSpeed, s_MaxSpinDelta);
        if(!m_scene.update(m_yaw.yaw()))
            return false;
        SetSmokeYawWindowTitle(m_yaw.yaw(), m_yaw.manualControl(), s_2PI, m_context);
        if(!TickSmokeWorld(safeDelta, *m_world))
            return false;
        reportReflectionStatistics();
        ++m_m4RenderedFrameCount;
        return true;
    }


private:
    void destroyWorld(){
        DestroySmokeSkinnedRenderWorld(m_context, m_world);
    }

    void reportReflectionStatistics(){
        if(!m_reflectionDiagnosticsEnabled)
            return;
        const auto statisticsResult = m_renderer.tryGetLatestReflectionStatistics();
        if(!statisticsResult)
            return;
        const NWB::Impl::ReflectionStatistics& statistics = *statisticsResult;
        if(statistics.sequence == m_reflectionStatisticsSequence && statistics.generation == m_reflectionStatisticsGeneration)
            return;
        m_reflectionStatisticsSequence = statistics.sequence;
        m_reflectionStatisticsGeneration = statistics.generation;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressReflectionStatistics: sequence={} generation={} frame={} graphics_frame={}")
            NWB_TEXT(" hardware_ready={} transport_enabled={} candidates={} hardware_rays={} exterior_eligible_rays={}")
            NWB_TEXT(" query_budget_units={} physical_queries={} bootstrap_events={} transparent_paths={} unsupported_paths={}")
            NWB_TEXT(" screen_attempts={} screen_hits={} screen_returns={} screen_iterations={} screen_limit_misses={}")
            , statistics.sequence
            , statistics.generation
            , statistics.frameIndex
            , statistics.graphicsFrameIndex
            , statistics.hardwareReady ? 1u : 0u
            , statistics.opticalTransportEnabled ? 1u : 0u
            , statistics.candidates
            , statistics.hardwareRays
            , statistics.exteriorEligibleRays
            , statistics.queryBudgetUnits
            , statistics.physicalQueries
            , statistics.bootstrapEvents
            , statistics.transparentPaths
            , statistics.unsupportedPaths
            , statistics.screenAttempts
            , statistics.screenHits
            , statistics.screenReturns
            , statistics.screenIterations
            , statistics.screenLimitMisses
        );
    }

    b8 samplePresentationFps(){
        const u64 successfulPresentations = m_context.graphics.getSuccessfulPresentationCount();
        const Timer observationTime = TimerNow();
        m_pacingRing.record(successfulPresentations, observationTime);
        const auto status = m_fpsProbe.observe(successfulPresentations, observationTime);
        if(status == NWB::Tests::Smoke::PresentationFpsStatus::Invalid){
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: presentation measurement invalid counter or clock"));
            return false;
        }
        if(!m_cpuTimingProbe.observe(m_context.perfSession, m_fpsProbe, successfulPresentations, status == NWB::Tests::Smoke::PresentationFpsStatus::Complete))
            return false;
        if(status == NWB::Tests::Smoke::PresentationFpsStatus::Waiting)
            return true;

        const auto& interval = m_fpsProbe.interval();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: presentation fps avg={} presentations={} seconds={} first={} last={}")
            , interval.averageFps()
            , interval.presentations()
            , interval.wallSeconds
            , interval.firstPresentationCount
            , interval.lastPresentationCount
        );
        if(status == NWB::Tests::Smoke::PresentationFpsStatus::Complete){
            const auto& total = m_fpsProbe.total();
            const NWB::Tests::Smoke::PresentationPacingSummary pacingSummary = m_pacingRing.summarize();
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: presentation pacing samples={} p50ms={} p95ms={} maxms={} stalls50ms={}")
                , pacingSummary.samples
                , pacingSummary.p50Ms
                , pacingSummary.p95Ms
                , pacingSummary.maxMs
                , pacingSummary.stallsOver50Ms
            );
            m_timingComplete = true;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: presentation measurement complete fps={} presentations={} seconds={} first={} last={}")
                , total.averageFps()
                , total.presentations()
                , total.wallSeconds
                , total.firstPresentationCount
                , total.lastPresentationCount
            );
            m_context.requestQuit();
        }
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Impl::RendererSystem& m_renderer;
    NWB::Tests::Smoke::StressCrowdScene m_scene;
    NWB::Tests::Smoke::AvboitTimingRenderPass m_timingRenderPass{ m_context.graphics };
    NWB::Tests::Smoke::PresentationFpsProbe m_fpsProbe{ TimingEnabled() ? 5.0 : 0.25, TimingEnabled() ? 30.0 : 0.0 };
    NWB::Tests::Smoke::PresentationPacingRing m_pacingRing;
    NWB::Tests::Smoke::SmokeCpuGpuTimingProbe m_cpuTimingProbe{ NWB_TEXT("stress"), m_context.objectArena };
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ s_ProjectName };
    ArrowYawInputHandler m_arrowYawInput;
#if defined(NWB_ASYNC_SHADOW_M4_BENCHMARK)
    NWB::Tests::Smoke::SmokeEnvironmentString m_frameGraphSnapshotPath{ m_context.objectArena };
#endif
    u64 m_reflectionStatisticsSequence = 0u;
    u64 m_reflectionStatisticsGeneration = 0u;
    NWB::Tests::Smoke::YawSpinController m_yaw;
    u32 m_charactersPerClass = s_DefaultCharactersPerClass;
    u32 m_m4RenderedFrameCount = 0u;
    const b8 m_timingEnabled = TimingEnabled();
    const b8 m_cpuDiagnosticsEnabled = ReadSmokeEnvironmentFlag("NWB_STRESS_CPU_DIAGNOSTICS");
    const b8 m_reflectionDiagnosticsEnabled = ReadSmokeEnvironmentFlag("NWB_STRESS_REFLECTION_DIAGNOSTICS");
    b8 m_timingComplete = false;
    b8 m_m4PixelCapturePaused = false;
    b8 m_rendererBaselineCapturePaused = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{
#if defined(NWB_HARDWARE_SHADOW_BOUNDARY_BENCHMARK)
    return NWB_TEXT("NWB Hardware Shadow Boundary Benchmark");
#elif defined(NWB_ASYNC_SHADOW_M4_BENCHMARK)
    return NWB_TEXT("NWB Async Shadow M4 Benchmark");
#else
    return NWB_TEXT("NWB Stress Test Smoke");
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_stress_test_smoke::StressTestSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

