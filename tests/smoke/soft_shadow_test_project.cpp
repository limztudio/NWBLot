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


// SOFT-SHADOW scene: OPAQUE + GLASS `body` casters on one ground plane, lit by warm DIRECTIONAL + RED point + BLUE spot,
// each with a physical source size (penumbra widens with distance, hardens at contact). Glass tints its shadow. Both spin.
// A/B levers: NWB_SOFT_SHADOW_TEST_ANGLE = sun angular radius (0.001 hard ref .. 0.05 very soft);
// NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS = point/spot sphere radius (nearer light softens via asin(radius/dist)).
// No-RayQuery devices take the SW path (half-res trace -> a-trous -> upsample); HW applies the same cone jitter at full
// res without denoise. Arrow keys scrub yaw (NWB_SOFT_SHADOW_TEST_SPIN_ANGLE pins it); shared materials, fixed-ambient timing.
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

// Point / spot light params. All three lights (directional + point + spot) are lit AT ONCE, spread so their coloured
// shadows rake in different directions. Point lights attenuate by distance -> brighter than the directional sun.
static constexpr f32 s_PointLightIntensity = 10.0f;
static constexpr f32 s_PointLightRange = 14.0f;
static constexpr f32 s_SpotLightIntensity = 13.0f;
static constexpr f32 s_SpotLightRange = 16.0f;
static constexpr f32 s_SpotLightPitch = 1.5f;                        // near-overhead, aimed ~straight down at the caster
static constexpr f32 s_SpotLightYaw = 0.0f;
static constexpr f32 s_SpotInnerConeCos = 0.85f;
static constexpr f32 s_SpotOuterConeCos = 0.55f;                     // WIDE cone so the caster stays lit even if the aim is approximate

