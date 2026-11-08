// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "smoke_capture_factory.h"
#include "framebuffer_capture.h"
#include "avboit_timing_render_pass.h"
#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "smoke_capture_teardown.h"
#include "refraction_gallery_scene.h"
#include "refraction_scene.h"
#include "refraction_query_diagnostics.h"
#include "smoke_environment.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"
#include "stress_frame_graph_snapshot.h"

#include <loader/project_entry.h>

#include <impl/ecs_scene/module.h>
#include <impl/ecs_render/module.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>

#include <global/simd/constant.h>
#include <global/simd/frame.h>
#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Tests::Smoke;

struct LifecyclePhase{
    AStringView name;
    f32 glassX;
    b8 enabled;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_ProjectName = NWB_TEXT("RefractionSmokeProject");

static constexpr SmokeMeshRef s_SphereMesh{"project/meshes/caustic_sphere"};
static constexpr SmokeMaterialRef s_TransparentMaterial{"project/smoke/refraction/materials/transparent"};

static constexpr Array<LifecyclePhase, 7> s_LifecyclePhases = {{
    { "baseline", 0.0f, false },
    { "active", 0.0f, true },
    { "moved", 1.9f, true },
    { "edge", 4.2f, true },
    { "empty", 100.0f, true },
    { "disabled", 0.0f, false },
    { "reenabled", 0.0f, true },
}};

[[nodiscard]] static Expected<Optional<u32>> ReadDensityPercent(NWB::Core::Alloc::ScratchArena& arena){
    const auto configured = ReadEnvironmentVariable(arena, "NWB_REFRACTION_SMOKE_DENSITY");
    if(!configured){
        NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: failed to read density fixture selection"));
        return MakeUnexpected(Failure{});
    }
    if(!*configured)
        return Optional<u32>{};
    const AStringView text(**configured);
    if(text == "0")
        return Optional<u32>{0u};
    if(text == "1")
        return Optional<u32>{1u};
    if(text == "10")
        return Optional<u32>{10u};
    if(text == "50")
        return Optional<u32>{50u};
    if(text == "100")
        return Optional<u32>{100u};
    NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: density must be exactly 0, 1, 10, 50, or 100"));
    return MakeUnexpected(Failure{});
}

class RefractionSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> CreateWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(s_ProjectName, context);
        auto& renderer = AddSmokeRenderSystems(*world, context);
        // Isolate transmission by disabling reflection.
        NWB::Impl::ReflectionSettings reflection;
        reflection.traceMode = NWB::Impl::ReflectionTraceMode::Disabled;
        NWB_FATAL_ASSERT_MSG(renderer.setReflectionSettings(reflection), NWB_TEXT("RefractionSmokeProject: invalid reflection settings"));
        const auto enabled = ReadSmokeEnvironmentF32("NWB_REFRACTION_SMOKE_ENABLED");
        const b8 refractionEnabled = !enabled || *enabled != 0.0f;
        renderer.setRefractionEnabled(refractionEnabled);
        const auto hardware = ReadSmokeEnvironmentF32("NWB_REFRACTION_SMOKE_HARDWARE");
        const b8 hardwareEnabled = !hardware || *hardware != 0.0f;
        renderer.setRefractionHardwareTracingEnabled(hardwareEnabled);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionSmokeProject: refraction {}"), refractionEnabled ? NWB_TEXT("enabled") : NWB_TEXT("disabled"));
        return world;
    }


public:
    explicit RefractionSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context), m_world(CreateWorldOrDie(context)), m_lifecyclePaths(context.objectArena){}
    virtual ~RefractionSmokeProject()noexcept override{
        m_timingRenderPass.stop();
        DestroyCapturedSmokeRenderWorld(m_context, m_world, m_framebufferCapture);
    }


