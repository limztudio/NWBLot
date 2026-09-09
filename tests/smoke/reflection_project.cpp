// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/ecs/entity.h>
#include <core/graphics/runtime/runtime.h>
#include <global/math/constant.h>
#include <global/math/convert.h>
#include <global/math/frame.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_scene/module.h>

#include "framebuffer_capture.h"
#include "fps_probe.h"
#include "gpu_pass_timing_probe.h"
#include "reflection_feedback_scene.h"
#include "reflection_optical_scene.h"
#include "reflection_roughness_scene.h"
#include "smoke_environment.h"
#include "smoke_project_helpers.h"
#include "smoke_scene_helpers.h"
#include "smoke_skinned_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Tests::Smoke;

static constexpr SmokeMeshRef s_PlaneMesh{"project/meshes/shadow_plane"};
static constexpr SmokeMeshRef s_SphereMesh{"project/meshes/caustic_sphere"};
static constexpr SmokeMaterialRef s_OpaqueMaterial{"project/smoke/reflection/materials/opaque"};
static constexpr SmokeMaterialRef s_GlassMaterial{"project/smoke/reflection/materials/glass"};
static constexpr SmokeMaterialRef s_TransparentMaterial{"project/smoke/refraction/materials/transparent"};
static constexpr AStringView s_MaterialInterface = "project/shaders/smoke_surface";
static constexpr f32 s_GlassIor = 3.8f;
static constexpr f32 s_GlassF0 = ((s_GlassIor - 1.0f) * (s_GlassIor - 1.0f)) / ((s_GlassIor + 1.0f) * (s_GlassIor + 1.0f));

static NWB::Impl::RendererSystem& CreateReflectionRenderer(NWB::Core::ECS::World& world, NWB::ProjectRuntimeContext& context){
    SmokeEnvironmentString caseText(context.objectArena);
    const bool hasCase = ReadSmokeEnvironmentText("NWB_REFLECTION_SMOKE_CASE", caseText);
    const AStringView caseName(caseText.data(), caseText.size());
    if(hasCase && (caseName == "temporal_deform" || caseName == "rough_deform")){
        AddSmokeSkinnedRenderSystems(world, context);
        auto* renderer = world.getSystem<NWB::Impl::RendererSystem>();
        NWB_FATAL_ASSERT(renderer);
        return *renderer;
    }
    return AddSmokeRenderSystems(world, context);
}

class ReflectionSmokeProject final : public NWB::IProjectEntryCallbacks{
public:
    explicit ReflectionSmokeProject(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateSmokeWorldOrDie(context, NWB_TEXT("ReflectionSmokeProject")))
        , m_renderer(CreateReflectionRenderer(*m_world, context)){}
    virtual ~ReflectionSmokeProject()override{ destroyWorld(); }