static constexpr f32 s_SpinSpeed = 0.5f;                             // radians / second, gentle auto-spin until an arrow takes over
static constexpr f32 s_ManualYawSpeed = 0.6f;                        // radians / second for the arrow-key manual yaw scrub
static constexpr f32 s_TwoPi = 6.2831853f;
static constexpr f32 s_MaxSpinDelta = 1.0f / 15.0f;                  // clamp huge stalls so the spin can't jump


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
        auto world = CreateSmokeWorldOrDie(context, NWB_TEXT("SoftShadowTestSmokeProject"));

        AddSmokeSkinnedRenderSystems(*world, context);

        return world;
    }

    void destroyWorld(){
        DestroySmokeSkinnedRenderWorld(m_context, m_world);
    }

    // The directional light's angular radius (radians), read once from NWB_SOFT_SHADOW_TEST_ANGLE (default s_DefaultAngularRadius).
    // Larger = softer penumbra; clamped to a sane [0, 0.2] rad (0..~11.5deg) so a typo can't blow the penumbra out.
    static f32 configuredAngularRadius(){
        static const f32 s_angle = [](){
            f32 parsed = s_DefaultAngularRadius;
            if(!ReadSmokeEnvironmentF32("NWB_SOFT_SHADOW_TEST_ANGLE", parsed))
                return s_DefaultAngularRadius;
            return Clamp(parsed, 0.0f, 0.2f);
        }();
        return s_angle;
    }

    // The point/spot emissive sphere radius (world units), read once from NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS (default
    // s_DefaultSourceRadius). Larger = softer; clamped to [0, 1]. The penumbra ALSO widens as the light nears the caster
    // (the source subtends asin(radius/dist)), so moving the light softens it too -- radius is only half the story.
    static f32 configuredSourceRadius(){
        static const f32 s_radius = [](){
            f32 parsed = s_DefaultSourceRadius;
            if(!ReadSmokeEnvironmentF32("NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS", parsed))
                return s_DefaultSourceRadius;
            return Clamp(parsed, 0.0f, 1.0f);
        }();
        return s_radius;
    }

    // Diagnostic freeze (read once): NWB_SOFT_SHADOW_TEST_SPIN_ANGLE pins the yaw to a fixed radians value so the character
    // holds one orientation -- two captures then differ only via non-determinism (shimmer), not motion.
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
        m_context.input.removeHandler(m_arrowYawInput); // idempotent backstop if onShutdown was skipped (dispatcher outlives us)
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        m_timingEnabled = NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_SOFT_SHADOW_TEST_TIMING");
        if(m_timingEnabled){
            m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());
            if(!m_timingRenderPass.start())
                return false;
        }
        // addHandlerToBack gives this scrubber first crack at the arrow keys; it consumes only Left/Right.
        m_context.input.addHandlerToBack(m_arrowYawInput);

        const NWB::Core::ECS::EntityID activeCamera = CreateSmokeCamera(*m_world, s_CameraHeight, s_CameraDistance, s_CameraPitch);

        // Three tinted soft lights at once (warm sun sideways, red point opposite, blue spot overhead); each casts its own
        // tinted penumbra. Source size drives softness (sun: angularRadius; point/spot: asin(R/dist)); env-tunable.
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

        // The OPAQUE caster: the `body` character with an opaque lambert material, front-RIGHT. Its cast shadow is the grey
        // reference -- CRISP at the feet (contact) and softening up the body, the physical soft-shadow signature.
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
            NWB_LOGGER_ERROR(NWB_TEXT("SoftShadowTestSmokeProject: failed to set character tint"));

        // A GLASS (transparent) `body` caster front-LEFT, beside the opaque one: the same body model with a refractive
        // material + a coloured tint. Its shadow is the COLORED transparent soft shadow -- the light passing through the
        // glass is tinted by it, so the cast shadow carries the glass colour AND softens with occluder->receiver distance.
        bool glassTintApplied = false;
        m_glassOwner = CreateTintedModelEntity(
            *m_world,
            m_context.objectArena,
            s_Model,
            transparentMaterial,
            s_SmokeSurfaceMaterialInterface,
            // Glass tint = (kept shadow colour . density): nwbMakeGlassSurface derives renderCoverage +
            // shadowAbsorptionTint together; Beer-Lambert integrates the mesh chord, so pair tint with thickness.
            Float4(0.20f, 0.55f, 0.12f, 0.6f),
            Float4(-0.6f, 0.0f, -1.1f, 0.0f),
            Float4(1.0f, 1.0f, 1.0f, 0.0f),
            glassTintApplied
        );
        if(!glassTintApplied)
            NWB_LOGGER_ERROR(NWB_TEXT("SoftShadowTestSmokeProject: failed to set glass tint"));

        SyncSmokeModelRuntimes(*m_world);

        NWB_FATAL_ASSERT_MSG(
            activeCamera.valid() && m_groundEntity.valid() && m_characterOwner.valid() && m_glassOwner.valid(),
            NWB_TEXT("SoftShadowTestSmokeProject failed to create all scene entities")
        );

        if(m_timingEnabled){
            const NWB::Core::ECS::EntityID lights[] = { directionalLight, pointLight, spotLight };
            for(const auto lightEntity : lights){
                const auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(lightEntity);
                if(!light || light->enableCaustics){
                    NWB_LOGGER_ERROR(NWB_TEXT("ShadowTimingProbe: timing light policy unavailable"));
                    return false;
                }
            }
            const bool hardwareAvailable = m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct)
                && m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery);
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ShadowTimingProbe: natural shadow route {}")
                , hardwareAvailable ? NWB_TEXT("hardware") : NWB_TEXT("software")
            );
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ShadowTimingProbe: caustic emission 0"));
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ShadowTimingProbe: indirect response hemi-ambient"));
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ShadowTimingProbe: source extents angular={} radius={}")
                , static_cast<f64>(configuredAngularRadius())
                , static_cast<f64>(configuredSourceRadius())
            );
        }
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("SoftShadowTestSmokeProject: opaque + glass characters on a ground plane, 3 coloured lights, angularRadius={} rad")
            , static_cast<f64>(configuredAngularRadius())
        );
        return true;
    }

    virtual void onShutdown()override{
        m_timingRenderPass.stop();
        m_context.graphics.setFrameSubmissionSuspended(false);
        m_context.input.removeHandler(m_arrowYawInput);
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SoftShadowTestSmokeProject: shutdown"));
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
                    NWB_TEXT("SoftShadowTestSmokeProject: renderer baseline capture ready after {} update callbacks; render submission suspended"),
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
        // Yaw selection: 1) NWB_SOFT_SHADOW_TEST_SPIN_ANGLE env freeze (pins one orientation); 2) manual arrow scrub
        // (latches off auto-spin the moment Left/Right is first pressed); 3) auto-spin.
        const f32 frozen = frozenYaw();
        m_yaw.update(safeDelta, frozen, frozen >= 0.0f, m_arrowYawInput, s_ManualYawSpeed, s_SpinSpeed, s_MaxSpinDelta);
        spinCasters();
        updateWindowTitle();
        m_world->tick(safeDelta);
        ++m_rendererBaselineRenderedFrameCount;
        return true;
    }

    // Reflect the current yaw (wrapped to [0, 2pi)) + both soft source sizes (the directional sun angle in deg + the
    // point/spot radius in world units) in the title bar, so the parameters in effect can be read off at a glance.
    void updateWindowTitle(){
        const auto yawDisplay = MakeSmokeYawDisplay(m_yaw.yaw(), s_TwoPi);
        const f32 angleDegrees = configuredAngularRadius() * (360.0f / s_TwoPi);

        static constexpr usize s_TitleCapacity = 256u;
        tchar title[s_TitleCapacity];
        NWB_TSPRINTF(
            title, s_TitleCapacity,
            NWB_TEXT("%s  |  yaw %.2f deg  |  sun %.2f deg  |  src r %.3f%s"),
            NWB::QueryProjectWindowTitle(), yawDisplay.degrees, angleDegrees, configuredSourceRadius(),
            m_yaw.manualControl() ? NWB_TEXT("  [manual: <- ->]") : NWB_TEXT("")
        );
        const tchar* titlePtr = title;
        m_context.graphics.setWindowTitle(MakeNotNull(titlePtr));
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Core::ECS::EntityID m_characterOwner = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_glassOwner = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_groundEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Tests::Smoke::ShadowTimingRenderPass m_timingRenderPass{ m_context.graphics };
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ NWB_TEXT("SoftShadowTestSmokeProject") };
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ NWB_TEXT("SoftShadowTestSmokeProject") };
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


const tchar* NWB::QueryProjectWindowTitle(){
    return NWB_TEXT("NWB Soft Shadow Test");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_soft_shadow_test_smoke::SoftShadowTestSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

