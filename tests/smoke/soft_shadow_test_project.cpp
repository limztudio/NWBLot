// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
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
using NWB::Tests::Smoke::ReadSmokeFrozenYawFromEnvironment;
using NWB::Tests::Smoke::SyncSmokeModelRuntimes;

using SoftShadowModelRef = NWB::Core::Assets::AssetRef<NWB::Impl::Model>;
using SoftShadowMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;
using SoftShadowMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// DEDICATED SOFT-SHADOW scene (soft-ray-traced-shadow feature): an OPAQUE and a GLASS `body` character standing SIDE BY
// SIDE on ONE opaque ground plane, lit by THREE differently-coloured lights AT ONCE -- a warm-white DIRECTIONAL sun, a RED
// POINT light, and a BLUE SPOT -- each with a PHYSICAL source size. That source size is what makes each shadow soft: the
// trace jitters the ray over the light's source (the sun's angularRadius disk, or a point/spot's sourceRadius sphere
// subtending asin(R/dist)), so every penumbra emerges + WIDENS with occluder->receiver distance and HARDENS at contact. The
// GLASS caster casts a COLORED transparent soft shadow beside the opaque grey one. Both spin (arrow keys).
//
// A/B levers:
//   - THREE differently-coloured soft-shadowed lights are lit AT ONCE (a warm-white directional sun, a RED point, a BLUE
//     spot), so all three soft penumbras are on screen together, distinguishable by tint (overlaps blend the colours).
//   - NWB_SOFT_SHADOW_TEST_ANGLE (radians, default 0.03 ~ 1.7deg): the DIRECTIONAL sun's angular radius. Sweep it --
//     ~0.001 is a near-HARD reference (tight penumbra), ~0.05 is very soft. Shown live in the title bar (deg).
//   - NWB_SOFT_SHADOW_TEST_SOURCE_RADIUS (world units, default 0.15): the POINT + SPOT emissive sphere radius. Larger =
//     softer; the penumbra ALSO widens as the light nears the caster (asin(radius/dist)) -- physical distance softening.
//   - A device without RayQuery-capable hardware naturally selects the SOFTWARE path, which runs the full soft pipeline
//     (half-res jittered trace -> a-trous denoise -> bilateral upsample). The hardware path applies the same cone jitter
//     at full resolution without the denoise (per-frame shimmer expected until the temporal stage).
//   - Arrow keys (Left/Right) scrub the character yaw so the sweeping soft edge can be checked for crawl (it should NOT
//     crawl -- a soft edge has nothing to alias); NWB_SOFT_SHADOW_TEST_SPIN_ANGLE pins a fixed yaw for a deterministic A/B.
// Reuses the benchmark's cooked body model + ground material (no new assets).
static constexpr SoftShadowModelRef s_Model = []() constexpr{
    SoftShadowModelRef result;
    result.virtualPath = Name("project/characters/body/model");
    return result;
}();
static constexpr SoftShadowMaterialRef s_OpaqueMaterial = []() constexpr{
    SoftShadowMaterialRef result;
    result.virtualPath = Name("project/smoke/transparent_multi/materials/ground");
    return result;
}();
static constexpr SoftShadowMaterialRef s_TransparentMaterial = []() constexpr{
    SoftShadowMaterialRef result;
    result.virtualPath = Name("project/smoke/transparent_multi/materials/shared");
    return result;
}();
static constexpr SoftShadowMeshRef s_GroundMesh = []() constexpr{
    SoftShadowMeshRef result;
    result.virtualPath = Name("project/meshes/shadow_plane");
    return result;
}();
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";

static constexpr f32 s_GroundScale = 8.0f;

static constexpr f32 s_CameraDistance = 3.2f;
static constexpr f32 s_CameraHeight = 1.5f;
static constexpr f32 s_CameraPitch = 0.30f;

