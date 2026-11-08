// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "smoke_capture_factory.h"
#include "csg_smoke_helpers.h"
#include "csg_shadow_mode.h"
#include "csg_shadow_atlas.h"
#include "framebuffer_capture.h"
#include "smoke_capture_teardown.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"

#if defined(NWB_CSG_CAUSTIC_SMOKE)
#include "csg_caustic_timing_probe.h"
#endif

#include <tests/smoke/scene_types.adhd.h>

#include <loader/project_entry.h>

#include <impl/assets_csg/shape_id.adhd.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_render/module.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>

#include <global/simd/frame.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_shadow_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_ProjectName = NWB_TEXT("CsgShadowSmokeProject");

static constexpr AStringView s_Directional = NWB::Tests::Smoke::s_ShadowSmokeDirectional;
static constexpr AStringView s_Point = NWB::Tests::Smoke::s_ShadowSmokePoint;
static constexpr AStringView s_Hard = NWB::Tests::Smoke::s_ShadowSmokeHard;
static constexpr AStringView s_Finite = NWB::Tests::Smoke::s_ShadowSmokeFinite;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Tests::Smoke;
namespace Arm = NWB::Tests::Smoke::CsgShadowArm;
namespace OpenCase = NWB::Tests::Smoke::CsgShadowOpenCase;

static constexpr SmokeMeshRef s_PlaneMesh{"project/meshes/shadow_plane"};
#if defined(NWB_CSG_CAUSTIC_SMOKE)
static constexpr SmokeMaterialRef s_OpaqueMaterial{"project/smoke/csg_caustic/materials/receiver"};
#else
static constexpr SmokeMaterialRef s_OpaqueMaterial{"project/smoke/csg_shadow/materials/opaque"};
#endif
static constexpr f32 s_CameraDepth = NWB::Tests::Smoke::s_ShadowSmokeCameraDepth;
static constexpr u32 s_MoveUpdate = NWB::Tests::Smoke::s_ShadowSmokeMoveUpdate;




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgShadowSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> CreateWorld(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(s_ProjectName, context);
        auto& renderer = AddSmokeRenderSystems(*world, context);
        NWB::Impl::ReflectionSettings reflection;
        reflection.traceMode = NWB::Impl::ReflectionTraceMode::Disabled;
        NWB_FATAL_ASSERT_MSG(renderer.setReflectionSettings(reflection), NWB_TEXT("Invalid CSG shadow reflection settings"));
        renderer.setRefractionEnabled(false);
        NWB::Impl::PresentationSettings presentation;
        presentation.toneMap = NWB::Impl::PresentationToneMap::LinearClamp;
        NWB_FATAL_ASSERT_MSG(renderer.setPresentationSettings(presentation), NWB_TEXT("Invalid CSG shadow presentation settings"));
        return world;
    }


public:
    explicit CsgShadowSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateWorld(context))
        , m_atlas(*m_world, m_mode, s_OpaqueMaterial)
    {}
    virtual ~CsgShadowSmokeProject()noexcept override{ destroyWorld(); }


