// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/renderer_frame_pipeline_graph_shared.h>
#include <impl/ecs_render/raytrace/task_graph_float_clear_tasks.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/shadow/soft_shadow_split_task_chain.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererFramePipeline::declareDeferredShadowVisibilityTask(
    Core::Alloc::ScratchArena& scratchArena,
    DeferredFrameTargets& deferredTargets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources,
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
    const RayTracingShadowVisibilityGraphPlanSnapshot& rayTracingPlan,
    const RayTracingSceneGraphReads& hardwareSceneReads,
    const bool hardwareShadowSupported,
    const Core::GpuGraphResourceId worldPosition,
    const Core::GpuGraphResourceId normal,
    const Core::GpuGraphResourceId depth,
    const Core::GpuGraphResourceId shadowVisibility,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId sceneShading,
    const Core::GpuGraphResourceId lights,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuGraphResourceId* const traceGeometryResources,
    const usize traceGeometryResourceCount,
    const Core::GpuGraphResourceSetId traceGeometrySet,
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
    const Core::GpuTaskId prefixTask,
    const Core::GpuExternalCompletionId laggedLightingHistoryWriterDrainCompletion,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>& opaqueResolveTiming,
    Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
    bool& opaqueProduced,
    bool& transparentTraceProduced,
    u32& opaqueFrameIndex
){
    using namespace RendererTaskGraphDetail;

    const bool hardwareTransparentTrace = rayTracingPlan.hardwareTransparentTrace;
    const bool csgShadow = rayTracingPlan.lightSpace.ready && (rayTracingPlan.lightSpace.push.csgFlags & NWB_CSG_SHADOW_FLAG_ENABLED) != 0u;
    const bool hardwareCsgShadow = hardwareShadowSupported && csgShadow;

    m_deferredShadowVisibilityOpaqueTask = {};
    m_deferredShadowVisibilityOpaqueFirstWaveletTask = {};
    m_deferredShadowVisibilityOpaqueResolveTask = {};
    m_deferredShadowCombinedUpsample = false;
    m_deferredShadowCombinedWavelet = false;
    m_deferredShadowCombinedTemporal = false;
    m_deferredShadowVisibilityTransparentTraceTask = {};
    m_deferredShadowVisibilityTransparentTemporalMergeTask = {};
    m_deferredShadowVisibilityTransparentFirstWaveletTask = {};
    m_deferredShadowVisibilityDiagnosticsTask = {};
    m_deferredShadowVisibilityAdaptiveCounterClearTask = {};
    m_deferredShadowVisibilityAllLitClearTask = {};
    m_deferredShadowVisibilityTask = {};
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !worldPosition.valid()
        || !normal.valid()
        || !depth.valid()
        || !shadowVisibility.valid()
        || !currentBindlessSlots.valid()
        || !sceneShading.valid()
        || !lights.valid()
        || !prefixTask.valid()
        || (traceGeometryResourceCount != 0u && !traceGeometryResources)
        || (hardwareTransparentTrace && !hardwareSceneReads.valid())
    )
        return false;

    Core::GpuTaskExternalStateSource shadowVisibilityStateSources[2u] = {};
    usize shadowVisibilityStateSourceCount = 0u;
    const auto* const shadowScratchStates = m_shadowComputePersistentState.source();
    if(shadowScratchStates){
        shadowVisibilityStateSources[shadowVisibilityStateSourceCount++] = { .states = shadowScratchStates, };
    }
    const auto* const shadowReturnStates = m_shadowVisibilityReturnState.source();
    if(shadowReturnStates){
        shadowVisibilityStateSources[shadowVisibilityStateSourceCount++] = { .states = shadowReturnStates, .applicableConsumerQueueClass = Core::CommandQueue::Compute, };
    }
    const Core::GpuTaskExternalStateSource* const shadowVisibilityStateSourceData = shadowVisibilityStateSourceCount != 0u ? shadowVisibilityStateSources : nullptr;

    const Core::GpuExternalCompletionId* const laggedLightingHistoryWriterDrainDependencies = laggedLightingHistoryWriterDrainCompletion.valid() ? &laggedLightingHistoryWriterDrainCompletion : nullptr;
    const usize laggedLightingHistoryWriterDrainDependencyCount = laggedLightingHistoryWriterDrainCompletion.valid() ? 1u : 0u;

    opaqueProduced = false;
    transparentTraceProduced = false;
    opaqueFrameIndex = 0u;
    // Only the fully prepared soft-transparent route can expose this boundary. Direct, adaptive, and resource-degraded paths retain the established monolithic Shadow Visibility callback.
    const bool preparedSoftTransparentFoldCandidate =
        rayTracingPlan.softTransparentFoldReady
        && deferredTargets.shadowCoarseTransmittance
        && deferredTargets.shadowSoftHalfA
        && deferredTargets.shadowSoftHalfB
        && deferredTargets.shadowSoftGeometry
        && deferredTargets.shadowSoftGeometryPrev
        && deferredTargets.shadowHistA
        && deferredTargets.shadowHistB
        && deferredTargets.shadowMomentsA
        && deferredTargets.shadowMomentsB
        && deferredTargets.transparentSoftHalf
        && deferredTargets.transparentHistA
        && deferredTargets.transparentHistB
        && deferredTargets.transparentMomentsA
        && deferredTargets.transparentMomentsB
        && materialContextSlots.valid()
        && traceGeometryResourceCount != 0u
    ;
    const bool splitSoftTransparentFold = preparedSoftTransparentFoldCandidate;
    const bool combinedSoftUpsample = splitSoftTransparentFold && rayTracingPlan.combinedSoftUpsample;
    m_deferredShadowCombinedUpsample = combinedSoftUpsample;
    const bool combinedSoftWavelet = CanCombineSoftShadowWavelets(
        combinedSoftUpsample,
        rayTracingPlan.combinedSoftWaveletReady,
        rayTracingPlan.opaqueTemporalMergeReady,
        rayTracingPlan.transparentTemporalMergeReady,
        NWB_SHADOW_RESOLVE_PASS_COUNT,
        NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT
    );
    m_deferredShadowCombinedWavelet = combinedSoftWavelet;
    const bool combinedSoftTemporal = combinedSoftWavelet && rayTracingPlan.combinedSoftTemporalReady;
    m_deferredShadowCombinedTemporal = combinedSoftTemporal;
    // The adaptive fallback remains in the monolithic callback, but its raw buffer primitives and acceptance-time diagnostic lifecycle are deterministic from this frozen route.
    // A frame with no clear/copy work still owns its tick through the semantic task's accepted hook without gaining empty graph nodes.
    const bool graphOwnedAdaptiveCandidate = !splitSoftTransparentFold && rayTracingPlan.adaptivePlan.enabled;
    GraphOwnedAdaptiveShadowPlan graphOwnedAdaptivePlan = graphOwnedAdaptiveCandidate ? rayTracingPlan.adaptivePlan : GraphOwnedAdaptiveShadowPlan{};
    // The merge pipeline is ready before its retained history has an accepted frame. Bootstrap still publishes the selected output pair, but only usable history may sample with previous geometry.
    const bool softShadowHistoryReadable = rayTracingPlan.softShadowHistoryReadable;
    // The opaque temporal merge runs before the transparent tail and shares its history selector. Freeze its exact input/output pair while the compiled packet owns the prepared temporal route.
    const bool graphOwnsOpaqueTemporalMergeEntryStates = splitSoftTransparentFold && rayTracingPlan.opaqueTemporalMergeReady;
    const bool opaqueHistoryFrontIsA = rayTracingPlan.historyFrontIsA;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const auto sceneGeometryDomainResult = m_deferredLightingTaskGraph.importHazardDomain(HazardDomainDesc(Name("render.shadow_visibility.scene_geometry"), RendererFramePipelineDetail::s_SceneGeometryDomainLabel));
    if(!sceneGeometryDomainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred shadow-visibility graph resources"));
        return false;
    }
    const Core::GpuGraphResourceId sceneGeometryDomain = *sceneGeometryDomainResult;

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    const bool traceGeometryStatesGraphOwned = traceGeometrySet.valid();
    constexpr usize s_ShadowVisibilityResourceUseCapacity = 40u;
    resourceUses.reserve(s_ShadowVisibilityResourceUseCapacity + (traceGeometryStatesGraphOwned ? 0u : traceGeometryResourceCount));
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(normal));
    // Shadow visibility samples the bindless depth image, so its declared layout must match the native shader read.
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
    resourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    // The split opaque producer overwrites visibility; the software monolith also folds transparency in place.
    resourceUses.push_back(splitSoftTransparentFold ? WriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess) : ReadWriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));

    const auto appendOptionalWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        if(!texture)
            return true;
        const auto resourceResult = importTexture(texture, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalOpaqueTemporalHistoryTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label, const bool isInput){
        if(!texture)
            return true;
        const auto resourceResult = importTexture(texture, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        if(graphOwnsOpaqueTemporalMergeEntryStates)
            resourceUses.push_back(isInput ? ReadUse(resource, Core::ResourceStates::ShaderResource) : WriteUse(resource, Core::ResourceStates::UnorderedAccess));
        else if(!splitSoftTransparentFold){
            if(isInput){
                if(softShadowHistoryReadable)
                    resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
            }
            else if(rayTracingPlan.opaqueTemporalMergeReady)
                resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));
        }
        else
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalOpaqueTemporalStaticReadTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        if(!texture)
            return true;
        const auto resourceResult = importTexture(texture, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        if(softShadowHistoryReadable)
            resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        else if(splitSoftTransparentFold && !graphOwnsOpaqueTemporalMergeEntryStates)
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalTransparentTemporalHistoryTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label, const bool isInput){
        if(!texture)
            return true;
        const auto resourceResult = importTexture(texture, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        if(rayTracingPlan.transparentTemporalMergeReady){
            if(isInput){
                if(softShadowHistoryReadable)
                    resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
            }
            else
                resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));
        }
        return true;
    };
    const auto appendOptionalReadWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const auto resourceResult = importBuffer(buffer, identity, label);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        resourceUses.push_back(ReadWriteUse(resource, state));
        return true;
    };
    bool optionalResourcesImported =
        (
            // The retained monolith selects its adaptive fallback only after runtime slot checks. Declare coarse as an output across that whole route, while prepared split callbacks omit it completely.
            splitSoftTransparentFold
            || appendOptionalWriteTexture(deferredTargets.shadowCoarseTransmittance, Name("render.shadow_visibility.coarse_transmittance"), "Shadow Coarse Transmittance")
        )
        // The retained callback first overwrites these scratch targets, then resolves them locally within the same command list.
        // Its graph entry must therefore preserve Unknown -> UAV first writes rather than fabricate a ReadWrite native source for a fresh target generation.
        && appendOptionalWriteTexture(deferredTargets.shadowSoftHalfA, Name(RendererFramePipelineDetail::s_ShadowSoftHalfAName), RendererFramePipelineDetail::s_ShadowSoftHalfALabel)
        && appendOptionalWriteTexture(deferredTargets.shadowSoftHalfB, Name(RendererFramePipelineDetail::s_ShadowSoftHalfBName), RendererFramePipelineDetail::s_ShadowSoftHalfBLabel)
        && appendOptionalWriteTexture(deferredTargets.shadowSoftGeometry, Name(RendererFramePipelineDetail::s_ShadowSoftGeometryName), RendererFramePipelineDetail::s_ShadowSoftGeometryLabel)
        && appendOptionalOpaqueTemporalStaticReadTexture(deferredTargets.shadowSoftGeometryPrev, Name(RendererFramePipelineDetail::s_ShadowSoftGeometryPreviousName), RendererFramePipelineDetail::s_ShadowSoftGeometryPreviousLabel)
        && appendOptionalOpaqueTemporalHistoryTexture(deferredTargets.shadowHistA, Name(RendererFramePipelineDetail::s_ShadowHistoryAName), RendererFramePipelineDetail::s_ShadowHistoryALabel, opaqueHistoryFrontIsA)
        && appendOptionalOpaqueTemporalHistoryTexture(deferredTargets.shadowHistB, Name(RendererFramePipelineDetail::s_ShadowHistoryBName), RendererFramePipelineDetail::s_ShadowHistoryBLabel, !opaqueHistoryFrontIsA)
        && appendOptionalOpaqueTemporalHistoryTexture(deferredTargets.shadowMomentsA, Name(RendererFramePipelineDetail::s_ShadowMomentsAName), RendererFramePipelineDetail::s_ShadowMomentsALabel, opaqueHistoryFrontIsA)
        && appendOptionalOpaqueTemporalHistoryTexture(deferredTargets.shadowMomentsB, Name(RendererFramePipelineDetail::s_ShadowMomentsBName), RendererFramePipelineDetail::s_ShadowMomentsBLabel, !opaqueHistoryFrontIsA)
        && appendOptionalWriteTexture(deferredTargets.transparentSoftHalf, Name(RendererFramePipelineDetail::s_TransparentSoftHalfName), RendererFramePipelineDetail::s_TransparentSoftHalfLabel)
        && appendOptionalTransparentTemporalHistoryTexture(deferredTargets.transparentHistA, Name(RendererFramePipelineDetail::s_TransparentHistoryAName), RendererFramePipelineDetail::s_TransparentHistoryALabel, opaqueHistoryFrontIsA)
        && appendOptionalTransparentTemporalHistoryTexture(deferredTargets.transparentHistB, Name(RendererFramePipelineDetail::s_TransparentHistoryBName), RendererFramePipelineDetail::s_TransparentHistoryBLabel, !opaqueHistoryFrontIsA)
        && appendOptionalTransparentTemporalHistoryTexture(deferredTargets.transparentMomentsA, Name(RendererFramePipelineDetail::s_TransparentMomentsAName), RendererFramePipelineDetail::s_TransparentMomentsALabel, opaqueHistoryFrontIsA)
        && appendOptionalTransparentTemporalHistoryTexture(deferredTargets.transparentMomentsB, Name(RendererFramePipelineDetail::s_TransparentMomentsBName), RendererFramePipelineDetail::s_TransparentMomentsBLabel, !opaqueHistoryFrontIsA)
        && (hardwareTransparentTrace || RendererFramePipelineDetail::AppendRayTracingSceneShadowBuffers(
            rayTracingResources,
            importBuffer,
            [&](const Core::GpuGraphResourceId resource, const Core::ResourceStates::Mask state)noexcept{
                resourceUses.push_back(ReadUse(resource, state));
            }
        ))
        && appendOptionalReadWriteBuffer(rayTracingShadowResources.swShadowEdgeCounterBuffer, Name(RendererFramePipelineDetail::s_ShadowEdgeCounterName), RendererFramePipelineDetail::s_ShadowEdgeCounterLabel, Core::ResourceStates::UnorderedAccess)
        && appendOptionalReadWriteBuffer(rayTracingShadowResources.swShadowEdgeListBuffer, Name("render.shadow_visibility.edge_list"), "Shadow Edge List", Core::ResourceStates::UnorderedAccess)
        && appendOptionalReadWriteBuffer(rayTracingShadowResources.swShadowIndirectArgsBuffer, Name("render.shadow_visibility.indirect_args"), "Shadow Indirect Arguments", Core::ResourceStates::UnorderedAccess)
    ;
    Core::GpuGraphResourceId sceneTlas;
    if(rayTracingShadowResources.sceneTlas){
        const auto sceneTlasResult = m_deferredLightingTaskGraph.importAccelStruct(
            rayTracingShadowResources.sceneTlas,
            AccelStructResourceDesc(Name(RendererFramePipelineDetail::s_SceneTlasResourceName), RendererFramePipelineDetail::s_SceneTlasResourceLabel)
                .setInitialState(m_raytracingSystem.sceneTlasBackingInitialState())
        );
        if(sceneTlasResult)
            sceneTlas = *sceneTlasResult;
        optionalResourcesImported = optionalResourcesImported && sceneTlasResult.has_value();
        if(sceneTlas.valid() && !hardwareTransparentTrace){
            resourceUses.push_back(ReadUse(sceneTlas, Core::ResourceStates::AccelStructRead));
        }
    }
    Core::GpuGraphResourceId hardwareTransparentScratch[s_HardwareTransparentShadowScratchCount] = {};
    if(hardwareTransparentTrace){
        const ReferenceWrapper<const Core::BufferHandle> buffers[] = { rayTracingResources.hardwareTransparentCrossingsBuffer, rayTracingResources.hardwareTransparentOverflowListBuffer, rayTracingResources.hardwareTransparentOverflowArgsBuffer, };
        for(usize index = 0u; index < LengthOf(buffers); ++index){
            const Core::BufferHandle& buffer = buffers[index].get();
            if(!buffer)
                return false;
            const Core::BufferDesc& bufferDesc = buffer->getCreationDescription();
            if(!bufferDesc.debugName)
                return false;
            const auto hardwareTransparentScratchIndexResult = m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(bufferDesc.debugName, "Hardware Transparent Shadow Scratch").setInitialState(bufferDesc.initialState));
            if(!hardwareTransparentScratchIndexResult){
                hardwareTransparentScratch[index] = {};
                return false;
            }
            hardwareTransparentScratch[index] = *hardwareTransparentScratchIndexResult;
            resourceUses.push_back(ReadWriteUse(hardwareTransparentScratch[index], Core::ResourceStates::UnorderedAccess));
        }
        for(const Core::GpuTaskResourceUse& use : hardwareSceneReads.uses)
            resourceUses.push_back(use);
    }
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a deferred shadow-visibility dynamic resource"));
        return false;
    }
    Core::GpuGraphResourceId adaptiveEdgeCounter;
    if(graphOwnedAdaptivePlan.compact){
        const auto adaptiveEdgeCounterResult = importBuffer(rayTracingShadowResources.swShadowEdgeCounterBuffer, Name(RendererFramePipelineDetail::s_ShadowEdgeCounterName), RendererFramePipelineDetail::s_ShadowEdgeCounterLabel);
        if(!adaptiveEdgeCounterResult){
            adaptiveEdgeCounter = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graph-owned adaptive shadow counter resource"));
            return false;
        }
        adaptiveEdgeCounter = *adaptiveEdgeCounterResult;
    }
    resourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    if(materialContextSlots.valid() && !hardwareTransparentTrace)
        resourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    if(!traceGeometryStatesGraphOwned){
        for(usize resourceIndex = 0u; resourceIndex < traceGeometryResourceCount; ++resourceIndex){
            const Core::GpuGraphResourceId resource = traceGeometryResources[resourceIndex];
            if(!resource.valid())
                return false;
            resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
    }
    const RendererFramePipelineDetail::TraceResourceSetUses traceResourceSets = RendererFramePipelineDetail::MakeTraceResourceSetUses(traceGeometrySet, traceMaterialSampledTextureSet);
    Core::GpuTaskResourceSetUse traceResourceSetUses[2u] = {};
    usize traceResourceSetUseCount = 0u;
    for(usize traceSetIndex = 0u; traceSetIndex < traceResourceSets.useCount; ++traceSetIndex)
        traceResourceSetUses[traceResourceSetUseCount++] = traceResourceSets.uses[traceSetIndex];
    const Core::GpuTaskResourceSetUse traceGeometrySetUse = traceResourceSets.useCount != 0u ? traceResourceSets.uses[0u] : Core::GpuTaskResourceSetUse{};

    if(splitSoftTransparentFold){
        // The prepared soft path keeps the opaque first wavelet, optional resolve tail, transparent trace, and temporal/RGB resolve as adjacent callbacks. Re-importing retains identities while making each handoff explicit.
        const SoftShadowSplitRoute splitRoute{
            .hardwareShadowSupported = hardwareShadowSupported,
            .hardwareCsgShadow = hardwareCsgShadow,
            .csgShadow = csgShadow,
            .traceGeometryStatesGraphOwned = traceGeometryStatesGraphOwned,
            .combinedSoftUpsample = combinedSoftUpsample,
            .combinedSoftWavelet = combinedSoftWavelet,
            .combinedSoftTemporal = combinedSoftTemporal,
            .graphOwnsOpaqueTemporalMergeEntryStates = graphOwnsOpaqueTemporalMergeEntryStates,
        };
        const SoftShadowSplitResourceInputs splitResourceInputs{
            .targets = deferredTargets,
            .rayTracingResources = rayTracingResources,
            .rayTracingPlan = rayTracingPlan,
            .hardwareSceneReads = hardwareSceneReads,
            .hardwareTransparentScratch = hardwareTransparentScratch,
            .route = splitRoute,
            .traceGeometryResources = traceGeometryResources,
            .traceGeometryResourceCount = traceGeometryResourceCount,
            .worldPosition = worldPosition,
            .normal = normal,
            .depth = depth,
            .shadowVisibility = shadowVisibility,
            .currentBindlessSlots = currentBindlessSlots,
            .sceneShading = sceneShading,
            .lights = lights,
            .materialContextSlots = materialContextSlots,
            .sceneGeometryDomain = sceneGeometryDomain,
            .sceneTlas = sceneTlas,
        };
        SoftShadowSplitResourceUses splitResourceUses(scratchArena);
        SoftShadowSplitResourceUseBuilder resourceUseBuilder(m_deferredLightingTaskGraph);
        if(!resourceUseBuilder.build(splitResourceInputs, splitResourceUses))
            return false;

        const SoftShadowSplitTaskChainInputs taskChainInputs{
            .targets = deferredTargets,
            .deferredLightingResources = deferredLightingResources,
            .rayTracingResources = rayTracingResources,
            .rayTracingPlan = rayTracingPlan,
            .shadowPrepared = m_shadowPreparationOutcome.ready,
            .timingTicket = timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .opaqueResolveTiming = opaqueResolveTiming,
            .transparentResolveTiming = transparentResolveTiming,
            .opaqueProduced = opaqueProduced,
            .transparentTraceProduced = transparentTraceProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .route = splitRoute,
            .traceGeometryResources = traceGeometryResources,
            .traceGeometryResourceCount = traceGeometryResourceCount,
            .traceResourceSetUses = traceResourceSetUses,
            .traceResourceSetUseCount = traceResourceSetUseCount,
            .traceGeometrySetUse = traceGeometrySetUse,
            .stateSources = shadowVisibilityStateSourceData,
            .stateSourceCount = shadowVisibilityStateSourceCount,
            .historyWriterDrainDependencies = laggedLightingHistoryWriterDrainDependencies,
            .historyWriterDrainDependencyCount = laggedLightingHistoryWriterDrainDependencyCount,
            .currentBindlessSlots = currentBindlessSlots,
            .sceneShading = sceneShading,
            .lights = lights,
            .materialContextSlots = materialContextSlots,
            .prefixTask = prefixTask,
        };
        SoftShadowSplitTasks splitTasks;
        SoftShadowSplitTaskChainBuilder taskChainBuilder(m_deferredLightingTaskGraph, m_graphics, m_arena, m_raytracingSystem);
        const bool declared = taskChainBuilder.declare(scratchArena, taskChainInputs, splitResourceUses, splitTasks);
        m_deferredShadowVisibilityOpaqueTask = splitTasks.opaque;
        m_deferredShadowVisibilityOpaqueFirstWaveletTask = splitTasks.opaqueFirstWavelet;
        m_deferredShadowVisibilityOpaqueResolveTask = splitTasks.opaqueResolve;
        m_deferredShadowVisibilityTransparentTraceTask = splitTasks.transparentTrace;
        m_deferredShadowVisibilityTransparentTemporalMergeTask = splitTasks.transparentTemporalMerge;
        m_deferredShadowVisibilityTransparentFirstWaveletTask = splitTasks.transparentFirstWavelet;
        m_deferredShadowVisibilityTask = splitTasks.fold;
        m_deferredShadowVisibilityDiagnosticsTask = splitTasks.diagnostics;
        return declared;
    }

    Core::GpuTaskId shadowVisibilityDependency = prefixTask;
    bool adaptivePrimitivePrecedesVisibility = false;
    if(graphOwnedAdaptivePlan.enabled){
        // The append counter is private adaptive scratch.
        // The compiler owns its CopyDest -> UAV handoff before the retained Shadow Visibility callback, which still decides whether the adaptive producer runs.
        Core::GpuTaskSchedulingHint primitiveScheduling;
        primitiveScheduling.cost = Core::GpuTaskCostHint::Tiny;
        primitiveScheduling.forceSubmissionBoundary = false;
        primitiveScheduling.allowPacketMerge = true;
        EnableSameFamilyComputeEffectRouting(primitiveScheduling, false);
        EnableCrossFamilyComputeEffectRouting(primitiveScheduling);

        if(graphOwnedAdaptivePlan.compact){
            Core::GpuTaskSchedulingHint counterClearScheduling = primitiveScheduling;
            counterClearScheduling.mergeWithPrevious = adaptivePrimitivePrecedesVisibility;
            EnableSameFamilyComputeEffectRouting(counterClearScheduling, adaptivePrimitivePrecedesVisibility);
            Core::GpuTaskDesc counterClearDesc;
            counterClearDesc.setIdentity(Name("render.shadow_visibility.adaptive_counter_clear")).setMarkerLabel("Shadow Adaptive Counter Clear").setScheduling(counterClearScheduling).setDependencies(&shadowVisibilityDependency, 1u);
            const auto mDeferredShadowVisibilityAdaptiveCounterClearTaskResult = m_deferredLightingTaskGraph.addClearBufferTask(counterClearDesc, Core::GpuClearBufferTaskDesc{ .destination = adaptiveEdgeCounter, .clearValue = 0u, });
            if(!mDeferredShadowVisibilityAdaptiveCounterClearTaskResult){
                m_deferredShadowVisibilityAdaptiveCounterClearTask = {};
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned adaptive shadow counter clear"));
                return false;
            }
            m_deferredShadowVisibilityAdaptiveCounterClearTask = *mDeferredShadowVisibilityAdaptiveCounterClearTaskResult;
            shadowVisibilityDependency = m_deferredShadowVisibilityAdaptiveCounterClearTask;
            adaptivePrimitivePrecedesVisibility = true;
        }
    }


    // Preparedness is established while Shadow Prepare records, after this graph is declared.
    // The normal monolithic path begins with an unconditional all-lit clear: real shadow producers overwrite it, while a missing producer retains the same white fallback without a callback-local state bridge.
    // Keep this Compute-native clear on the selected Compute packet with Shadow Visibility.
    Core::GpuTaskSchedulingHint allLitClearScheduling;
    allLitClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    allLitClearScheduling.forceSubmissionBoundary = false;
    allLitClearScheduling.allowPacketMerge = true;
    allLitClearScheduling.mergeWithPrevious = adaptivePrimitivePrecedesVisibility;
    EnableSameFamilyComputeEffectRouting(allLitClearScheduling, adaptivePrimitivePrecedesVisibility);
    EnableCrossFamilyComputeEffectRouting(allLitClearScheduling);
    const Core::GpuTaskResourceUse allLitClearResourceUse = WriteTextureUse(shadowVisibility, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::CopyDest);
    Core::GpuTaskDesc allLitClearDesc;
    allLitClearDesc
        .setIdentity(Name("render.shadow_visibility.all_lit_clear"))
        .setMarkerLabel("Shadow Visibility All-Lit Clear")
        .setScheduling(allLitClearScheduling)
        .setDependencies(&shadowVisibilityDependency, 1u)
        .setExternalDependencies(laggedLightingHistoryWriterDrainDependencies, laggedLightingHistoryWriterDrainDependencyCount)
        .setResourceUses(&allLitClearResourceUse, 1u)
    ;
    const auto mDeferredShadowVisibilityAllLitClearTaskResult = m_deferredLightingTaskGraph.addTask<
        ECSRenderDetail::FloatTextureClearGraphTask
    >(allLitClearDesc, ECSRenderDetail::FloatTextureClearGraphTask::Payload{ .destination = shadowVisibility, .subresources = ECSRenderDetail::s_ShadowVisibilitySubresources, .floatValue = ECSRenderDetail::s_ShadowVisibilityAllLitClearColor, });
    if(!mDeferredShadowVisibilityAllLitClearTaskResult){
        m_deferredShadowVisibilityAllLitClearTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned all-lit shadow-visibility clear"));
        return false;
    }
    m_deferredShadowVisibilityAllLitClearTask = *mDeferredShadowVisibilityAllLitClearTaskResult;
    shadowVisibilityDependency = m_deferredShadowVisibilityAllLitClearTask;

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    EnableSameFamilyComputeEffectRouting(scheduling);
    EnableCrossFamilyComputeEffectRouting(scheduling);
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.shadow_visibility"))
        .setMarkerLabel(RendererGpuPassText::s_ShadowVisibilityLabel)
        .setScheduling(scheduling)
        .setDependencies(&shadowVisibilityDependency, 1u)
        .setExternalStateSources(shadowVisibilityStateSourceData, shadowVisibilityStateSourceCount)
        .setResourceUses(resourceUses.data(), resourceUses.size())
        .setResourceSetUses(traceResourceSetUseCount != 0u ? traceResourceSetUses : nullptr, traceResourceSetUseCount)
    ;
    const auto mDeferredShadowVisibilityTaskResult2 = m_raytracingSystem.declareShadowVisibilityTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        deferredLightingResources,
        m_shadowPreparationOutcome.ready,
        hardwareShadowSupported,
        timingTicket,
        graphOwnedAdaptivePlan
    );
    if(!mDeferredShadowVisibilityTaskResult2){
        m_deferredShadowVisibilityTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred shadow-visibility graph task"));
        return false;
    }
    m_deferredShadowVisibilityTask = *mDeferredShadowVisibilityTaskResult2;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

