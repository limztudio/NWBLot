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
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"

#include <global/environment.h>   // ReadEnvironmentVariableBuffer -- NWB_FLICKER_TEST_SPIN_ANGLE freeze for deterministic A/B
#include <global/simplemath.h>     // FMod -- wrapping the displayed yaw into [0, 2pi)
#include <global/text_utils.h>     // ParseF32FromChars -- env float diagnostics


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_flicker_test_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::AddSmokeSkinnedRenderSystems;
using NWB::Tests::Smoke::ArrowYawInputHandler;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::CreateTintedModelEntity;
using NWB::Tests::Smoke::DestroySmokeSkinnedRenderWorld;
using NWB::Tests::Smoke::SyncSmokeModelRuntimes;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// MINIMAL FLICKER-REPRO scene: ONE opaque `body` character on ONE opaque ground plane, lit by a SINGLE directional light.
// The character spins about its vertical axis (root-transform rotation only, bind pose) so its ground shadow + self-shadow
// sweep -- the tightest possible isolation of the hard-shadow edge crawl (no transparency, no point light, no crowd). Arrow
// keys (Left/Right) scrub the yaw by hand; the live angle shows in the title bar so the exact orientation a flicker appears
// at can be read off and reproduced via NWB_FLICKER_TEST_SPIN_ANGLE. Reuses the benchmark's cooked body model + ground
// material (no new assets).
static constexpr AStringView s_ModelPath = "project/characters/body/model";
static constexpr AStringView s_OpaqueMaterialPath = "project/smoke/transparent_multi/materials/ground";  // opaque lambert
static constexpr AStringView s_TransparentMaterialPath = "project/smoke/transparent_multi/materials/shared"; // glass
static constexpr AStringView s_GroundMeshPath = "project/meshes/shadow_plane";
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";

static constexpr f32 s_GroundScale = 8.0f;
static constexpr f32 s_CharacterSpacingX = 0.22f;                    // small lateral offset so both read; they stand close
static constexpr f32 s_TransparentFrontZ = -0.32f;                  // glass female IN FRONT (toward the camera)
static constexpr f32 s_OpaqueBackZ = 0.32f;                         // opaque female behind, so the glass shadow overlaps it

static constexpr f32 s_CameraDistance = 3.8f;
static constexpr f32 s_CameraHeight = 1.6f;
static constexpr f32 s_CameraPitch = 0.22f;

static constexpr f32 s_DirectionalLightPitch = 0.9f;
static constexpr f32 s_DirectionalLightYaw = 0.65f;
static constexpr f32 s_DirectionalLightIntensity = 2.0f;

// Second shadowed light: a point light (the flicker appears specifically on the POINT-light shadow, whose ray direction
// varies per pixel + has a finite tMax, unlike the directional light's constant direction / infinite tMax).
static constexpr f32 s_PointLightHeight = 2.6f;
static constexpr f32 s_PointLightIntensity = 9.0f;                   // point lights attenuate by distance, so brighter
static constexpr f32 s_PointLightRange = 16.0f;

static constexpr f32 s_SpinSpeed = 0.5f;                             // radians / second, gentle auto-spin until an arrow takes over
static constexpr f32 s_ManualYawSpeed = 0.6f;                        // radians / second for the arrow-key manual yaw scrub
static constexpr f32 s_TwoPi = 6.2831853f;
static constexpr f32 s_MaxSpinDelta = 1.0f / 15.0f;                  // clamp huge stalls so the spin can't jump


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FlickerTestSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.threadPool);
        if(!world){
            NWB_LOGGER_FATAL(NWB_TEXT("FlickerTestSmokeProject initialization failed: ECS world allocation failed"));
            throw RuntimeException("FlickerTestSmokeProject initialization failed");
        }
        if(!context.shaderPathResolver){
            NWB_LOGGER_FATAL(NWB_TEXT("FlickerTestSmokeProject initialization failed: shader path resolver callback is null"));
            throw RuntimeException("FlickerTestSmokeProject initialization failed");
        }

        // Force ray-tracing emulation so the SOFTWARE shadow path runs even on RT-capable hardware -- the A/B sibling of
        // the hardware path. Default OFF: the demo runs the hardware (hybrid) path.
#if defined(NWB_FLICKER_TEST_FORCE_RT_EMULATION) && !defined(NWB_FINAL)
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

    // Diagnostic freeze (read once): NWB_FLICKER_TEST_SPIN_ANGLE pins the yaw to a fixed radians value so the character
    // holds one orientation -- two captures then differ only via non-determinism (a flicker/race), not motion.
    static f32 frozenYaw(){
        static const f32 s_yaw = [](){
            char value[32] = {};
            if(!ReadEnvironmentVariableBuffer("NWB_FLICKER_TEST_SPIN_ANGLE", value, sizeof(value)))
                return -1.0f;
            f32 parsed = -1.0f;
            return ParseF32FromChars(value, value + NWB_STRLEN(value), parsed) ? parsed : -1.0f;
        }();
        return s_yaw;
    }

    void spinCharacters(){
        for(const NWB::Core::ECS::EntityID owner : { m_opaqueOwner, m_transparentOwner }){
            auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(owner);
            if(!transform)
                continue;
            StoreFloat(QuaternionRotationRollPitchYaw(0.0f, m_yaw, 0.0f), &transform->rotation);
        }
    }


