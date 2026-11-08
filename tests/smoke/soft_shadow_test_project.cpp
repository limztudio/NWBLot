// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "arrow_yaw_input_handler.h"
#include "soft_shadow_scene.h"
#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "avboit_timing_render_pass.h"
#include "smoke_capture_teardown.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"

#include <loader/project_entry.h>

#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_model/system.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_mesh/skinning/system.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/input/module.h>
#include <core/graphics/runtime/runtime.h>

#include <global/simd/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_soft_shadow_test_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_ProjectName = NWB_TEXT("SoftShadowTestSmokeProject");

using NWB::Tests::Smoke::ArrowYawInputHandler;
using NWB::Tests::Smoke::CreateSmokeSkinnedRenderWorldOrDie;
using NWB::Tests::Smoke::DestroySmokeSkinnedRenderWorld;
using NWB::Tests::Smoke::ReadSmokeEnvironmentF32;
using NWB::Tests::Smoke::RendererBaselineCaptureFreezeFrame;
using NWB::Tests::Smoke::RendererBaselineFixedDelta;
using NWB::Tests::Smoke::ReadSmokeFrozenYawCached;
using NWB::Tests::Smoke::TickSmokeWorld;



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_DefaultAngularRadius = 0.03f;
static constexpr f32 s_DefaultSourceRadius = 0.15f;

static constexpr f32 s_SpinSpeed = 0.5f;
static constexpr f32 s_ManualYawSpeed = 0.6f;
static constexpr f32 s_MaxSpinDelta = 1.0f / 15.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SoftShadowTestSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    // Directional light angular radius in radians.
    static f32 ConfiguredAngularRadius(){
        static const f32 s_Angle = []()noexcept{
            const auto parsed = ReadSmokeEnvironmentF32("NWB_SOFT_SHADOW_TEST_ANGLE");
            if(!parsed)
                return s_DefaultAngularRadius;
            return *parsed > 0.0f ? Min(0.2f, *parsed) : 0.0f;
        }();
        return s_Angle;
    }

    // Point and spot source radius in world units.
    static f32 ConfiguredSourceRadius(){
        static const f32 s_Radius = []()noexcept{
            const auto parsed = ReadSmokeEnvironmentF32("NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS");
            if(!parsed)
                return s_DefaultSourceRadius;
            return *parsed > 0.0f ? Min(1.0f, *parsed) : 0.0f;
        }();
        return s_Radius;
    }

    // Pin yaw for captures that isolate shimmer from motion.
    static f32 FrozenYaw(){
        return ReadSmokeFrozenYawCached("NWB_SOFT_SHADOW_TEST_SPIN_ANGLE");
    }


public:
    explicit SoftShadowTestSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateSmokeSkinnedRenderWorldOrDie(s_ProjectName, {}, context))
    {}
    virtual ~SoftShadowTestSmokeProject()noexcept override{
        m_timingRenderPass.stop();
        m_context.input.removeHandler(m_arrowYawInput);
        destroyWorld();
    }


