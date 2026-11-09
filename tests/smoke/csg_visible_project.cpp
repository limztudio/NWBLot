// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "framebuffer_capture.h"
#include "csg_visible_scene.h"
#include <tests/smoke/presentation_types.adhd.h>
#include "gpu_pass_timing_probe.h"
#include "smoke_scene_helpers.h"

#include <tests/smoke/scene_types.adhd.h>

#include <loader/project_entry.h>

#include <impl/assets_csg/shape_id.adhd.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material/material_instance.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>
#include <core/perf/report.h>

#include <global/simd/frame.h>
#include <global/simdmath.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_visible_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_GiTemporalEnvironment = "NWB_CSG_GI_TEMPORAL";

using NWB::Tests::Smoke::AddSmokeRenderSystems;
using NWB::Tests::Smoke::CreateSmokeWorldOrDie;
using NWB::Tests::Smoke::DestroySmokeRenderWorld;
using NWB::Tests::Smoke::TickSmokeWorld;

using CsgVisibleMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_CameraStartDepth = 6.5f;
static constexpr Float2U s_CameraStart = Float2U(0.0f, NWB::Tests::Smoke::CsgVisibleScene::s_CameraTargetY);
static constexpr f32 s_DefaultDirectionalLightPitch = -0.65f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.45f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 3.0f;
static constexpr f32 s_CubeRotationSpeed = 0.25f;
static constexpr f32 s_MaxAnimationDelta = 1.0f / 15.0f;
static constexpr CsgVisibleMaterialRef s_SolidMaterial{"project/smoke/csg_visible/materials/solid"};
static constexpr CsgVisibleMaterialRef s_IndirectMaterial{"project/smoke/csg_visible/materials/indirect"};
static constexpr CsgVisibleMaterialRef s_DirectMaterial{"project/smoke/csg_visible/materials/direct"};
static constexpr CsgVisibleMaterialRef s_NormalMaterial{"project/smoke/csg_visible/materials/normal"};
static constexpr CsgVisibleMaterialRef s_PositionMaterial{"project/smoke/csg_visible/materials/position"};
static constexpr CsgVisibleMaterialRef s_CapStateMaterial{"project/smoke/csg_visible/materials/cap_state"};
static constexpr CsgVisibleMaterialRef s_IntervalStateMaterial{"project/smoke/csg_visible/materials/interval_state"};
static constexpr CsgVisibleMaterialRef s_EventStateMaterial{"project/smoke/csg_visible/materials/event_state"};
static constexpr CsgVisibleMaterialRef s_EventDataMaterial{"project/smoke/csg_visible/materials/event_data"};
static constexpr CsgVisibleMaterialRef s_EventOrderMaterial{"project/smoke/csg_visible/materials/event_order"};
static constexpr CsgVisibleMaterialRef s_SpanStateMaterial{"project/smoke/csg_visible/materials/span_state"};
static constexpr u32 s_TemporalSampleCount = 32u;
static constexpr u64 s_TemporalMinimumFrame = 360u;
static constexpr f64 s_TemporalWarmupSeconds = 30.0;
static constexpr u64 s_TemporalFrameInterval = 8u;



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static TStringView CsgVisibleFpsLabel()noexcept{
    return NWB_TEXT("CsgVisibleSmokeProject");
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgVisibleSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> CreateWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(CsgVisibleFpsLabel(), context);

        if(context.graphics.queryFeatureSupport(NWB::Core::Feature::Meshlets))
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: natural native mesh-shader route selected"));
        else
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: natural indexed route selected because Meshlets are unavailable"));

        auto& renderer = AddSmokeRenderSystems(*world, context);
        if(NWB::Tests::Smoke::ReadSmokeEnvironmentFlag(s_GiTemporalEnvironment)){
            NWB::Impl::ReflectionSettings reflection;
            reflection.traceMode = NWB::Impl::ReflectionTraceMode::Disabled;
            NWB_FATAL_ASSERT_MSG(renderer.setReflectionSettings(reflection), NWB_TEXT("Invalid temporal reflection settings"));
            renderer.setFrameLaggedAsyncLightingEnabled(NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_CSG_GI_TEMPORAL_LAGGED"));
        }

        return world;
    }

    [[nodiscard]] static b8 ShouldCapture(void* const context, const u64 frame)noexcept{
        return frame >= MakeNotNull(static_cast<CsgVisibleSmokeProject*>(context))->m_nextCaptureFrame;
    }


public:
    explicit CsgVisibleSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateWorldOrDie(context))
        , m_capturePaths(context.objectArena)
    {}
    virtual ~CsgVisibleSmokeProject()noexcept override{
        destroyWorld();
    }


