// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/module.h>
#include <global/math/frame.h>
#include <global/math/constant.h>
#include <global/math/quaternion.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_mesh/skinning/module.h>

#include "arrow_yaw_input_handler.h"
#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_stress_test_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::AddSmokeSkinnedRenderSystems;
using NWB::Tests::Smoke::ArrowYawInputHandler;
using NWB::Tests::Smoke::CreateSmokeCamera;
using NWB::Tests::Smoke::CreateSmokeWorldOrDie;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::CreateTintedModelEntity;
using NWB::Tests::Smoke::DestroySmokeSkinnedRenderWorld;
using NWB::Tests::Smoke::ReadSmokeEnvironmentF32;
using NWB::Tests::Smoke::ReadSmokeEnvironmentFlag;
using NWB::Tests::Smoke::ReadSmokeFrozenYawFromEnvironment;
using NWB::Tests::Smoke::SetSmokeYawWindowTitle;
using NWB::Tests::Smoke::SyncSmokeModelRuntimes;

using StressModelRef = NWB::Core::Assets::AssetRef<NWB::Impl::Model>;
using StressMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;
using StressMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// STRESS scene: TEN skinned `body` characters in a tight ZIGZAG inside a coloured open-front GI box -- alternating
// TRANSPARENT (glass) and OPAQUE, staggered front/back so their shadows overlap -- lit by one directional + one point light.
// Each character SPINS about its vertical axis
// (no skeleton-pose animation; the bodies render in bind pose and the whole entity rotates), so its instance transform
// changes every frame -- exercising the per-frame scene BVH/TLAS rebuild + the hybrid shadow path (opaque->HW binary,
// transparent->SW colored) across TWO shadowed lights as the occluders sweep. Reuses the body model + transparent_multi
// glass/ground materials (no new assets).
static constexpr StressModelRef s_Model = []() constexpr{
    StressModelRef result;
    result.virtualPath = Name("project/characters/body/model");
    return result;
}();
static constexpr StressMaterialRef s_TransparentMaterial = []() constexpr{
    StressMaterialRef result;
    result.virtualPath = Name("project/smoke/transparent_multi/materials/shared");
    return result;
}(); // glass
static constexpr StressMaterialRef s_OpaqueMaterial = []() constexpr{
    StressMaterialRef result;
    result.virtualPath = Name("project/smoke/transparent_multi/materials/ground");
    return result;
}(); // opaque lambert
static constexpr StressMaterialRef s_GroundMaterial = []() constexpr{
    StressMaterialRef result;
    result.virtualPath = Name("project/smoke/transparent_multi/materials/ground");
    return result;
}();
static constexpr StressMeshRef s_GroundMesh = []() constexpr{
    StressMeshRef result;
    result.virtualPath = Name("project/meshes/shadow_plane");
    return result;
}();
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";

static constexpr u32 s_CharactersPerClass = 5u;                       // 5 transparent + 5 opaque
static constexpr u32 s_CharacterCount = s_CharactersPerClass * 2u;
static constexpr f32 s_CharacterSpacingX = 0.72f;                     // tight so neighbours' shadows overlap
static constexpr f32 s_TransparentRowZ = -0.55f;                      // even index -> front of the zigzag
static constexpr f32 s_OpaqueRowZ = 0.55f;                            // odd index  -> back of the zigzag
static constexpr f32 s_CharacterLift = 0.0f;

// GI CORNELL BOX (added to see the surfel GI bounce): three coloured walls (-X blue, +X red, +Z green BACK wall) + a
// ceiling enclose the spinning crowd so each wall's coloured indirect bounce lands on the characters + floor. The camera
// sits on -Z looking toward +Z, so the green BACK wall is at +Z (the far end, IN VIEW) and the box is OPEN on the -Z side
// (behind the camera) -- all three wall colours + the ceiling are visible. Reuses the ground plane mesh + opaque material
// + per-instance colour_tint (no new assets).
static constexpr f32 s_BoxHalfX = 4.0f;                    // side walls at +-4 (just outside the +-3.24 character spread)
static constexpr f32 s_BoxHalfZ = 4.5f;                    // +Z back wall at +4.5; open -Z front at -4.5 (camera at -4.8 looks in)
static constexpr f32 s_BoxHeight = 4.0f;                   // wall height / ceiling y (point light at 2.6 stays inside)
static constexpr f32 s_GroundScale = 2.0f * s_BoxHalfZ;    // floor spans the box depth (+-4.5) so it meets the side + back walls

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


class StressTestSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    [[nodiscard]] static u32 m4PixelCaptureFreezeFrame(){
#if defined(NWB_ASYNC_SHADOW_M4_BENCHMARK)
        static const u32 s_captureFrame = [](){
            f32 configuredFrame = 0.0f;
            if(
                !ReadSmokeEnvironmentF32("NWB_M4_PIXEL_CAPTURE_FREEZE_FRAME", configuredFrame)
                || !IsFinite(configuredFrame)
                || configuredFrame < 1.0f
            ){
                return 0u;
            }
            return static_cast<u32>(Min(configuredFrame, 1000000.0f));
        }();
        return s_captureFrame;
#else
        return 0u;
#endif
    }

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

    [[nodiscard]] static bool hybridShadowOpaqueBaseline(){
#if defined(NWB_HYBRID_SHADOW_BOUNDARY_BENCHMARK)
        static const bool s_enabled = ReadSmokeEnvironmentFlag("NWB_HYBRID_SHADOW_BOUNDARY_OPAQUE_BASELINE");
        return s_enabled;
#else
        return false;
#endif
    }

    static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(context, NWB_TEXT("StressTestSmokeProject"));

        AddSmokeSkinnedRenderSystems(*world, context);
        return world;
    }

    void destroyWorld(){
        DestroySmokeSkinnedRenderWorld(m_context, m_world);
    }

    [[nodiscard]] NWB::Core::ECS::EntityID createCharacter(const u32 index){
        // Zigzag: alternate transparent / opaque along one tight line, staggering even indices to the front row and odd
        // to the back. Neighbours are a transparent and an opaque character offset diagonally, so their (directional +
        // point) shadows overlap on the ground -- the colored transparent shadow folds onto the hard opaque shadow and
        // adjacent characters' shadows pile up, making the shadow duplication / combine easy to observe.
        const bool transparentMaterialClass = (index % 2u) == 0u;
        const bool transparent = !hybridShadowOpaqueBaseline() && transparentMaterialClass;
        const u32 classIndex = index / 2u; // 0..4 within each material class (tint palette)
        const f32 x = (static_cast<f32>(index) - static_cast<f32>(s_CharacterCount - 1u) * 0.5f) * s_CharacterSpacingX;
        const f32 z = transparentMaterialClass ? s_TransparentRowZ : s_OpaqueRowZ;

        bool tintApplied = false;
        const NWB::Core::ECS::EntityID entity = CreateTintedModelEntity(
            *m_world,
            m_context.objectArena,
            s_Model,
            transparent ? s_TransparentMaterial : s_OpaqueMaterial,
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

    // A coloured wall: a scaled ground plane stood VERTICAL (90deg pitch) then yawed to face inward, mirroring the GI-test
    // box builder. `spanScale` is the wall's horizontal length (X for the -Z wall, Z for the side walls); the height is
    // s_BoxHeight. Reuses the opaque ground material + the per-instance colour tint (no new assets).
    [[nodiscard]] NWB::Core::ECS::EntityID createWall(const Float4& position, const Float4& colorTint, const f32 yawDeg, const f32 spanScale){
        const SIMDVector pitchQuat = QuaternionRotationRollPitchYaw(s_PIDIV2, 0.0f, 0.0f);   // stand the plane up (vertical)
        const f32 yawRad = yawDeg * (s_PI / 180.0f);
        const SIMDVector yawQuat = QuaternionRotationRollPitchYaw(0.0f, yawRad, 0.0f);
        // QuaternionMultiply(A, B) applies B FIRST then A, so pass (yaw, pitch) to stand the plane up (pitch) THEN orient
        // it (yaw). Passing (pitch, yaw) yaws the still-flat plane first, which leaves the +-X walls facing the camera
        // (spanning X) instead of facing inward (spanning Z) -- the 90-degrees-off "backdrop" look.
        const SIMDVector wallRotation = QuaternionMultiply(yawQuat, pitchQuat);

        const NWB::Core::ECS::EntityID entity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_GroundMesh,
            s_OpaqueMaterial,
            s_SmokeSurfaceMaterialInterface,
            colorTint,
            position,
            Float4(spanScale, 1.0f, s_BoxHeight, 0.0f)
        );
        if(entity.valid()){
            if(auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity))
                StoreFloat(wallRotation, &transform->rotation);
        }
        return entity;
    }

    // The ceiling: a scaled ground plane at the box top, FLIPPED (180deg pitch) so its lit face points DOWN into the box,
    // bouncing indirect fill onto the crowd. Covers the full box footprint (2*halfX by 2*halfZ).
    [[nodiscard]] NWB::Core::ECS::EntityID createCeiling(const Float4& colorTint){
        const SIMDVector ceilingRotation = QuaternionRotationRollPitchYaw(s_PI, 0.0f, 0.0f);   // face -Y (down into the box)
        const NWB::Core::ECS::EntityID entity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_GroundMesh,
            s_OpaqueMaterial,
            s_SmokeSurfaceMaterialInterface,
            colorTint,
            Float4(0.0f, s_BoxHeight, 0.0f, 0.0f),
            Float4(2.0f * s_BoxHalfX, 1.0f, 2.0f * s_BoxHalfZ, 0.0f)
        );
        if(entity.valid()){
            if(auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity))
                StoreFloat(ceilingRotation, &transform->rotation);
        }
        return entity;
    }

    // Spin every character about its vertical (Y) axis around its fixed position; a per-character phase staggers the
    // start angles so the crowd isn't in lockstep. Only the root transform's rotation changes -- the skinned bodies
    // stay in bind pose -- so each frame the instance/scene BVH + the two lights' shadows re-resolve as they turn.
    // Diagnostic freeze (read once): NWB_STRESS_TEST_SPIN_ANGLE pins yawBase to a fixed radians value so the skinned
    // crowd holds one orientation -- two captures then differ only via non-determinism (a flicker/race), not motion.
    static f32 frozenYaw(){
        static const f32 s_yaw = ReadSmokeFrozenYawFromEnvironment("NWB_STRESS_TEST_SPIN_ANGLE");
        return s_yaw;
    }

    void spinCharacters(){
        const f32 yawBase = m_yaw.yaw();
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

        const NWB::Core::ECS::EntityID activeCamera = CreateSmokeCamera(*m_world, s_CameraHeight, s_CameraDistance, s_CameraPitch);

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

        m_groundEntity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_GroundMesh,
            s_GroundMaterial,
            s_SmokeSurfaceMaterialInterface,
            Float4(0.82f, 0.82f, 0.85f, 1.0f),
            Float4(0.0f, 0.0f, 0.0f, 0.0f),
            Float4(s_GroundScale, 1.0f, s_GroundScale, 0.0f)
        );

        // GI box: three coloured walls + a ceiling around the crowd (see the s_BoxHalf* notes). Distinct saturated hues so
        // each wall's indirect bounce reads as a different colour on the floor + characters; the ceiling is a warm fill.
        m_wallPosX = createWall(Float4(s_BoxHalfX, s_BoxHeight * 0.5f, 0.0f, 0.0f), Float4(0.80f, 0.08f, 0.08f, 1.0f), -90.0f, 2.0f * s_BoxHalfZ); // +X red
        m_wallNegX = createWall(Float4(-s_BoxHalfX, s_BoxHeight * 0.5f, 0.0f, 0.0f), Float4(0.08f, 0.12f, 0.80f, 1.0f), 90.0f, 2.0f * s_BoxHalfZ); // -X blue
        m_wallPosZ = createWall(Float4(0.0f, s_BoxHeight * 0.5f, s_BoxHalfZ, 0.0f), Float4(0.10f, 0.72f, 0.14f, 1.0f), 180.0f, 2.0f * s_BoxHalfX); // +Z green (far back wall, faces -Z toward the crowd)
        m_ceiling = createCeiling(Float4(0.90f, 0.86f, 0.72f, 1.0f));                                                                             // warm off-white ceiling

        m_characterOwners.reserve(s_CharacterCount);
        for(u32 index = 0u; index < s_CharacterCount; ++index)
            m_characterOwners.push_back(createCharacter(index));

        SyncSmokeModelRuntimes(*m_world);

        bool allCharactersValid = m_characterOwners.size() == s_CharacterCount;
        for(const NWB::Core::ECS::EntityID owner : m_characterOwners)
            allCharactersValid = allCharactersValid && owner.valid();

        NWB_FATAL_ASSERT_MSG(
            activeCamera.valid() && directionalLight.valid() && pointLight.valid() && m_groundEntity.valid() && allCharactersValid
            && m_wallPosX.valid() && m_wallNegX.valid() && m_wallPosZ.valid() && m_ceiling.valid(),
            NWB_TEXT("StressTestSmokeProject failed to create all scene entities")
        );