public:
    virtual b8 onStartup()override{
        m_timingEnabled = NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_SOFT_SHADOW_TEST_TIMING");
        if(m_timingEnabled){
            m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());
            if(!m_timingRenderPass.start())
                return false;
        }
        // Register arrow yaw scrubbing at the front of the input chain.
        m_context.input.addHandlerToBack(m_arrowYawInput);

        if(!m_scene.create(*m_world, m_timingEnabled, ConfiguredAngularRadius(), ConfiguredSourceRadius()))
            return false;

        if(m_timingEnabled){
            const b8 hardwareAvailable = m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct) && m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery);
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ShadowTimingProbe: natural shadow route {}"), hardwareAvailable ? NWB_TEXT("hardware") : NWB_TEXT("software"));
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ShadowTimingProbe: caustic emission 0"));
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ShadowTimingProbe: indirect response hemi-ambient"));
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ShadowTimingProbe: source extents angular={} radius={}"), static_cast<f64>(ConfiguredAngularRadius()), static_cast<f64>(ConfiguredSourceRadius()));
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SoftShadowTestSmokeProject: opaque + glass characters on a ground plane, 3 coloured lights, angularRadius={} rad"), static_cast<f64>(ConfiguredAngularRadius()));
        return true;
    }

    virtual void onShutdown()override{
        m_timingRenderPass.stop();
        m_context.graphics.setFrameSubmissionSuspended(false);
        m_context.input.removeHandler(m_arrowYawInput);
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SoftShadowTestSmokeProject: shutdown"));
    }

    virtual b8 onUpdate(const f32 delta)override{
        const u32 captureFreezeFrame = RendererBaselineCaptureFreezeFrame();
        if(captureFreezeFrame != 0u && m_rendererBaselineRenderedFrameCount >= captureFreezeFrame){
            if(!m_rendererBaselineCapturePaused){
                // Log the held update phase for the baseline capture runner.
                m_context.graphics.setFrameSubmissionSuspended(true);
                m_rendererBaselineCapturePaused = true;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SoftShadowTestSmokeProject: renderer baseline capture ready after {} update callbacks; render submission suspended"), m_rendererBaselineRenderedFrameCount);
            }
            return true;
        }

        const f32 fixedDelta = RendererBaselineFixedDelta();
        const f32 safeDelta = fixedDelta > 0.0f ? fixedDelta : (IsFinite(delta) ? Max(delta, 0.0f) : 0.0f);
        m_fpsProbe.recordFrame(safeDelta);
        if(m_timingEnabled)
            m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
        // Yaw priority: fixed override, manual scrub, then automatic spin.
        const f32 frozen = FrozenYaw();
        m_yaw.update(safeDelta, frozen, frozen >= 0.0f, m_arrowYawInput, s_ManualYawSpeed, s_SpinSpeed, s_MaxSpinDelta);
        m_scene.spinCasters(*m_world, m_yaw.yaw());
        updateWindowTitle();
        if(!TickSmokeWorld(safeDelta, *m_world))
            return false;
        ++m_rendererBaselineRenderedFrameCount;
        return true;
    }


private:
    void destroyWorld(){
        DestroySmokeSkinnedRenderWorld(m_context, m_world);
    }

    void updateWindowTitle(){
        f32 wrappedYaw = FMod(m_yaw.yaw(), s_2PI);
        if(wrappedYaw < 0.0f)
            wrappedYaw += s_2PI;
        const f32 yawDegrees = wrappedYaw * (360.0f / s_2PI);
        const f32 angleDegrees = ConfiguredAngularRadius() * 360.0f / s_2PI;

        static constexpr usize s_TitleCapacity = 256u;
        Array<tchar, s_TitleCapacity> title{};
        NWB_TSPRINTF(
            title.data(), title.size(),
            NWB_TEXT("%.*s  |  yaw %.2f deg  |  sun %.2f deg  |  src r %.3f%s"),
            static_cast<i32>(NWB::QueryProjectWindowTitle().size()), NWB::QueryProjectWindowTitle().data(), yawDegrees, angleDegrees, ConfiguredSourceRadius(),
            m_yaw.manualControl() ? NWB_TEXT("  [manual: <- ->]") : NWB_TEXT("")
        );
        m_context.graphics.setWindowTitle(TStringView(title.data()));
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Tests::Smoke::ShadowTimingRenderPass m_timingRenderPass{ m_context.graphics };
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ s_ProjectName };
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ s_ProjectName };
    ArrowYawInputHandler m_arrowYawInput;
    NWB::Tests::Smoke::SoftShadowScene m_scene;
    NWB::Tests::Smoke::YawSpinController m_yaw;
    u32 m_rendererBaselineRenderedFrameCount = 0u;
    b8 m_timingEnabled = false;
    b8 m_rendererBaselineCapturePaused = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{
    return NWB_TEXT("NWB Soft Shadow Test");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_soft_shadow_test_smoke::SoftShadowTestSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