public:
    virtual b8 onStartup()override{
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        constexpr b8 s_Caustic = true;
#else
        constexpr b8 s_Caustic = false;
#endif
        if(!m_mode.read(s_Caustic))
            return false;
        const auto camera = CreateSmokeCamera(0.0f, -s_CameraDepth, 0.0f, *m_world);
        NWB_FATAL_ASSERT_MSG(camera.valid(), NWB_TEXT("CsgShadowSmokeProject: camera creation failed"));
        auto& cameraTransform = *MakeNotNull(m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(camera));
        const f32 cameraX = m_mode.arm == Arm::CameraShift ? 0.3f : 0.0f;
        cameraTransform.position.x = cameraX;
        const auto receiver = CreateTintedStaticMeshEntity(s_PlaneMesh, s_OpaqueMaterial, s_CsgShadowMaterialInterface, Float4(1.0f, 1.0f, 1.0f, 1.0f), Float4(0.0f, 0.0f, 0.0f, 0.0f), Float4(4.0f, 1.0f, 3.0f, 0.0f), *m_world);
        NWB_FATAL_ASSERT_MSG(receiver.valid(), NWB_TEXT("CsgShadowSmokeProject: camera/receiver creation failed"));
        auto& receiverTransform = *MakeNotNull(m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(receiver));
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.0f, 0.0f), receiverTransform.rotation);
        const auto light = m_mode.pointLight
            ? NWB::Impl::Scene::CreatePointLightEntity(*m_world, Float4(0.0f, 0.0f, -12.0f, 0.0f), Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 30.0f)
            : NWB::Impl::Scene::CreateDirectionalLightEntity(*m_world, 0.0f, 0.0f, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f);
        NWB_FATAL_ASSERT_MSG(light.valid(), NWB_TEXT("CsgShadowSmokeProject: light creation failed"));
        auto& lightComponent = *MakeNotNull(m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(light));
        lightComponent.angularRadius = m_mode.finiteLightSource && !m_mode.pointLight ? 0.005f : 0.0f;
        lightComponent.sourceRadius = m_mode.finiteLightSource && m_mode.pointLight ? 0.02f : 0.0f;
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        lightComponent.enableCaustics = !ReadSmokeEnvironmentFlag("NWB_CSG_CAUSTIC_DISABLED");
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticSmokeProject: photons {}"), lightComponent.enableCaustics ? NWB_TEXT("enabled") : NWB_TEXT("disabled"));
#else
        lightComponent.enableCaustics = false;
#endif
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: light_source={} angular_radius={:.3f} source_radius={:.3f}")
            , ConvertString(m_mode.finiteLightSource ? s_Finite : s_Hard)
            , static_cast<f64>(lightComponent.angularRadius)
            , static_cast<f64>(lightComponent.sourceRadius)
        );
        if(!m_atlas.create())
            return false;
        const b8 hardware = m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery) && m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: atlas arm={} light={} hardware={} camera_x={} caster_z_max=-5 receiver_z=0")
            , static_cast<u32>(m_mode.arm)
            , ConvertString(m_mode.pointLight ? s_Point : s_Directional)
            , hardware ? 1u : 0u
            , static_cast<f64>(cameraX)
        );
        constexpr u32 s_DefaultCaptureFrameCount = NWB::Tests::Smoke::s_ShadowSmokeDefaultCaptureFrameCount;
        auto capture = NWB::Tests::Smoke::SmokeCaptureFactory(m_context).configure(s_ProjectName, s_DefaultCaptureFrameCount, {});
        if(!capture)
            return false;
        m_capture = Move(*capture);
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        NWB::Core::Alloc::ScratchArena scratchArena(NWB::Tests::Smoke::s_SmokeEnvironmentArena);
        if(!configureTiming(scratchArena))
            return false;
#endif
        return true;
    }

    virtual void onShutdown()override{
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        if(m_timing)
            m_timing->finish();
#endif
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: shutdown"));
    }

    virtual b8 onUpdate(const f32 delta)override{
        if(m_capture)
            m_capture->update();
        ++m_update;
        if(m_mode.arm == Arm::Moved && m_update == s_MoveUpdate){
            m_atlas.moveCutters();
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgShadowSmokeProject: cutters moved at update40"));
        }
        const f32 fixedDelta = RendererBaselineFixedDelta();
        if(!TickSmokeWorld(fixedDelta > 0.0f ? fixedDelta : delta, *m_world))
            return false;
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        if(m_timing && !m_timing->update())
            return false;
#endif
        return true;
    }


private:

#if defined(NWB_CSG_CAUSTIC_SMOKE)
    b8 configureTiming(NWB::Core::Alloc::ScratchArena& scratchArena){
        const auto mode = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_CAUSTIC_TIMING_MODE");
        const auto output = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_CAUSTIC_TIMING_FILE");
        const auto cpuOutput = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_CAUSTIC_CPU_GPU_FILE");
        const auto start = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_CAUSTIC_TIMING_START_FILE");
        if(!mode){
            if(output || cpuOutput || start){
                NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: mode and explicit output must agree"));
                return false;
            }
            return true;
        }
        const AStringView modeView(mode->data(), mode->size());
        const auto route = ReadSmokeEnvironmentText(scratchArena, "NWB_CSG_CAUSTIC_TIMING_ROUTE");
        const AStringView routeView = route ? AStringView(route->data(), route->size()) : AStringView{};
        if(
            (modeView != "performance" && modeView != "diagnostic") || (routeView != "hardware" && routeView != "software") || !output || !start || m_capture || !m_context.requestQuit
            || (m_mode.arm != Arm::Reference && m_mode.arm != Arm::Cut) || m_mode.pointLight || m_mode.finiteLightSource || m_mode.analyticAtlas || m_mode.openAtlas || ReadSmokeEnvironmentFlag("NWB_CSG_CAUSTIC_DISABLED")
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: requires uncaptured directional hard-light reference/cut boxes and an explicit route"));
            return false;
        }
        const b8 rayTracing = m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct);
        const b8 rayQuery = m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery);
        const b8 hardware = rayTracing && rayQuery;
        if(routeView == "hardware" && !hardware){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticTiming: hardware unavailable rt_accel={} ray_query={}"), rayTracing ? 1u : 0u, rayQuery ? 1u : 0u);
            m_context.requestQuit();
            return true;
        }
        if((routeView == "hardware") != hardware){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: actual route differs from requested acquisition"));
            return false;
        }
        const usize receivers = m_world->view<NWB::Impl::StaticCsgMeshComponent>().candidateCount() + m_world->view<NWB::Impl::SkinnedCsgMeshComponent>().candidateCount();
        const usize cutters = m_world->view<NWB::Impl::CsgCutterComponent>().candidateCount();
        if(receivers != (m_mode.arm == Arm::Cut ? 1u : 0u) || cutters != receivers){
            NWB_LOGGER_ERROR(NWB_TEXT("CsgCausticTiming: fixture receiver/cutter identity differs from selected arm"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("CsgCausticTiming: scene arm={} receivers={} cutters={} expected_shader_csg={} camera=960x720 light=directional capture=0")
            , m_mode.arm == Arm::Cut ? NWB_TEXT("cut") : NWB_TEXT("reference")
            , receivers
            , cutters
            , receivers != 0u ? 1u : 0u
        );
        m_timing = MakeUnique<NWB::Tests::Smoke::CsgCausticTimingProbe>(m_context);
        if(!m_timing->initialize(
            modeView == "diagnostic", receivers != 0u, hardware, AStringView(output->data(), output->size())
            , cpuOutput ? AStringView(cpuOutput->data(), cpuOutput->size()) : AStringView{}, AStringView(start->data(), start->size())
        ))
            return false;
        m_context.graphics.addRenderPassToBack(*m_timing);
        m_timingRegistered = true;
        return true;
    }
#endif

    void destroyWorld(){
#if defined(NWB_CSG_CAUSTIC_SMOKE)
        if(m_timingRegistered){
            m_context.graphics.removeRenderPass(*m_timing);
            m_timingRegistered = false;
        }
        m_timing.reset();
#endif
        if(m_capture){
            m_capture->stop();
            m_capture.reset();
        }
        DestroySmokeRenderWorld(m_context, m_world);
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    CsgShadowMode m_mode;
    CsgShadowAtlas m_atlas;
    UniquePtr<FramebufferCapture> m_capture;
#if defined(NWB_CSG_CAUSTIC_SMOKE)
    UniquePtr<NWB::Tests::Smoke::CsgCausticTimingProbe> m_timing;
#endif
    u32 m_update = 0u;
#if defined(NWB_CSG_CAUSTIC_SMOKE)
    b8 m_timingRegistered = false;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){ return { 960, 720 }; }

TStringView NWB::QueryProjectWindowTitle()noexcept{ return NWB_TEXT("NWB CSG Shadow Smoke"); }

UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_csg_shadow_smoke::CsgShadowSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

