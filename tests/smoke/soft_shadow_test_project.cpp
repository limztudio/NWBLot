// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>
#include <global/math/frame.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_mesh/skinning/module.h>

#include "arrow_yaw_input_handler.h"
#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "shadow_timing_render_pass.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_soft_shadow_test_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::AddSmokeSkinnedRenderSystems;
using NWB::Tests::Smoke::ArrowYawInputHandler;
using NWB::Tests::Smoke::CreateSmokeCamera;
using NWB::Tests::Smoke::CreateSmokeWorldOrDie;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::CreateTintedModelEntity;
using NWB::Tests::Smoke::DestroySmokeSkinnedRenderWorld;
using NWB::Tests::Smoke::MakeSmokeYawDisplay;
using NWB::Tests::Smoke::ReadSmokeEnvironmentF32;
using NWB::Tests::Smoke::RendererBaselineCaptureFreezeFrame;
using NWB::Tests::Smoke::RendererBaselineFixedDelta;
using NWB::Tests::Smoke::ReadSmokeFrozenYawFromEnvironment;
using NWB::Tests::Smoke::SyncSmokeModelRuntimes;

using SoftShadowModelRef = NWB::Core::Assets::AssetRef<NWB::Impl::Model>;
using SoftShadowMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;
using SoftShadowMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Opaque and glass casters expose contact-to-distant penumbras under directional, point and spot lights.
// NWB_SOFT_SHADOW_TEST_ANGLE and NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS control softness;
// NWB_SOFT_SHADOW_TEST_SPIN_ANGLE pins yaw for A/B captures.
static constexpr SoftShadowModelRef s_Model{"project/characters/body/model"};
static constexpr SoftShadowMaterialRef s_OpaqueMaterial{"project/smoke/transparent_multi/materials/ground"};
static constexpr SoftShadowMaterialRef s_TransparentMaterial{"project/smoke/transparent_multi/materials/shared"};
static constexpr SoftShadowMaterialRef s_TimingOpaqueMaterial{"project/smoke/soft_shadow/materials/timing_opaque"};
static constexpr SoftShadowMaterialRef s_TimingTransparentMaterial{"project/smoke/soft_shadow/materials/timing_transparent"};
static constexpr SoftShadowMeshRef s_GroundMesh{"project/meshes/shadow_plane"};
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";

static constexpr f32 s_GroundScale = 8.0f;

static constexpr f32 s_CameraDistance = 3.2f;
static constexpr f32 s_CameraHeight = 1.5f;
static constexpr f32 s_CameraPitch = 0.30f;

// Directional sun rakes sideways (large yaw, moderate pitch): long high-contrast shadow, crisp at feet, soft far.
static constexpr f32 s_DirectionalLightPitch = 0.65f;
static constexpr f32 s_DirectionalLightYaw = 1.4f;
static constexpr f32 s_DirectionalLightIntensity = 2.0f;
static constexpr f32 s_DefaultAngularRadius = 0.03f;
static constexpr f32 s_DefaultSourceRadius = 0.15f;

// Different light colors and directions separate their overlapping penumbras.
static constexpr f32 s_PointLightIntensity = 10.0f;
static constexpr f32 s_PointLightRange = 14.0f;
static constexpr f32 s_SpotLightIntensity = 13.0f;
static constexpr f32 s_SpotLightRange = 16.0f;
static constexpr f32 s_SpotLightPitch = 1.5f;
static constexpr f32 s_SpotLightYaw = 0.0f;
static constexpr f32 s_SpotInnerConeCos = 0.85f;
static constexpr f32 s_SpotOuterConeCos = 0.55f;

