// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "smoke_capture_factory.h"
#include "arrow_yaw_input_handler.h"
#include "avboit_timing_render_pass.h"
#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "smoke_capture_teardown.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"
#include "transparent_multi_renderer_setup.h"
#include "transparent_multi_scene.h"
#include "transparent_multi_scene_transforms.h"
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
#include "framebuffer_capture.h"
#endif
#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
#include "csg_smoke_helpers.h"
#endif

#include <loader/project_entry.h>

#if defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
#include <impl/ecs_csg/module.h>
#endif
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material/material_instance.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/input/module.h>
#include <core/graphics/runtime/runtime.h>

#include <global/simd/frame.h>
#include <global/simd/constant.h>
#include <global/simdmath.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_transparent_multi_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_TransparentProjectName = NWB_TEXT("TransparentMultiSmokeProject");
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
constexpr TStringView s_CausticProjectName = NWB_TEXT("CausticSphereSmokeProject");
constexpr AStringView s_CausticTimingEnvironment = "NWB_CAUSTIC_SMOKE_TIMING";
constexpr AStringView s_SparsePreset = "sparse";
#endif

using NWB::Tests::Smoke::ArrowYawInputHandler;
using NWB::Tests::Smoke::CreateSmokeWorldOrDie;
using NWB::Tests::Smoke::DestroySmokeRenderWorld;
using NWB::Tests::Smoke::ReadSmokeEnvironmentF32;
using NWB::Tests::Smoke::RendererBaselineCaptureFreezeFrame;
using NWB::Tests::Smoke::RendererBaselineFixedDelta;
using NWB::Tests::Smoke::SetSmokeYawWindowTitle;
using NWB::Tests::Smoke::TickSmokeWorld;
using NWB::Tests::Smoke::TransparentMultiRendererSetup;
using NWB::Tests::Smoke::TransparentMultiScene;
using NWB::Tests::Smoke::TransparentMultiSceneTransforms;

static constexpr f32 s_CameraStartDepth = 2.2f;
static constexpr f32 s_MaxAnimationDelta = 1.0f / 30.0f;
static constexpr f32 s_ManualYawSpeed = 0.6f;
// Spin the transparent-shadow scene while keeping the caustic sphere static by default.
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
static constexpr f32 s_TransparentSceneRotationSpeed = 0.0f;
#else
static constexpr f32 s_TransparentSceneRotationSpeed = 0.55f;
#endif
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
// Toggle frame-lagged lighting on F1 to test teardown and re-bootstrap.
class FrameLaggedAsyncLightingToggleInputHandler final : public NWB::Core::IInputEventHandler{
public:
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(const i32 key, const i32, const i32 action, const i32)noexcept override{
        if(key != NWB::Core::Key::F1)
            return false;
        if(action == NWB::Core::InputAction::Press)
            m_toggleRequested = true;
        return true;
    }

    [[nodiscard]] b8 consumeToggleRequest()noexcept{
        const b8 requested = m_toggleRequested;
        m_toggleRequested = false;
        return requested;
    }


private:
    b8 m_toggleRequested = false;
};

// Opt into unfocused rendering so smoke runs continue when Windows denies foreground focus.
class FrameLaggedAsyncLightingUnfocusedPass final : public NWB::Core::IRenderPass{
public:
    explicit FrameLaggedAsyncLightingUnfocusedPass(NWB::Core::GraphicsRuntime& graphics)noexcept
        : IRenderPass(graphics)
    {}


public:
    virtual b8 shouldRenderUnfocused()noexcept override{ return true; }
};
#endif

[[nodiscard]] static TStringView TransparentMultiFpsLabel()noexcept{
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
    return NWB_TEXT("FrameLaggedAsyncLightingSmokeProject");
#elif defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    return NWB_TEXT("TransparentCsgSmokeProject");
#elif defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
    return s_CausticProjectName;
#else
    return s_TransparentProjectName;
#endif
}

class TransparentMultiSmokeProject final : public NWB::IProjectEntryCallbacks{
public:
    // Vary spin speed from one build to compare caustic motion-vector reprojection.
    static f32 EffectiveRotationSpeed(){
        static const f32 s_Speed = []()noexcept{
            const auto parsed = ReadSmokeEnvironmentF32("NWB_TRANSPARENT_MULTI_SPIN_SPEED");
            if(!parsed)
                return s_TransparentSceneRotationSpeed;
            return IsFinite(*parsed) && (*parsed >= 0.0f) ? *parsed : s_TransparentSceneRotationSpeed;
        }();
        return s_Speed;
    }