public:
    explicit FlickerTestSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
    {}

    virtual ~FlickerTestSmokeProject()override{
        m_context.input.removeHandler(m_arrowYawInput); // idempotent backstop if onShutdown was skipped (dispatcher outlives us)
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        // Arrow keys (Left/Right) scrub the character yaw by hand; the live angle shows in the title bar so the exact angle a
        // flicker appears at can be read off and reproduced via NWB_FLICKER_TEST_SPIN_ANGLE. addHandlerToBack gives this
        // scrubber first crack at the arrow keys; it consumes only Left/Right.
        m_context.input.addHandlerToBack(m_arrowYawInput);

        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(
            *m_world,
            Float4(0.0f, s_CameraHeight, -s_CameraDistance, 0.0f)
        );
        // Tilt the camera down slightly so the character + her ground shadow are framed.
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
        const NWB::Core::ECS::EntityID pointLight = NWB::Impl::Scene::CreatePointLightEntity(
            *m_world,
            Float4(0.8f, s_PointLightHeight, -0.6f, 0.0f),
            Float4(0.60f, 0.76f, 1.00f),
            s_PointLightIntensity,
            s_PointLightRange
        );
        // Caustics are the CONFIRMED source of the spinning-glass flicker. Kept ON by default (so the flicker reproduces
        // and a fix can be A/B'd); NWB_FLICKER_TEST_NO_CAUSTICS=1 opts both lights out (LightComponent::enableCaustics =
        // false) -> the refractive glass female casts NO caustic irradiance (black-cleared additive no-op), the flicker-
        // free reference.
        static const bool s_disableCaustics = [](){
            char value[8] = {};
            return ReadEnvironmentVariableBuffer("NWB_FLICKER_TEST_NO_CAUSTICS", value, sizeof(value)) && value[0] == '1';
        }();
        if(s_disableCaustics){
            for(const NWB::Core::ECS::EntityID lightEntity : { directionalLight, pointLight }){
                if(auto* light = m_world->tryGetComponent<NWB::Impl::Scene::LightComponent>(lightEntity))
                    light->enableCaustics = false;
            }
        }

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

        // Opaque character (left) + transparent glass character (right), side by side so their shadows overlap as they spin.
        bool opaqueTintApplied = false;
        m_opaqueOwner = CreateTintedModelEntity(
            *m_world,
            m_context.objectArena,
            s_ModelPath,
            s_OpaqueMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.86f, 0.80f, 0.74f, 1.0f),
            Float4(-s_CharacterSpacingX, 0.0f, s_OpaqueBackZ, 0.0f),
            Float4(1.0f, 1.0f, 1.0f, 0.0f),
            &opaqueTintApplied
        );
        bool transparentTintApplied = false;
        m_transparentOwner = CreateTintedModelEntity(
            *m_world,
            m_context.objectArena,
            s_ModelPath,
            s_TransparentMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.72f, 0.86f, 1.00f, 0.42f), // glass tint, sub-1 alpha
            Float4(s_CharacterSpacingX, 0.0f, s_TransparentFrontZ, 0.0f),
            Float4(1.0f, 1.0f, 1.0f, 0.0f),
            &transparentTintApplied
        );
        if(!opaqueTintApplied || !transparentTintApplied)
            NWB_LOGGER_ERROR(NWB_TEXT("FlickerTestSmokeProject: failed to set character tint (opaque {}, transparent {})"), opaqueTintApplied, transparentTintApplied);

        SyncSmokeModelRuntimes(*m_world);

        NWB_FATAL_ASSERT_MSG(
            activeCamera.camera.valid() && m_groundEntity.valid() && m_opaqueOwner.valid() && m_transparentOwner.valid(),
            NWB_TEXT("FlickerTestSmokeProject failed to create all scene entities")
        );

        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("FlickerTestSmokeProject: spawned one opaque + one transparent character over an opaque ground plane, directional + point light")
        );
        return true;
    }

    virtual void onShutdown()override{
        m_context.input.removeHandler(m_arrowYawInput);
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("FlickerTestSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_fpsProbe.recordFrame(safeDelta);
        // Yaw selection: 1) NWB_FLICKER_TEST_SPIN_ANGLE env freeze (pins one orientation); 2) manual arrow scrub (latches off
        // auto-spin the moment Left/Right is first pressed, so the character can be parked on a precise angle); 3) auto-spin.
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
        spinCharacters();
        updateWindowTitleYaw();
        m_world->tick(safeDelta);
        return true;
    }

    // Reflect the current character yaw in the title bar (wrapped to [0, 2pi)) so the exact orientation a flicker appears at
    // can be read off and reproduced via NWB_FLICKER_TEST_SPIN_ANGLE.
    void updateWindowTitleYaw(){
        f32 wrapped = FMod(m_yaw, s_TwoPi);
        if(wrapped < 0.0f)
            wrapped += s_TwoPi;
        const f32 degrees = wrapped * (360.0f / s_TwoPi);

        static constexpr usize s_TitleCapacity = 192u;
        tchar title[s_TitleCapacity];
        NWB_TSPRINTF(
            title,
            s_TitleCapacity,
            NWB_TEXT("%s  |  yaw %.4f rad (%.2f deg)%s"),
            NWB::QueryProjectWindowTitle(),
            wrapped,
            degrees,
            m_manualYawControl ? NWB_TEXT("  [manual: <- ->]") : NWB_TEXT("")
        );
        const tchar* titlePtr = title;
        m_context.graphics.setWindowTitle(MakeNotNull(titlePtr));
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Core::ECS::EntityID m_opaqueOwner = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_transparentOwner = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_groundEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ NWB_TEXT("FlickerTestSmokeProject") };
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
    return NWB_TEXT("NWB Flicker Test");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_flicker_test_smoke::FlickerTestSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

