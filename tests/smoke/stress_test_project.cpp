// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "arrow_yaw_input_handler.h"
#include "avboit_timing_render_pass.h"
#include "gpu_pass_timing_probe.h"
#include "presentation_fps_probe.h"
#include "presentation_pacing_ring.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"

#include <loader/project_entry.h>

#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_mesh/skinning/module.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>

#include <global/math/frame.h>
#include <global/math/constant.h>
#include <global/math/quaternion.h>


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
using NWB::Tests::Smoke::RendererBaselineCaptureFreezeFrame;
using NWB::Tests::Smoke::RendererBaselineFixedDelta;
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
// changes every frame -- exercising scene TLAS updates and separate opaque/transparent hardware shadows across TWO
// shadowed lights as the occluders sweep. Reuses the body model + transparent_multi
// glass/ground materials (no new assets).
static constexpr StressModelRef s_Model{"project/characters/body/model"};
static constexpr StressMaterialRef s_TransparentMaterial{"project/smoke/transparent_multi/materials/shared"}; // glass
static constexpr StressMaterialRef s_OpaqueMaterial{"project/smoke/transparent_multi/materials/ground"}; // opaque lambert
static constexpr StressMaterialRef s_GroundMaterial{"project/smoke/transparent_multi/materials/ground"};
static constexpr StressMeshRef s_GroundMesh{"project/meshes/shadow_plane"};
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
static constexpr Float2U s_BoxHalf = Float2U(4.0f, 4.5f);                    // x: side walls at +-4 (just outside the +-3.24 character spread); y: +Z back wall at +4.5; open -Z front at -4.5 (camera at -4.8 looks in)
static constexpr f32 s_BoxHeight = 4.0f;                   // wall height / ceiling y (point light at 2.6 stays inside)
static constexpr f32 s_GroundScale = 2.0f * s_BoxHalf.y;    // floor spans the box depth (+-4.5) so it meets the side + back walls

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
        return RendererBaselineCaptureFreezeFrame();
    }

    [[nodiscard]] static f32 rendererBaselineFixedDelta(){
        return RendererBaselineFixedDelta();
    }

    // The legacy benchmark identifier now compares hardware transparent shadows against an opaque-only scene.
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
            tintApplied
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
                StoreFloat(wallRotation, transform->rotation);
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
            Float4(2.0f * s_BoxHalf.x, 1.0f, 2.0f * s_BoxHalf.y, 0.0f)
        );
        if(entity.valid()){
            if(auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity))
                StoreFloat(ceilingRotation, transform->rotation);
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
            StoreFloat(QuaternionRotationRollPitchYaw(0.0f, yawBase + phase, 0.0f), transform->rotation);
        }
    }


    void reportReflectionStatistics(){
        if(!m_reflectionDiagnosticsEnabled)
            return;
        NWB::Impl::ReflectionStatistics statistics;
        if(!m_renderer.tryGetLatestReflectionStatistics(statistics))
            return;
        if(statistics.sequence == m_reflectionStatisticsSequence && statistics.generation == m_reflectionStatisticsGeneration)
            return;
        m_reflectionStatisticsSequence = statistics.sequence;
        m_reflectionStatisticsGeneration = statistics.generation;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressReflectionStatistics: sequence={} generation={} frame={} graphics_frame={}")
            NWB_TEXT(" hardware_ready={} transport_enabled={} candidates={} hardware_rays={} exterior_eligible_rays={}")
            NWB_TEXT(" hardware_queries={} bootstrap_events={} transparent_paths={} unsupported_paths={}")
            , statistics.sequence
            , statistics.generation
            , statistics.frameIndex
            , statistics.graphicsFrameIndex
            , statistics.hardwareReady ? 1u : 0u
            , statistics.opticalTransportEnabled ? 1u : 0u
            , statistics.candidates
            , statistics.hardwareRays
            , statistics.exteriorEligibleRays
            , statistics.hardwareQueries
            , statistics.bootstrapEvents
            , statistics.transparentPaths
            , statistics.unsupportedPaths
        );
    }

    bool samplePresentationFps(){
        const u64 successfulPresentations = m_context.graphics.getSuccessfulPresentationCount();
        const Timer observationTime = TimerNow();
        m_pacingRing.record(successfulPresentations, observationTime);
        const auto status = m_fpsProbe.observe(successfulPresentations, observationTime);
        if(status == NWB::Tests::Smoke::PresentationFpsStatus::Invalid){
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: presentation measurement invalid counter or clock"));
            return false;
        }
        if(status == NWB::Tests::Smoke::PresentationFpsStatus::Waiting)
            return true;

        const auto& interval = m_fpsProbe.interval();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: presentation fps avg={} presentations={} seconds={} first={} last={}")
            , interval.averageFps()
            , interval.presentations()
            , interval.wallSeconds
            , interval.firstPresentationCount
            , interval.lastPresentationCount
        );
        if(status == NWB::Tests::Smoke::PresentationFpsStatus::Complete){
            const auto& total = m_fpsProbe.total();
            const NWB::Tests::Smoke::PresentationPacingSummary pacingSummary = m_pacingRing.summarize();
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: presentation pacing samples={} p50ms={} p95ms={} maxms={} stalls50ms={}")
                , pacingSummary.samples
                , pacingSummary.p50Ms
                , pacingSummary.p95Ms
                , pacingSummary.maxMs
                , pacingSummary.stallsOver50Ms
            );
            m_timingComplete = true;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: presentation measurement complete fps={} presentations={} seconds={} first={} last={}")
                , total.averageFps()
                , total.presentations()
                , total.wallSeconds
                , total.firstPresentationCount
                , total.lastPresentationCount
            );
            m_context.requestQuit();
        }
        return true;
    }


