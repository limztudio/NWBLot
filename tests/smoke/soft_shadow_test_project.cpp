// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <core/mesh/frame_math.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_render/model_renderer.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material_instance.h>
#include <impl/ecs_mesh/skinning/module.h>

#include "arrow_yaw_input_handler.h"
#include "fps_probe.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"

#include <global/environment.h>   // ReadEnvironmentVariableBuffer -- NWB_SOFT_SHADOW_TEST_ANGLE / _SPIN_ANGLE
#include <global/text_utils.h>     // ParseF32FromChars -- env float parse


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
using NWB::Tests::Smoke::ReadSmokeFrozenYawFromEnvironment;
using NWB::Tests::Smoke::SyncSmokeModelRuntimes;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// DEDICATED SOFT-SHADOW scene (soft-ray-traced-shadow feature): an OPAQUE and a GLASS `body` character standing SIDE BY
// SIDE on ONE opaque ground plane, lit by THREE differently-coloured lights AT ONCE -- a warm-white DIRECTIONAL sun, a RED
// POINT light, and a BLUE SPOT -- each with a PHYSICAL source size. That source size is what makes each shadow soft: the
// trace jitters the ray over the light's source (the sun's angularRadius disk, or a point/spot's sourceRadius sphere
// subtending asin(R/dist)), so every penumbra emerges + WIDENS with occluder->receiver distance and HARDENS at contact. The
// GLASS caster casts a COLORED transparent soft shadow (Stage 5) beside the opaque grey one. Both spin (arrow keys).
//
// A/B levers:
//   - THREE differently-coloured soft-shadowed lights are lit AT ONCE (a warm-white directional sun, a RED point, a BLUE
//     spot), so all three soft penumbras are on screen together, distinguishable by tint (overlaps blend the colours).
//   - NWB_SOFT_SHADOW_TEST_ANGLE (radians, default 0.03 ~ 1.7deg): the DIRECTIONAL sun's angular radius. Sweep it --
//     ~0.001 is a near-HARD reference (tight penumbra), ~0.05 is very soft. Shown live in the title bar (deg).
//   - NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS (world units, default 0.15): the POINT + SPOT emissive sphere radius. Larger =
//     softer; the penumbra ALSO widens as the light nears the caster (asin(radius/dist)) -- physical distance softening.
//   - The _sw_smoke build (NWB_SOFT_SHADOW_TEST_FORCE_RT_EMULATION) forces the SOFTWARE path, which runs the FULL soft
//     pipeline (half-res jittered trace -> a-trous denoise -> bilateral upsample). The HW build applies the same cone
//     jitter but at full res without the denoise (per-frame shimmer expected until the temporal stage).
//   - Arrow keys (Left/Right) scrub the character yaw so the sweeping soft edge can be checked for crawl (it should NOT
//     crawl -- a soft edge has nothing to alias); NWB_SOFT_SHADOW_TEST_SPIN_ANGLE pins a fixed yaw for a deterministic A/B.
// Reuses the benchmark's cooked body model + ground material (no new assets).
static constexpr AStringView s_ModelPath = "project/characters/body/model";
static constexpr AStringView s_OpaqueMaterialPath = "project/smoke/transparent_multi/materials/ground";  // opaque lambert
static constexpr AStringView s_TransparentMaterialPath = "project/smoke/transparent_multi/materials/shared"; // glass (colored transmittance)
static constexpr AStringView s_GroundMeshPath = "project/meshes/shadow_plane";
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";

static constexpr f32 s_GroundScale = 8.0f;

static constexpr f32 s_CameraDistance = 3.2f;                        // fits the opaque + glass casters both in frame
static constexpr f32 s_CameraHeight = 1.5f;
static constexpr f32 s_CameraPitch = 0.30f;                         // tilt down a bit more to frame the ground shadow