    // Pin yaw when NWB_TRANSPARENT_MULTI_SPIN_ANGLE is set.
    static f32 EffectiveFrozenAngle(){
        static const f32 s_Angle = []()noexcept{
            const auto parsed = ReadSmokeEnvironmentF32("NWB_TRANSPARENT_MULTI_SPIN_ANGLE");
            if(!parsed)
                return Limit<f32>::s_QuietNaN;
            return IsFinite(*parsed) ? *parsed : Limit<f32>::s_QuietNaN;
        }();
        return s_Angle;
    }


private:
    static NotNullUniquePtr<NWB::Core::ECS::World> CreateWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(s_TransparentProjectName, context);

        TransparentMultiRendererSetup::Configure(*world, context);
        return world;
    }


public:
    explicit TransparentMultiSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateWorldOrDie(context))
    {}
    virtual ~TransparentMultiSmokeProject()noexcept override{
        m_timingRenderPass.stop();
        m_context.input.removeHandler(m_arrowYawInput);
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
        m_context.input.removeHandler(m_frameLaggedAsyncLightingToggleInput);
        removeFrameLaggedAsyncLightingUnfocusedPass();
#endif
        destroyWorld();
    }


public:
    virtual b8 onStartup()override{
        // Enable both the timing sink and graphics recorder for per-pass observations.
        m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());

        b8 causticTiming = false;
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
        causticTiming = NWB::Tests::Smoke::ReadSmokeEnvironmentFlag(s_CausticTimingEnvironment);
#endif
        if((causticTiming || NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_AVBOIT_SMOKE_TIMING")) && !m_timingRenderPass.start(causticTiming))
            return false;

#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
        if(!configureFramebufferCapture())
            return false;
#endif

        // Register arrow yaw scrubbing at the front of the input chain.
        m_context.input.addHandlerToBack(m_arrowYawInput);
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
        m_context.graphics.addRenderPassToBack(m_frameLaggedAsyncLightingUnfocusedPass);
        m_frameLaggedAsyncLightingUnfocusedPassRegistered = true;
        // Bind F1 to toggle frame-lagged lighting.
        m_context.input.addHandlerToBack(m_frameLaggedAsyncLightingToggleInput);
#endif

        f32 cameraDistance = s_CameraStartDepth;
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
        if(causticTiming){
            // Camera distance varies visible footprint while the optical geometry, light and photon quality stay fixed.
            NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
            const auto presetText = NWB::Tests::Smoke::ReadSmokeEnvironmentText(scratchArena, "NWB_CAUSTIC_SMOKE_CAMERA_PRESET");
            if(!presetText){
                NWB_LOGGER_ERROR(NWB_TEXT("CausticTimingProbe: camera preset is required"));
                return false;
            }
            const AStringView preset(presetText->data(), presetText->size());
            if(preset != "populated" && preset != s_SparsePreset){
                NWB_LOGGER_ERROR(NWB_TEXT("CausticTimingProbe: camera preset must be populated or sparse"));
                return false;
            }
            cameraDistance = cameraDistance * (preset == s_SparsePreset ? 2.0f : 1.0f);
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CausticTimingProbe: camera {} distance {} height {}"), preset == "sparse" ? NWB_TEXT("sparse") : NWB_TEXT("populated"), cameraDistance, TransparentMultiSceneTransforms::s_CameraTargetY);
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CausticTimingProbe: fixed delta {} yaw {} sphere scale {}"), RendererBaselineFixedDelta(), EffectiveFrozenAngle(), TransparentMultiScene::s_CausticSphereScale);
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CausticTimingProbe: photon phases {}"), NWB::Impl::s_CausticTemporalPhaseCount);
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CausticTimingProbe: directional pitch {} yaw {} intensity {}"), TransparentMultiScene::s_DefaultDirectionalLightPitch, TransparentMultiScene::s_DefaultDirectionalLightYaw, TransparentMultiScene::s_DefaultDirectionalLightIntensity);
        }
#endif
        if(!TransparentMultiScene::Populate(cameraDistance, causticTiming, m_sceneYaw.yaw(), *m_world, m_sceneTransforms))
            return false;
        return true;
    }

    virtual void onShutdown()override{
        m_timingRenderPass.stop();
        m_context.graphics.setFrameSubmissionSuspended(false);
        m_context.input.removeHandler(m_arrowYawInput);
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
        m_context.input.removeHandler(m_frameLaggedAsyncLightingToggleInput);
        removeFrameLaggedAsyncLightingUnfocusedPass();
#endif
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentMultiSmokeProject: shutdown"));
    }

    virtual b8 onUpdate(const f32 delta)override{
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
        if(m_framebufferCapture)
            m_framebufferCapture->update();
        // Skip update-count freeze while the framebuffer capture observer is active.
        const u32 captureFreezeFrame = m_framebufferCapture ? 0u : RendererBaselineCaptureFreezeFrame();
#else
        const u32 captureFreezeFrame = RendererBaselineCaptureFreezeFrame();
#endif
        if(captureFreezeFrame != 0u && m_rendererBaselineRenderedFrameCount >= captureFreezeFrame){
            if(!m_rendererBaselineCapturePaused){
                // Suspend rendering at the target frame while keeping the window loop alive for capture.
                m_context.graphics.setFrameSubmissionSuspended(true);
                m_rendererBaselineCapturePaused = true;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("TransparentMultiSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"), m_rendererBaselineRenderedFrameCount);
            }
            return true;
        }

        const f32 fixedDelta = RendererBaselineFixedDelta();
        const f32 safeDelta = fixedDelta > 0.0f ? fixedDelta : (IsFinite(delta) ? Max(delta, 0.0f) : 0.0f);
        m_fpsProbe.recordFrame(safeDelta);
        m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
        if(m_frameLaggedAsyncLightingToggleInput.consumeToggleRequest()){
            NWB::Impl::RendererSystem& rendererSystem = *MakeNotNull(m_world->getSystem<NWB::Impl::RendererSystem>());
            m_frameLaggedAsyncLightingEnabled = !m_frameLaggedAsyncLightingEnabled;
            rendererSystem.setFrameLaggedAsyncLightingEnabled(m_frameLaggedAsyncLightingEnabled);
            if(m_frameLaggedAsyncLightingEnabled)
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("FrameLaggedAsyncLightingSmoke: F1 re-enabled frame-lagged async lighting"));
            else
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("FrameLaggedAsyncLightingSmoke: F1 requested current-frame path"));
        }
