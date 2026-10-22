// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "framebuffer_capture.h"
#include "avboit_timing_render_pass.h"
#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "smoke_capture_teardown.h"
#include "refraction_gallery_scene.h"
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


constexpr TStringView s_ProjectName = NWB_TEXT("RefractionSmokeProject");

using namespace NWB::Tests::Smoke;

static constexpr SmokeMeshRef s_PlaneMesh{"project/meshes/shadow_plane"};
static constexpr SmokeMeshRef s_SphereMesh{"project/meshes/caustic_sphere"};
static constexpr SmokeMeshRef s_CubeMesh{"project/meshes/cube_hard_edges"};
static constexpr SmokeMaterialRef s_ClearMaterial{"project/smoke/refraction/materials/clear"};
static constexpr SmokeMaterialRef s_OpaqueMaterial{"project/smoke/refraction/materials/opaque"};
static constexpr SmokeMaterialRef s_TransparentMaterial{"project/smoke/refraction/materials/transparent"};
static constexpr AStringView s_MaterialInterface = "project/shaders/smoke_surface";

struct LifecyclePhase{
    AStringView name;
    f32 glassX;
    bool enabled;
};

static constexpr LifecyclePhase s_LifecyclePhases[] = {
    { "baseline", 0.0f, false },
    { "active", 0.0f, true },
    { "moved", 1.9f, true },
    { "edge", 4.2f, true },
    { "empty", 100.0f, true },
    { "disabled", 0.0f, false },
    { "reenabled", 0.0f, true },
};

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
        auto world = CreateSmokeWorldOrDie(context, s_ProjectName);
        auto& renderer = AddSmokeRenderSystems(*world, context);
        // These comparisons isolate transmission. Reflection has its own combined optical capture matrix.
        NWB::Impl::ReflectionSettings reflection;
        reflection.traceMode = NWB::Impl::ReflectionTraceMode::Disabled;
        NWB_FATAL_ASSERT_MSG(renderer.setReflectionSettings(reflection), NWB_TEXT("RefractionSmokeProject: invalid reflection settings"));
        const auto enabled = ReadSmokeEnvironmentF32("NWB_REFRACTION_SMOKE_ENABLED");
        const bool refractionEnabled = !enabled || *enabled != 0.0f;
        renderer.setRefractionEnabled(refractionEnabled);
        const auto hardware = ReadSmokeEnvironmentF32("NWB_REFRACTION_SMOKE_HARDWARE");
        const bool hardwareEnabled = !hardware || *hardware != 0.0f;
        renderer.setRefractionHardwareTracingEnabled(hardwareEnabled);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionSmokeProject: refraction {}"), refractionEnabled ? NWB_TEXT("enabled") : NWB_TEXT("disabled"));
        return world;
    }