public:
    virtual b8 onStartup()override{
        m_temporalEnabled = NWB::Tests::Smoke::ReadSmokeEnvironmentFlag(s_GiTemporalEnvironment);
        const b8 overlapping = m_temporalEnabled && NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_CSG_GI_TEMPORAL_OVERLAPPING");
        CsgVisibleMaterialRef material = s_SolidMaterial;
        if(m_temporalEnabled){
            NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
            m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());
            const auto output = NWB::Tests::Smoke::ReadSmokeEnvironmentText(scratchArena, "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH");
            const auto view = NWB::Tests::Smoke::ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_GI_TEMPORAL_VIEW");
            if(!output || !view)
                return false;
            if(*view == "indirect")
                material = s_IndirectMaterial;
            else if(*view == "direct")
                material = s_DirectMaterial;
            else if(*view == "normal")
                material = s_NormalMaterial;
            else if(*view == "position")
                material = s_PositionMaterial;
            else if(*view == "cap_state")
                material = s_CapStateMaterial;
            else if(*view == "interval_state")
                material = s_IntervalStateMaterial;
            else if(*view == "event_state")
                material = s_EventStateMaterial;
            else if(*view == "event_data"){
                material = s_EventDataMaterial;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: event-data red stores missing-span flags divided by 33"));
            }
            else if(*view == "event_order"){
                material = s_EventOrderMaterial;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: event-order RGB stores span/flags and validated raw FRONT=1 BACK=0.5"));
            }
            else if(*view == "span_state"){
                material = s_SpanStateMaterial;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: span-state RGB stores span count, flags, raw event count divided by 33"));
            }
            else if(*view != "full")
                return false;
            const NWB::Tests::Smoke::FramebufferCaptureOptions captureOptions{ .shouldCapture = ShouldCapture, .predicateContext = this, .requiredWidth = 1280u, .requiredHeight = 900u, .quitWhenReady = false };
            auto captureSeries = NWB::Tests::Smoke::SmokeCaptureFactory(m_context).prepareSeries(AStringView(*output), s_TemporalSampleCount, captureOptions);
            if(!captureSeries)
                return false;
            m_capturePaths = Move(captureSeries->paths);
            m_capture = Move(captureSeries->capture);
            m_temporalAnimated = NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_CSG_GI_TEMPORAL_ANIMATED");
            if(overlapping && m_temporalAnimated){
                NWB_LOGGER_ERROR(NWB_TEXT("CsgVisibleSmokeProject: overlapping receiver regression requires a static scene"));
                return false;
            }
            const auto intervalText = NWB::Tests::Smoke::ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_GI_TEMPORAL_INTERVAL");
            if(intervalText){
                const auto interval = ParseU64(AStringView(intervalText->data(), intervalText->size()));
                if(!interval || *interval == 0u || *interval > 64u){
                    NWB_LOGGER_ERROR(NWB_TEXT("CsgVisibleSmokeProject: temporal interval must be between 1 and 64"));
                    return false;
                }
                m_temporalFrameInterval = *interval;
            }
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: temporal scene animated={} view={} minimum_frame=360 warmup_seconds=30 interval={} samples=32"), static_cast<u32>(m_temporalAnimated), ConvertString(*view), m_temporalFrameInterval);
            if(m_temporalAnimated)
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: front-facing triangle pose from graphics frame"));
        }
        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(*m_world, Float4(s_CameraStart.x, s_CameraStart.y, -s_CameraStartDepth));
        const auto directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(*m_world, s_DefaultDirectionalLightPitch, s_DefaultDirectionalLightYaw, 0.0f, Float4(1.0f, 0.96f, 0.88f), s_DefaultDirectionalLightIntensity);

        m_scene.create(material, overlapping, activeCamera.camera, directionalLight, *m_world);

        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: visible CSG interval receiver scene created"));
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: shutdown"));
    }

    virtual b8 onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_fpsProbe.recordFrame(safeDelta);
        if(m_temporalEnabled){
            m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
            const u32 posePhase = static_cast<u32>(m_context.graphics.getFrameIndex() % 240u);
            m_animationTime = m_temporalAnimated ? static_cast<f32>(Min(posePhase, 240u - posePhase)) * 0.3f / 120.0f : 0.0f;
            updateTemporalCapture();
        }
        else
            m_animationTime = Min(safeDelta, s_MaxAnimationDelta) * s_CubeRotationSpeed + m_animationTime;
        m_scene.update(m_animationTime, *m_world);

        return TickSmokeWorld(m_temporalEnabled ? 1.0f / 60.0f : safeDelta, *m_world);
    }