    virtual bool onStartup()override{
        SmokeEnvironmentString caseText(m_context.objectArena);
        const bool hasCase = ReadSmokeEnvironmentText("NWB_REFLECTION_SMOKE_CASE", caseText);
        const AStringView caseName = hasCase ? AStringView(caseText.data(), caseText.size()) : AStringView("offscreen");
        m_feedbackCase = caseName.starts_with("feedback_");
        m_opticalCase = caseName.starts_with("optical_");
        m_extendedCase = m_feedbackCase || m_opticalCase || caseName.starts_with("rough") || caseName.starts_with("temporal_");
        m_opticalTir = caseName == "optical_tir";
        m_furnace = caseName == "rough_furnace";
        m_glassRoughnessCase = caseName == "rough_glass";
        if(!configureRenderer())
            return false;
        const bool screenCase = caseName == "onscreen" || caseName == "onscreen_moved" || caseName == "boundary" || caseName == "floor";
        if(caseName != "offscreen" && caseName != "moved" && caseName != "opaque_glass" && !screenCase && !m_extendedCase){
            NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: unknown case '{}'"), StringConvert(caseName));
            return false;
        }
        const auto cameraId = CreateSmokeCamera(*m_world, 1.4f, 6.0f, 0.0f);
        auto& camera = m_world->entity(cameraId).getComponent<NWB::Impl::Scene::CameraComponent>();
        camera.setVerticalFovRadians(s_PI / 3.0f);
        const auto extent = NWB::QueryProjectFrameClientSize();
        camera.setAspectRatio(static_cast<f32>(extent.width) / static_cast<f32>(extent.height));
        const auto light = NWB::Impl::Scene::CreateDirectionalLightEntity(
            *m_world, 0.6f, 0.4f, 0.0f, Float4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f
        );
        NWB_FATAL_ASSERT_MSG(cameraId.valid() && light.valid(), NWB_TEXT("ReflectionSmokeProject: camera/light creation failed"));

        if(m_feedbackCase){
            m_feedbackScene = MakeUnique<ReflectionFeedbackScene>(m_context, *m_world);
            if(!m_feedbackScene->create(caseName, m_freshFinalState))
                return false;
        }
        else if(m_opticalCase){
            if(!CreateReflectionOpticalScene(m_context, *m_world, caseName))
                return false;
        }
        else if(m_extendedCase && !m_glassRoughnessCase){
            m_roughnessScene = MakeUnique<ReflectionRoughnessScene>(m_context, *m_world, cameraId, light, m_authoredRoughness);
            if(!m_roughnessScene->create(caseName, m_freshFinalState))
                return false;
            if(
                m_roughnessScene->mutationCase()
                && !m_roughnessScene->deforming()
                && m_reflectionSettings.temporalEnabled
                && (m_targetSamples < 32u || m_postResetSamples > m_targetSamples)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: mutation history cap must cover warm-up and post-reset samples"));
                return false;
            }
        }
        else if(caseName == "opaque_glass" || m_glassRoughnessCase)
            createOpaqueGlassScene();
        else if(caseName == "floor")
            createFloorMirrorScene();
        else if(screenCase)
            createOnscreenMirrorScene(caseName == "onscreen_moved", caseName == "boundary");
        else
            createMirrorScene(caseName == "moved");
        if(!configureFramebufferCapture())
            return false;
        if(ReadSmokeEnvironmentFlag("NWB_REFLECTION_SMOKE_TIMING"))
            m_context.setPerfCapture(NWB::Core::Perf::CaptureOptions::GpuTimingOnly());
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: case {} created"), StringConvert(caseName));
        return true;
    }

    virtual void onShutdown()override{
        destroyWorld();
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: shutdown"));
    }

