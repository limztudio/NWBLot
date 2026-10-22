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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_ProjectName = NWB_TEXT("RefractionSmokeProject");

using namespace NWB::Tests::Smoke;

static constexpr SmokeMeshRef s_PlaneMesh{"project/meshes/shadow_plane"};
static constexpr SmokeMeshRef s_SphereMesh{"project/meshes/caustic_sphere"};
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
        auto capture = ConfigureSmokeFramebufferCapture(m_context, scratchArena, s_ProjectName, s_DefaultCaptureFrameCount);
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
        if(m_timingEnabled){
            m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());
            if(!m_timingRenderPass.start())
                return false;
        }
        if(!configureFramebufferCapture(scratchArena))
            return false;

        const auto galleryCase = ReadSmokeEnvironmentText(scratchArena, "NWB_REFRACTION_SMOKE_CASE");
        if(galleryCase && m_lifecycleEnabled){
            NWB_LOGGER_ERROR(NWB_TEXT("RefractionSmokeProject: lifecycle and gallery cases are mutually exclusive"));
            return false;
        }
        if(galleryCase)
            return CreateRefractionGalleryScene(m_context, *m_world, AStringView(galleryCase->data(), galleryCase->size()), ReadSmokeEnvironmentFlag("NWB_REFRACTION_SMOKE_GEOMETRY"));

        const auto camera = CreateSmokeCamera(*m_world, 1.4f, 6.0f, 0.0f);
        // The production TLAS is shared with shadow preparation.
        // A light exercises that normal preparation path; the unlit material ignores its radiance.
        const auto light = NWB::Impl::Scene::CreateDirectionalLightEntity(*m_world, 0.6f, 0.4f, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f);
        m_glass = CreateTintedStaticMeshEntity(*m_world, m_context.objectArena, s_SphereMesh, s_ClearMaterial, s_MaterialInterface, Float4(1.0f, 1.0f, 1.0f, 1.0f), Float4(0.0f, 1.4f, 0.0f, 0.0f), Float4(1.15f, 1.15f, 1.15f, 0.0f));
        NWB_FATAL_ASSERT_MSG(camera.valid() && m_glass.valid() && light.valid(), NWB_TEXT("RefractionSmokeProject: camera/glass/light creation failed"));

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
        if(m_framebufferCapture)
            m_framebufferCapture->update();
        if(!advanceLifecycleCapture())
            return false;
        if(m_timingEnabled){
            m_fpsProbe.recordFrame(delta);
            m_gpuPassTimingProbe.recordFrame(delta, m_context.gpuTimingView());
        }
        const f32 fixedDelta = RendererBaselineFixedDelta();
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
    NWB::Core::ECS::EntityID m_glass = {};
    usize m_lifecyclePhase = 0u;
    bool m_lifecycleEnabled = false;
    bool m_timingEnabled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    NWB::Core::Alloc::ScratchArena arena(Tests::Smoke::s_SmokeEnvironmentArena);
    const auto extent = Tests::Smoke::ReadSmokeEnvironmentText(arena, "NWB_REFRACTION_SMOKE_EXTENT");
    if(!extent || *extent == "native")
        return { 960, 720 };
    NWB_FATAL_ASSERT_MSG(*extent == "npot", NWB_TEXT("RefractionSmokeProject: extent must be native or npot"));
    return { 967, 727 };
}
TStringView NWB::QueryProjectWindowTitle()noexcept{ return NWB_TEXT("NWB Refraction Smoke"); }
UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_refraction_smoke::RefractionSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