public:
    virtual b8 onStartup()override{
        NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
        m_lifecycleEnabled = ReadSmokeEnvironmentFlag("NWB_REFRACTION_SMOKE_LIFECYCLE");
        m_timingEnabled = ReadSmokeEnvironmentFlag("NWB_REFRACTION_SMOKE_TIMING");
        const auto density = ReadDensityPercent(scratchArena);
        if(!density)
            return false;
        m_densityEnabled = density->has_value();
        if(!m_queryDiagnostics.configure(scratchArena, m_densityEnabled, m_lifecycleEnabled, m_timingEnabled))
            return false;
        if(!configureFrameGraphSnapshot(scratchArena))
            return false;
        if(m_timingEnabled){
            m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());
            if(!m_timingRenderPass.start())
                return false;
        }
        if(!configureFramebufferCapture(scratchArena))
            return false;

        const auto galleryCase = ReadSmokeEnvironmentText(scratchArena, "NWB_REFRACTION_SMOKE_CASE");
        if((galleryCase && m_lifecycleEnabled) || (m_densityEnabled && (galleryCase || m_lifecycleEnabled))){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: lifecycle, gallery, and density cases are mutually exclusive"));
            return false;
        }
        if(m_densityEnabled)
            return RefractionScene::CreateDensity(**density, scratchArena, *m_world, m_context, m_glass);
        if(galleryCase)
            return CreateRefractionGalleryScene(AStringView(galleryCase->data(), galleryCase->size()), ReadSmokeEnvironmentFlag("NWB_REFRACTION_SMOKE_GEOMETRY"), *m_world);

        const auto camera = CreateSmokeCamera(1.4f, 6.0f, 0.0f, *m_world);
        // Include a light so shared shadow and TLAS preparation runs.
        const auto light = NWB::Impl::Scene::CreateDirectionalLightEntity(*m_world, 0.6f, 0.4f, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f);
        m_glass = CreateTintedStaticMeshEntity(s_SphereMesh, RefractionScene::s_ClearMaterial, RefractionScene::s_MaterialInterface, Float4(1.0f, 1.0f, 1.0f, 1.0f), Float4(0.0f, 1.4f, 0.0f, 0.0f), Float4(1.15f, 1.15f, 1.15f, 0.0f), *m_world);
        NWB_FATAL_ASSERT_MSG(camera.valid() && m_glass.valid() && light.valid(), NWB_TEXT("RefractionSmokeProject: camera/glass/light creation failed"));
        m_world->entity(light).getComponent<NWB::Impl::Scene::LightComponent>().enableCaustics = false;

        // Unlit opaque stripes provide a static background for measuring refraction displacement.
        const u32 stripeCount = m_lifecycleEnabled ? 48u : 20u;
        const f32 stripeStart = m_lifecycleEnabled ? -7.05f : -2.85f;
        for(u32 stripe = 0u; stripe < stripeCount; ++stripe){
            const f32 value = (stripe & 1u) != 0u ? 0.92f : 0.04f;
            const f32 x = static_cast<f32>(stripe) * 0.3f + stripeStart;
            RefractionScene::CreatePanel(RefractionScene::s_OpaqueMaterial, Float4(value, value, value, 1.0f), Float4(x, 1.4f, 2.8f, 0.0f), Float4(0.15f, 1.0f, 2.0f, 0.0f), *m_world);
        }
        // Place the red strip in front of the glass and the blue strip behind it.
        RefractionScene::CreatePanel(s_TransparentMaterial, Float4(1.0f, 0.025f, 0.025f, 0.75f), Float4(0.0f, 0.85f, -1.7f, 0.0f), Float4(1.6f, 1.0f, 0.08f, 0.0f), *m_world);
        RefractionScene::CreatePanel(s_TransparentMaterial, Float4(0.025f, 0.1f, 1.0f, 0.70f), Float4(0.0f, 1.9f, 1.6f, 0.0f), Float4(2.0f, 1.0f, 0.12f, 0.0f), *m_world);

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionSmokeProject: zero-coverage clear sphere + opaque stripes + foreground/background AVBOIT panels created"));
        if(m_lifecycleEnabled)
            applyLifecyclePhase();
        return true;
    }

    virtual void onShutdown()override{
        m_timingRenderPass.stop();
        DestroyCapturedSmokeRenderWorld(m_context, m_world, m_framebufferCapture);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionSmokeProject: shutdown"));
    }

    virtual b8 onUpdate(const f32 delta)override{
        m_queryDiagnostics.update();
        if(m_framebufferCapture)
            m_framebufferCapture->update();
        if(!advanceLifecycleCapture())
            return false;
        if(!updateFrameGraphSnapshot())
            return false;
        if(m_frameGraphSnapshotCompleted)
            return true;
        if(m_timingEnabled){
            m_fpsProbe.recordFrame(delta);
            m_gpuPassTimingProbe.recordFrame(delta, m_context.gpuTimingView());
        }
        const f32 fixedDelta = m_densityEnabled ? 1.f / 60.f : RendererBaselineFixedDelta();
        return TickSmokeWorld(fixedDelta > 0.0f ? fixedDelta : delta, *m_world);
    }