// Directional light aimed to cast the shadow SIDEWAYS across the plane (large yaw), NOT behind the character where her
// own body would hide it -- the shadow rakes out to one side, in full view, and its penumbra widens along its length
// (crisp at the feet, soft at the far end). A moderate pitch keeps the plane well-lit (high shadow contrast) while still
// giving a long shadow. Warm sun tint. Intensity is clamped to 2.0 by the shading, so 2.0 is the useful max.
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
        static const u32 s_captureFrame = [](){
            f32 configuredFrame = 0.0f;
            if(
                !ReadSmokeEnvironmentF32("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", configuredFrame)
                || !IsFinite(configuredFrame)
                || configuredFrame < 1.0f
            ){
                return 0u;
            }
            return static_cast<u32>(Min(configuredFrame, 1000000.0f));
        }();
        return s_captureFrame;
    }

    [[nodiscard]] static f32 rendererBaselineFixedDelta(){
        static const f32 s_fixedDelta = [](){
            f32 configuredDelta = 0.0f;
            if(
                !ReadSmokeEnvironmentF32("NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", configuredDelta)
                || !IsFinite(configuredDelta)
                || configuredDelta <= 0.0f
                || configuredDelta > 1.0f
            ){
                return 0.0f;
            }
            return configuredDelta;
        }();
        return s_fixedDelta;
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
            return Min(Max(parsed, 0.0f), 0.2f);
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
            Float4(1.00f, 0.96f, 0.88f),
            s_DirectionalLightIntensity
        );
        if(auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(directionalLight))
            light->angularRadius = configuredAngularRadius();

        const NWB::Core::ECS::EntityID pointLight = NWB::Impl::Scene::CreatePointLightEntity(
            *m_world,
            Float4(1.5f, 2.2f, 0.3f, 0.0f),
            Float4(1.00f, 0.35f, 0.30f),
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
            Float4(0.35f, 0.55f, 1.00f),
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
            s_GroundMesh,
            s_OpaqueMaterial,
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
            s_OpaqueMaterial,
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
            s_Model,
            s_TransparentMaterial,
            s_SmokeSurfaceMaterialInterface,
            // Glass tint is (shadow colour . DENSITY): the RGB is the colour the shadow KEEPS, the A is the glass
            // DENSITY (how solid). nwbMakeGlassSurface (smoke_transparent.surface) seeds BOTH consumers together --
            // renderCoverage = density, shadowAbsorptionTint = lerp(white, rgb, density) -- so a denser glass is more
            // opaque on screen AND casts a darker, matching shadow. Beer-Lambert uses the mesh's actual entry->exit chord,
            // so this thin shell naturally casts a lighter shadow than a thick volume with the same material: choose mesh
            // thickness and tint/density together. Here a DEEP GREEN glass (keeps green, absorbs red+blue) casts a green
            // penumbra -- a bright rgb like (0.35,0.925,..) would still read faint because it barely absorbs. To decouple
            // look from shadow deliberately, override renderCoverage in the hook after the constructor.
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
        m_context.graphics.setFrameSubmissionSuspended(false);
        m_context.input.removeHandler(m_arrowYawInput);
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("SoftShadowTestSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const u32 captureFreezeFrame = rendererBaselineCaptureFreezeFrame();
        if(captureFreezeFrame != 0u && m_rendererBaselineRenderedFrameCount >= captureFreezeFrame){
            if(!m_rendererBaselineCapturePaused){
                // Soft-shadow history must stop on a known accepted frame. This test-only control freezes the
                // smoke loop after the requested history phase without adding a runtime renderer override.
                m_context.graphics.setFrameSubmissionSuspended(true);
                m_rendererBaselineCapturePaused = true;
                NWB_LOGGER_ESSENTIAL_INFO(
                    NWB_TEXT("SoftShadowTestSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"),
                    m_rendererBaselineRenderedFrameCount
                );
            }
            return true;
        }

        const f32 fixedDelta = rendererBaselineFixedDelta();
        const f32 safeDelta = fixedDelta > 0.0f ? fixedDelta : (IsFinite(delta) ? Max(delta, 0.0f) : 0.0f);
        m_fpsProbe.recordFrame(safeDelta);
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
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ NWB_TEXT("SoftShadowTestSmokeProject") };
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

