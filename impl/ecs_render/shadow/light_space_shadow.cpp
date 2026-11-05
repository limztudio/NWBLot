// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_shadow.h"

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <core/graphics/backend_selection/backend.h>

#include <global/hash_utils.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u32> LightSpaceShadowDrawCount(const Span<const LightSpaceShadowCaster> casters)noexcept{
    u64 drawCount = 0u;
    for(const LightSpaceShadowCaster& caster : casters){
        drawCount += LightSpaceCasterDrawCount(caster.meshletCount);
        if(drawCount > Limit<u32>::s_Max)
            return MakeUnexpected(Failure{});
    }
    return static_cast<u32>(drawCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::setSoftwareShadowSettings(const SoftwareShadowSettings& settings)noexcept{
    if(!ValidateSoftwareShadowSettings(settings))
        return false;
    auto& state = m_lightSpaceShadow;
    if(SameSoftwareShadowSettings(state.m_settings, settings))
        return true;
    state.m_settings = settings;
    state.m_captureHistory.invalidate();
    state.m_captureReuseLogged = false;
    state.m_snapshot.ready = false;
    state.m_resourcesPrepared = false;
    state.m_dispatchLogged = false;
    m_rayTracingState.m_softShadowTemporalSeeded = false;
    m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    return true;
}

LightSpaceShadowSnapshot RendererRayTracingSystem::lightSpaceShadowSnapshot()const noexcept{
    LightSpaceShadowSnapshot result = m_lightSpaceShadow.m_snapshot;
    result.casters = m_lightSpaceShadow.m_casters;
    result.csgContextBytes = m_lightSpaceShadow.m_csg.bytes;
    result.csgDynamicBounds = m_lightSpaceShadow.m_csg.dynamicBounds;
    return result;
}

void RendererRayTracingSystem::acceptLightSpaceShadowCapture(const LightSpaceCaptureTicket& ticket, const b8 prepared){
    auto& state = m_lightSpaceShadow;
    if(!prepared || !state.m_captureHistory.accept(ticket))
        return;
    const u32 requiredReuses = ResolveSoftwareShadowRequiredReuses(state.m_settings.captureCadence);
    if(ticket.reuse && !state.m_captureReuseLogged && state.m_captureHistory.acceptedReuseAge() == requiredReuses){
        state.m_captureReuseLogged = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: accepted light-space capture reuse (cadence={})"), static_cast<u32>(state.m_settings.captureCadence) + 1u);
    }
}

void RendererRayTracingSystem::preflightLightSpaceShadowResources(){
    auto& state = m_lightSpaceShadow;
    auto& diagnostics = state.m_diagnostics;
    if(state.m_csg.snapshot.hasCsg || diagnostics.submission.valid()){
        ++diagnostics.frameIndex;
        const Core::QueueSubmissionToken submission = diagnostics.submission;
        if(submission.valid() && submission.hasPhysicalQueueIdentity()){
            auto& device = m_graphics.getDevice();
            const Core::GpuPhysicalQueueId queue{ submission.physicalQueueIndex, submission.deviceGeneration };
            const Expected<u64> completedValue = device.queueGetCompletedInstance(queue);
            if(completedValue && *completedValue >= submission.value){
                const Expected<void*> mappedFailures = device.mapBuffer(*diagnostics.readback, Core::CpuAccessMode::Read);
                if(mappedFailures){
                    const u32 failureFlags = *MakeNotNull(static_cast<const u32*>(*mappedFailures));
                    device.unmapBuffer(*diagnostics.readback);
                    LogNewLightSpaceCsgRayFailures(failureFlags, diagnostics.reportedFailures);
                    diagnostics.submission = {};
                }
            }
        }
    }
    state.m_resourcesPrepared = false;
    state.m_csgRequired = false;
    if(!state.m_csg.snapshot.hasCsg && (m_shadowVisibilityHardwareSupported || state.m_settings.backend == SoftwareShadowBackend::SoftwareTrace || !state.m_sceneEligible))
        return;
    // Use the same scene ordering and shadow-slot allocator as the later prefix. That prefix may only select a fitting generation.
    Array<SceneLightGpuData, NWB_SCENE_MAX_LIGHTS> lights{};
    Array<f32, NWB_SCENE_MAX_LIGHTS> causticImportance{};
    const u32 lightCount = ECSRenderDetail::ResolveSceneLights(m_world, lights, causticImportance);
    const Span<const SceneLightGpuData> activeLights(lights.data(), lightCount);
    state.m_csgRequired = HasAnyLightSpaceCsgActiveLight(state.m_csg.snapshot.hasCsg, activeLights);
    const auto planResult = buildLightSpaceShadowPlan(activeLights);
    if(!planResult)
        return;
    const LightSpacePlan& plan = *planResult;
    state.m_resourcesPrepared = ensureLightSpaceShadowPipelines() && ensureLightSpaceShadowStorage(plan);
}

Expected<LightSpacePlan> RendererRayTracingSystem::buildLightSpaceShadowPlan(const Span<const SceneLightGpuData> lights)const noexcept{
    const auto& state = m_lightSpaceShadow;
    const usize lightCount = lights.size();
    if(
        (!state.m_csg.snapshot.hasCsg && (m_shadowVisibilityHardwareSupported || state.m_settings.backend == SoftwareShadowBackend::SoftwareTrace))
        || !state.m_sceneEligible || !m_shadowVisibilityPreparedTargets || (!m_shadowVisibilityHardwareSupported && !m_preparedSceneSwBvhReady)
        || !m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady
        || state.m_casters.empty() || lightCount > NWB_SCENE_MAX_LIGHTS
    )
        return MakeUnexpected(Failure{});
    const auto drawCount = LightSpaceShadowDrawCount(state.m_casters);
    if(!drawCount)
        return MakeUnexpected(drawCount.error());
    return TryBuildLightSpacePlanForSceneLights(
        state.m_settings,
        lights,
        m_graphics.getDevice().getMaxStorageBufferRange(),
        *drawCount,
        state.m_csg.snapshot.hasCsg,
        state.m_csg.bytes.size()
    );
}

// Graph declaration can change the admitted light list, but never creates or grows its GPU resources.
void RendererRayTracingSystem::prepareLightSpaceShadows(const Span<const SceneLightGpuData> lights){
    auto& state = m_lightSpaceShadow;
    auto& snapshot = state.m_snapshot;
    snapshot.ready = false;
    snapshot.diagnostics = nullptr;
    snapshot.diagnosticReadback = nullptr;
    if(
        !state.m_resourcesPrepared || !snapshot.layout || !snapshot.viewPipeline || !snapshot.cullPipeline || !snapshot.opaqueResolve
        || !snapshot.transparentResolve || !snapshot.opaqueFallback || !snapshot.transparentFallback || !snapshot.shadePipeline
        || !snapshot.opaqueCapture || !snapshot.transparentCapture
        || !snapshot.counts || !snapshot.events || !snapshot.views || !snapshot.drawArguments || !snapshot.depth
    )
        return;
    const auto planResult = buildLightSpaceShadowPlan(lights);
    if(!planResult)
        return;
    const LightSpacePlan& plan = *planResult;
    const auto drawCount = LightSpaceShadowDrawCount(state.m_casters);
    NWB_ASSERT(drawCount);
    const LightSpaceShadowStorageCapacity capacity{
        snapshot.counts->getCreationDescription().byteSize, snapshot.events->getCreationDescription().byteSize,
        snapshot.views->getCreationDescription().byteSize, snapshot.drawArguments->getCreationDescription().byteSize,
        snapshot.depth->getCreationDescription().width,
        snapshot.depth->getCreationDescription().arraySize,
    };
    if(!LightSpaceShadowStorageFits(plan, capacity))
        return;
    for(u32 view = 0u; view < plan.viewCount; ++view){
        if(!snapshot.opaqueFramebuffers[view] || !snapshot.transparentFramebuffers[view])
            return;
    }
    DeferredFrameTargets& targets = *m_shadowVisibilityPreparedTargets;
    const LightSpaceCaptureIdentity identity = BuildLightSpaceCaptureIdentity(
        state.m_captureSceneIdentity,
        state.m_captureSceneTrusted,
        lights,
        targets.width,
        targets.height,
        plan
    );
    snapshot.captureTicket = state.m_captureHistory.prepare(identity, state.m_settings.captureCadence);
    snapshot.captureHistory = &state.m_captureHistory;
    // Reuse retains the fitted views, CSG context and converted crossings together; current templates must not overwrite them.
    if(!snapshot.captureTicket.reuse){
        snapshot.plan = plan;
        // Keep hashed resource objects alive until their map generation is replaced; pointer identities cannot alias replacements.
        state.m_captureBuffers.assign(state.m_sceneBuffers.begin(), state.m_sceneBuffers.end());
        state.m_captureTextures.assign(state.m_sceneTextures.begin(), state.m_sceneTextures.end());
    }
    b8 hasOrdinaryTransparent = false;
    if(state.m_csg.snapshot.hasCsg){
        for(const LightSpaceShadowCaster& caster : state.m_casters)
            hasOrdinaryTransparent = hasOrdinaryTransparent || (caster.transparent && !caster.csg);
    }
    snapshot.csgTlasDescriptor = m_shadowVisibilityHardwareSupported ? m_rayTracingState.m_tlasHeapHandle : Core::GpuDescriptorHandle::Invalid();
    PopulatePreparedLightSpaceShadowPush(
        snapshot.viewsDescriptor.slot(),
        m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
        snapshot.countsDescriptor.slot(),
        snapshot.eventsDescriptor.slot(),
        snapshot.depthDescriptor.slot(),
        static_cast<u32>(state.m_casters.size()),
        *drawCount,
        targets.width,
        targets.shadowReceiverFactor,
        targets.height,
        targets.bindless.slotsBufferDescriptor.slot(),
        m_shadowVisibilityHardwareSupported ? 0u : m_preparedSceneBvh.nodeHeapHandle().slot(),
        state.m_csg.snapshot.hasCsg,
        m_shadowVisibilityHardwareSupported,
        hasOrdinaryTransparent,
        snapshot.csgContextDescriptor.slot(),
        snapshot.csgOpaqueDepthDescriptor.slot(),
        plan.viewCount,
        snapshot.drawArgumentsDescriptor.slot(),
        snapshot.push
    );
    auto& diagnostics = state.m_diagnostics;
    snapshot.diagnostics = state.m_csg.snapshot.hasCsg ? &diagnostics : nullptr;
    if(state.m_csg.snapshot.hasCsg && diagnostics.readback && !diagnostics.submission.valid() && (diagnostics.acceptedFrame == 0u || diagnostics.frameIndex - diagnostics.acceptedFrame >= s_LightSpaceDiagnosticInterval)){
        snapshot.diagnosticReadback = diagnostics.readback;
        snapshot.diagnosticFrame = diagnostics.frameIndex;
    }
    snapshot.casters = state.m_casters;
    snapshot.ready = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

