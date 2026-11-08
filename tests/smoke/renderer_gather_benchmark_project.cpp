// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_gather_benchmark_probe.h"
#include "renderer_gather_benchmark_scene.h"
#include "renderer_compiler_statistics_probe.h"
#include "smoke_capture_teardown.h"
#include "smoke_project_helpers.h"
#include "smoke_skinned_scene_helpers.h"

#include <loader/project_entry.h>

#include <impl/ecs_render/module.h>
#include <impl/ecs_mesh/skinning/system.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_gather_benchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_OpaqueWorkload = "opaque";
constexpr AStringView s_HybridWorkload = "hybrid";
constexpr AStringView s_UniqueWorkload = "unique";
constexpr AStringView s_OverridesWorkload = "overrides";
constexpr AStringView s_RuntimeWorkload = "runtime";
constexpr AStringView s_MemoryMode = "memory";

using namespace NWB;
using namespace NWB::Tests::Smoke;

static constexpr f32 s_FixedDelta = 0.016666667f;
static constexpr u32 s_InFlightRanges = 32u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TimingPass final : public NWB::Core::IRenderPass{
public:
    explicit TimingPass(NWB::Core::GraphicsRuntime& graphics)noexcept
        : IRenderPass(graphics)
    {}


public:
    [[nodiscard]] b8 prepareQueries(){
        auto& graphics = getGraphics();
        for(const StringView scope : RendererGatherBenchmarkProbe::s_GpuNames){
            // Hardware-only reflection has no depth pyramid, temporal pass or spatial filter.
            if(scope == "render.reflection_depth_pyramid")
                continue;
            const auto scopeName = Name::FromText(scope);
            if(!scopeName)
                return false;
            if(!graphics.gpuTiming().prepareScopeQueries(*scopeName, graphics.getDevice(), s_InFlightRanges))
                return false;
        }
        return true;
    }

    virtual b8 shouldRenderUnfocused()noexcept override{ return true; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererGatherBenchmarkProject final : public IProjectEntryCallbacks{
public:
    explicit RendererGatherBenchmarkProject(ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateSmokeWorldOrDie(NWB_TEXT("RendererGatherBenchmark"), context))
        , m_scene(*m_world)
        , m_timingPass(context.graphics)
        , m_probe(context.objectArena)
        , m_compilerProbe(context.graphics, context.objectArena)
        , m_workload(context.objectArena)
        , m_output(context.objectArena)
    {}
    virtual ~RendererGatherBenchmarkProject()noexcept override{ destroyWorld(); }


public:
    virtual b8 onStartup()override{
        NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
        auto requestedWorkload = ReadSmokeEnvironmentText(m_context.objectArena, "NWB_GATHER_BENCHMARK_WORKLOAD");
        if(!requestedWorkload)
            return false;
        auto requestedOutput = ReadSmokeEnvironmentText(m_context.objectArena, "NWB_GATHER_BENCHMARK_OUTPUT");
        if(!requestedOutput)
            return false;
        m_workload = Move(*requestedWorkload);
        m_output = Move(*requestedOutput);
        const AStringView workload(m_workload.data(), m_workload.size());
        if(workload != s_OpaqueWorkload && workload != s_HybridWorkload && workload != "shared" && workload != s_UniqueWorkload && workload != s_OverridesWorkload && workload != s_RuntimeWorkload)
            return false;
        const auto mode = ReadSmokeEnvironmentText(scratchArena, "NWB_GATHER_BENCHMARK_MODE");
        if(!mode || (*mode != "timing" && *mode != s_MemoryMode))
            return false;
        m_memoryEnabled = *mode == s_MemoryMode;
        m_runtime = workload == s_RuntimeWorkload;
        if(!m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct) || !m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererGatherBenchmark: hardware ray queries are required"));
            return false;
        }
        if(m_runtime)
            AddSmokeSkinnedRenderSystems(*m_world, m_context);
        else
            AddSmokeRenderSystems(*m_world, m_context);
        NWB::Impl::RendererSystem& renderer = *MakeNotNull(m_world->getSystem<NWB::Impl::RendererSystem>());
        m_worldReady = true;
        NWB::Impl::ReflectionSettings settings;
        settings.traceMode = NWB::Impl::ReflectionTraceMode::Hardware;
        settings.maxHardwareRaysPerFrame = 4096u;
        settings.maxOpticalQueries = 16u;
        settings.temporalEnabled = false;
        settings.spatialFilterEnabled = false;
        settings.screenFeedbackEnabled = false;
        settings.diagnosticsEnabled = false;
        settings.samplingSeed = 0u;
        if(!renderer.setReflectionSettings(settings))
            return false;
        renderer.setRefractionEnabled(true);
        renderer.setRefractionHardwareTracingEnabled(true);
        if(!m_scene.build(workload, m_runtime, scratchArena))
            return false;
        if(!m_timingPass.prepareQueries())
            return false;
        m_context.graphics.addRenderPassToBack(m_timingPass);
        m_timingRegistered = true;
        const auto compilerOutput = ReadSmokeEnvironmentText(scratchArena, "NWB_GATHER_COMPILER_STATISTICS_FILE");
        if(compilerOutput){
            if(!m_compilerProbe.start(AStringView(*compilerOutput), renderer))
                return false;
        }
        NWB::Core::Perf::CaptureOptions capture;
        capture.enabled = true;
        capture.cpuTiming = true;
        capture.gpuTiming = true;
        capture.memory = m_memoryEnabled;
        m_context.setPerfCapture(capture);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererGatherBenchmark: workload {} mode {} fixed objects 64"), ConvertString(workload), ConvertString(*mode));
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererGatherBenchmark: hardware reflection 4096 queries 16 refraction 1 diagnostics 0 temporal 0 spatial 0 feedback 0"));
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererGatherBenchmark: fixed delta 0.016666667 extent 960x720 async compute requested 1"));
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererGatherBenchmark: vsync {}"), m_context.graphics.isVSyncEnabled() ? 1u : 0u);
        return true;
    }

    virtual void onShutdown()override{
        m_compilerProbe.stop();
        if(!m_compilerProbe.write())
            NWB_LOGGER_ERROR(NWB_TEXT("RendererGatherBenchmark: compiler statistics diagnostic is incomplete or could not be written"));
        if(!m_probe.write(AStringView(m_output), AStringView(m_workload), m_memoryEnabled, m_scene.counts().renderers, m_scene.counts().runtimeRenderers, m_scene.counts().transparentRenderers, m_scene.counts().runtimeOwners))
            NWB_LOGGER_ERROR(NWB_TEXT("RendererGatherBenchmark: complete result could not be written; successful frames {}"), m_probe.successfulFrames());
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererGatherBenchmark: shutdown successful frames {}"), m_probe.successfulFrames());
    }

    virtual b8 onUpdate(const f32)override{
        if(!m_probe.poll(m_context.perfSession.report(), m_memoryEnabled)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererGatherBenchmark: incomplete or mismatched successful CPU frame"));
            return false;
        }
        if(m_probe.finished()){
            m_context.requestQuit();
            return true;
        }
        if(m_probe.successfulFrames() == 64u && !m_actualCountsRead){
            if(!readActualCounts())
                return false;
            m_actualCountsRead = true;
        }
        return TickSmokeWorld(s_FixedDelta, *m_world);
    }


private:
    [[nodiscard]] b8 readActualCounts(){
        NWB::Impl::MeshSystem& meshes = *MakeNotNull(m_world->getSystem<NWB::Impl::MeshSystem>());
        m_scene.counts().renderers = 0u;
        m_scene.counts().runtimeRenderers = 0u;
        auto view = m_world->view<NWB::Impl::RendererComponent>();
        for(auto&& [entity, renderer] : view){
            if(!renderer.visible)
                continue;
            const auto meshResult = meshes.resolveRenderableMeshStatus(entity);
            if(!meshResult)
                continue;
            const auto& mesh = *meshResult;
            ++m_scene.counts().renderers;
            if(mesh.runtime)
                ++m_scene.counts().runtimeRenderers;
        }
        if(m_scene.counts().renderers != 65u || (m_runtime && (m_scene.counts().runtimeRenderers != 8u || m_scene.counts().runtimeOwners != 8u)) || (!m_runtime && m_scene.counts().runtimeRenderers != 0u)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererGatherBenchmark: actual ready renderer/runtime counts do not match the workload"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererGatherBenchmark: warmed ready renderers {} runtime {} transparent {}"), m_scene.counts().renderers, m_scene.counts().runtimeRenderers, m_scene.counts().transparentRenderers);
        return true;
    }

    void destroyWorld(){
        m_compilerProbe.stop();
        if(m_timingRegistered){
            m_context.graphics.removeRenderPass(m_timingPass);
            m_timingRegistered = false;
        }
        if(!m_worldReady)
            return;
        if(m_runtime)
            DestroySmokeSkinnedRenderWorld(m_context, m_world);
        else
            DestroySmokeRenderWorld(m_context, m_world);
        m_worldReady = false;
    }


private:
    ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    RendererGatherBenchmarkScene m_scene;
    TimingPass m_timingPass;
    RendererGatherBenchmarkProbe m_probe;
    RendererCompilerStatisticsProbe m_compilerProbe;
    SmokeEnvironmentString m_workload;
    SmokeEnvironmentString m_output;
    b8 m_runtime = false;
    b8 m_memoryEnabled = false;
    b8 m_worldReady = false;
    b8 m_timingRegistered = false;
    b8 m_actualCountsRead = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){ return { 960u, 720u }; }

TStringView NWB::QueryProjectWindowTitle()noexcept{ return NWB_TEXT("NWB Renderer Gather Benchmark"); }

UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_renderer_gather_benchmark::RendererGatherBenchmarkProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