private:
    bool configureFrameGraphSnapshot(NWB::Core::Alloc::ScratchArena& arena){
        const auto path = ReadEnvironmentVariable(arena, "NWB_REFRACTION_FRAME_GRAPH_SNAPSHOT_PATH");
        if(!path){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: failed to read frame-graph snapshot path"));
            return false;
        }
        if(!*path)
            return true;
        const auto readback = ReadEnvironmentVariable(arena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
        const auto freeze = ReadEnvironmentVariable(arena, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME");
        if(!readback || !freeze || (**path).empty() || m_timingEnabled || m_lifecycleEnabled || m_diagnosticsEnabled || *readback || *freeze){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: a nonempty frame-graph snapshot path requires timing, lifecycle, framebuffer readback, and frame freeze disabled"));
            return false;
        }
        m_frameGraphSnapshotPath = SmokeEnvironmentString(**path, m_context.objectArena);
        return true;
    }

    bool configureQueryDiagnostics(NWB::Core::Alloc::ScratchArena& arena){
        const auto configured = ReadEnvironmentVariable(arena, "NWB_REFRACTION_SMOKE_DIAGNOSTICS");
        if(!configured || (*configured && **configured != "0" && **configured != "1")){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: diagnostics must be absent, 0, or 1"));
            return false;
        }
        m_diagnosticsEnabled = *configured && **configured == "1";
        if(!m_diagnosticsEnabled)
            return true;
        const auto freeze = ReadEnvironmentVariable(arena, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME");
        const auto readback = ReadSmokeEnvironmentText(arena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
        const auto hardware = ReadEnvironmentVariable(arena, "NWB_REFRACTION_SMOKE_HARDWARE");
        if(!freeze || !hardware || *freeze || !readback || !m_densityEnabled || m_lifecycleEnabled || m_timingEnabled || (*hardware && **hardware != "1")){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: query diagnostics require a hardware density framebuffer capture with timing, lifecycle, and frame freeze disabled"));
            return false;
        }
        auto* const renderer = m_world->getSystem<NWB::Impl::RendererSystem>();
        NWB_FATAL_ASSERT(renderer);
        renderer->setRefractionDiagnosticsEnabled(true);
        return true;
    }

    void reportQueryDiagnostics(){
        if(!m_diagnosticsEnabled || m_diagnosticsReported)
            return;
        i32 width = 0;
        i32 height = 0;
        m_context.graphics.getWindowDimensions(width, height);
        const auto target = NWB::QueryProjectFrameClientSize();
        const u64 currentFrame = m_context.graphics.getFrameIndex();
        if(width != target.width || height != target.height || (m_diagnosticsTargetStart && currentFrame < *m_diagnosticsTargetStart)){
            m_diagnosticsTargetStart.reset();
            return;
        }
        if(!m_diagnosticsTargetStart){
            m_diagnosticsTargetStart = currentFrame;
            return;
        }
        constexpr u64 s_RequiredTargetFrames = 16u;
        if(currentFrame - *m_diagnosticsTargetStart < s_RequiredTargetFrames)
            return;
        const auto* const renderer = m_world->getSystem<NWB::Impl::RendererSystem>();
        NWB_FATAL_ASSERT(renderer);
        const auto result = renderer->tryGetLatestRefractionStatistics();
        if(!result)
            return;
        const auto& statistics = *result;
        if(statistics.width != target.width || statistics.height != target.height || statistics.graphicsFrameIndex < *m_diagnosticsTargetStart || statistics.graphicsFrameIndex - *m_diagnosticsTargetStart < s_RequiredTargetFrames || statistics.graphicsFrameIndex > currentFrame)
            return;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionQueryDiagnostics: sequence={} generation={} graphics_frame={} observed_frame={} target_start_frame={} completed_target_frames={} frame_index={} width={} height={}")
            NWB_TEXT(" hardware_requested={} hardware_available={} hardware_ready={} csg_enabled={} token_valid={} physical_valid={} token_queue={} token_value={} physical_queue={} device_generation={}")
            , statistics.sequence, statistics.generation, statistics.graphicsFrameIndex, currentFrame, *m_diagnosticsTargetStart
            , statistics.graphicsFrameIndex - *m_diagnosticsTargetStart, statistics.frameIndex, statistics.width, statistics.height
            , statistics.hardwareRequested ? 1u : 0u, statistics.hardwareAvailable ? 1u : 0u, statistics.hardwareReady ? 1u : 0u, statistics.csgHardwareEnabled ? 1u : 0u
            , statistics.acceptedToken.valid() ? 1u : 0u, statistics.acceptedToken.hasPhysicalQueueIdentity() ? 1u : 0u
            , static_cast<u32>(statistics.acceptedToken.queue), statistics.acceptedToken.value, statistics.acceptedToken.physicalQueueIndex, statistics.acceptedToken.deviceGeneration
        );
#define NWB_SMOKE_REFRACTION_LOG_COUNTER(name, offset) \
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionQueryCounter: sequence={} generation={} name={} value={}"), statistics.sequence, statistics.generation, NWB_TEXT(#name), statistics.name);
        NWB_REFRACTION_DIAGNOSTIC_COUNTER_FIELDS(NWB_SMOKE_REFRACTION_LOG_COUNTER)
#undef NWB_SMOKE_REFRACTION_LOG_COUNTER
#define NWB_SMOKE_REFRACTION_COUNT_COUNTER(name, offset) + 1u
        constexpr u32 s_CounterCount = 0u NWB_REFRACTION_DIAGNOSTIC_COUNTER_FIELDS(NWB_SMOKE_REFRACTION_COUNT_COUNTER);
#undef NWB_SMOKE_REFRACTION_COUNT_COUNTER
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionQueryDiagnostics: complete sequence={} generation={} counters={}"), statistics.sequence, statistics.generation, s_CounterCount);
        m_diagnosticsSourceFrame = statistics.graphicsFrameIndex;
        m_diagnosticsReported = true;
    }

    bool updateFrameGraphSnapshot(){
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
        if(!CaptureStressFrameGraphSnapshot(m_context, AStringView(m_frameGraphSnapshotPath), completedFrames))
            return false;
        m_frameGraphSnapshotCompleted = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionFrameGraphSnapshot: target_extent {}x{} completed_target_frames {}"), target.width, target.height, completedFrames);
        m_context.requestQuit();
        return true;
    }

    bool configureFramebufferCapture(NWB::Core::Alloc::ScratchArena& scratchArena){
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
            for(usize phase = 0u; phase < LengthOf(s_LifecyclePhases); ++phase){
                const auto path = phase + 1u == LengthOf(s_LifecyclePhases) ? SmokeEnvironmentString(*output, m_context.objectArena) : StringFormat(m_context.objectArena, "{}.{}.bmp", *output, s_LifecyclePhases[phase].name);
                m_lifecyclePaths.emplace_back(m_context.objectArena, AStringView(path));
            }
            const auto extent = NWB::QueryProjectFrameClientSize();
            const FramebufferCaptureOptions options{ .requiredWidth = extent.width, .requiredHeight = extent.height, .quitWhenReady = false, };
            m_framebufferCapture = MakeUnique<FramebufferCapture>(m_context, Move(m_lifecyclePaths.front()), captureFrames, options);
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
        if(m_diagnosticsEnabled){
            options.predicateContext = this;
            options.shouldCapture = [](void* owner, const u64 graphicsFrame)noexcept{
                const auto& project = *static_cast<RefractionSmokeProject*>(owner);
                return project.m_diagnosticsReported && graphicsFrame > project.m_diagnosticsSourceFrame;
            };
        }
        auto capture = ConfigureSmokeFramebufferCapture(m_context, scratchArena, s_ProjectName, s_DefaultCaptureFrameCount, options);
        if(!capture)
            return false;
        m_framebufferCapture = Move(*capture);
        return true;
    }

    NWB::Core::ECS::EntityID createPanel(const SmokeMaterialRef& material, const Float4& tint, const Float4& position, const Float4& scale){
        const auto entity = CreateTintedStaticMeshEntity(*m_world, m_context.objectArena, s_PlaneMesh, material, s_MaterialInterface, tint, position, scale);
        NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("RefractionSmokeProject: panel creation failed"));
        auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        // The plane starts in XZ with a +Y normal; face the camera along -Z.
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.0f, 0.0f), transform->rotation);
        return entity;
    }

    bool createDensityScene(NWB::Core::Alloc::ScratchArena& arena, const u32 percent){
        const auto configuredExtent = ReadSmokeEnvironmentText(arena, "NWB_REFRACTION_SMOKE_EXTENT");
        if(!configuredExtent || (*configuredExtent != "960x720" && *configuredExtent != "1600x900" && *configuredExtent != "1920x1080")){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: density requires explicit extent 960x720, 1600x900, or 1920x1080"));
            return false;
        }
        const auto enabled = ReadEnvironmentVariable(arena, "NWB_REFRACTION_SMOKE_ENABLED");
        const auto hardware = ReadEnvironmentVariable(arena, "NWB_REFRACTION_SMOKE_HARDWARE");
        if(!enabled || !hardware || (*enabled && **enabled != "1") || (*hardware && **hardware != "0" && **hardware != "1")){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: density requires refraction enabled and hardware selection absent, 0, or 1"));
            return false;
        }
        const bool hardwareRequested = !*hardware || **hardware == "1";
        const auto extent = NWB::QueryProjectFrameClientSize();
        const u32 frameWidth = extent.width;
        const u32 frameHeight = extent.height;
        const f32 width = static_cast<f32>(frameWidth);
        const f32 height = static_cast<f32>(frameHeight);
        const f32 aspect = width / height;
        const f32 tangent = Tan(s_PI / 6.f);
        const f32 fractionEdge = Sqrt(static_cast<f32>(percent) * 0.01f);
        const u32 coveredWidth = Min(frameWidth, 2u * static_cast<u32>(width * fractionEdge * 0.5f + 0.5f));
        const u32 coveredHeight = Min(frameHeight, 2u * static_cast<u32>(height * fractionEdge * 0.5f + 0.5f));
        const u32 left = percent == 0u ? 0u : (frameWidth - coveredWidth) / 2u;
        const u32 top = percent == 0u ? 0u : (frameHeight - coveredHeight) / 2u;
        const u32 right = left + coveredWidth;
        const u32 bottom = top + coveredHeight;
        const u64 admittedPixels = static_cast<u64>(coveredWidth) * coveredHeight;
        const u64 totalPixels = static_cast<u64>(extent.width) * extent.height;
        constexpr u32 s_TileEdge = 8u;
        const u32 activeTiles = percent == 0u ? 0u : (DivideUp(right, s_TileEdge) - left / s_TileEdge) * (DivideUp(bottom, s_TileEdge) - top / s_TileEdge);
        const u32 totalTiles = DivideUp(frameWidth, s_TileEdge) * DivideUp(frameHeight, s_TileEdge);

        const auto camera = CreateSmokeCamera(*m_world, 1.4f, 6.f, 0.f);
        if(!camera.valid())
            return false;
        auto& cameraComponent = m_world->entity(camera).getComponent<NWB::Impl::Scene::CameraComponent>();
        cameraComponent.setVerticalFovRadians(s_PI / 3.f);
        cameraComponent.setAspectRatio(aspect);
        const auto light = NWB::Impl::Scene::CreateDirectionalLightEntity(*m_world, 0.6f, 0.4f, 0.f, Float4(1.f, 1.f, 1.f, 1.f), 1.f);
        if(!light.valid())
            return false;
        m_world->entity(light).getComponent<NWB::Impl::Scene::LightComponent>().enableCaustics = false;

        // The opaque base closes stripe seams and fully occludes the in-frustum zero-coverage slab.
        // All density cases retain this same unlit backdrop, camera, and light.
        constexpr f32 s_BackdropDepth = 2.8f;
        const f32 backdropHalfHeight = (6.f + s_BackdropDepth) * tangent;
        const f32 backdropHalfWidth = backdropHalfHeight * aspect;
        createPanel(s_OpaqueMaterial, Float4(0.04f, 0.04f, 0.04f, 1.f), Float4(0.f, 1.4f, s_BackdropDepth, 0.f), Float4(backdropHalfWidth * 1.05f, 1.f, backdropHalfHeight * 1.05f, 0.f));
        constexpr u32 s_StripeCount = 48u;
        const f32 stripeHalfWidth = backdropHalfWidth / static_cast<f32>(s_StripeCount);
        for(u32 stripe = 1u; stripe < s_StripeCount; stripe += 2u){
            const f32 x = -backdropHalfWidth + (2.f * static_cast<f32>(stripe) + 1.f) * stripeHalfWidth;
            createPanel(s_OpaqueMaterial, Float4(0.92f, 0.92f, 0.92f, 1.f), Float4(x, 1.4f, s_BackdropDepth - 0.01f, 0.f), Float4(stripeHalfWidth, 1.f, backdropHalfHeight * 1.05f, 0.f));
        }

        // cube_hard_edges spans [-0.5, 0.5]; its front rectangle projects onto integer pixel boundaries.
        // The full case extends one pixel past every edge; the empty case keeps one admitted refractor behind the opaque wall so capture and classification stay live.
        const f32 frontDepth = percent == 0u ? 4.f : 0.f;
        const f32 projectedWidth = percent == 0u || percent == 100u ? width + 2.f : static_cast<f32>(coveredWidth);
        const f32 projectedHeight = percent == 0u || percent == 100u ? height + 2.f : static_cast<f32>(coveredHeight);
        const f32 halfHeight = (6.f + frontDepth) * tangent;
        const Float4 scale(2.f * halfHeight * aspect * projectedWidth / width, 2.f * halfHeight * projectedHeight / height, 0.2f, 0.f);
        m_glass = CreateTintedStaticMeshEntity(*m_world, m_context.objectArena, s_CubeMesh, s_ClearMaterial, s_MaterialInterface, Float4(1.f, 1.f, 1.f, 0.f), Float4(0.f, 1.4f, frontDepth + 0.1f, 0.f), scale);
        if(!m_glass.valid())
            return false;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionDensity: extent {}x{} requested_percent {} admitted_pixels {} total_pixels {} active_tiles {} total_tiles {} glass_instances 1 capture_mode {}")
            , extent.width, extent.height, percent, admittedPixels, totalPixels, activeTiles, totalTiles, percent == 0u ? NWB_TEXT("occluded") : NWB_TEXT("visible")
        );
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionDensity: pixel_bounds {} {} {} {}"), left, top, right, bottom);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionDensity: tracing requested {} ray_query_supported {} refraction_enabled 1")
            , hardwareRequested ? NWB_TEXT("hardware") : NWB_TEXT("screen-space")
            , m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery) ? 1u : 0u
        );
        return true;
    }

    void applyLifecyclePhase(){
        const auto& phase = s_LifecyclePhases[m_lifecyclePhase];
        auto* const transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(m_glass);
        auto* const renderer = m_world->getSystem<NWB::Impl::RendererSystem>();
        NWB_FATAL_ASSERT(transform && renderer);
        transform->position = Float4(phase.glassX, 1.4f, 0.0f, 0.0f);
        renderer->setRefractionEnabled(phase.enabled);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionLifecycle: phase {} applied"), StringConvert(phase.name));
    }

    bool advanceLifecycleCapture(){
        if(!m_lifecycleEnabled || !m_framebufferCapture->captureReady() || m_lifecyclePhase == LengthOf(s_LifecyclePhases))
            return true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RefractionLifecycle: captured {} graphics frame {}"), StringConvert(s_LifecyclePhases[m_lifecyclePhase].name), m_framebufferCapture->capturedGraphicsFrameIndex());
        ++m_lifecyclePhase;
        if(m_lifecyclePhase == LengthOf(s_LifecyclePhases)){
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


public:
    explicit RefractionSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context), m_world(CreateWorldOrDie(context)), m_lifecyclePaths(context.objectArena){}
    virtual ~RefractionSmokeProject()noexcept override{
        m_timingRenderPass.stop();
        DestroyCapturedSmokeRenderWorld(m_context, m_world, m_framebufferCapture);
    }


public:
    virtual bool onStartup()override{
        NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
        m_lifecycleEnabled = ReadSmokeEnvironmentFlag("NWB_REFRACTION_SMOKE_LIFECYCLE");
        m_timingEnabled = ReadSmokeEnvironmentFlag("NWB_REFRACTION_SMOKE_TIMING");
        const auto density = ReadDensityPercent(scratchArena);
        if(!density)
            return false;
        m_densityEnabled = density->has_value();
        if(!configureQueryDiagnostics(scratchArena))
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
            return createDensityScene(scratchArena, **density);
        if(galleryCase)
            return CreateRefractionGalleryScene(m_context, *m_world, AStringView(galleryCase->data(), galleryCase->size()), ReadSmokeEnvironmentFlag("NWB_REFRACTION_SMOKE_GEOMETRY"));

        const auto camera = CreateSmokeCamera(*m_world, 1.4f, 6.0f, 0.0f);
        // The production TLAS is shared with shadow preparation.
        // A light exercises that normal preparation path; the unlit material ignores its radiance.
        const auto light = NWB::Impl::Scene::CreateDirectionalLightEntity(*m_world, 0.6f, 0.4f, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f);
        m_glass = CreateTintedStaticMeshEntity(*m_world, m_context.objectArena, s_SphereMesh, s_ClearMaterial, s_MaterialInterface, Float4(1.0f, 1.0f, 1.0f, 1.0f), Float4(0.0f, 1.4f, 0.0f, 0.0f), Float4(1.15f, 1.15f, 1.15f, 0.0f));
        NWB_FATAL_ASSERT_MSG(camera.valid() && m_glass.valid() && light.valid(), NWB_TEXT("RefractionSmokeProject: camera/glass/light creation failed"));
        m_world->entity(light).getComponent<NWB::Impl::Scene::LightComponent>().enableCaustics = false;

        // Fixed opaque stripes make displaced edges measurable.
        // Unlit materials keep the comparison independent of GI, shadows, and caustic histories.
        const u32 stripeCount = m_lifecycleEnabled ? 48u : 20u;
        const f32 stripeStart = m_lifecycleEnabled ? -7.05f : -2.85f;
        for(u32 stripe = 0u; stripe < stripeCount; ++stripe){
            const f32 value = (stripe & 1u) != 0u ? 0.92f : 0.04f;
            Float4 position;
            StoreFloat(VectorMultiplyAddExpression(VectorSet(static_cast<f32>(stripe), 0.0f, 0.0f, 0.0f), VectorSet(0.3f, 0.0f, 0.0f, 0.0f), VectorSet(stripeStart, 1.4f, 2.8f, 0.0f)), position);
            createPanel(s_OpaqueMaterial, Float4(value, value, value, 1.0f), position, Float4(0.15f, 1.0f, 2.0f, 0.0f));
        }
        // Red foreground transparency must stay straight across the glass.
        // The blue strip is behind it and participates in the transmitted background.
        createPanel(s_TransparentMaterial, Float4(1.0f, 0.025f, 0.025f, 0.75f), Float4(0.0f, 0.85f, -1.7f, 0.0f), Float4(1.6f, 1.0f, 0.08f, 0.0f));
        createPanel(s_TransparentMaterial, Float4(0.025f, 0.1f, 1.0f, 0.70f), Float4(0.0f, 1.9f, 1.6f, 0.0f), Float4(2.0f, 1.0f, 0.12f, 0.0f));

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

    virtual bool onUpdate(const f32 delta)override{
        reportQueryDiagnostics();
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
        const auto updated = m_world->tick(fixedDelta > 0.0f ? fixedDelta : delta);
        if(!updated){
            NWB_LOGGER_ERROR(NWB_TEXT("Project: world update rejected CPU work: {}"), static_cast<u32>(updated.error()));
            return false;
        }
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    UniquePtr<FramebufferCapture> m_framebufferCapture;
    AvboitTimingRenderPass m_timingRenderPass{ m_context.graphics };
    FpsProbe m_fpsProbe{ s_ProjectName };
    GpuPassTimingProbe m_gpuPassTimingProbe{ s_ProjectName };
    Vector<::Path<NWB::Core::Alloc::GlobalArena>, NWB::Core::Alloc::GlobalArena> m_lifecyclePaths;
    SmokeEnvironmentString m_frameGraphSnapshotPath{ m_context.objectArena };
    Optional<u64> m_frameGraphTargetStart;
    Optional<u64> m_diagnosticsTargetStart;
    u64 m_diagnosticsSourceFrame = 0u;
    NWB::Core::ECS::EntityID m_glass = {};
    usize m_lifecyclePhase = 0u;
    bool m_lifecycleEnabled = false;
    bool m_timingEnabled = false;
    bool m_densityEnabled = false;
    bool m_diagnosticsEnabled = false;
    bool m_diagnosticsReported = false;
    bool m_frameGraphSnapshotCompleted = false;
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

