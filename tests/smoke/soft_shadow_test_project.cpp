// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <core/input/module.h> // InputDispatcher / IInputEventHandler / Key -- arrow-key manual yaw spin
#include <core/mesh/frame_math.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_render/model_renderer.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material_instance.h>
#include <impl/ecs_mesh/skinning/module.h>

#include "fps_probe.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"

#include <global/environment.h>   // ReadEnvironmentVariableBuffer -- NWB_SOFT_SHADOW_TEST_ANGLE / _SPIN_ANGLE
#include <global/simplemath.h>     // FMod -- wrapping the displayed yaw into [0, 2pi)
#include <global/text_utils.h>     // ParseF32FromChars -- env float parse


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_soft_shadow_test_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::AddSmokeSkinnedRenderSystems;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::CreateTintedModelEntity;
using NWB::Tests::Smoke::DestroySmokeSkinnedRenderWorld;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// DEDICATED SOFT-SHADOW scene (Stage 1 of the soft-ray-traced-shadow feature): ONE opaque `body` character standing on ONE
// opaque ground plane, lit by a SINGLE directional light with a PHYSICAL angular source size. That source size is what
// makes the shadow soft: the shadow trace jitters the ray inside a cone of the light's `angularRadius`, so the penumbra
// emerges + WIDENS with occluder->receiver distance and HARDENS at contact -- read it off the character's cast shadow
// (crisp at the feet, softening up the body). Deliberately directional-only (Stage 1 softens directional lights only) and
// opaque-only (no transparency / caustics) so the soft directional penumbra is the ONLY thing on screen.
//
// A/B levers:
//   - NWB_SOFT_SHADOW_TEST_ANGLE (radians, default 0.03 ~ 1.7deg): the light's angular radius. Sweep it -- ~0.001 is a
//     near-HARD reference (tight penumbra), ~0.05 is very soft. Shown live in the title bar (deg).
//   - The _sw_smoke build (NWB_SOFT_SHADOW_TEST_FORCE_RT_EMULATION) forces the SOFTWARE path, which runs the FULL soft
//     pipeline (half-res jittered trace -> a-trous denoise -> bilateral upsample). The HW build applies the same cone
//     jitter but at full res without the denoise (per-frame shimmer expected until the temporal stage).
//   - Arrow keys (Left/Right) scrub the character yaw so the sweeping soft edge can be checked for crawl (it should NOT
//     crawl -- a soft edge has nothing to alias); NWB_SOFT_SHADOW_TEST_SPIN_ANGLE pins a fixed yaw for a deterministic A/B.
// Reuses the benchmark's cooked body model + ground material (no new assets).
static constexpr AStringView s_ModelPath = "project/characters/body/model";
static constexpr AStringView s_OpaqueMaterialPath = "project/smoke/transparent_multi/materials/ground";  // opaque lambert
static constexpr AStringView s_GroundMeshPath = "project/meshes/shadow_plane";
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";

static constexpr f32 s_GroundScale = 8.0f;

static constexpr f32 s_CameraDistance = 2.4f;                        // pulled in close so the cast ground shadow reads large
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

static constexpr f32 s_SpinSpeed = 0.5f;                             // radians / second, gentle auto-spin until an arrow takes over
static constexpr f32 s_ManualYawSpeed = 0.6f;                        // radians / second for the arrow-key manual yaw scrub
static constexpr f32 s_TwoPi = 6.2831853f;
static constexpr f32 s_MaxSpinDelta = 1.0f / 15.0f;                  // clamp huge stalls so the spin can't jump


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Arrow-key manual yaw scrubber (Left/Right), so the sweeping soft-shadow edge can be parked on a precise orientation to
// check for crawl. Consumes only the two arrow keys so any other handler still sees everything else.
class ArrowYawInputHandler final : public NWB::Core::IInputEventHandler{
public:
    bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override{
        static_cast<void>(scancode);
        static_cast<void>(mods);
        const bool held = (action != NWB::Core::InputAction::Release); // Press or Repeat -> held
        switch(key){
        case NWB::Core::Key::Left:  m_leftHeld = held;  return true;
        case NWB::Core::Key::Right: m_rightHeld = held; return true;
        default:                    return false;
        }
    }

    // Signed scrub direction this frame: +1 Right, -1 Left, 0 idle. Both held cancels out.
    [[nodiscard]] f32 axis()const{ return (m_rightHeld ? 1.0f : 0.0f) - (m_leftHeld ? 1.0f : 0.0f); }


private:
    bool m_leftHeld = false;
    bool m_rightHeld = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SoftShadowTestSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.threadPool);
        if(!world){
            NWB_LOGGER_FATAL(NWB_TEXT("SoftShadowTestSmokeProject initialization failed: ECS world allocation failed"));
            throw RuntimeException("SoftShadowTestSmokeProject initialization failed");
        }
        if(!context.shaderPathResolver){
            NWB_LOGGER_FATAL(NWB_TEXT("SoftShadowTestSmokeProject initialization failed: shader path resolver callback is null"));
            throw RuntimeException("SoftShadowTestSmokeProject initialization failed");
        }

        // Force ray-tracing emulation so the SOFTWARE shadow path runs even on RT-capable hardware -- the software path is
        // the one that runs the FULL soft pipeline (half-res jittered trace -> a-trous denoise -> bilateral upsample), so
        // the _sw_smoke build is the intended way to see the denoised soft shadow. Default OFF: the HW (hybrid) path.
