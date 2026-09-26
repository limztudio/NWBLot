// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_shadow.h"

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <core/graphics/vulkan/backend.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LightSpaceShadowStorageFits(const LightSpacePlan& plan, const LightSpaceShadowStorageCapacity& capacity)noexcept{
    return
        plan.viewCount != 0u && plan.textureResolution != 0u
        && plan.countByteSize != 0u && plan.eventByteSize != 0u && plan.viewByteSize != 0u && plan.drawArgumentByteSize != 0u
        && plan.countByteSize <= capacity.countBytes && plan.eventByteSize <= capacity.eventBytes
        && plan.viewByteSize <= capacity.viewBytes && plan.drawArgumentByteSize <= capacity.drawArgumentBytes
        && plan.textureResolution <= capacity.textureResolution
        && plan.viewCount <= capacity.arrayLayers
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::setSoftwareShadowSettings(const SoftwareShadowSettings& settings){
    if(!ValidateSoftwareShadowSettings(settings))
        return false;
    auto& state = m_lightSpaceShadow;
    if(
        state.m_settings.backend == settings.backend && state.m_settings.coverage == settings.coverage
        && state.m_settings.blockerSearch == settings.blockerSearch && state.m_settings.captureCadence == settings.captureCadence
        && state.m_settings.directionalResolution == settings.directionalResolution
        && state.m_settings.pointResolution == settings.pointResolution && state.m_settings.memoryBudgetBytes == settings.memoryBudgetBytes
    )
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

LightSpaceShadowSnapshot RendererRayTracingSystem::lightSpaceShadowSnapshot()const{
    LightSpaceShadowSnapshot result = m_lightSpaceShadow.m_snapshot;
    result.casters = m_lightSpaceShadow.m_casters.data();
    result.casterCount = m_lightSpaceShadow.m_casters.size();
    return result;
}

void RendererRayTracingSystem::acceptLightSpaceShadowCapture(const LightSpaceCaptureTicket& ticket, const bool prepared){
    auto& state = m_lightSpaceShadow;
    if(!prepared || !state.m_captureHistory.accept(ticket))
        return;
    if(ticket.reuse && !state.m_captureReuseLogged){
        state.m_captureReuseLogged = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: accepted light-space capture reuse (cadence=2)"));
    }
}

void RendererRayTracingSystem::invalidateLightSpaceShadowCapture()noexcept{
    m_lightSpaceShadow.m_captureHistory.invalidate();
}

bool RendererRayTracingSystem::buildLightSpaceShadowPlan(
    const ECSRenderDetail::SceneLightGpuData* const lights, const u32 lightCount, LightSpacePlan& plan)const{
    const auto& state = m_lightSpaceShadow;
    if(
        m_shadowVisibilityHardwareSupported || state.m_settings.backend == SoftwareShadowBackend::SoftwareTrace
        || !state.m_sceneEligible || !m_shadowVisibilityPreparedTargets || !m_preparedSceneSwBvhReady
        || !m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady
        || state.m_casters.empty() || !lights || lightCount > NWB_SCENE_MAX_LIGHTS
    )
        return false;
    Array<LightSpaceLightRequest, NWB_SCENE_SHADOW_SLOT_COUNT> requests;
    usize requestCount = 0u;
    for(u32 index = 0u; index < lightCount; ++index){
        const auto& light = lights[index];
        if(light.params.z < 0.f || light.params.z >= static_cast<f32>(NWB_SCENE_SHADOW_SLOT_COUNT))
            continue;
        if(requestCount == requests.size())
            return false;
        const auto type = light.params.y < ECSRenderDetail::s_LightTypeDirectionalMax ? Scene::LightType::Directional
            : light.params.y < ECSRenderDetail::s_LightTypePointMax ? Scene::LightType::Point : Scene::LightType::Spot;
        requests[requestCount++] = { index, static_cast<u32>(light.params.z), type, true };
    }
    return
        BuildLightSpacePlan(state.m_settings, requests.data(), requestCount, m_graphics.getDevice().getMaxStorageBufferRange(),
            static_cast<u32>(state.m_casters.size()), plan)
        && plan.viewCount != 0u
    ;
}

void RendererRayTracingSystem::preflightLightSpaceShadowResources(){
    auto& state = m_lightSpaceShadow;
    state.m_resourcesPrepared = false;
    if(m_shadowVisibilityHardwareSupported || state.m_settings.backend == SoftwareShadowBackend::SoftwareTrace || !state.m_sceneEligible)
        return;
    // Use the same scene ordering and shadow-slot allocator as the later prefix. That prefix may only select a fitting generation.
    ECSRenderDetail::SceneLightGpuData lights[NWB_SCENE_MAX_LIGHTS];
    f32 causticImportance[NWB_SCENE_MAX_LIGHTS] = {};
    const u32 lightCount = ECSRenderDetail::ResolveSceneLights(m_world, lights, causticImportance, NWB_SCENE_MAX_LIGHTS);
    LightSpacePlan plan;
    if(!buildLightSpaceShadowPlan(lights, lightCount, plan))
        return;
    state.m_resourcesPrepared = ensureLightSpaceShadowPipelines() && ensureLightSpaceShadowStorage(plan);
}

// Graph declaration can change the admitted light list, but never creates or grows its GPU resources.
void RendererRayTracingSystem::prepareLightSpaceShadows(const ECSRenderDetail::SceneLightGpuData* const lights, const u32 lightCount){
    auto& state = m_lightSpaceShadow;
    auto& snapshot = state.m_snapshot;
    snapshot.ready = false;
    if(
        !state.m_resourcesPrepared || !snapshot.layout || !snapshot.viewPipeline || !snapshot.opaqueResolve
        || !snapshot.transparentResolve || !snapshot.opaqueFallback || !snapshot.transparentFallback || !snapshot.shadePipeline
        || !snapshot.opaqueCapture || !snapshot.transparentCapture
        || !snapshot.counts || !snapshot.events || !snapshot.views || !snapshot.drawArguments || !snapshot.depth
    )
        return;
    LightSpacePlan plan;
    if(!buildLightSpaceShadowPlan(lights, lightCount, plan))
        return;
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
    LightSpaceCaptureIdentity identity;
    identity.scene = state.m_captureSceneIdentity;
    identity.trusted = state.m_captureSceneTrusted;
    identity.lighting = FNV64_OFFSET_BASIS;
    Fnv64AppendValue(identity.lighting, lightCount);
    for(u32 index = 0u; index < lightCount; ++index){
        Fnv64AppendValue(identity.lighting, lights[index].position);
        Fnv64AppendValue(identity.lighting, lights[index].direction);
        Fnv64AppendValue(identity.lighting, lights[index].colorIntensity);
        Fnv64AppendValue(identity.lighting, lights[index].params);
        Fnv64AppendValue(identity.lighting, lights[index].params2);
    }
    identity.layout = FNV64_OFFSET_BASIS;
    Fnv64AppendValue(identity.layout, targets.width);
    Fnv64AppendValue(identity.layout, targets.height);
    Fnv64AppendValue(identity.layout, plan.lightCount);
    Fnv64AppendValue(identity.layout, plan.viewCount);
    Fnv64AppendValue(identity.layout, plan.textureResolution);
    Fnv64AppendValue(identity.layout, plan.drawArgumentByteSize);
    for(u32 view = 0u; view < plan.viewCount; ++view){
        Fnv64AppendValue(identity.layout, plan.views[view].map);
        Fnv64AppendValue(identity.layout, plan.views[view].light);
    }
    snapshot.captureTicket = state.m_captureHistory.prepare(identity, state.m_settings.captureCadence);
    snapshot.captureHistory = &state.m_captureHistory;
    // The map generation includes the GPU-fitted views. A reuse task must not upload the new zero-fit templates.
    if(!snapshot.captureTicket.reuse){
        snapshot.plan = plan;
        // Keep hashed resource objects alive until their map generation is replaced; pointer identities cannot alias replacements.
        state.m_captureBuffers.assign(state.m_sceneBuffers.begin(), state.m_sceneBuffers.end());
        state.m_captureTextures.assign(state.m_sceneTextures.begin(), state.m_sceneTextures.end());
    }
    snapshot.push.viewSlot = snapshot.viewsDescriptor.slot();
    snapshot.push.materialContextSlot = m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot();
    snapshot.push.countsSlot = snapshot.countsDescriptor.slot();
    snapshot.push.eventsSlot = snapshot.eventsDescriptor.slot();
    snapshot.push.depthSlot = snapshot.depthDescriptor.slot();
    snapshot.push.instanceCount = static_cast<u32>(state.m_casters.size());
    snapshot.push.width = targets.width;
    snapshot.push.height = targets.height;
    snapshot.push.deferredResourcesSlot = targets.bindless.slotsBufferDescriptor.slot();
    snapshot.push.sceneRootSlot = m_preparedSceneBvhNodeHeapHandle.slot();
    snapshot.push.viewCount = plan.viewCount;
    snapshot.push.outputSlot = snapshot.drawArgumentsDescriptor.slot();
    snapshot.casters = state.m_casters.data();
    snapshot.casterCount = state.m_casters.size();
    snapshot.ready = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

