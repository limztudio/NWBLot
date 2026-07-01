// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <core/input/module.h> // InputDispatcher / IInputEventHandler / Key -- arrow-key manual yaw scrub
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
#include "gpu_pass_timing_probe.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"

#include <global/environment.h>   // ReadEnvironmentVariableBuffer -- NWB_STRESS_TEST_SPIN_ANGLE freeze for deterministic A/B
#include <global/simplemath.h>     // FMod -- wrapping the displayed yaw into [0, 2pi)
#include <global/text_utils.h>     // ParseF32FromChars -- env float diagnostics


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_stress_test_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::AddSmokeSkinnedRenderSystems;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::CreateTintedModelEntity;
using NWB::Tests::Smoke::DestroySmokeSkinnedRenderWorld;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// STRESS scene: an empty ground plane with TEN skinned `body` characters in a tight ZIGZAG -- alternating TRANSPARENT
// (glass) and OPAQUE, staggered front/back so their shadows overlap -- lit by one directional + one point light. Each
// character SPINS about its vertical axis
// (no skeleton-pose animation; the bodies render in bind pose and the whole entity rotates), so its instance transform
// changes every frame -- exercising the per-frame scene BVH/TLAS rebuild + the hybrid shadow path (opaque->HW binary,
// transparent->SW colored) across TWO shadowed lights as the occluders sweep. Reuses the body model + transparent_multi
// glass/ground materials (no new assets).
static constexpr AStringView s_ModelPath = "project/characters/body/model";
static constexpr AStringView s_TransparentMaterialPath = "project/smoke/transparent_multi/materials/shared"; // glass
static constexpr AStringView s_OpaqueMaterialPath = "project/smoke/transparent_multi/materials/ground";      // opaque lambert
static constexpr AStringView s_GroundMaterialPath = "project/smoke/transparent_multi/materials/ground";
static constexpr AStringView s_GroundMeshPath = "project/meshes/shadow_plane";
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";

static constexpr u32 s_CharactersPerClass = 5u;                       // 5 transparent + 5 opaque
static constexpr u32 s_CharacterCount = s_CharactersPerClass * 2u;    // = 10
static constexpr f32 s_CharacterSpacingX = 0.72f;                     // tight so neighbours' shadows overlap
static constexpr f32 s_TransparentRowZ = -0.55f;                      // even index -> front of the zigzag
static constexpr f32 s_OpaqueRowZ = 0.55f;                            // odd index  -> back of the zigzag
static constexpr f32 s_CharacterLift = 0.0f;
static constexpr f32 s_GroundScale = 8.0f;

static constexpr f32 s_CameraDistance = 4.8f;
static constexpr f32 s_CameraHeight = 1.8f;
static constexpr f32 s_CameraPitch = 0.2f;                           // tilt down a touch more to read the ground shadows

static constexpr f32 s_DirectionalLightPitch = 0.9f;
static constexpr f32 s_DirectionalLightYaw = 0.65f;
static constexpr f32 s_DirectionalLightIntensity = 2.0f;

static constexpr f32 s_PointLightHeight = 2.6f;
static constexpr f32 s_PointLightIntensity = 9.0f;                   // point lights attenuate by distance, so brighter
static constexpr f32 s_PointLightRange = 16.0f;