public:
    explicit StressTestSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(createWorldOrDie(context))
        , m_renderer([this]() -> NWB::Impl::RendererSystem&{
            auto* const renderer = m_world->getSystem<NWB::Impl::RendererSystem>();
            NWB_FATAL_ASSERT(renderer);
            return *renderer;
        }())
        , m_characterOwners(context.objectArena)
    {}

    virtual ~StressTestSmokeProject()override{
        m_context.input.removeHandler(m_arrowYawInput); // idempotent backstop if onShutdown was skipped (dispatcher outlives us)
        m_timingRenderPass.stop();
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        if(m_reflectionDiagnosticsEnabled){
            NWB::Impl::ReflectionSettings settings;
            settings.diagnosticsEnabled = true;
            if(!m_renderer.setReflectionSettings(settings))
                return false;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: reflection diagnostics enabled"));
        }
        // GPU durations are sampled diagnostics; FPS comes only from accepted native presentations and steady wall time.
        m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());
        if(m_timingEnabled){
            if(m4PixelCaptureFreezeFrame() != 0u || rendererBaselineCaptureFreezeFrame() != 0u || !m_context.requestQuit){
                NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: timing requires continuous submissions and a quit callback"));
                return false;
            }
            if(!m_timingRenderPass.start(true))
                return false;
            m_pacingRing.reset();
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: presentation timing warmup_seconds=5 measure_seconds=30 clock=steady accepted_native_present=1"));
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: device capability meshlets={} rayquery={} raypipeline={} accelstruct={} wavelanes={} renderer={}")
                , m_context.graphics.queryFeatureSupport(NWB::Core::Feature::Meshlets) ? 1u : 0u
                , m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery) ? 1u : 0u
                , m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingPipeline) ? 1u : 0u
                , m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct) ? 1u : 0u
                , m_context.graphics.queryWaveLaneCount()
                , m_context.graphics.getRendererString()
            );
        }

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
        m_wallPosX = createWall(Float4(s_BoxHalf.x, s_BoxHeight * 0.5f, 0.0f, 0.0f), Float4(0.80f, 0.08f, 0.08f, 1.0f), -90.0f, 2.0f * s_BoxHalf.y); // +X red
        m_wallNegX = createWall(Float4(-s_BoxHalf.x, s_BoxHeight * 0.5f, 0.0f, 0.0f), Float4(0.08f, 0.12f, 0.80f, 1.0f), 90.0f, 2.0f * s_BoxHalf.y); // -X blue
        m_wallPosZ = createWall(Float4(0.0f, s_BoxHeight * 0.5f, s_BoxHalf.y, 0.0f), Float4(0.10f, 0.72f, 0.14f, 1.0f), 180.0f, 2.0f * s_BoxHalf.x); // +Z green (far back wall, faces -Z toward the crowd)
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
                NWB_TEXT("StressTestSmokeProject: hardware shadow boundary skipped because RayQuery-capable hardware is unavailable")
            );
        }else{
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("StressTestSmokeProject: RayQuery-capable hardware shadow route available")
            );
            if(hybridShadowOpaqueBaseline())
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: enabled natural opaque hardware-shadow baseline"));
            else
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressTestSmokeProject: enabled healthy hardware transparent-shadow benchmark"));
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
        m_timingRenderPass.stop();
        if(m_timingEnabled && !m_timingComplete)
            NWB_LOGGER_ERROR(NWB_TEXT("StressTestSmokeProject: presentation measurement incomplete"));
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
        if(!samplePresentationFps())
            return false;
        m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
        // Yaw selection: 1) NWB_STRESS_TEST_SPIN_ANGLE env freeze (pins one orientation); 2) manual arrow scrub (latches off
        // auto-spin the moment Left/Right is first pressed, so the crowd can be parked on a precise angle); 3) auto-spin.
        const f32 frozen = frozenYaw();
        m_yaw.update(safeDelta, frozen, frozen >= 0.0f, m_arrowYawInput, s_ManualYawSpeed, s_SpinSpeed, s_MaxSpinDelta);
        spinCharacters();
        SetSmokeYawWindowTitle(m_context, m_yaw.yaw(), m_yaw.manualControl(), s_TwoPi);
        m_world->tick(safeDelta);
        reportReflectionStatistics();
        ++m_m4RenderedFrameCount;
        return true;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Impl::RendererSystem& m_renderer;
    Vector<NWB::Core::ECS::EntityID, NWB::Core::Alloc::GlobalArena> m_characterOwners;
    NWB::Core::ECS::EntityID m_groundEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_wallPosX = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_wallNegX = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_wallPosZ = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_ceiling = NWB::Core::ECS::ENTITY_ID_INVALID;
    const bool m_timingEnabled = ReadSmokeEnvironmentFlag("NWB_STRESS_SMOKE_TIMING");
    const bool m_reflectionDiagnosticsEnabled = ReadSmokeEnvironmentFlag("NWB_STRESS_REFLECTION_DIAGNOSTICS");
    u64 m_reflectionStatisticsSequence = 0u;
    u64 m_reflectionStatisticsGeneration = 0u;
    NWB::Tests::Smoke::AvboitTimingRenderPass m_timingRenderPass{ m_context.graphics };
    NWB::Tests::Smoke::PresentationFpsProbe m_fpsProbe{ m_timingEnabled ? 5.0 : 0.25, m_timingEnabled ? 30.0 : 0.0 };
    NWB::Tests::Smoke::PresentationPacingRing m_pacingRing;
    bool m_timingComplete = false;
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