#if defined(NWB_HYBRID_SHADOW_BOUNDARY_BENCHMARK)
        const bool rayQueryCapable =
            m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct)
            && m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery)
        ;
        if(!rayQueryCapable){
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("StressTestSmokeProject: hybrid shadow boundary skipped because RayQuery-capable hardware is unavailable")
            );
        }else{
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("StressTestSmokeProject: RayQuery-capable hybrid shadow hardware available")
            );
            if(hybridShadowOpaqueBaseline())
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: enabled natural opaque hardware-shadow baseline"));
            else
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: enabled healthy hybrid transparent-shadow benchmark"));
        }
#endif

        const u32 transparentCharacterCount = hybridShadowOpaqueBaseline() ? 0u : s_CharactersPerClass;
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("StressTestSmokeProject: spawned {} spinning characters ({} transparent + {} opaque) over ground, directional + point light")
            , s_CharacterCount
            , transparentCharacterCount
            , s_CharacterCount - transparentCharacterCount
        );
        return true;
    }

    virtual void onShutdown()override{
        m_context.graphics.setFrameSubmissionSuspended(false);
        m_context.input.removeHandler(m_arrowYawInput);
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        const u32 m4CaptureFreezeFrame = m4PixelCaptureFreezeFrame();
        if(m4CaptureFreezeFrame != 0u && m_m4RenderedFrameCount >= m4CaptureFreezeFrame){
            if(!m_m4PixelCapturePaused){
                // Stop runFrame before publishing the marker. The external M4 harness can therefore settle and capture
                // the last completed image without advancing temporal shadow/caustic history after this frame index.
                m_context.graphics.setFrameSubmissionSuspended(true);
                m_m4PixelCapturePaused = true;
                NWB_LOGGER_ESSENTIAL_INFO(
                    NWB_TEXT("StressTestSmokeProject: M4 pixel capture ready after {} rendered frames; render submission suspended"),
                    m_m4RenderedFrameCount
                );
            }
            return true;
        }

        const u32 baselineCaptureFreezeFrame = rendererBaselineCaptureFreezeFrame();
        if(baselineCaptureFreezeFrame != 0u && m_m4RenderedFrameCount >= baselineCaptureFreezeFrame){
            if(!m_rendererBaselineCapturePaused){
                // Keep the M4-specific capture contract above intact. Ordinary baseline captures use their own
                // smoke-only marker and share the fixed submitted-frame counter without affecting async tests.
                m_context.graphics.setFrameSubmissionSuspended(true);
                m_rendererBaselineCapturePaused = true;
                NWB_LOGGER_ESSENTIAL_INFO(
                    NWB_TEXT("StressTestSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"),
                    m_m4RenderedFrameCount
                );
            }
            return true;
        }

        const f32 fixedDelta = rendererBaselineFixedDelta();
        const f32 safeDelta = fixedDelta > 0.0f ? fixedDelta : (IsFinite(delta) ? Max(delta, 0.0f) : 0.0f);
        m_fpsProbe.recordFrame(safeDelta);
        m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
        // Yaw selection: 1) NWB_STRESS_TEST_SPIN_ANGLE env freeze (pins one orientation); 2) manual arrow scrub (latches off
        // auto-spin the moment Left/Right is first pressed, so the crowd can be parked on a precise angle); 3) auto-spin.
        const f32 frozen = frozenYaw();
        m_yaw.update(safeDelta, frozen, frozen >= 0.0f, m_arrowYawInput, s_ManualYawSpeed, s_SpinSpeed, s_MaxSpinDelta);
        spinCharacters();
        SetSmokeYawWindowTitle(m_context, m_yaw.yaw(), m_yaw.manualControl(), s_TwoPi);
        m_world->tick(safeDelta);
        ++m_m4RenderedFrameCount;
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    Vector<NWB::Core::ECS::EntityID, NWB::Core::Alloc::GlobalArena> m_characterOwners;
    NWB::Core::ECS::EntityID m_groundEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_wallPosX = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_wallNegX = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_wallPosZ = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_ceiling = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Tests::Smoke::FpsProbe m_fpsProbe{ NWB_TEXT("StressTestSmokeProject") };
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ NWB_TEXT("StressTestSmokeProject") };
    NWB::Tests::Smoke::YawSpinController m_yaw;
    ArrowYawInputHandler m_arrowYawInput;
    u32 m_m4RenderedFrameCount = 0u;
    bool m_m4PixelCapturePaused = false;
    bool m_rendererBaselineCapturePaused = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


const tchar* NWB::QueryProjectWindowTitle(){
#if defined(NWB_HYBRID_SHADOW_BOUNDARY_BENCHMARK)
    return NWB_TEXT("NWB Hybrid Shadow Boundary Benchmark");
#elif defined(NWB_ASYNC_SHADOW_M4_BENCHMARK)
    return NWB_TEXT("NWB Async Shadow M4 Benchmark");
#else
    return NWB_TEXT("NWB Stress Test Smoke");
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_stress_test_smoke::StressTestSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

