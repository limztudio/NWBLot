// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>
#include <global/math/frame.h>
#include <global/math/constant.h>
#include <global/math/quaternion.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_mesh/skinning/module.h>

#include "gpu_pass_timing_probe.h"
#include "csg_gi_scene.h"
#include "presentation_fps_probe.h"
#include "smoke_environment.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gi_test_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::Smoke::CreateSmokeCamera;
using NWB::Tests::Smoke::CreateSmokeWorldOrDie;
using NWB::Tests::Smoke::CreateTintedStaticMeshEntity;
using NWB::Tests::Smoke::DestroySmokeRenderWorld;
using NWB::Tests::Smoke::AddSmokeRenderSystems;
using NWB::Tests::Smoke::RendererBaselineCaptureFreezeFrame;
using NWB::Tests::Smoke::RendererBaselineFixedDelta;

using GiTestMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::Mesh>;
using GiTestMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The baseline is an open box; the optional A/B maze admits light only through its skylight.

static constexpr GiTestMeshRef s_GroundMesh{"project/meshes/shadow_plane"};
static constexpr GiTestMeshRef s_BoxMesh{"project/meshes/cube_hard_edges"};
static constexpr GiTestMaterialRef s_OpaqueMaterial{"project/smoke/transparent_multi/materials/ground"};
static constexpr GiTestMaterialRef s_ComplexDiffuseMaterial{"project/smoke/gi/materials/diffuse"};
static constexpr GiTestMaterialRef s_ComplexIndirectMaterial{"project/smoke/gi/materials/indirect"};
static constexpr GiTestMaterialRef s_CoverageProbeMaterial{"project/smoke/gi/materials/coverage_probe"};
static constexpr AStringView s_SmokeSurfaceMaterialInterface = "project/shaders/smoke_surface";

static constexpr f32 s_BoxHalfExtent = 2.0f;
static constexpr f32 s_BoxHeight = 2.0f;
static constexpr f32 s_BoxScale = 4.0f;

// The elevated view exposes floor bounce without wall occlusion or top-down foreshortening.
static constexpr f32 s_CameraDistance = 7.5f;
static constexpr f32 s_CameraHeight = 3.6f;
static constexpr f32 s_CameraPitch = 0.42f;
static constexpr f32 s_ComplexCameraDistance = 4.2f;
static constexpr f32 s_ComplexCameraHeight = 1.65f;
static constexpr f32 s_ComplexCameraPitch = 0.12f;

// Light the red wall while shadowing the floor to isolate its indirect bounce.
static constexpr f32 s_DirectionalLightPitch = 0.85f;
static constexpr f32 s_DirectionalLightYaw = 0.6f;
static constexpr f32 s_DirectionalLightIntensity = 2.0f;
static constexpr f32 s_ComplexLightPitch = 1.45f;      // nearly vertical: passes through the small roof aperture

// The optional A/B layout uses solid blocks so doorway silhouettes, receiver faces, and contact shadows remain clear.
static constexpr Float4 s_NeutralPanelTint(0.76f, 0.76f, 0.76f, 1.0f);
static constexpr Float4 s_WhiteReceiverTint(0.92f, 0.92f, 0.92f, 1.0f);
static constexpr Float4 s_RedBounceTint(0.84f, 0.10f, 0.08f, 1.0f);
static constexpr Float4 s_BlueBounceTint(0.08f, 0.14f, 0.86f, 1.0f);
static constexpr Float4 s_BlackEnclosureTint(0.0f, 0.0f, 0.0f, 1.0f);
static constexpr u32 s_ComplexRandomBodyCount = 20u;
static constexpr u32 s_ComplexRandomBodySeed = 0x7A31C5E9u;

struct ComplexSceneBox{
    Float4 position;
    Float4 scale;
    Float4 tint;
    f32 yawDegrees;
    bool indirectOnly = false;
};