static constexpr f32 s_SpinSpeed = 0.8f;                             // radians / second
static constexpr f32 s_ManualYawSpeed = 0.6f;                        // radians / second for the arrow-key manual yaw scrub
static constexpr f32 s_TwoPi = 6.2831853f;
static constexpr f32 s_MaxSpinDelta = 1.0f / 15.0f;                  // clamp huge stalls so the spin can't jump


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Distinct per-character tint so the ten bodies read apart. Transparent rows carry a sub-1 alpha (glass), opaque rows
// stay fully opaque.
[[nodiscard]] static Float4 CharacterTint(const u32 classIndex, const bool transparent){
    static const Float4 s_transparentTints[s_CharactersPerClass] = {
        Float4(0.72f, 0.86f, 1.00f, 0.42f),
        Float4(0.60f, 1.00f, 0.85f, 0.42f),
        Float4(1.00f, 0.82f, 0.88f, 0.44f),
        Float4(0.86f, 0.80f, 1.00f, 0.45f),
        Float4(0.70f, 0.96f, 1.00f, 0.40f),
    };
    static const Float4 s_opaqueTints[s_CharactersPerClass] = {
        Float4(0.90f, 0.52f, 0.42f, 1.0f),
        Float4(0.52f, 0.70f, 0.90f, 1.0f),
        Float4(0.62f, 0.82f, 0.52f, 1.0f),
        Float4(0.88f, 0.78f, 0.50f, 1.0f),
        Float4(0.74f, 0.62f, 0.84f, 1.0f),
    };
    const u32 slot = classIndex % s_CharactersPerClass;
    return transparent ? s_transparentTints[slot] : s_opaqueTints[slot];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Arrow-key manual yaw scrubber (mirrors transparent_multi): registered with the InputDispatcher so the crowd rotation can
// be stepped by hand (Left/Right) to the exact orientation a flicker appears at and read off the title bar. Consumes only
// the two arrow keys so any other handler still sees everything else.
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


class StressTestSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.threadPool);
        if(!world){
            NWB_LOGGER_FATAL(NWB_TEXT("StressTestSmokeProject initialization failed: ECS world allocation failed"));
            throw RuntimeException("StressTestSmokeProject initialization failed");
        }
        if(!context.shaderPathResolver){
            NWB_LOGGER_FATAL(NWB_TEXT("StressTestSmokeProject initialization failed: shader path resolver callback is null"));
            throw RuntimeException("StressTestSmokeProject initialization failed");
        }

        // Force ray-tracing emulation so the SOFTWARE shadow path runs even on RT-capable hardware -- the A/B sibling of
        // the hardware path. Default OFF: the demo runs the hardware (hybrid) path.
#if defined(NWB_STRESS_TEST_FORCE_RT_EMULATION) && !defined(NWB_FINAL)
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

    [[nodiscard]] NWB::Core::ECS::EntityID createCharacter(const u32 index){
        // Zigzag: alternate transparent / opaque along one tight line, staggering even indices to the front row and odd
        // to the back. Neighbours are a transparent and an opaque character offset diagonally, so their (directional +
        // point) shadows overlap on the ground -- the colored transparent shadow folds onto the hard opaque shadow and
        // adjacent characters' shadows pile up, making the shadow duplication / combine easy to observe.
        const bool transparent = (index % 2u) == 0u;
        const u32 classIndex = index / 2u; // 0..4 within each material class (tint palette)
        const f32 x = (static_cast<f32>(index) - static_cast<f32>(s_CharacterCount - 1u) * 0.5f) * s_CharacterSpacingX;
        const f32 z = transparent ? s_TransparentRowZ : s_OpaqueRowZ;

        bool tintApplied = false;
        const NWB::Core::ECS::EntityID entity = CreateTintedModelEntity(
            *m_world,
            m_context.objectArena,
            s_ModelPath,
            transparent ? s_TransparentMaterialPath : s_OpaqueMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            CharacterTint(classIndex, transparent),
            Float4(x, s_CharacterLift, z, 0.0f),
            Float4(1.0f, 1.0f, 1.0f, 0.0f),
            &tintApplied
        );
        if(!tintApplied)
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: failed to set character tint (index {})"), index);

        return entity;
    }

    // Spin every character about its vertical (Y) axis around its fixed position; a per-character phase staggers the
    // start angles so the crowd isn't in lockstep. Only the root transform's rotation changes -- the skinned bodies
    // stay in bind pose -- so each frame the instance/scene BVH + the two lights' shadows re-resolve as they turn.
    // Diagnostic freeze (read once): NWB_STRESS_TEST_SPIN_ANGLE pins yawBase to a fixed radians value so the skinned
    // crowd holds one orientation -- two captures then differ only via non-determinism (a flicker/race), not motion.
    static f32 frozenYaw(){
        static const f32 s_yaw = [](){
            char value[32] = {};
            if(!ReadEnvironmentVariableBuffer("NWB_STRESS_TEST_SPIN_ANGLE", value, sizeof(value)))
                return -1.0f;
            f32 parsed = -1.0f;
            return ParseF32FromChars(value, value + NWB_STRLEN(value), parsed) ? parsed : -1.0f;
        }();
        return s_yaw;
    }

    void spinCharacters(){
        const f32 yawBase = m_yaw;
        for(usize index = 0u; index < m_characterOwners.size(); ++index){
            auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(m_characterOwners[index]);
            if(!transform)
                continue;

            const f32 phase = static_cast<f32>(index) * (s_TwoPi / static_cast<f32>(s_CharacterCount));
            StoreFloat(QuaternionRotationRollPitchYaw(0.0f, yawBase + phase, 0.0f), &transform->rotation);
        }
    }