// Directional light aimed to cast the shadow SIDEWAYS across the plane (large yaw), NOT behind the character where her
// own body would hide it -- the shadow rakes out to one side, in full view, and its penumbra widens along its length
// (crisp at the feet, soft at the far end). A moderate pitch keeps the plane well-lit (high shadow contrast) while still
// giving a long shadow. Warm sun tint. Intensity is clamped to 2.0 by the shading, so 2.0 is the useful max.
static constexpr f32 s_DirectionalLightPitch = 0.65f;
static constexpr f32 s_DirectionalLightYaw = 1.4f;
static constexpr f32 s_DirectionalLightIntensity = 2.0f;
static constexpr f32 s_DefaultAngularRadius = 0.03f;                 // ~1.7deg: clearly soft but not mushy (env-overridable)
static constexpr f32 s_DefaultSourceRadius = 0.15f;                  // point/spot emissive sphere radius (world units); env-overridable

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
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(context, NWB_TEXT("SoftShadowTestSmokeProject"));

        // Force ray-tracing emulation so the SOFTWARE shadow path runs even on RT-capable hardware -- the software path is
        // the one that runs the FULL soft pipeline (half-res jittered trace -> a-trous denoise -> bilateral upsample), so
        // the _sw_smoke build is the intended way to see the denoised soft shadow. Default OFF: the HW (hybrid) path.
#if defined(NWB_SOFT_SHADOW_TEST_FORCE_RT_EMULATION) && !defined(NWB_FINAL)
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayTracingAccelStruct, true);
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayTracingPipeline, true);
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayQuery, true);
#endif

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
            char value[32] = {};
            if(!ReadEnvironmentVariableBuffer("NWB_SOFT_SHADOW_TEST_ANGLE", value, sizeof(value)))
                return s_DefaultAngularRadius;
            f32 parsed = s_DefaultAngularRadius;
            if(!ParseF32FromChars(value, value + NWB_STRLEN(value), parsed))
                return s_DefaultAngularRadius;
            return Min(Max(parsed, 0.0f), 0.2f);
        }();
        return s_angle;
    }

    // The point/spot emissive sphere radius (world units), read once from NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS (default
    // s_DefaultSourceRadius). Larger = softer; clamped to [0, 1]. The penumbra ALSO widens as the light nears the caster
    // (the source subtends asin(radius/dist)), so moving the light softens it too -- radius is only half the story.
    static f32 configuredSourceRadius(){
        static const f32 s_radius = [](){
            char value[32] = {};
            if(!ReadEnvironmentVariableBuffer("NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS", value, sizeof(value)))
                return s_DefaultSourceRadius;
            f32 parsed = s_DefaultSourceRadius;
            if(!ParseF32FromChars(value, value + NWB_STRLEN(value), parsed))
                return s_DefaultSourceRadius;
            return Min(Max(parsed, 0.0f), 1.0f);
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
                StoreFloat(QuaternionRotationRollPitchYaw(0.0f, m_yaw.yaw(), 0.0f), &transform->rotation);
        }
    }