    virtual bool onUpdate(const f32 delta)override{
        if(m_framebufferCapture)
            m_framebufferCapture->update();
        updateRoughnessScene();
        updateFeedbackScene();
        m_fpsProbe.recordFrame(delta);
        m_gpuPassTimingProbe.recordFrame(delta, m_context.gpuTimingView());
        const f32 fixedDelta = RendererBaselineFixedDelta();
        m_world->tick(fixedDelta > 0.0f ? fixedDelta : delta);
        reportReflectionStatistics();
        return true;
    }


private:
    bool readFlag(const char* name, bool& value){
        SmokeEnvironmentString text(m_context.objectArena);
        if(!ReadSmokeEnvironmentText(name, text))
            return true;
        if(text == "0")
            value = false;
        else if(text == "1")
            value = true;
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: {} must be 0 or 1"), StringConvert(name));
            return false;
        }
        return true;
    }

    bool readU32(const char* name, u32& value, const u32 minimum, const u32 maximum){
        SmokeEnvironmentString text(m_context.objectArena);
        if(!ReadSmokeEnvironmentText(name, text))
            return true;
        u64 parsed = 0u;
        if(!ParseU64(AStringView(text.data(), text.size()), parsed) || parsed < minimum || parsed > maximum){
            NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: {} must be in [{}, {}]"), StringConvert(name), minimum, maximum);
            return false;
        }
        value = static_cast<u32>(parsed);
        return true;
    }

    void updateRoughnessScene(){
        if(!m_roughnessScene || !m_roughnessScene->mutationCase() || m_mutationApplied || m_latestStatistics.sequence == 0u)
            return;
        const bool warmed = m_roughnessScene->deforming() || !m_reflectionSettings.temporalEnabled
            ? m_latestStatistics.frameIndex >= 32u : m_latestStatistics.historySampleCount >= 32u;
        if(!warmed)
            return;
        m_mutationGraphicsFrame = m_context.graphics.getFrameIndex();
        if(m_freshFinalState){
            m_reflectionSettings.samplingSeed = m_requestedSeed;
            NWB_FATAL_ASSERT_MSG(m_renderer.setReflectionSettings(m_reflectionSettings), NWB_TEXT("ReflectionSmokeProject: fresh seed reset failed"));
        }
        else
            NWB_FATAL_ASSERT_MSG(m_roughnessScene->applyMutation(), NWB_TEXT("ReflectionSmokeProject: scene mutation failed"));
        m_mutationApplied = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeMutation: graphics_frame={} fresh_final={} seed={}")
            , m_mutationGraphicsFrame, m_freshFinalState ? 1u : 0u, m_requestedSeed
        );
    }

    void updateFeedbackScene(){
        if(!m_feedbackScene || !m_feedbackScene->mutationCase() || m_mutationApplied)
            return;
        if(m_latestStatistics.feedbackSequence < m_targetSamples || (!m_freshFinalState && !m_feedbackObservedBypass))
            return;
        m_mutationGraphicsFrame = m_context.graphics.getFrameIndex();
        if(m_freshFinalState){
            m_reflectionSettings.samplingSeed = m_requestedSeed;
            NWB_FATAL_ASSERT_MSG(m_renderer.setReflectionSettings(m_reflectionSettings), NWB_TEXT("ReflectionSmokeProject: fresh feedback reset failed"));
        }
        else
            NWB_FATAL_ASSERT_MSG(m_feedbackScene->applyMutation(), NWB_TEXT("ReflectionSmokeProject: feedback mutation failed"));
        m_mutationApplied = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeFeedbackMutation: graphics_frame={} fresh_final={}")
            , m_mutationGraphicsFrame, m_freshFinalState ? 1u : 0u
        );
    }

    bool shouldCapture(const u64 graphicsFrame)const{
        if(m_feedbackScene && m_feedbackScene->mutationCase())
            return m_mutationApplied && graphicsFrame == m_mutationGraphicsFrame;
        if(m_feedbackCapture)
            return m_latestStatistics.feedbackSequence >= m_targetSamples;
        if(m_roughnessScene && m_roughnessScene->mutationCase()){
            if(!m_mutationApplied)
                return false;
            if(m_postResetSamples == 1u)
                return graphicsFrame == m_mutationGraphicsFrame;
            return
                m_latestStatistics.historyStartGraphicsFrame == m_mutationGraphicsFrame
                && m_latestStatistics.historySampleCount >= m_postResetSamples
            ;
        }
        return m_reflectionSettings.temporalEnabled && !(m_roughnessScene && m_roughnessScene->deforming())
            ? m_latestStatistics.historySampleCount >= m_targetSamples
            : m_latestStatistics.frameIndex >= Max(m_targetSamples, 3u)
        ;
    }

    bool configureRenderer(){
        auto& settings = m_reflectionSettings;
        settings.traceMode = NWB::Impl::ReflectionTraceMode::Hardware;
        settings.environmentTop = Float3U(0.0f, 0.0f, 0.0f);
        settings.environmentBottom = Float3U(0.0f, 0.0f, 0.0f);
        settings.maxHardwareRaysPerFrame = 2u * 960u * 720u;
        settings.diagnosticsEnabled = true;
        settings.temporalEnabled = m_extendedCase && !m_opticalCase && !m_feedbackCase;
        settings.spatialFilterEnabled = false;
        m_targetSamples = m_extendedCase && !m_opticalCase ? 64u : 16u;
        if(
            !readFlag("NWB_REFLECTION_SMOKE_DIAGNOSTICS", settings.diagnosticsEnabled)
            || !readFlag("NWB_REFLECTION_SMOKE_FEEDBACK", settings.screenFeedbackEnabled)
            || !readFlag("NWB_REFLECTION_SMOKE_FEEDBACK_CAPTURE", m_feedbackCapture)
            || !readFlag("NWB_REFLECTION_SMOKE_TEMPORAL", settings.temporalEnabled)
            || !readFlag("NWB_REFLECTION_SMOKE_SPATIAL", settings.spatialFilterEnabled)
            || !readFlag("NWB_REFLECTION_SMOKE_FINAL_STATE", m_freshFinalState)
            || !readU32("NWB_REFLECTION_SMOKE_HISTORY_SAMPLES", m_targetSamples, 1u, 256u)
            || !readU32("NWB_REFLECTION_SMOKE_POST_RESET_SAMPLES", m_postResetSamples, 1u, 256u)
            || !readU32("NWB_REFLECTION_SMOKE_SEED", m_requestedSeed, 0u, Limit<u32>::s_Max)
            || !readU32("NWB_REFLECTION_SMOKE_OPTICAL_QUERIES", settings.maxOpticalQueries, 1u, 16u)
            || !readU32("NWB_REFLECTION_SMOKE_SCREEN_STEPS", settings.screenMaxSteps, 1u, 256u)
        )
            return false;
        settings.temporalMaxSamples = m_targetSamples;
        settings.samplingSeed = m_freshFinalState ? m_requestedSeed + 1u : m_requestedSeed;
        if(m_extendedCase && !m_feedbackCase){
            m_authoredRoughness = 0.4f;
            SmokeEnvironmentString roughnessText(m_context.objectArena);
            if(
                ReadSmokeEnvironmentText("NWB_REFLECTION_SMOKE_ROUGHNESS", roughnessText)
                && (!ParseF32FromChars(roughnessText.data(), roughnessText.data() + roughnessText.size(), m_authoredRoughness)
                    || !IsFinite(m_authoredRoughness) || m_authoredRoughness < 0.f || m_authoredRoughness > 1.f)
            )
                return false;
            NWB::Impl::PresentationSettings presentation;
            presentation.shoulder = 1.f;
            if(!m_renderer.setPresentationSettings(presentation))
                return false;
        }
        if(m_furnace || m_opticalTir){
            settings.environmentTop = Float3U(1.f, 1.f, 1.f);
            settings.environmentBottom = Float3U(1.f, 1.f, 1.f);
            settings.roughnessCutoff = 1.f;
        }
        SmokeEnvironmentString budgetText(m_context.objectArena);
        if(ReadSmokeEnvironmentText("NWB_REFLECTION_SMOKE_RAY_BUDGET", budgetText)){
            u64 parsed = 0u;
            if(!ParseU64(AStringView(budgetText.data(), budgetText.size()), parsed) || parsed > Limit<u32>::s_Max){
                NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: ray budget must be a nonnegative u32"));
                return false;
            }
            settings.maxHardwareRaysPerFrame = static_cast<u32>(parsed);
        }
        SmokeEnvironmentString routeText(m_context.objectArena);
        const bool hasRoute = ReadSmokeEnvironmentText("NWB_REFLECTION_SMOKE_MODE", routeText);
        const AStringView route = hasRoute ? AStringView(routeText.data(), routeText.size()) : AStringView("hardware");
        if(route == "disabled")
            settings.traceMode = NWB::Impl::ReflectionTraceMode::Disabled;
        else if(route == "screen")
            settings.traceMode = NWB::Impl::ReflectionTraceMode::ScreenSpace;
        else if(route == "hybrid")
            settings.traceMode = NWB::Impl::ReflectionTraceMode::Hybrid;
        else if(route != "hardware"){
            NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: unknown reflection mode '{}'"), StringConvert(route));
            return false;
        }
        SmokeEnvironmentString debugText(m_context.objectArena);
        if(ReadSmokeEnvironmentText("NWB_REFLECTION_SMOKE_DEBUG", debugText)){
            const AStringView debugView(debugText.data(), debugText.size());
            if(debugView == "source")
                settings.debugView = NWB::Impl::ReflectionDebugView::TraceSource;
            else if(debugView == "confidence")
                settings.debugView = NWB::Impl::ReflectionDebugView::Confidence;
            else if(debugView != "none"){
                NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: unknown debug view '{}'"), StringConvert(debugView));
                return false;
            }
        }
        if(!m_renderer.setReflectionSettings(settings))
            return false;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: screen feedback {}"), settings.screenFeedbackEnabled ? 1u : 0u);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: screen steps {}"), settings.screenMaxSteps);
        m_renderer.setRefractionEnabled(true);
        m_renderer.setRefractionHardwareTracingEnabled(true);
        const bool hardwareAvailable = m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct)
            && m_context.graphics.queryFeatureSupport(NWB::Core::Feature::RayQuery);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: hardware {}")
            , hardwareAvailable ? NWB_TEXT("available") : NWB_TEXT("unavailable")
        );
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: reflection mode {}"), StringConvert(route));
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: hardware ray budget {}"), settings.maxHardwareRaysPerFrame);
        if(m_opticalCase)
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeProject: optical query limit {}"), settings.maxOpticalQueries);
        return true;
    }

    void reportReflectionStatistics(){
        NWB::Impl::ReflectionStatistics statistics;
        if(!m_renderer.tryGetLatestReflectionStatistics(statistics))
            return;
        if(statistics.sequence == m_statisticsSequence && statistics.generation == m_statisticsGeneration)
            return;
        m_statisticsSequence = statistics.sequence;
        m_statisticsGeneration = statistics.generation;
        m_latestStatistics = statistics;
        m_feedbackObservedBypass = m_feedbackObservedBypass || statistics.feedbackBypassedPixels > 0u;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeStatistics: sequence={} generation={} frame={} mode={} width={} height={}")
            NWB_TEXT(" requested_budget={} effective_budget={} queue_capacity={} hardware_requested={} hardware_available={} hardware_ready={}")
            NWB_TEXT(" token_queue={} token_value={} physical_queue={} device_generation={}")
            NWB_TEXT(" candidates={} hardware_rays={} hardware_hits={} opaque_pixels={} glass_pixels={} fallback_pixels={}")
            NWB_TEXT(" screen_attempts={} screen_hits={}")
            , statistics.sequence, statistics.generation, statistics.frameIndex, static_cast<u32>(statistics.traceMode)
            , statistics.width, statistics.height, statistics.requestedHardwareBudget, statistics.effectiveHardwareBudget
            , statistics.queueCapacity, statistics.hardwareRequested ? 1u : 0u, statistics.hardwareAvailable ? 1u : 0u
            , statistics.hardwareReady ? 1u : 0u, static_cast<u32>(statistics.acceptedToken.queue), statistics.acceptedToken.value
            , statistics.acceptedToken.physicalQueueIndex, statistics.acceptedToken.deviceGeneration
            , statistics.candidates, statistics.hardwareRays, statistics.hardwareHits, statistics.opaquePixels, statistics.glassPixels
            , statistics.fallbackPixels, statistics.screenAttempts, statistics.screenHits
        );
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeHistory: sequence={} generation={} graphics_frame={} epoch={}")
            NWB_TEXT(" start_graphics_frame={} count={} sample_index={} seed={} eligible={} reused={} reset={} reason={}")
            , statistics.sequence, statistics.generation, statistics.graphicsFrameIndex, statistics.historyEpoch
            , statistics.historyStartGraphicsFrame, statistics.historySampleCount, statistics.sampleIndex, statistics.samplingSeed
            , statistics.historyEligible ? 1u : 0u, statistics.historyReused ? 1u : 0u, statistics.historyReset ? 1u : 0u
            , static_cast<u32>(statistics.historyResetReason)
        );
        {
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeOptics: sequence={} generation={} max_queries={} hardware_queries={}")
                NWB_TEXT(" bootstrap_events={} transparent_paths={} unsupported_paths={} limited_paths={}")
                NWB_TEXT(" ambiguous_paths={} tir_events={} medium_overflow_paths={} transport_enabled={}")
                , statistics.sequence, statistics.generation, statistics.maxOpticalQueries, statistics.hardwareQueries
                , statistics.bootstrapEvents, statistics.transparentPaths, statistics.unsupportedPaths, statistics.limitedPaths
                , statistics.ambiguousPaths, statistics.tirEvents, statistics.mediumOverflowPaths
                , statistics.opticalTransportEnabled ? 1u : 0u
            );
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSmokeFeedback: sequence={} generation={} graphics_frame={}")
            NWB_TEXT(" feedback_sequence={} epoch={} start_graphics_frame={} probe_index={}")
            NWB_TEXT(" requested={} enabled={} reused={} reset={} reason={} scheduling_valid={}")
            NWB_TEXT(" potential_receivers={} screen_returns={} bypassed_pixels={} probe_tiles={}")
            NWB_TEXT(" screen_iterations={} screen_limit_misses={}")
            , statistics.sequence, statistics.generation, statistics.graphicsFrameIndex
            , statistics.feedbackSequence, statistics.feedbackEpoch, statistics.feedbackStartGraphicsFrame, statistics.feedbackProbeIndex
            , statistics.feedbackRequested ? 1u : 0u, statistics.feedbackEnabled ? 1u : 0u, statistics.feedbackReused ? 1u : 0u
            , statistics.feedbackReset ? 1u : 0u, static_cast<u32>(statistics.feedbackResetReason), statistics.schedulingCounterValid ? 1u : 0u
            , statistics.potentialReceivers, statistics.screenReturns, statistics.feedbackBypassedPixels, statistics.feedbackProbeTiles
            , statistics.screenIterations, statistics.screenLimitMisses
        );
        if(
            (m_extendedCase || m_feedbackCapture) && m_framebufferCapture && m_framebufferCapture->captureReady()
            && statistics.graphicsFrameIndex >= m_framebufferCapture->capturedGraphicsFrameIndex()
        )
            m_framebufferCapture->finish();
    }

    bool configureFramebufferCapture(){
        SmokeEnvironmentString outputPath(m_context.objectArena);
        if(!ReadSmokeEnvironmentText("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", outputPath))
            return true;
        u32 frameCount = 16u;
        SmokeEnvironmentString frameText(m_context.objectArena);
        if(ReadSmokeEnvironmentText("NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT", frameText)){
            u64 parsed = 0u;
            if(!ParseU64(AStringView(frameText.data(), frameText.size()), parsed) || parsed == 0u || parsed > Limit<u32>::s_Max){
                NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: capture frame count must be a positive u32"));
                return false;
            }
            frameCount = static_cast<u32>(parsed);
        }
        FramebufferCaptureOptions options;
        if(m_extendedCase || m_feedbackCapture){
            if(!m_reflectionSettings.diagnosticsEnabled){
                NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSmokeProject: controlled completed-state capture requires diagnostics"));
                return false;
            }
            options.quitWhenReady = false;
            options.predicateContext = this;
            options.shouldCapture = [](void* owner, const u64 graphicsFrame){
                return static_cast<ReflectionSmokeProject*>(owner)->shouldCapture(graphicsFrame);
            };
        }
        auto capture = MakeUnique<FramebufferCapture>(m_context, AStringView(outputPath.data(), outputPath.size()), frameCount, options);
        if(!capture || !capture->start())
            return false;
        m_framebufferCapture = Move(capture);
        return true;
    }

    void destroyWorld(){
        if(m_world.owner() && m_world->getSystem<NWB::Impl::MeshSkinningSystem>())
            DestroySmokeSkinnedRenderWorld(m_context, m_world);
        else
            DestroySmokeRenderWorld(m_context, m_world);
        if(m_framebufferCapture){
            m_framebufferCapture->stop();
            m_framebufferCapture.reset();
        }
    }

    NWB::Core::ECS::EntityID createMesh(const SmokeMeshRef& mesh, const SmokeMaterialRef& material,
        const Float4& color, const Float4& position, const Float4& scale, const f32 specularF0 = 0.0f){
        const auto entity = CreateTintedStaticMeshEntity(
            *m_world, m_context.objectArena, mesh, material, s_MaterialInterface, color, position, scale
        );
        NWB_FATAL_ASSERT_MSG(entity.valid(), NWB_TEXT("ReflectionSmokeProject: mesh creation failed"));
        const Name materialInterface(s_MaterialInterface);
        const Half4U f0 = MakeHalf4U(specularF0, specularF0, specularF0, 0.0f);
        const Half roughness = ConvertFloatToHalf(m_glassRoughnessCase && specularF0 == s_GlassF0 ? m_authoredRoughness : 0.f);
        NWB_FATAL_ASSERT_MSG(NWB::Impl::SetMaterialMutableParameter(
            *m_world, entity, materialInterface, "runtime.specular_f0", NWB::Impl::MaterialLayoutFieldType::Half3,
            NWB::Impl::PackMaterialInstanceBytes(f0.raw, sizeof(Half) * 3u)
        ), NWB_TEXT("ReflectionSmokeProject: F0 override failed"));
        NWB_FATAL_ASSERT_MSG(NWB::Impl::SetMaterialMutableParameter(
            *m_world, entity, materialInterface, "runtime.perceptual_roughness", NWB::Impl::MaterialLayoutFieldType::Half,
            NWB::Impl::PackMaterialInstanceBytes(&roughness, sizeof(roughness))
        ), NWB_TEXT("ReflectionSmokeProject: roughness override failed"));
        return entity;
    }

    void createPanel(const SmokeMaterialRef& material, const Float4& color, const Float4& position,
        const Float4& scale, const f32 specularF0 = 0.0f){
        const auto entity = createMesh(s_PlaneMesh, material, color, position, scale, specularF0);
        auto& transform = m_world->entity(entity).getComponent<NWB::Impl::Scene::TransformComponent>();
        StoreFloat(QuaternionRotationRollPitchYaw(-s_PIDIV2, 0.0f, 0.0f), transform.rotation);
    }

    void createMirrorBackdrop(){
        createPanel(s_OpaqueMaterial, Float4(0.12f, 0.12f, 0.12f, 1.0f),
            Float4(0.0f, 1.4f, 2.0f, 0.0f), Float4(6.0f, 1.0f, 4.5f, 0.0f));
        createPanel(s_OpaqueMaterial, Float4(0.008f, 0.008f, 0.008f, 1.0f),
            Float4(0.0f, 1.4f, 0.0f, 0.0f), Float4(2.8f, 1.0f, 1.8f, 0.0f), 0.95f);
    }

    void createMirrorScene(const bool moved){
        createMirrorBackdrop();
        const f32 shift = moved ? 0.9f : 0.0f;
        // Both marker spheres are behind the camera. Reflecting them through z=0 gives virtual images at z=+8.
        const auto red = createMesh(s_SphereMesh, s_OpaqueMaterial, Float4(1.0f, 0.01f, 0.01f, 1.0f),
            Float4(-1.6f + shift, 1.0f, -8.0f, 0.0f), Float4(0.65f, 0.65f, 0.65f, 0.0f));
        const auto green = createMesh(s_SphereMesh, s_OpaqueMaterial, Float4(0.01f, 1.0f, 0.01f, 1.0f),
            Float4(1.6f + shift, 2.0f, -8.0f, 0.0f), Float4(0.65f, 0.65f, 0.65f, 0.0f));
        NWB_FATAL_ASSERT_MSG(red.valid() && green.valid(), NWB_TEXT("ReflectionSmokeProject: offscreen markers failed"));
    }

    void createOnscreenMirrorScene(const bool moved, const bool boundary){
        createMirrorBackdrop();
        const f32 redX = moved ? -1.45f : -1.7f;
        const f32 greenX = boundary ? 3.0f : (moved ? 1.45f : 1.7f);
        // Rays approach these two-sided markers from the back. Screen hits are provisional; hybrid must resolve the ambiguity.
        // Direct images are three times larger than their virtual images at z=+3, and occupy separate screen regions.
        createPanel(s_OpaqueMaterial, Float4(1.0f, 0.01f, 0.01f, 1.0f),
            Float4(redX, 1.0f, -3.0f, 0.0f), Float4(0.3f, 1.0f, 0.25f, 0.0f));
        createPanel(s_OpaqueMaterial, Float4(0.01f, 1.0f, 0.01f, 1.0f),
            Float4(greenX, 2.0f, -3.0f, 0.0f), Float4(0.3f, 1.0f, 0.35f, 0.0f));
    }

    void createFloorMirrorScene(){
        createPanel(s_OpaqueMaterial, Float4(0.12f, 0.12f, 0.12f, 1.0f),
            Float4(0.0f, 1.4f, 5.0f, 0.0f), Float4(6.0f, 1.0f, 4.5f, 0.0f));
        const auto floor = createMesh(s_PlaneMesh, s_OpaqueMaterial, Float4(0.008f, 0.008f, 0.008f, 1.0f),
            Float4(0.0f, 0.0f, 0.0f, 0.0f), Float4(4.0f, 1.0f, 4.0f, 0.0f), 0.95f);
        NWB_FATAL_ASSERT_MSG(floor.valid(), NWB_TEXT("ReflectionSmokeProject: floor mirror creation failed"));
        // Camera and reflected rays both approach the marker fronts. Reflection through y=0 moves their virtual centers to y=-2.
        createPanel(s_OpaqueMaterial, Float4(1.0f, 0.01f, 0.01f, 1.0f),
            Float4(-1.7f, 2.0f, 3.0f, 0.0f), Float4(0.3f, 1.0f, 0.25f, 0.0f));
        createPanel(s_OpaqueMaterial, Float4(0.01f, 1.0f, 0.01f, 1.0f),
            Float4(1.7f, 2.0f, 3.0f, 0.0f), Float4(0.3f, 1.0f, 0.35f, 0.0f));
    }

    void createOpaqueGlassScene(){
        for(u32 stripe = 0u; stripe < 24u; ++stripe){
            const f32 value = (stripe & 1u) != 0u ? 0.55f : 0.015f;
            createPanel(s_OpaqueMaterial, Float4(value, value, value, 1.0f),
                Float4(-3.45f + static_cast<f32>(stripe) * 0.3f, 1.4f, 2.8f, 0.0f),
                Float4(0.15f, 1.0f, 3.0f, 0.0f));
        }
        const auto opaque = createMesh(s_SphereMesh, s_OpaqueMaterial, Float4(0.008f, 0.008f, 0.008f, 1.0f),
            Float4(-1.3f, 1.5f, 0.0f, 0.0f), Float4(0.95f, 0.95f, 0.95f, 0.0f), 0.95f);
        const auto glass = createMesh(s_SphereMesh, s_GlassMaterial, Float4(1.0f, 1.0f, 1.0f, 0.0f),
            Float4(1.3f, 1.5f, 0.0f, 0.0f), Float4(0.95f, 0.95f, 0.95f, 0.0f), s_GlassF0);
        NWB_FATAL_ASSERT_MSG(opaque.valid() && glass.valid(), NWB_TEXT("ReflectionSmokeProject: opaque/glass spheres failed"));
        m_world->entity(glass).getComponent<NWB::Impl::RendererComponent>().opticalBoundaryMode = NWB::Impl::OpticalBoundaryMode::ClosedNested;
        createPanel(s_OpaqueMaterial, Float4(1.0f, 0.01f, 0.01f, 1.0f),
            Float4(-4.0f, 1.4f, -8.0f, 0.0f), Float4(4.0f, 1.0f, 4.0f, 0.0f));
        createPanel(s_OpaqueMaterial, Float4(0.01f, 1.0f, 0.01f, 1.0f),
            Float4(4.0f, 1.4f, -8.0f, 0.0f), Float4(4.0f, 1.0f, 4.0f, 0.0f));
        createPanel(s_TransparentMaterial, Float4(0.015f, 0.025f, 1.0f, 0.90f),
            Float4(0.0f, 0.85f, -1.7f, 0.0f), Float4(2.0f, 1.0f, 0.08f, 0.0f));
    }


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Impl::RendererSystem& m_renderer;
    UniquePtr<FramebufferCapture> m_framebufferCapture;
    FpsProbe m_fpsProbe{ NWB_TEXT("ReflectionSmokeProject") };
    GpuPassTimingProbe m_gpuPassTimingProbe{ NWB_TEXT("ReflectionSmokeProject") };
    u64 m_statisticsSequence = 0u;
    u64 m_statisticsGeneration = 0u;
    NWB::Impl::ReflectionSettings m_reflectionSettings;
    NWB::Impl::ReflectionStatistics m_latestStatistics;
    UniquePtr<ReflectionRoughnessScene> m_roughnessScene;
    UniquePtr<ReflectionFeedbackScene> m_feedbackScene;
    u64 m_mutationGraphicsFrame = Limit<u64>::s_Max;
    u32 m_targetSamples = 16u;
    u32 m_postResetSamples = 1u;
    u32 m_requestedSeed = 0u;
    f32 m_authoredRoughness = 0.f;
    bool m_extendedCase = false;
    bool m_feedbackCase = false;
    bool m_feedbackCapture = false;
    bool m_feedbackObservedBypass = false;
    bool m_opticalCase = false;
    bool m_opticalTir = false;
    bool m_furnace = false;
    bool m_glassRoughnessCase = false;
    bool m_freshFinalState = false;
    bool m_mutationApplied = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    Core::Alloc::GlobalArena arena(Tests::Smoke::s_SmokeEnvironmentArena);
    Tests::Smoke::SmokeEnvironmentString extent(arena);
    if(!Tests::Smoke::ReadSmokeEnvironmentText("NWB_REFLECTION_SMOKE_EXTENT", extent) || extent == "native")
        return { 960, 720 };
    NWB_FATAL_ASSERT_MSG(extent == "npot", NWB_TEXT("ReflectionSmokeProject: extent must be native or npot"));
    return { 953, 713 };
}
const tchar* NWB::QueryProjectWindowTitle(){ return NWB_TEXT("NWB Reflection Smoke"); }
UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<__hidden_reflection_smoke::ReflectionSmokeProject>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