// Cube vertices span -0.5..+0.5, so each scale is the desired full physical size. Both partitions have offset
// openings; each camera-visible white receiver sees a different mix of the red and blue bounce surfaces.
static constexpr ComplexSceneBox s_ComplexSceneBoxes[] = {
    // Front doorway: x=-0.4..0.5 at z=-0.35, with a high lintel.
    { Float4(-1.20f, 0.625f, -0.35f, 0.0f), Float4(1.60f, 1.25f, 0.13f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4( 1.25f, 0.625f, -0.35f, 0.0f), Float4(1.50f, 1.25f, 0.13f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4(-0.42f, 1.525f, -0.35f, 0.0f), Float4(0.13f, 0.55f, 0.16f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4( 0.52f, 1.525f, -0.35f, 0.0f), Float4(0.13f, 0.55f, 0.16f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4( 0.05f, 1.90f, -0.35f, 0.0f), Float4(0.90f, 0.20f, 0.13f, 0.0f), s_NeutralPanelTint, 0.0f },

    // Rear doorway is shifted right, forcing a turn and hiding much of the far receiver from direct view.
    { Float4(-0.675f, 0.85f, 0.95f, 0.0f), Float4(2.65f, 1.70f, 0.13f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4( 1.725f, 0.85f, 0.95f, 0.0f), Float4(0.55f, 1.70f, 0.13f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4( 1.05f, 1.85f, 0.95f, 0.0f), Float4(0.80f, 0.30f, 0.13f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4(-0.65f, 0.60f, 0.30f, 0.0f), Float4(0.13f, 1.20f, 1.15f, 0.0f), s_NeutralPanelTint, 0.0f },

    // Foreground pillars and plinths provide distinct shadow edges and horizontal indirect-light receivers.
    { Float4(-1.60f, 0.65f, -1.50f, 0.0f), Float4(0.30f, 1.30f, 0.30f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4( 1.60f, 0.65f, -1.35f, 0.0f), Float4(0.30f, 1.30f, 0.30f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4(-1.80f, 0.48f, -1.82f, 0.0f), Float4(0.18f, 0.96f, 0.56f, 0.0f), s_BlueBounceTint, 0.0f },
    { Float4( 1.80f, 0.48f, -1.82f, 0.0f), Float4(0.18f, 0.96f, 0.56f, 0.0f), s_RedBounceTint, 0.0f },
    { Float4(-0.85f, 0.23f, -1.30f, 0.0f), Float4(0.70f, 0.46f, 0.60f, 0.0f), s_WhiteReceiverTint, 0.0f },
    { Float4( 0.95f, 0.32f, -1.45f, 0.0f), Float4(0.60f, 0.64f, 0.65f, 0.0f), s_WhiteReceiverTint, 0.0f },
    { Float4( 0.00f, 0.20f,  0.35f, 0.0f), Float4(0.52f, 0.40f, 0.50f, 0.0f), s_WhiteReceiverTint, 0.0f },

    // Angled white fins reveal directional color bleed; colored panels act as local bounce sources.
    { Float4(-1.30f, 0.65f, -0.75f, 0.0f), Float4(0.10f, 1.30f, 1.00f, 0.0f), s_WhiteReceiverTint,  55.0f, true },
    { Float4( 1.30f, 0.65f, -0.75f, 0.0f), Float4(0.10f, 1.30f, 1.00f, 0.0f), s_WhiteReceiverTint, -55.0f, true },
    { Float4( 1.72f, 0.70f,  0.38f, 0.0f), Float4(0.12f, 1.40f, 0.72f, 0.0f), s_RedBounceTint, 0.0f },
    { Float4(-1.72f, 0.70f,  0.38f, 0.0f), Float4(0.12f, 1.40f, 0.72f, 0.0f), s_BlueBounceTint, 0.0f },

    // Rear receivers and broken canopy make a deeper, partly skylit third bay with another indirect-light path.
    { Float4(-1.12f, 0.60f, 1.55f, 0.0f), Float4(0.34f, 1.20f, 0.34f, 0.0f), s_WhiteReceiverTint, 0.0f },
    { Float4( 1.05f, 0.55f, 1.55f, 0.0f), Float4(0.40f, 1.10f, 0.42f, 0.0f), s_WhiteReceiverTint, 0.0f },
    { Float4(-1.10f, 1.93f, 1.55f, 0.0f), Float4(1.50f, 0.14f, 0.78f, 0.0f), s_NeutralPanelTint, 0.0f },
    { Float4( 1.30f, 1.93f, 1.55f, 0.0f), Float4(1.10f, 0.14f, 0.78f, 0.0f), s_NeutralPanelTint, 0.0f },

    // A 1.9 by 1.1 metre skylight admits the near-vertical sun onto the colored floor patches below.
    { Float4(-1.475f, 2.04f,  0.000f, 0.0f), Float4(1.05f, 0.12f, 4.00f, 0.0f), s_BlackEnclosureTint, 0.0f },
    { Float4( 1.475f, 2.04f,  0.000f, 0.0f), Float4(1.05f, 0.12f, 4.00f, 0.0f), s_BlackEnclosureTint, 0.0f },
    { Float4( 0.000f, 2.04f, -1.575f, 0.0f), Float4(1.90f, 0.12f, 0.85f, 0.0f), s_BlackEnclosureTint, 0.0f },
    { Float4( 0.000f, 2.04f,  0.975f, 0.0f), Float4(1.90f, 0.12f, 2.05f, 0.0f), s_BlackEnclosureTint, 0.0f },
};

// Two upward-facing, camera-visible source patches are directly below the skylight. Their adjacent white fins
// use an indirect-only display material so the captured color on those fins comes from bounced light.
static constexpr ComplexSceneBox s_ComplexBouncePatches[] = {
    { Float4(-0.45f, 0.025f, -0.55f, 0.0f), Float4(0.85f, 0.05f, 0.80f, 0.0f), s_RedBounceTint, 0.0f },
    { Float4( 0.45f, 0.025f, -0.55f, 0.0f), Float4(0.85f, 0.05f, 0.80f, 0.0f), s_BlueBounceTint, 0.0f },
};

// The camera sits at z=-4.2. These shell pieces extend the floor and roof behind the maze and close the front at
// z=-5. Full-depth overlapping side walls seal the joins to the original planes and roof, leaving only the skylight.
static constexpr ComplexSceneBox s_ComplexFrontEnclosureBoxes[] = {
    { Float4(-2.00f, 1.00f, -1.50f, 0.0f), Float4(0.12f, 6.00f, 7.10f, 0.0f), s_BlackEnclosureTint, 0.0f },
    { Float4( 2.00f, 1.00f, -1.50f, 0.0f), Float4(0.12f, 6.00f, 7.10f, 0.0f), s_BlackEnclosureTint, 0.0f },
    { Float4( 0.00f, 1.00f, -5.00f, 0.0f), Float4(4.10f, 2.10f, 0.12f, 0.0f), s_BlackEnclosureTint, 0.0f },
    { Float4( 0.00f,-0.06f, -3.50f, 0.0f), Float4(4.10f, 0.12f, 3.10f, 0.0f), s_BlackEnclosureTint, 0.0f },
    { Float4( 0.00f, 2.04f, -3.50f, 0.0f), Float4(4.10f, 0.12f, 3.10f, 0.0f), s_BlackEnclosureTint, 0.0f },
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GiTestSmokeProject final : public NWB::IProjectEntryCallbacks{
private:
    [[nodiscard]] static u32 RendererBaselineCaptureFreezeFrame(){
        return NWB::Tests::Smoke::RendererBaselineCaptureFreezeFrame();
    }

    [[nodiscard]] static f32 RendererBaselineFixedDelta(){
        return NWB::Tests::Smoke::RendererBaselineFixedDelta();
    }


    static NotNullUniquePtr<NWB::Core::ECS::World> CreateWorldOrDie(NWB::ProjectRuntimeContext& context){
        auto world = CreateSmokeWorldOrDie(context, NWB_TEXT("GiTestSmokeProject"));

        AddSmokeRenderSystems(*world, context);
        return world;
    }

    void destroyWorld(){
        m_csgGiScene.stop();
        DestroySmokeRenderWorld(m_context, m_world);
    }


public:
    explicit GiTestSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateWorldOrDie(context))
        , m_csgGiScene(context, *m_world)
    {}

    virtual ~GiTestSmokeProject()override{
        destroyWorld();
    }


public:
    virtual bool onStartup()override{
        const auto csgGi = m_csgGiScene.start();
        if(!csgGi)
            return false;
        if(*csgGi)
            return true;
        m_resolveSwitchEnabled = NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_GI_SMOKE_RESOLVE_SWITCH");
        m_coverageViewEnabled = NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_GI_SMOKE_COVERAGE_VIEW");
        m_complexSceneEnabled = m_coverageViewEnabled || NWB::Tests::Smoke::ReadSmokeEnvironmentFlag("NWB_GI_SMOKE_COMPLEX_SCENE");
        if(m_complexSceneEnabled){
            const auto settleValue = NWB::Tests::Smoke::ReadSmokeEnvironmentText(m_context.objectArena, "NWB_GI_SMOKE_MIN_SETTLE_SECONDS");
            if(settleValue){
                const auto requestedSettleSeconds = ParseF32FromChars(AStringView(settleValue->data(), settleValue->size()));
                if(!requestedSettleSeconds || !IsFinite(*requestedSettleSeconds) || *requestedSettleSeconds < 0.0f){
                    NWB_LOGGER_ERROR(NWB_TEXT("GiTestSmokeProject: invalid NWB_GI_SMOKE_MIN_SETTLE_SECONDS"));
                    return false;
                }
                m_complexMinSettleSeconds = *requestedSettleSeconds;
            }
        }
        if(m_resolveSwitchEnabled){
            auto* const renderer = m_world->getSystem<NWB::Impl::RendererSystem>();
            if(!renderer)
                return false;
            renderer->setFrameLaggedAsyncLightingEnabled(true);
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: surfel GI resolve switch probe enabled (lagged_lighting=1)"));
        }

        // Emit per-pass GPU timings (render.surfel_*) for A/B capture via NWB_GPU_TIMING_FILE.
        m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());

        const NWB::Core::ECS::EntityID activeCamera = CreateSmokeCamera(
            *m_world,
            m_complexSceneEnabled ? s_ComplexCameraHeight : s_CameraHeight,
            m_complexSceneEnabled ? s_ComplexCameraDistance : s_CameraDistance,
            m_complexSceneEnabled ? s_ComplexCameraPitch : s_CameraPitch
        );

        // The original box keeps its grazing sun. The closed layout uses a near-vertical sun so both colored
        // floor patches receive direct light through the small skylight while their upright catchers stay shaded.
        const NWB::Core::ECS::EntityID directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world,
            m_complexSceneEnabled ? s_ComplexLightPitch : s_DirectionalLightPitch,
            m_complexSceneEnabled ? 0.0f : s_DirectionalLightYaw,
            0.0f,
            Float4(1.00f, 0.96f, 0.88f),
            s_DirectionalLightIntensity
        );

        const f32 enclosureScale = m_complexSceneEnabled ? s_BoxHalfExtent : s_BoxScale;
        const f32 wallHeightScale = m_complexSceneEnabled ? s_BoxHeight * 0.5f : s_BoxHeight;

        // The white floor reveals colored GI in the wall's direct shadow.
        m_floorEntity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_GroundMesh,
            m_complexSceneEnabled ? s_ComplexDiffuseMaterial : s_OpaqueMaterial,
            s_SmokeSurfaceMaterialInterface,
            m_complexSceneEnabled ? s_BlackEnclosureTint : Float4(0.90f, 0.90f, 0.90f, 1.0f),
            Float4(0.0f, 0.0f, 0.0f, 0.0f),
            Float4(enclosureScale, 1.0f, enclosureScale, 0.0f)
        );

        m_redWallEntity = createWall(
            Float4(s_BoxHalfExtent, s_BoxHeight * 0.5f, 0.0f, 0.0f),
            m_complexSceneEnabled ? s_BlackEnclosureTint : Float4(0.80f, 0.08f, 0.08f, 1.0f),
            -90.0f, enclosureScale, wallHeightScale
        );

        // Opposing red and blue bounce distinguish GI from constant ambient light.
        m_blueWallNegX = createWall(
            Float4(-s_BoxHalfExtent, s_BoxHeight * 0.5f, 0.0f, 0.0f),
            m_complexSceneEnabled ? s_BlackEnclosureTint : Float4(0.08f, 0.08f, 0.80f, 1.0f),
            90.0f, enclosureScale, wallHeightScale
        );
        // The far (+Z) wall closes the rear of both layouts. The complex layout adds a black front wall behind its
        // camera in createComplexScene(); the original Cornell-style layout keeps its camera-facing side open.
        m_whiteWallPosZ = createWall(
            Float4(0.0f, s_BoxHeight * 0.5f, s_BoxHalfExtent, 0.0f),
            m_complexSceneEnabled ? s_BlackEnclosureTint : Float4(0.88f, 0.88f, 0.88f, 1.0f),
            180.0f, enclosureScale, wallHeightScale
        );

        if(m_complexSceneEnabled && !createComplexScene())
            return false;

        NWB_FATAL_ASSERT_MSG(
            activeCamera.valid()
            && directionalLight.valid()
            && m_floorEntity.valid()
            && m_redWallEntity.valid()
            && m_blueWallNegX.valid()
            && m_whiteWallPosZ.valid(),
            NWB_TEXT("GiTestSmokeProject failed to create all scene entities")
        );

        if(m_complexSceneEnabled){
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("GiTestSmokeProject: closed black enclosure around camera and maze, small skylight, one vertical sun, red/blue bounce patches")
            );
        }else{
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("GiTestSmokeProject: open-top box + red/blue opposite walls, directional light -> indirect red+blue bleed on shadowed floor")
            );
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: complex_scene={}"), m_complexSceneEnabled ? 1u : 0u);
        if(m_complexSceneEnabled)
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: min_elapsed_settle_seconds={}"), m_complexMinSettleSeconds);
        return true;
    }

    virtual void onShutdown()override{
        m_context.graphics.setFrameSubmissionSuspended(false);
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        if(m_csgGiScene.active()){
            m_csgGiScene.update();
            m_world->tick(1.0f / 60.0f);
            return true;
        }
        f64 elapsedSeconds = 0.0;
        if(m_complexSceneEnabled){
            const Timer now = TimerNow();
            if(!m_complexSceneTimingStarted){
                m_complexSceneStartTime = now;
                m_complexSceneTimingStarted = true;
            }
            elapsedSeconds = DurationInSeconds<f64>(now, m_complexSceneStartTime);
        }
        const u32 captureFreezeFrame = RendererBaselineCaptureFreezeFrame();
        const u64 successfulPresentations = m_context.graphics.getSuccessfulPresentationCount();
        const u64 captureProgress = m_complexSceneEnabled ? successfulPresentations : m_rendererBaselineRenderedFrameCount;
        if(captureFreezeFrame != 0u && captureProgress >= captureFreezeFrame && elapsedSeconds >= m_complexMinSettleSeconds){
            if(!m_rendererBaselineCapturePaused){
                // The complex A/B scene freezes after accepted presentations so framebuffer capture can request
                // the same frame. Retain the established update-tick boundary for the original baseline profile.
                m_context.graphics.setFrameSubmissionSuspended(true);
                m_rendererBaselineCapturePaused = true;
                if(m_complexSceneEnabled){
                    NWB_LOGGER_ESSENTIAL_INFO(
                        NWB_TEXT("GiTestSmokeProject: renderer baseline capture ready after {} successful presentations and {} elapsed seconds; render submission suspended"),
                        successfulPresentations,
                        elapsedSeconds
                    );
                }else{
                    NWB_LOGGER_ESSENTIAL_INFO(
                        NWB_TEXT("GiTestSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"),
                        m_rendererBaselineRenderedFrameCount
                    );
                }
            }
            return true;
        }

        if(!samplePresentationFps())
            return false;

        if(m_resolveSwitchEnabled && (m_rendererBaselineRenderedFrameCount == 30u || m_rendererBaselineRenderedFrameCount == 90u)){
            auto* const renderer = m_world->getSystem<NWB::Impl::RendererSystem>();
            NWB::Impl::SurfelGiQualitySettings settings;
            settings.resolveResolution = m_rendererBaselineRenderedFrameCount == 30u
                ? NWB::Impl::SurfelGiResolveResolution::Quarter
                : NWB::Impl::SurfelGiResolveResolution::Half
            ;
            if(!renderer || !renderer->setSurfelGiQualitySettings(settings))
                return false;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: requested surfel GI resolve switch factor={}")
                , static_cast<u32>(settings.resolveResolution)
            );
        }

        if(m_resolveSwitchEnabled && (m_rendererBaselineRenderedFrameCount == 150u || m_rendererBaselineRenderedFrameCount == 210u)){
            auto* const renderer = m_world->getSystem<NWB::Impl::RendererSystem>();
            NWB::Impl::ShadowQualitySettings settings;
            settings.receiverResolution = m_rendererBaselineRenderedFrameCount == 150u
                ? NWB::Impl::ShadowReceiverResolution::Quarter
                : NWB::Impl::ShadowReceiverResolution::Half
            ;
            if(!renderer || !renderer->setShadowQualitySettings(settings))
                return false;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: requested shadow receiver switch factor={}")
                , static_cast<u32>(settings.receiverResolution)
            );
        }

        const f32 fixedDelta = RendererBaselineFixedDelta();
        const f32 safeDelta = fixedDelta > 0.0f ? fixedDelta : (IsFinite(delta) ? Max(delta, 0.0f) : 0.0f);
        m_gpuPassTimingProbe.recordFrame(safeDelta, m_context.gpuTimingView());
        m_world->tick(safeDelta);
        ++m_rendererBaselineRenderedFrameCount;
        return true;
    }


private:
    [[nodiscard]] bool samplePresentationFps(){
        const u64 successfulPresentations = m_context.graphics.getSuccessfulPresentationCount();
        const auto status = m_presentationFpsProbe.observe(successfulPresentations, TimerNow());
        if(status == NWB::Tests::Smoke::PresentationFpsStatus::Invalid){
            NWB_LOGGER_ERROR(NWB_TEXT("GiTestSmokeProject: presentation measurement invalid counter or clock"));
            return false;
        }
        if(status == NWB::Tests::Smoke::PresentationFpsStatus::Waiting)
            return true;

        const auto& interval = m_presentationFpsProbe.interval();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: presentation fps avg={} presentations={} seconds={} first={} last={}")
            , interval.averageFps()
            , interval.presentations()
            , interval.wallSeconds
            , interval.firstPresentationCount
            , interval.lastPresentationCount
        );
        return true;
    }

    [[nodiscard]] bool createComplexScene(){
        for(const ComplexSceneBox& box : s_ComplexSceneBoxes){
            const auto entity = CreateTintedStaticMeshEntity(
                *m_world, m_context.objectArena, s_BoxMesh,
                box.indirectOnly ? s_ComplexIndirectMaterial : s_ComplexDiffuseMaterial,
                s_SmokeSurfaceMaterialInterface, box.tint, box.position, box.scale
            );
            if(!entity.valid())
                return false;
            if(box.yawDegrees != 0.0f){
                auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
                if(!transform)
                    return false;
                StoreFloat(QuaternionRotationRollPitchYaw(0.0f, box.yawDegrees * (s_PI / 180.0f), 0.0f), transform->rotation);
            }
        }

        for(const ComplexSceneBox& box : s_ComplexBouncePatches){
            const auto entity = CreateTintedStaticMeshEntity(
                *m_world, m_context.objectArena, s_BoxMesh, s_ComplexDiffuseMaterial,
                s_SmokeSurfaceMaterialInterface, box.tint, box.position, box.scale
            );
            if(!entity.valid())
                return false;
        }

        for(const ComplexSceneBox& box : s_ComplexFrontEnclosureBoxes){
            const auto entity = CreateTintedStaticMeshEntity(
                *m_world, m_context.objectArena, s_BoxMesh, s_ComplexDiffuseMaterial,
                s_SmokeSurfaceMaterialInterface, box.tint, box.position, box.scale
            );
            if(!entity.valid())
                return false;
        }

        // Fixed-seed scattered bodies add varied occlusion without moving the colored sources or changing the
        // skylight. Reject overlap with the maze and one another so all 20 bodies remain distinct in a replay.
        ComplexSceneBox placedBodies[s_ComplexRandomBodyCount] = {};
        u32 placedBodyCount = 0u;
        u32 randomState = s_ComplexRandomBodySeed;
        const auto nextRandom01 = [&randomState]() -> f32{
            randomState = randomState * 1664525u + 1013904223u;
            return static_cast<f32>(randomState >> 8u) * (1.0f / 16777216.0f);
        };
        const auto absolute = [](f32 value) -> f32{ return value < 0.0f ? -value : value; };
        const auto overlaps = [absolute](const ComplexSceneBox& a, const ComplexSceneBox& b) -> bool{
            if(a.position.y + a.scale.y * 0.5f <= b.position.y - b.scale.y * 0.5f ||
               b.position.y + b.scale.y * 0.5f <= a.position.y - a.scale.y * 0.5f)
                return false;
            const f32 aAngle = a.yawDegrees * (s_PI / 180.0f);
            const f32 bAngle = b.yawDegrees * (s_PI / 180.0f);
            const f32 aCos = absolute(Cos(aAngle)), aSin = absolute(Sin(aAngle));
            const f32 bCos = absolute(Cos(bAngle)), bSin = absolute(Sin(bAngle));
            const f32 aHalfX = (aCos * a.scale.x + aSin * a.scale.z) * 0.5f;
            const f32 aHalfZ = (aSin * a.scale.x + aCos * a.scale.z) * 0.5f;
            const f32 bHalfX = (bCos * b.scale.x + bSin * b.scale.z) * 0.5f;
            const f32 bHalfZ = (bSin * b.scale.x + bCos * b.scale.z) * 0.5f;
            return absolute(a.position.x - b.position.x) < aHalfX + bHalfX + 0.06f &&
                   absolute(a.position.z - b.position.z) < aHalfZ + bHalfZ + 0.06f;
        };
        for(u32 attempt = 0u; placedBodyCount < s_ComplexRandomBodyCount && attempt < 4096u; ++attempt){
            const f32 x = -1.70f + 3.40f * nextRandom01();
            const f32 z = -2.70f + 4.45f * nextRandom01();
            const f32 width = 0.18f + 0.26f * nextRandom01();
            const f32 depth = 0.18f + 0.36f * nextRandom01();
            const f32 height = (z < -1.8f ? 0.22f : 0.30f) + (z < -1.8f ? 0.34f : 0.68f) * nextRandom01();
            const f32 shade = 0.62f + 0.32f * nextRandom01();
            const f32 yawDegrees = (static_cast<f32>(static_cast<u32>(nextRandom01() * 7.0f)) - 3.0f) * 15.0f;
            const ComplexSceneBox body{
                Float4(x, height * 0.5f, z, 0.0f), Float4(width, height, depth, 0.0f),
                Float4(shade, shade, shade, 1.0f), yawDegrees
            };
            // Leave the line from the camera through the front doorway clear.
            if(z < -1.8f && absolute(x) < 0.72f)
                continue;
            // Sun through the roof opening must still land on both colored floor sources.
            if(x > -1.12f && x < 1.12f && z > -1.20f && z < 0.18f)
                continue;

            bool blocked = false;
            for(const ComplexSceneBox& box : s_ComplexSceneBoxes)
                blocked |= overlaps(body, box);
            for(const ComplexSceneBox& box : s_ComplexBouncePatches)
                blocked |= overlaps(body, box);
            for(const ComplexSceneBox& box : s_ComplexFrontEnclosureBoxes)
                blocked |= overlaps(body, box);
            for(u32 i = 0u; i < placedBodyCount; ++i)
                blocked |= overlaps(body, placedBodies[i]);
            if(blocked)
                continue;

            const auto entity = CreateTintedStaticMeshEntity(
                *m_world, m_context.objectArena, s_BoxMesh, s_ComplexDiffuseMaterial,
                s_SmokeSurfaceMaterialInterface, body.tint, body.position, body.scale
            );
            if(!entity.valid())
                return false;
            if(body.yawDegrees != 0.0f){
                auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
                if(!transform)
                    return false;
                StoreFloat(QuaternionRotationRollPitchYaw(0.0f, body.yawDegrees * (s_PI / 180.0f), 0.0f), transform->rotation);
            }
            placedBodies[placedBodyCount++] = body;
        }
        if(placedBodyCount != s_ComplexRandomBodyCount){
            NWB_LOGGER_ERROR(NWB_TEXT("GiTestSmokeProject: could place only {} of {} seeded bodies"), placedBodyCount, s_ComplexRandomBodyCount);
            return false;
        }

        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("GiTestSmokeProject: complex scene {} maze solids, {} colored floor patches, {} closed-front shell solids, and {} seeded bodies (seed={}) created"),
            LengthOf(s_ComplexSceneBoxes), LengthOf(s_ComplexBouncePatches), LengthOf(s_ComplexFrontEnclosureBoxes),
            placedBodyCount, s_ComplexRandomBodySeed
        );
        if(m_coverageViewEnabled){
            // Adjacent -X and -Z faces span the same 0.6 m spatial cell near the cube's front-left edge.
            // Their different normals need separate surfels. Keeping this above the short seeded bodies makes
            // both faces visible while the closed-room scene still exercises ordinary GI around the probe.
            const auto probe = CreateTintedStaticMeshEntity(
                *m_world, m_context.objectArena, s_BoxMesh, s_CoverageProbeMaterial,
                s_SmokeSurfaceMaterialInterface, Float4(1.0f, 1.0f, 1.0f, 1.0f),
                Float4(0.95f, 1.48f, -2.65f, 0.0f), Float4(0.60f, 0.30f, 0.30f, 0.0f)
            );
            if(!probe.valid())
                return false;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("GiTestSmokeProject: coverage probe with perpendicular same-cell faces created"));
        }
        return true;
    }

    // The source plane spans -1..+1 in XZ, so wall scales are half-extents.
    NWB::Core::ECS::EntityID createWall(
        const Float4& position,
        const Float4& colorTint,
        const f32 yawDeg,
        const f32 width = s_BoxScale,
        const f32 height = s_BoxHeight
    ){
        const SIMDVector pitchQuat = QuaternionRotationRollPitchYaw(s_PIDIV2, 0.0f, 0.0f);
        const f32 yawRad = yawDeg * (s_PI / 180.0f);
        const SIMDVector yawQuat = QuaternionRotationRollPitchYaw(0.0f, yawRad, 0.0f);
        const SIMDVector wallRotation = QuaternionMultiply(pitchQuat, yawQuat);

        auto entity = CreateTintedStaticMeshEntity(
            *m_world,
            m_context.objectArena,
            s_GroundMesh,
            m_complexSceneEnabled ? s_ComplexDiffuseMaterial : s_OpaqueMaterial,
            s_SmokeSurfaceMaterialInterface,
            colorTint,
            position,
            Float4(width, 1.0f, height, 0.0f)
        );
        if(entity.valid()){
            if(auto* transform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity))
                StoreFloat(wallRotation, transform->rotation);
        }
        return entity;
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Tests::Smoke::CsgGiScene m_csgGiScene;
    NWB::Core::ECS::EntityID m_floorEntity = NWB::Core::ECS::s_InvalidEntityId;
    NWB::Core::ECS::EntityID m_redWallEntity = NWB::Core::ECS::s_InvalidEntityId;
    NWB::Core::ECS::EntityID m_blueWallNegX = NWB::Core::ECS::s_InvalidEntityId;
    NWB::Core::ECS::EntityID m_whiteWallPosZ = NWB::Core::ECS::s_InvalidEntityId;
    NWB::Tests::Smoke::PresentationFpsProbe m_presentationFpsProbe;
    NWB::Tests::Smoke::GpuPassTimingProbe m_gpuPassTimingProbe{ NWB_TEXT("GiTestSmokeProject") };
    u32 m_rendererBaselineRenderedFrameCount = 0u;
    bool m_rendererBaselineCapturePaused = false;
    bool m_resolveSwitchEnabled = false;
    bool m_complexSceneEnabled = false;
    bool m_coverageViewEnabled = false;
    f32 m_complexMinSettleSeconds = 0.0f;
    Timer m_complexSceneStartTime = {};
    bool m_complexSceneTimingStarted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { 1280, 900 };
}


TStringView NWB::QueryProjectWindowTitle(){
    return NWB_TEXT("NWB GI Test");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_gi_test_smoke::GiTestSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