public:
    explicit StressTestSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
        , m_characterOwners(context.objectArena)
    {}

    virtual ~StressTestSmokeProject()override{
        m_context.input.removeHandler(m_arrowYawInput); // idempotent backstop if onShutdown was skipped (dispatcher outlives us)
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        // Per-pass GPU timing so the FPS / GPU-pass probes report the skinning + shadow costs each interval.
        m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());

        // Arrow keys (Left/Right) scrub the crowd yaw by hand; the live angle shows in the title bar so the exact angle a
        // flicker appears at can be read off and reproduced via NWB_STRESS_TEST_SPIN_ANGLE. addHandlerToBack gives this
        // scrubber first crack at the arrow keys; it consumes only Left/Right.
        m_context.input.addHandlerToBack(m_arrowYawInput);

        auto activeCameraEntity = m_world->createEntity();
        auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
        activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(
            *m_world,
            Float4(0.0f, s_CameraHeight, -s_CameraDistance, 0.0f)
        );
        // Tilt the camera down slightly so the whole 2x5 crowd + their ground shadows are framed.
        if(auto* cameraTransform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(activeCamera.camera))
            StoreFloat(QuaternionRotationRollPitchYaw(s_CameraPitch, 0.0f, 0.0f), &cameraTransform->rotation);

        NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            s_DirectionalLightPitch,
            s_DirectionalLightYaw,
            0.0f,
            Float4(1.0f, 0.96f, 0.88f),
            s_DirectionalLightIntensity
        );
        NWB::Impl::Scene::CreatePointLightEntity(
            *m_world,
            Float4(0.8f, s_PointLightHeight, -0.6f, 0.0f),
            Float4(0.60f, 0.76f, 1.00f),
            s_PointLightIntensity,
            s_PointLightRange
        );

        m_groundEntity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_GroundMeshPath,
            s_GroundMaterialPath,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.82f, 0.82f, 0.85f, 1.0f),
            Float4(0.0f, 0.0f, 0.0f, 0.0f),
            Float4(s_GroundScale, 1.0f, s_GroundScale, 0.0f)
        );

        m_characterOwners.reserve(s_CharacterCount);
        for(u32 index = 0u; index < s_CharacterCount; ++index)
            m_characterOwners.push_back(createCharacter(index));

        // One tick spawns the model object entities (skeleton/mesh) so the first rendered frame is fully populated.
        m_world->tick(0.0f);

        bool allCharactersValid = m_characterOwners.size() == s_CharacterCount;
        for(const NWB::Core::ECS::EntityID owner : m_characterOwners)
            allCharactersValid = allCharactersValid && owner.valid();

        NWB_FATAL_ASSERT_MSG(
            activeCamera.camera.valid() && m_groundEntity.valid() && allCharactersValid,
            NWB_TEXT("StressTestSmokeProject failed to create all scene entities")
        );

        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("StressTestSmokeProject: spawned {} spinning characters ({} transparent + {} opaque) over ground, directional + point light")
            , s_CharacterCount
            , s_CharactersPerClass
            , s_CharactersPerClass
        );
        return true;
    }

    virtual void onShutdown()override{
        m_context.input.removeHandler(m_arrowYawInput);
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
        m_fpsProbe.recordFrame(safeDelta);
        m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
        // Yaw selection: 1) NWB_STRESS_TEST_SPIN_ANGLE env freeze (pins one orientation); 2) manual arrow scrub (latches off
        // auto-spin the moment Left/Right is first pressed, so the crowd can be parked on a precise angle); 3) auto-spin.
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

    // Reflect the current crowd yaw in the title bar (wrapped to [0, 2pi)) so the exact orientation a flicker appears at can
    // be read off and reproduced via NWB_STRESS_TEST_SPIN_ANGLE.
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
    Vector<NWB::Core::ECS::EntityID, NWB::Core::Alloc::GlobalArena> m_characterOwners;
    NWB::Core::ECS::EntityID m_groundEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ NWB_TEXT("StressTestSmokeProject") };
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ NWB_TEXT("StressTestSmokeProject") };
    f32 m_yaw = 0.0f;                  // accumulated crowd yaw (radians): env-freeze / arrow-scrub / auto-spin
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
    return NWB_TEXT("NWB Stress Test Smoke");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_stress_test_smoke::StressTestSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