public:
    explicit SoftShadowTestSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
    {}

    virtual ~SoftShadowTestSmokeProject()override{
        m_context.input.removeHandler(m_arrowYawInput); // idempotent backstop if onShutdown was skipped (dispatcher outlives us)
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        // addHandlerToBack gives this scrubber first crack at the arrow keys; it consumes only Left/Right.
        m_context.input.addHandlerToBack(m_arrowYawInput);

        const NWB::Core::ECS::EntityID activeCamera = CreateSmokeCamera(*m_world, s_CameraHeight, s_CameraDistance, s_CameraPitch);

        // THREE differently-coloured soft-shadowed lights AT ONCE (not a chooser): a warm-white DIRECTIONAL sun (rakes its
        // shadow to one side), a RED POINT light on the opposite side (rakes the other way), and a BLUE SPOT overhead (a
        // short contact pool). Each casts its OWN soft shadow in its own tint, so the three penumbras are on screen together
        // and distinguishable by colour (overlaps blend). Each light's physical source size drives its softness: the
        // directional reads angularRadius (a constant sun-disk angle), point/spot read sourceRadius (the emissive sphere,
        // whose subtended angle asin(R/dist) softens more as the light nears the caster). Both env-tunable + A/B-controllable.
        const NWB::Core::ECS::EntityID directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DirectionalLightPitch,
            s_DirectionalLightYaw,
            0.0f,
            Float4(1.00f, 0.96f, 0.88f), // warm white sun
            s_DirectionalLightIntensity
        );
        if(auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(directionalLight))
            light->angularRadius = configuredAngularRadius();

        const NWB::Core::ECS::EntityID pointLight = NWB::Impl::Scene::CreatePointLightEntity(
            *m_world,
            Float4(1.5f, 2.2f, 0.3f, 0.0f),
            Float4(1.00f, 0.35f, 0.30f), // red
            s_PointLightIntensity,
            s_PointLightRange
        );
        if(auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(pointLight))
            light->sourceRadius = configuredSourceRadius();

        const NWB::Core::ECS::EntityID spotLight = NWB::Impl::Scene::CreateSpotLightEntity(
            *m_world,
            Float4(0.4f, 3.0f, 0.4f, 0.0f),
            s_SpotLightPitch,
            s_SpotLightYaw,
            0.0f,
            Float4(0.35f, 0.55f, 1.00f), // blue
            s_SpotLightIntensity,
            s_SpotLightRange,
            s_SpotInnerConeCos,
            s_SpotOuterConeCos
        );
        if(auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(spotLight))
            light->sourceRadius = configuredSourceRadius();

        m_groundEntity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_GroundMeshPath,
            s_OpaqueMaterialPath,
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
            s_ModelPath,
            s_OpaqueMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.86f, 0.80f, 0.74f, 1.0f),
            Float4(0.7f, 0.0f, -1.1f, 0.0f),
            Float4(1.0f, 1.0f, 1.0f, 0.0f),
            &tintApplied
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
            s_ModelPath,
            s_TransparentMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            // Glass tint is now (shadow colour . DENSITY): the RGB is the colour the shadow KEEPS, the A is the glass
            // DENSITY (how solid). nwbMakeGlassSurface (smoke_transparent.surface) seeds BOTH consumers together --
            // renderCoverage = density, shadowAbsorptionTint = lerp(white, rgb, density) -- so a denser glass is more
            // opaque on screen AND casts a darker, matching shadow. Because this body is a THIN SHELL, the engine floors
            // the shadow's Beer-Lambert chord to NWB_SHADOW_MIN_OCCLUDER_THICKNESS so the density/tint (not the tiny shell
            // thickness) governs the shadow: pick a DARKER / more-saturated rgb for a darker shadow, raise A toward 1 for a
            // more solid glass. Here a DEEP GREEN glass (keeps green, absorbs red+blue) casts a clearly green penumbra --
            // a bright rgb like (0.35,0.925,..) would still read faint because it barely absorbs. To decouple look from
            // shadow deliberately, override renderCoverage in the hook after the constructor.
            Float4(0.20f, 0.55f, 0.12f, 0.6f),
            Float4(-0.6f, 0.0f, -1.1f, 0.0f),
            Float4(1.0f, 1.0f, 1.0f, 0.0f),
            &glassTintApplied
        );
        if(!glassTintApplied)
            NWB_LOGGER_ERROR(NWB_TEXT("SoftShadowTestSmokeProject: failed to set glass tint"));

        SyncSmokeModelRuntimes(*m_world);

        NWB_FATAL_ASSERT_MSG(
            activeCamera.valid() && m_groundEntity.valid() && m_characterOwner.valid() && m_glassOwner.valid(),
            NWB_TEXT("SoftShadowTestSmokeProject failed to create all scene entities")
        );

        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("SoftShadowTestSmokeProject: opaque + glass characters on a ground plane, 3 coloured lights, angularRadius={} rad")
            , static_cast<f64>(configuredAngularRadius())
        );
        return true;
    }

    virtual void onShutdown()override{
        m_context.input.removeHandler(m_arrowYawInput);
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SoftShadowTestSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_fpsProbe.recordFrame(safeDelta);
        // Yaw selection: 1) NWB_SOFT_SHADOW_TEST_SPIN_ANGLE env freeze (pins one orientation); 2) manual arrow scrub
        // (latches off auto-spin the moment Left/Right is first pressed); 3) auto-spin.
        const f32 frozen = frozenYaw();
        m_yaw.update(safeDelta, frozen, frozen >= 0.0f, m_arrowYawInput, s_ManualYawSpeed, s_SpinSpeed, s_MaxSpinDelta);
        spinCasters();
        updateWindowTitle();
        m_world->tick(safeDelta);
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
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ NWB_TEXT("SoftShadowTestSmokeProject") };
    NWB::Tests::Smoke::YawSpinController m_yaw;
    ArrowYawInputHandler m_arrowYawInput;
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