static constexpr f32 s_SpinSpeed = 0.5f; // radians/second
static constexpr f32 s_ManualYawSpeed = 0.6f; // radians/second
static constexpr f32 s_TwoPi = 6.2831853f;
static constexpr f32 s_MaxSpinDelta = 1.0f / 15.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SoftShadowTestSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    [[nodiscard]] static u32 rendererBaselineCaptureFreezeFrame(){
        return RendererBaselineCaptureFreezeFrame();
    }

    [[nodiscard]] static f32 rendererBaselineFixedDelta(){
        return RendererBaselineFixedDelta();
    }


    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(context, GLB_TEXT("SoftShadowTestSmokeProject"));

        AddSmokeSkinnedRenderSystems(*world, context);

        return world;
    }

    void destroyWorld(){
        DestroySmokeSkinnedRenderWorld(m_context, m_world);
    }

    // Angular radius is in radians; larger sources soften the penumbra.
    static f32 configuredAngularRadius(){
        static const f32 s_angle = [](){
            f32 parsed = s_DefaultAngularRadius;
            if(!ReadSmokeEnvironmentF32("NWB_SOFT_SHADOW_TEST_ANGLE", parsed))
                return s_DefaultAngularRadius;
            return Clamp(parsed, 0.0f, 0.2f);
        }();
        return s_angle;
    }

    // Point/spot softness depends on source radius in world units and light distance: asin(radius / distance).
    static f32 configuredSourceRadius(){
        static const f32 s_radius = [](){
            f32 parsed = s_DefaultSourceRadius;
            if(!ReadSmokeEnvironmentF32("NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS", parsed))
                return s_DefaultSourceRadius;
            return Clamp(parsed, 0.0f, 1.0f);
        }();
        return s_radius;
    }

    // Pin yaw for captures that isolate shimmer from motion.
    static f32 frozenYaw(){
        static const f32 s_yaw = ReadSmokeFrozenYawFromEnvironment("NWB_SOFT_SHADOW_TEST_SPIN_ANGLE");
        return s_yaw;
    }

    void spinCasters(){
        for(const NWB::Core::ECS::EntityID owner : { m_characterOwner, m_glassOwner }){
            auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(owner);
            if(transform)
                StoreFloat(QuaternionRotationRollPitchYaw(0.0f, m_yaw.yaw(), 0.0f), transform->rotation);
        }
    }


