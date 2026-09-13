// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <impl/ecs_render/module.h>
#include <impl/ecs_mesh/skinning/module.h>
#include <impl/ecs_skeleton/components.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>

#include "renderer_gather_benchmark_probe.h"
#include "renderer_compiler_statistics_probe.h"
#include "smoke_project_helpers.h"
#include "smoke_skinned_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_gather_benchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Tests::Smoke;

static constexpr SmokeMeshRef s_Mesh("project/meshes/octahedron");
static constexpr SmokeMeshRef s_Plane("project/meshes/shadow_plane");
static constexpr SmokeMaterialRef s_Opaque("project/smoke/reflection/materials/opaque");
static constexpr SmokeMaterialRef s_Glass("project/smoke/gather_benchmark/materials/glass_00");
static constexpr SmokeModelRef s_Model("project/characters/body/model");
static constexpr AStringView s_Interface = "project/shaders/smoke_surface";
static constexpr f32 s_FixedDelta = 0.016666667f;
static constexpr u32 s_ObjectCount = 64u;
static constexpr u32 s_InFlightRanges = 32u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TimingPass final : public Core::IRenderPass{
public:
    explicit TimingPass(Core::GraphicsRuntime& graphics)
        : IRenderPass(graphics)
    {}

    [[nodiscard]] bool prepareQueries(){
        auto& graphics = getGraphics();
        for(const char* scope : RendererGatherBenchmarkProbe::s_GpuNames){
            // Hardware-only reflection has no depth pyramid, temporal pass or spatial filter.
            if(AStringView(scope) == "render.reflection_depth_pyramid")
                continue;
            if(!graphics.gpuTiming().prepareScopeQueries(Name(scope), graphics.getDevice(), s_InFlightRanges))
                return false;
        }
        return true;
    }

    virtual bool shouldRenderUnfocused()override{ return true; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererGatherBenchmarkProject final : public IProjectEntryCallbacks{
public:
    explicit RendererGatherBenchmarkProject(ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateSmokeWorldOrDie(context, NWB_TEXT("RendererGatherBenchmark")))
        , m_timingPass(context.graphics)
        , m_probe(context.objectArena)
        , m_compilerProbe(context.graphics, context.objectArena)
        , m_workload(context.objectArena)
        , m_output(context.objectArena)
    {}
    virtual ~RendererGatherBenchmarkProject()override{ destroyWorld(); }

    virtual bool onStartup()override{
        if(
            !ReadSmokeEnvironmentText("NWB_GATHER_BENCHMARK_WORKLOAD", m_workload)
            || !ReadSmokeEnvironmentText("NWB_GATHER_BENCHMARK_OUTPUT", m_output)
        )
            return false;
        const AStringView workload(m_workload.data(), m_workload.size());
        if(
            workload != "opaque" && workload != "hybrid" && workload != "shared"
            && workload != "unique" && workload != "overrides" && workload != "runtime"
        )
            return false;
        SmokeEnvironmentString mode(m_context.objectArena);
        if(!ReadSmokeEnvironmentText("NWB_GATHER_BENCHMARK_MODE", mode) || (mode != "timing" && mode != "memory"))
            return false;
        m_memoryEnabled = mode == "memory";
        m_runtime = workload == "runtime";
        if(
            !m_context.graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
            || !m_context.graphics.queryFeatureSupport(Core::Feature::RayQuery)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererGatherBenchmark: hardware ray queries are required"));
            return false;
        }
        Impl::RendererSystem* renderer;
        if(m_runtime){
            AddSmokeSkinnedRenderSystems(*m_world, m_context);
            renderer = m_world->getSystem<Impl::RendererSystem>();
            NWB_ASSERT(renderer);
        }
        else
            renderer = &AddSmokeRenderSystems(*m_world, m_context);
        m_worldReady = true;
        Impl::ReflectionSettings settings;
        settings.traceMode = Impl::ReflectionTraceMode::Hardware;
        settings.maxHardwareRaysPerFrame = 4096u;
        settings.maxOpticalQueries = 16u;
        settings.temporalEnabled = false;
        settings.spatialFilterEnabled = false;
        settings.screenFeedbackEnabled = false;
        settings.diagnosticsEnabled = false;
        settings.samplingSeed = 0u;
        if(!renderer->setReflectionSettings(settings))
            return false;
        renderer->setRefractionEnabled(true);
        renderer->setRefractionHardwareTracingEnabled(true);
        const auto cameraEntity = CreateSmokeCamera(*m_world, 2.6f, 9.0f, 0.0f);
        if(!cameraEntity.valid())
            return false;
        const auto light = Impl::Scene::CreateDirectionalLightEntity(*m_world, 0.9f, 0.65f, 0.f, Float4(1.f, 0.96f, 0.88f), 2.f);
        m_world->entity(light).getComponent<Impl::Scene::LightComponent>().enableCaustics = false;
        if(!createBackdrop())
            return false;
        for(u32 index = 0u; index < s_ObjectCount; ++index){
            const Float4 position((static_cast<f32>(index % 8u) - 3.5f) * 0.75f,
                0.7f + static_cast<f32>(index / 8u) * 0.58f, 0.f, 0.f);
            if(m_runtime && index % 8u == 0u){
                if(!createRuntime(index, position))
                    return false;
                continue;
            }
            const bool opaque = workload == "opaque" || (workload == "hybrid" && index % 2u == 0u);
            SmokeMeshRef mesh = s_Mesh;
            SmokeMaterialRef material = opaque ? s_Opaque : s_Glass;
            if(workload == "unique"){
                const auto meshPath = StringFormat(m_context.objectArena, "project/smoke/gather_benchmark/meshes/mesh_{:02}", index);
                const auto materialPath = StringFormat(m_context.objectArena, "project/smoke/gather_benchmark/materials/glass_{:02}", index);
                mesh = SmokeMeshRef(meshPath.c_str());
                material = SmokeMaterialRef(materialPath.c_str());
            }
            auto entity = m_world->createEntity();
            auto& transform = entity.addComponent<Impl::Scene::TransformComponent>();
            transform.position = position;
            transform.scale = Float4(0.43f, 0.43f, 0.43f, 0.f);
            entity.addComponent<Impl::MeshComponent>().mesh = mesh;
            auto& component = entity.addComponent<Impl::RendererComponent>();
            component.material = material;
            if(!opaque){
                component.opticalBoundaryMode = Impl::OpticalBoundaryMode::ClosedNested;
                ++m_transparentRenderers;
            }
            ++m_renderers;
            if(workload == "overrides"){
                entity.addComponent<Impl::MaterialInstanceComponent>(m_context.objectArena, Name(s_Interface));
                if(!Impl::SetMaterialMutableHalf4(*m_world, entity.id(), Name(s_Interface), "runtime.color_tint",
                    Float4(0.55f + static_cast<f32>(index) / 256.f, 0.85f, 1.f, 1.f)))
                    return false;
            }
        }
        if(m_runtime)
            SyncSmokeModelRuntimes(*m_world);
        if(!m_timingPass.prepareQueries())
            return false;
        m_context.graphics.addRenderPassToBack(m_timingPass);
        m_timingRegistered = true;
        SmokeEnvironmentString compilerOutput(m_context.objectArena);
        if(ReadSmokeEnvironmentText("NWB_GATHER_COMPILER_STATISTICS_FILE", compilerOutput)){
            if(!m_compilerProbe.start(*renderer, MakeNotNull(compilerOutput.c_str())))
                return false;
        }
        Core::Perf::CaptureOptions capture;
        capture.enabled = true;
        capture.cpuTiming = true;
        capture.gpuTiming = true;
        capture.memory = m_memoryEnabled;
        m_context.setPerfCapture(capture);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererGatherBenchmark: workload {} mode {} fixed objects 64")
            , StringConvert(workload), StringConvert(mode)
        );
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererGatherBenchmark: hardware reflection 4096 queries 16 refraction 1 diagnostics 0 temporal 0 spatial 0 feedback 0"));
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererGatherBenchmark: fixed delta 0.016666667 extent 960x720 async compute requested 1"));
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererGatherBenchmark: vsync {}"), m_context.graphics.isVsyncEnabled() ? 1u : 0u);
        return true;
    }

    virtual void onShutdown()override{
        m_compilerProbe.stop();
        if(!m_compilerProbe.write())
            NWB_LOGGER_ERROR(NWB_TEXT("RendererGatherBenchmark: compiler statistics diagnostic is incomplete or could not be written"));
        if(!m_probe.write(MakeNotNull(m_output.c_str()), MakeNotNull(m_workload.c_str()), m_memoryEnabled, m_renderers, m_runtimeRenderers, m_transparentRenderers, m_runtimeOwners))
            NWB_LOGGER_ERROR(NWB_TEXT("RendererGatherBenchmark: complete result could not be written; successful frames {}"), m_probe.successfulFrames());
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererGatherBenchmark: shutdown successful frames {}"), m_probe.successfulFrames());
    }

    virtual bool onUpdate(const f32)override{
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
        m_world->tick(s_FixedDelta);
        return true;
    }


private:
    [[nodiscard]] bool createBackdrop(){
        const auto backdrop = CreateTintedStaticMeshEntity(*m_world, m_context.objectArena, s_Plane, s_Opaque, s_Interface,
            Float4(0.1f, 0.1f, 0.1f, 1.f), Float4(0.f, 2.6f, 2.f, 0.f), Float4(5.f, 1.f, 3.5f, 0.f));
        if(!backdrop.valid())
            return false;
        auto& transform = m_world->entity(backdrop).getComponent<Impl::Scene::TransformComponent>();
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.f, 0.f), transform.rotation);
        const Half4U f0 = MakeHalf4U(0.35f, 0.35f, 0.35f, 0.f);
        const Half roughness = ConvertFloatToHalf(0.f);
        if(!Impl::SetMaterialMutableParameter(*m_world, backdrop, Name(s_Interface), "runtime.specular_f0",
            Impl::MaterialLayoutFieldType::Half3, Impl::PackMaterialInstanceBytes(f0.raw, sizeof(Half) * 3u)))
            return false;
        if(!Impl::SetMaterialMutableParameter(*m_world, backdrop, Name(s_Interface), "runtime.perceptual_roughness",
            Impl::MaterialLayoutFieldType::Half, Impl::PackMaterialInstanceBytes(&roughness, sizeof(roughness))))
            return false;
        ++m_renderers;
        return true;
    }

    [[nodiscard]] bool createRuntime(const u32 index, const Float4& position){
        bool tintApplied = false;
        const auto owner = CreateTintedModelEntity(*m_world, m_context.objectArena, s_Model, s_Opaque, s_Interface,
            Float4(0.9f, 0.45f, 0.2f, 1.f), position, Float4(0.22f, 0.22f, 0.22f, 0.f), &tintApplied);
        if(!owner.valid() || !tintApplied)
            return false;
        SyncSmokeModelRuntimes(*m_world);
        const auto skeleton = FindSpawnedModelObject(*m_world, owner, Name("skeleton"), Impl::ModelObjectKind::Skeleton);
        if(!skeleton.valid())
            return false;
        auto& pose = m_world->entity(skeleton).getComponent<Impl::SkeletonPoseComponent>();
        if(pose.localJoints.size() < 2u)
            return false;
        const f32 yaw = (index & 8u) != 0u ? 0.08f : -0.08f;
        StoreFloat(MatrixMultiply(LoadFloat(pose.localJoints[1u]), MatrixRotationRollPitchYaw(0.f, yaw, 0.f)), pose.localJoints[1u]);
        // Held real skeletal pose: runtime preparation stays live, with no animation-phase difference between arms.
        ++m_runtimeOwners;
        return true;
    }

    [[nodiscard]] bool readActualCounts(){
        auto* meshes = m_world->getSystem<Impl::MeshSystem>();
        NWB_ASSERT(meshes);
        m_renderers = 0u;
        m_runtimeRenderers = 0u;
        auto view = m_world->view<Impl::RendererComponent>();
        for(auto&& [entity, renderer] : view){
            if(!renderer.visible)
                continue;
            Impl::RenderableMeshDesc mesh;
            if(!meshes->resolveRenderableMesh(entity, mesh))
                continue;
            ++m_renderers;
            if(mesh.runtime)
                ++m_runtimeRenderers;
        }
        if(m_renderers != 65u || (m_runtime && (m_runtimeRenderers != 8u || m_runtimeOwners != 8u)) || (!m_runtime && m_runtimeRenderers != 0u)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererGatherBenchmark: actual ready renderer/runtime counts do not match the workload"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererGatherBenchmark: warmed ready renderers {} runtime {} transparent {}")
            , m_renderers, m_runtimeRenderers, m_transparentRenderers
        );
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
    NotNullUniquePtr<Core::ECS::World> m_world;
    TimingPass m_timingPass;
    RendererGatherBenchmarkProbe m_probe;
    RendererCompilerStatisticsProbe m_compilerProbe;
    SmokeEnvironmentString m_workload;
    SmokeEnvironmentString m_output;
    u32 m_renderers = 0u;
    u32 m_runtimeRenderers = 0u;
    u32 m_runtimeOwners = 0u;
    u32 m_transparentRenderers = 0u;
    bool m_runtime = false;
    bool m_memoryEnabled = false;
    bool m_worldReady = false;
    bool m_timingRegistered = false;
    bool m_actualCountsRead = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){ return { 960u, 720u }; }
const tchar* NWB::QueryProjectWindowTitle(){ return NWB_TEXT("NWB Renderer Gather Benchmark"); }
UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_renderer_gather_benchmark::RendererGatherBenchmarkProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