private:
    void destroyWorld(){
        if(m_capture){
            m_capture->stop();
            m_capture.reset();
        }
        DestroySmokeRenderWorld(m_context, m_world);
    }

    void updateTemporalCapture(){
        if(m_temporalSample == s_TemporalSampleCount)
            return;
        if(!m_temporalWarmupStarted){
            const u64 frame = m_context.graphics.getFrameIndex();
            if(frame < s_TemporalMinimumFrame)
                return;
            m_temporalWarmupStart = TimerNow();
            m_temporalWarmupStarted = true;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: temporal warmup started graphics frame {}"), frame);
            return;
        }
        if(!m_temporalCaptureStarted){
            const f64 elapsed = DurationInSeconds<f64>(TimerNow(), m_temporalWarmupStart);
            if(elapsed < s_TemporalWarmupSeconds)
                return;
            m_nextCaptureFrame = m_context.graphics.getFrameIndex();
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: temporal warmup complete elapsed_seconds={:.6f} first_frame={}"), elapsed, m_nextCaptureFrame);
            m_temporalCaptureStarted = true;
            return;
        }
        // Consecutive captures complete the previous readback before reusing the single observer.
        if(m_temporalFrameInterval == 1u && m_capture->capturedGraphicsFrameIndex() != Limit<u64>::s_Max){
            if(!m_context.graphics.waitForIdle()){
                NWB_LOGGER_ERROR(NWB_TEXT("CsgVisibleSmokeProject: consecutive capture completion failed"));
                m_context.requestQuit();
                return;
            }
        }
        m_capture->update();
        if(!m_capture->captureReady())
            return;
        const u64 sourceFrame = m_capture->capturedGraphicsFrameIndex();
        if(sourceFrame != m_nextCaptureFrame){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgVisibleSmokeProject: temporal sample missed frame {} (captured {})"), m_nextCaptureFrame, sourceFrame);
            m_context.requestQuit();
            return;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: temporal sample {} graphics frame {}"), m_temporalSample, sourceFrame);
        ++m_temporalSample;
        if(m_temporalSample == s_TemporalSampleCount){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgVisibleSmokeProject: temporal series complete samples=32"));
            m_capture->finish();
            return;
        }
        m_nextCaptureFrame += m_temporalFrameInterval;
        if(!m_capture->rearm(Move(m_capturePaths[m_temporalSample])))
            m_context.requestQuit();
    }


private:
    NWB::Tests::Smoke::CsgVisibleScene m_scene;
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ CsgVisibleFpsLabel() };
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ CsgVisibleFpsLabel() };
    Vector<::Path<NWB::Core::Alloc::GlobalArena>, NWB::Core::Alloc::GlobalArena> m_capturePaths;
    UniquePtr<NWB::Tests::Smoke::FramebufferCapture> m_capture;
    Timer m_temporalWarmupStart;
    u64 m_nextCaptureFrame = Limit<u64>::s_Max;
    u64 m_temporalFrameInterval = s_TemporalFrameInterval;
    f32 m_animationTime = 0.0f;
    u32 m_temporalSample = 0u;
    b8 m_temporalEnabled = false;
    b8 m_temporalAnimated = false;
    b8 m_temporalWarmupStarted = false;
    b8 m_temporalCaptureStarted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{
    return NWB_TEXT("NWB CSG Visible Smoke");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_csg_visible_smoke::CsgVisibleSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