public:
    explicit SoftShadowTestSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
    {}

    virtual ~SoftShadowTestSmokeProject()override{
        m_timingRenderPass.stop();
        m_context.input.removeHandler(m_arrowYawInput); // Backstop for skipped onShutdown; the dispatcher outlives this project.
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        m_timingEnabled = NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_SOFT_SHADOW_TEST_TIMING");
        if(m_timingEnabled){
            m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::gpuTimingOnly());
            if(!m_timingRenderPass.start())
                return false;
        }
        // addHandlerToBack gives this scrubber first crack at the arrow keys; it consumes only Left/Right.
        m_context.input.addHandlerToBack(m_arrowYawInput);

        const NWB::Core::ECS::EntityID activeCamera = CreateSmokeCamera(*m_world, s_CameraHeight, s_CameraDistance, s_CameraPitch);

        const NWB::Core::ECS::EntityID directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DirectionalLightPitch,
            s_DirectionalLightYaw,
            0.0f,
            Float4(1.00f, 0.96f, 0.88f),
            s_DirectionalLightIntensity
        );
        if(auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(directionalLight)){
            light->angularRadius = configuredAngularRadius();
            if(m_timingEnabled)
                light->enableCaustics = false;
        }

        const NWB::Core::ECS::EntityID pointLight = NWB::Impl::Scene::CreatePointLightEntity(
            *m_world,
            Float4(1.5f, 2.2f, 0.3f, 0.0f),
            Float4(1.00f, 0.35f, 0.30f),
            s_PointLightIntensity,
            s_PointLightRange
        );
        if(auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(pointLight)){
            light->sourceRadius = configuredSourceRadius();
            if(m_timingEnabled)
                light->enableCaustics = false;
        }

        const NWB::Core::ECS::EntityID spotLight = NWB::Impl::Scene::CreateSpotLightEntity(
            *m_world,
            Float4(0.4f, 3.0f, 0.4f, 0.0f),
            s_SpotLightPitch,
            s_SpotLightYaw,
            0.0f,
            Float4(0.35f, 0.55f, 1.00f),
            s_SpotLightIntensity,
            s_SpotLightRange,
            s_SpotInnerConeCos,
            s_SpotOuterConeCos
        );
        if(auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(spotLight)){
            light->sourceRadius = configuredSourceRadius();
            if(m_timingEnabled)
                light->enableCaustics = false;
        }

        // These project-owned BXDFs preserve direct shadow response and ignore only the stochastic surfel contribution.
        // Surfel resource preparation/production remains an unchanged renderer responsibility in this fixture.
        const auto& opaqueMaterial = m_timingEnabled ? s_TimingOpaqueMaterial : s_OpaqueMaterial;
        const auto& transparentMaterial = m_timingEnabled ? s_TimingTransparentMaterial : s_TransparentMaterial;
        m_groundEntity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_GroundMesh,
            opaqueMaterial,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.82f, 0.82f, 0.85f, 1.0f),
            Float4(0.0f, 0.0f, 0.0f, 0.0f),
            Float4(s_GroundScale, 1.0f, s_GroundScale, 0.0f)
        );

        // The opaque caster provides the contact-hardening reference.
        bool tintApplied = false;
        m_characterOwner = CreateTintedModelEntity(
            *m_world,
            m_context.objectArena,
            s_Model,
            opaqueMaterial,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.86f, 0.80f, 0.74f, 1.0f),
            Float4(0.7f, 0.0f, -1.1f, 0.0f),
            Float4(1.0f, 1.0f, 1.0f, 0.0f),
            tintApplied
        );
        if(!tintApplied)
            NWB_LOGGER_ERROR(GLB_TEXT("SoftShadowTestSmokeProject: failed to set character tint"));

        // The glass caster must preserve tint while its penumbra widens with receiver distance.
        bool glassTintApplied = false;
        m_glassOwner = CreateTintedModelEntity(
            *m_world,
            m_context.objectArena,
            s_Model,
            transparentMaterial,
            s_SmokeSurfaceMaterialInterface,
            // Coverage and absorption share authored tint/density; Beer-Lambert integrates the actual mesh chord.
            Float4(0.20f, 0.55f, 0.12f, 0.6f),
            Float4(-0.6f, 0.0f, -1.1f, 0.0f),
            Float4(1.0f, 1.0f, 1.0f, 0.0f),
            glassTintApplied
        );
        if(!glassTintApplied)
            NWB_LOGGER_ERROR(GLB_TEXT("SoftShadowTestSmokeProject: failed to set glass tint"));

        SyncSmokeModelRuntimes(*m_world);

        GLB_FATAL_ASSERT_MSG(
            activeCamera.valid() && m_groundEntity.valid() && m_characterOwner.valid() && m_glassOwner.valid(),
            GLB_TEXT("SoftShadowTestSmokeProject failed to create all scene entities")
        );

        if(m_timingEnabled){
            const NWB::Core::ECS::EntityID lights[] = { directionalLight, pointLight, spotLight };
            for(const auto lightEntity : lights){
                const auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(lightEntity);
                if(!light || light->enableCaustics){
                    NWB_LOGGER_ERROR(GLB_TEXT("ShadowTimingProbe: timing light policy unavailable"));
                    return false;
                }
            }
            const bool hardwareAvailable = m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct)
                && m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery);
            NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("ShadowTimingProbe: natural shadow route {}")
                , hardwareAvailable ? GLB_TEXT("hardware") : GLB_TEXT("software")
            );
            NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("ShadowTimingProbe: caustic emission 0"));
            NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("ShadowTimingProbe: indirect response hemi-ambient"));
            NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("ShadowTimingProbe: source extents angular={} radius={}")
                , static_cast<f64>(configuredAngularRadius())
                , static_cast<f64>(configuredSourceRadius())
            );
        }
        NWB_LOGGER_ESSENTIAL_INFO(
            GLB_TEXT("SoftShadowTestSmokeProject: opaque + glass characters on a ground plane, 3 coloured lights, angularRadius={} rad")
            , static_cast<f64>(configuredAngularRadius())
        );
        return true;
    }

    virtual void onShutdown()override{
        m_timingRenderPass.stop();
        m_context.graphics.setFrameSubmissionSuspended(false);
        m_context.input.removeHandler(m_arrowYawInput);
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("SoftShadowTestSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const u32 captureFreezeFrame = rendererBaselineCaptureFreezeFrame();
        if(captureFreezeFrame != 0u && m_rendererBaselineRenderedFrameCount >= captureFreezeFrame){
            if(!m_rendererBaselineCapturePaused){
                // Freeze the requested update-callback phase; this is not an accepted GPU submission counter.
                // The capture runner waits for this marker before its settle delay and client capture.
                m_context.graphics.setFrameSubmissionSuspended(true);
                m_rendererBaselineCapturePaused = true;
                NWB_LOGGER_ESSENTIAL_INFO(
                    GLB_TEXT("SoftShadowTestSmokeProject: renderer baseline capture ready after {} update callbacks; render submission suspended"),
                    m_rendererBaselineRenderedFrameCount
                );
            }
            return true;
        }

        const f32 fixedDelta = rendererBaselineFixedDelta();
        const f32 safeDelta = fixedDelta > 0.0f ? fixedDelta : (IsFinite(delta) ? Max(delta, 0.0f) : 0.0f);
        m_fpsProbe.recordFrame(safeDelta);
        if(m_timingEnabled)
            m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
        // Yaw priority: fixed override, manual scrub, then automatic spin.
        const f32 frozen = frozenYaw();
        m_yaw.update(safeDelta, frozen, frozen >= 0.0f, m_arrowYawInput, s_ManualYawSpeed, s_SpinSpeed, s_MaxSpinDelta);
        spinCasters();
        updateWindowTitle();
        m_world->tick(safeDelta);
        ++m_rendererBaselineRenderedFrameCount;
        return true;
    }

    void updateWindowTitle(){
        const auto yawDisplay = MakeSmokeYawDisplay(m_yaw.yaw(), s_TwoPi);
        const f32 angleDegrees = configuredAngularRadius() * (360.0f / s_TwoPi);

        static constexpr usize s_TitleCapacity = 256u;
        tchar title[s_TitleCapacity];
        GLB_TSPRINTF(
            title, s_TitleCapacity,
            GLB_TEXT("%.*s  |  yaw %.2f deg  |  sun %.2f deg  |  src r %.3f%s"),
            static_cast<i32>(NWB::QueryProjectWindowTitle().size()), NWB::QueryProjectWindowTitle().data(), yawDisplay.degrees, angleDegrees, configuredSourceRadius(),
            m_yaw.manualControl() ? GLB_TEXT("  [manual: <- ->]") : GLB_TEXT("")
        );
        m_context.graphics.setWindowTitle(TStringView(title));
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Core::ECS::EntityID m_characterOwner = NWB::Core::ECS::s_InvalidEntityId;
    NWB::Core::ECS::EntityID m_glassOwner = NWB::Core::ECS::s_InvalidEntityId;
    NWB::Core::ECS::EntityID m_groundEntity = NWB::Core::ECS::s_InvalidEntityId;
    NWB::Tests::Smoke::ShadowTimingRenderPass m_timingRenderPass{ m_context.graphics };
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ GLB_TEXT("SoftShadowTestSmokeProject") };
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ GLB_TEXT("SoftShadowTestSmokeProject") };
    bool m_timingEnabled = false;
    NWB::Tests::Smoke::YawSpinController m_yaw;
    ArrowYawInputHandler m_arrowYawInput;
    u32 m_rendererBaselineRenderedFrameCount = 0u;
    bool m_rendererBaselineCapturePaused = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


TStringView NWB::QueryProjectWindowTitle(){
    return GLB_TEXT("NWB Soft Shadow Test");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_soft_shadow_test_smoke::SoftShadowTestSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