private:
    b8 configureFrameGraphSnapshot(NWB::Core::Alloc::ScratchArena& arena){
        const auto path = ReadEnvironmentVariable(arena, "NWB_REFRACTION_FRAME_GRAPH_SNAPSHOT_PATH");
        if(!path){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: failed to read frame-graph snapshot path"));
            return false;
        }
        if(!*path)
            return true;
        const auto readback = ReadEnvironmentVariable(arena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
        const auto freeze = ReadEnvironmentVariable(arena, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME");
        if(!readback || !freeze || (**path).empty() || m_timingEnabled || m_lifecycleEnabled || m_queryDiagnostics.enabled() || *readback || *freeze){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: a nonempty frame-graph snapshot path requires timing, lifecycle, framebuffer readback, and frame freeze disabled"));
            return false;
        }
        m_frameGraphSnapshotPath = SmokeEnvironmentString(**path, m_context.objectArena);
        return true;
    }

    b8 updateFrameGraphSnapshot(){
        if(m_frameGraphSnapshotPath.empty() || m_frameGraphSnapshotCompleted)
            return true;
        i32 width = 0;
        i32 height = 0;
        m_context.graphics.getWindowDimensions(width, height);
        const auto target = NWB::QueryProjectFrameClientSize();
        const u64 currentFrame = m_context.graphics.getFrameIndex();
        if(width != target.width || height != target.height || (m_frameGraphTargetStart && currentFrame < *m_frameGraphTargetStart)){
            m_frameGraphTargetStart.reset();
            return true;
        }
        if(!m_frameGraphTargetStart){
            m_frameGraphTargetStart = currentFrame;
            return true;
        }
        const u64 completedFrames = currentFrame - *m_frameGraphTargetStart;
        if(completedFrames < 360u || currentFrame == 0u)
            return true;
        if(!CaptureStressFrameGraphSnapshot(AStringView(m_frameGraphSnapshotPath), completedFrames, m_context))
            return false;
        m_frameGraphSnapshotCompleted = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionFrameGraphSnapshot: target_extent {}x{} completed_target_frames {}"), target.width, target.height, completedFrames);
        m_context.requestQuit();
        return true;
    }

    b8 configureFramebufferCapture(NWB::Core::Alloc::ScratchArena& scratchArena){
        constexpr u32 s_DefaultCaptureFrameCount = 16u;
        if(m_lifecycleEnabled){
            const auto output = ReadSmokeEnvironmentText(scratchArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
            if(!output){
                NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: lifecycle requires application framebuffer capture"));
                return false;
            }
            u32 captureFrames = s_DefaultCaptureFrameCount;
            const auto frames = ReadSmokeEnvironmentText(scratchArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT");
            if(frames){
                const auto parsed = ParseU64(AStringView(*frames));
                if(!parsed || *parsed == 0u || *parsed > Limit<u32>::s_Max){
                    NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: lifecycle frame count must be a positive u32"));
                    return false;
                }
                captureFrames = static_cast<u32>(*parsed);
            }
            for(usize phase = 0u; phase < s_LifecyclePhases.size(); ++phase){
                const auto path = phase + 1u == s_LifecyclePhases.size() ? SmokeEnvironmentString(*output, m_context.objectArena) : StringFormat(m_context.objectArena, "{}.{}.bmp", *output, s_LifecyclePhases[phase].name);
                m_lifecyclePaths.emplace_back(m_context.objectArena, AStringView(path));
            }
            const auto extent = NWB::QueryProjectFrameClientSize();
            const FramebufferCaptureOptions options{ .requiredWidth = extent.width, .requiredHeight = extent.height, .quitWhenReady = false, };
            m_framebufferCapture = MakeUnique<FramebufferCapture>(Move(m_lifecyclePaths.front()), captureFrames, m_context, options);
            return m_framebufferCapture && m_framebufferCapture->start();
        }
        FramebufferCaptureOptions options;
        if(m_densityEnabled){
            const auto extent = NWB::QueryProjectFrameClientSize();
            options.requiredWidth = extent.width;
            options.requiredHeight = extent.height;
            // The density harness confirms the exact client/framebuffer extent and readback, then closes normally.
            options.quitWhenReady = false;
        }
        if(m_queryDiagnostics.enabled()){
            options.predicateContext = &m_queryDiagnostics;
            options.shouldCapture = [](void* owner, const u64 graphicsFrame)noexcept{
                const auto& diagnostics = *MakeNotNull(static_cast<RefractionQueryDiagnostics*>(owner));
                return diagnostics.shouldCapture(graphicsFrame);
            };
        }
        auto capture = NWB::Tests::Smoke::SmokeCaptureFactory(m_context).configure(s_ProjectName, s_DefaultCaptureFrameCount, options, scratchArena);
        if(!capture)
            return false;
        m_framebufferCapture = Move(*capture);
        return true;
    }

    void applyLifecyclePhase(){
        const auto& phase = s_LifecyclePhases[m_lifecyclePhase];
        auto& transform = *MakeNotNull(m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(m_glass));
        auto& renderer = *MakeNotNull(m_world->getSystem<NWB::Impl::RendererSystem>());
        transform.position = Float4(phase.glassX, 1.4f, 0.0f, 0.0f);
        renderer.setRefractionEnabled(phase.enabled);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionLifecycle: phase {} applied"), ConvertString(phase.name));
    }

    b8 advanceLifecycleCapture(){
        if(!m_lifecycleEnabled || !m_framebufferCapture->captureReady() || m_lifecyclePhase == s_LifecyclePhases.size())
            return true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionLifecycle: captured {} graphics frame {}"), ConvertString(s_LifecyclePhases[m_lifecyclePhase].name), m_framebufferCapture->capturedGraphicsFrameIndex());
        ++m_lifecyclePhase;
        if(m_lifecyclePhase == s_LifecyclePhases.size()){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionLifecycle: complete samples=7"));
            m_framebufferCapture->finish();
            return true;
        }
        applyLifecyclePhase();
        if(!m_framebufferCapture->rearm(Move(m_lifecyclePaths[m_lifecyclePhase]))){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: failed to advance lifecycle capture"));
            return false;
        }
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    RefractionQueryDiagnostics m_queryDiagnostics{ m_context, *m_world };
    UniquePtr<FramebufferCapture> m_framebufferCapture;
    AvboitTimingRenderPass m_timingRenderPass{ m_context.graphics };
    FpsProbe m_fpsProbe{ s_ProjectName };
    GpuPassTimingProbe m_gpuPassTimingProbe{ s_ProjectName };
    Vector<::Path<NWB::Core::Alloc::GlobalArena>, NWB::Core::Alloc::GlobalArena> m_lifecyclePaths;
    SmokeEnvironmentString m_frameGraphSnapshotPath{ m_context.objectArena };
    Optional<u64> m_frameGraphTargetStart;
    usize m_lifecyclePhase = 0u;
    NWB::Core::ECS::EntityID m_glass = {};
    b8 m_lifecycleEnabled = false;
    b8 m_timingEnabled = false;
    b8 m_densityEnabled = false;
    b8 m_frameGraphSnapshotCompleted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    NWB::Core::Alloc::ScratchArena arena(Tests::Smoke::s_SmokeEnvironmentArena);
    const auto extent = ReadEnvironmentVariable(arena, "NWB_REFRACTION_SMOKE_EXTENT");
    NWB_FATAL_ASSERT_MSG(extent.has_value(), NWB_TEXT("RefractionSmokeProject: failed to read extent"));
    if(!*extent || **extent == "native" || **extent == "960x720")
        return { 960, 720 };
    if(**extent == "1600x900")
        return { 1600, 900 };
    if(**extent == "1920x1080")
        return { 1920, 1080 };
    NWB_FATAL_ASSERT_MSG(**extent == "npot", NWB_TEXT("RefractionSmokeProject: extent must be native, npot, 960x720, 1600x900, or 1920x1080"));
    return { 967, 727 };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{ return NWB_TEXT("NWB Refraction Smoke"); }

UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_refraction_smoke::RefractionSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