#endif
        // Yaw priority is environment override, manual arrow scrub, then auto-spin.
        const f32 frozenAngle = EffectiveFrozenAngle();
        m_sceneYaw.update(safeDelta, frozenAngle, IsFinite(frozenAngle), m_arrowYawInput, s_ManualYawSpeed, EffectiveRotationSpeed(), s_MaxAnimationDelta);
        m_sceneTransforms.update(m_sceneYaw.yaw(), *m_world);
        SetSmokeYawWindowTitle(m_sceneYaw.yaw(), m_sceneYaw.manualControl(), s_2PI, m_context);
        if(!TickSmokeWorld(safeDelta, *m_world))
            return false;
        ++m_rendererBaselineRenderedFrameCount;
        return true;
    }


private:
    void destroyWorld(){
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
        if(m_framebufferCapture){
            m_framebufferCapture->stop();
            m_framebufferCapture.reset();
        }
#endif
        DestroySmokeRenderWorld(m_context, m_world);
    }

#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
    [[nodiscard]] b8 configureFramebufferCapture(){
        constexpr u32 s_DefaultCaptureFrameCount = 360u;
        auto capture = NWB::Tests::Smoke::SmokeCaptureFactory(m_context).configure(s_CausticProjectName, s_DefaultCaptureFrameCount, {});
        if(!capture)
            return false;
        m_framebufferCapture = Move(*capture);
        return true;
    }
#endif

#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
    void removeFrameLaggedAsyncLightingUnfocusedPass(){
        if(!m_frameLaggedAsyncLightingUnfocusedPassRegistered)
            return;

        m_context.graphics.removeRenderPass(m_frameLaggedAsyncLightingUnfocusedPass);
        m_frameLaggedAsyncLightingUnfocusedPassRegistered = false;
    }
#endif




private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Tests::Smoke::AvboitTimingRenderPass m_timingRenderPass{ m_context.graphics };
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ TransparentMultiFpsLabel() };
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ TransparentMultiFpsLabel() };
    ArrowYawInputHandler m_arrowYawInput;
#if defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
    UniquePtr<NWB::Tests::Smoke::FramebufferCapture> m_framebufferCapture;
#endif
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
    FrameLaggedAsyncLightingToggleInputHandler m_frameLaggedAsyncLightingToggleInput;
    FrameLaggedAsyncLightingUnfocusedPass m_frameLaggedAsyncLightingUnfocusedPass{ m_context.graphics };
#endif
    TransparentMultiSceneTransforms m_sceneTransforms;
    NWB::Tests::Smoke::YawSpinController m_sceneYaw;
    u32 m_rendererBaselineRenderedFrameCount = 0u;
    b8 m_rendererBaselineCapturePaused = false;
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
    b8 m_frameLaggedAsyncLightingUnfocusedPassRegistered = false;
    b8 m_frameLaggedAsyncLightingEnabled = true;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{
#if defined(NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE)
    return NWB_TEXT("NWB Frame Lagged Async Lighting Smoke");
#elif defined(NWB_TRANSPARENT_MULTI_ENABLE_CSG)
    return NWB_TEXT("NWB Transparent CSG Smoke");
#elif defined(NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE)
    return NWB_TEXT("NWB Caustic Sphere Smoke");
#else
    return NWB_TEXT("NWB Transparent Multi Smoke");
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_transparent_multi_smoke::TransparentMultiSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