#if defined(NWB_SOFT_SHADOW_TEST_FORCE_RT_EMULATION) && !defined(NWB_FINAL)
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayTracingAccelStruct, true);
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayTracingPipeline, true);
        context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayQuery, true);
#endif

        AddSmokeSkinnedRenderSystems(*world, context);
        return MakeNotNullUnique(Move(world));
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

    // Diagnostic freeze (read once): NWB_SOFT_SHADOW_TEST_SPIN_ANGLE pins the yaw to a fixed radians value so the character
    // holds one orientation -- two captures then differ only via non-determinism (shimmer), not motion.
    static f32 frozenYaw(){
        static const f32 s_yaw = [](){
            char value[32] = {};
            if(!ReadEnvironmentVariableBuffer("NWB_SOFT_SHADOW_TEST_SPIN_ANGLE", value, sizeof(value)))
                return -1.0f;
            f32 parsed = -1.0f;
            return ParseF32FromChars(value, value + NWB_STRLEN(value), parsed) ? parsed : -1.0f;
        }();
        return s_yaw;
    }

    void spinCharacter(){
        auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(m_characterOwner);
        if(!transform)
            return;
        StoreFloat(QuaternionRotationRollPitchYaw(0.0f, m_yaw, 0.0f), &transform->rotation);
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

        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(
            *m_world,
            Float4(0.0f, s_CameraHeight, -s_CameraDistance, 0.0f)
        );
        // Tilt the camera down slightly so the character + her cast ground shadow are framed.
        if(auto* cameraTransform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(activeCamera.camera))
            StoreFloat(QuaternionRotationRollPitchYaw(s_CameraPitch, 0.0f, 0.0f), &cameraTransform->rotation);

        const NWB::Core::ECS::EntityID directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DirectionalLightPitch,
            s_DirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DirectionalLightIntensity
        );
        // The physical source size that makes the shadow soft. Overwrite the light's default angular radius with the
        // configured (env-tunable) value so the penumbra width is A/B-controllable.
        if(auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(directionalLight))
            light->angularRadius = configuredAngularRadius();

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

        // ONE opaque caster standing at the origin (feet on the plane): its cast shadow is CRISP at the feet (contact) and
        // softens up the body -- the physical soft-shadow signature, cleanly isolated.
        bool tintApplied = false;
        m_characterOwner = CreateTintedModelEntity(
            *m_world,
            m_context.objectArena,
            s_ModelPath,
            s_OpaqueMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.86f, 0.80f, 0.74f, 1.0f),
            Float4(0.0f, 0.0f, 0.0f, 0.0f),
            Float4(1.0f, 1.0f, 1.0f, 0.0f),
            &tintApplied
        );
        if(!tintApplied)
            NWB_LOGGER_ERROR(NWB_TEXT("SoftShadowTestSmokeProject: failed to set character tint"));

        // One tick spawns the model object entities (skeleton/mesh) so the first rendered frame is fully populated.
        m_world->tick(0.0f);

        NWB_FATAL_ASSERT_MSG(
            activeCamera.camera.valid() && m_groundEntity.valid() && m_characterOwner.valid(),
            NWB_TEXT("SoftShadowTestSmokeProject failed to create all scene entities")
        );

        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("SoftShadowTestSmokeProject: one opaque character on a ground plane, single directional light, angularRadius={} rad")
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
        if(frozen >= 0.0f){
            m_yaw = frozen;
        }
        else{
            const f32 manualAxis = m_arrowYawInput.axis();
            if(manualAxis != 0.0f)
                m_manualYawControl = true; // latch: first arrow input takes over from auto-spin
            if(m_manualYawControl)
                m_yaw += manualAxis * s_ManualYawSpeed * safeDelta;
            else
                m_yaw += Min(safeDelta, s_MaxSpinDelta) * s_SpinSpeed;
        }
        spinCharacter();
        updateWindowTitle();
        m_world->tick(safeDelta);
        return true;
    }

    // Reflect the current yaw (wrapped to [0, 2pi)) + the configured angular radius (deg) in the title bar, so the exact
    // orientation + the soft-shadow angle in effect can be read off at a glance.
    void updateWindowTitle(){
        f32 wrapped = FMod(m_yaw, s_TwoPi);
        if(wrapped < 0.0f)
            wrapped += s_TwoPi;
        const f32 yawDegrees = wrapped * (360.0f / s_TwoPi);
        const f32 angleDegrees = configuredAngularRadius() * (360.0f / s_TwoPi);

        static constexpr usize s_TitleCapacity = 224u;
        tchar title[s_TitleCapacity];
        NWB_TSPRINTF(
            title,
            s_TitleCapacity,
            NWB_TEXT("%s  |  yaw %.2f deg  |  source %.2f deg%s"),
            NWB::QueryProjectWindowTitle(),
            yawDegrees,
            angleDegrees,
            m_manualYawControl ? NWB_TEXT("  [manual: <- ->]") : NWB_TEXT("")
        );
        const tchar* titlePtr = title;
        m_context.graphics.setWindowTitle(MakeNotNull(titlePtr));
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Core::ECS::EntityID m_characterOwner = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_groundEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ NWB_TEXT("SoftShadowTestSmokeProject") };
    f32 m_yaw = 0.0f;                  // accumulated character yaw (radians): env-freeze / arrow-scrub / auto-spin
    ArrowYawInputHandler m_arrowYawInput;
    bool m_manualYawControl = false;   // latched true once an arrow key first drives the yaw
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
