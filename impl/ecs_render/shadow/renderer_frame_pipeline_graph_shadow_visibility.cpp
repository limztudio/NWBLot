// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shadow/shadow_visibility_graph_coordinator.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/kernel/task_graph_float_clear_tasks.h>
#include <impl/ecs_render/shadow/soft_shadow_split_task_chain.h>
#include <impl/ecs_render/shadow/shadow_visibility_resource_planner.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>
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


b8 ShadowVisibilityGraphCoordinator::declareDeferredShadowVisibilityTask(
    const ShadowVisibilityGraphContext& context,
    const DeferredLightingGraphResources& deferredLightingResources,
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources,
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
    const RayTracingShadowVisibilityGraphPlanSnapshot& rayTracingPlan,
    const RayTracingSceneGraphReads& hardwareSceneReads,
    const b8 hardwareShadowSupported,
    const Core::GpuGraphResourceId worldPosition,
    const Core::GpuGraphResourceId normal,
    const Core::GpuGraphResourceId depth,
    const Core::GpuGraphResourceId shadowVisibility,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId sceneShading,
    const Core::GpuGraphResourceId lights,
    const Core::GpuGraphResourceId materialContextSlots,
    const Span<const Core::GpuGraphResourceId> traceGeometryResources,
    const Core::GpuGraphResourceSetId traceGeometrySet,
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
    const Core::GpuTaskId prefixTask,
    const Core::GpuExternalCompletionId laggedLightingHistoryWriterDrainCompletion,
    Core::Alloc::ScratchArena& scratchArena,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming,
    Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>& opaqueResolveTiming,
    Optional<Core::GpuTimingMeasure>& transparentResolveTiming,
    b8& opaqueProduced,
    b8& transparentTraceProduced,
    u32& opaqueFrameIndex
){
    using namespace RendererTaskGraphDetail;

    const b8 hardwareTransparentTrace = rayTracingPlan.hardwareTransparentTrace;
    const b8 csgShadow = rayTracingPlan.lightSpace.ready && (rayTracingPlan.lightSpace.push.csgFlags & NWB_CSG_SHADOW_FLAG_ENABLED) != 0u;
    const b8 hardwareCsgShadow = hardwareShadowSupported && csgShadow;

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
        || (hardwareTransparentTrace && !hardwareSceneReads.valid())
    )
        return false;

    Array<Core::GpuTaskExternalStateSource, s_ShadowVisibilityStateSourceCapacity> shadowVisibilityStateSources = {};
    usize shadowVisibilityStateSourceCount = 0u;
    const auto* const shadowScratchStates = context.shadowComputeState.source();
    if(shadowScratchStates)
        shadowVisibilityStateSources[shadowVisibilityStateSourceCount++] = { .states = shadowScratchStates, };
    const auto* const shadowReturnStates = context.shadowReturnState.source();
    if(shadowReturnStates)
        shadowVisibilityStateSources[shadowVisibilityStateSourceCount++] = { .states = shadowReturnStates, .applicableConsumerQueueClass = Core::CommandQueue::Compute, };
    const Core::GpuTaskExternalStateSource* const shadowVisibilityStateSourceData = shadowVisibilityStateSourceCount != 0u ? shadowVisibilityStateSources.data() : nullptr;

    const Core::GpuExternalCompletionId* const laggedLightingHistoryWriterDrainDependencies = laggedLightingHistoryWriterDrainCompletion.valid() ? &laggedLightingHistoryWriterDrainCompletion : nullptr;
    const usize laggedLightingHistoryWriterDrainDependencyCount = laggedLightingHistoryWriterDrainCompletion.valid() ? 1u : 0u;

    opaqueProduced = false;
    transparentTraceProduced = false;
    opaqueFrameIndex = 0u;
    // Only the prepared route exposes this boundary; other paths keep the monolithic Shadow Visibility callback.
    const b8 preparedSoftTransparentFoldCandidate =
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
        && !traceGeometryResources.empty()
    ;
    const b8 splitSoftTransparentFold = preparedSoftTransparentFoldCandidate;
    const b8 combinedSoftUpsample = splitSoftTransparentFold && rayTracingPlan.combinedSoftUpsample;
    m_deferredShadowCombinedUpsample = combinedSoftUpsample;
    const b8 combinedSoftWavelet = CanCombineSoftShadowWavelets(
        combinedSoftUpsample,
        rayTracingPlan.combinedSoftWaveletReady,
        rayTracingPlan.opaqueTemporalMergeReady,
        rayTracingPlan.transparentTemporalMergeReady,
        NWB_SHADOW_RESOLVE_PASS_COUNT,
        NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT
    );
    m_deferredShadowCombinedWavelet = combinedSoftWavelet;
    const b8 combinedSoftTemporal = combinedSoftWavelet && rayTracingPlan.combinedSoftTemporalReady;
    m_deferredShadowCombinedTemporal = combinedSoftTemporal;
    // Adaptive fallback stays in the monolithic callback, but its primitives and diagnostic lifecycle are fixed by this route.
    // A frame with no clear/copy work still owns its tick through the semantic task's accepted hook without gaining empty graph nodes.
    const b8 graphOwnedAdaptiveCandidate = !splitSoftTransparentFold && rayTracingPlan.adaptivePlan.enabled;
    GraphOwnedAdaptiveShadowPlan graphOwnedAdaptivePlan = graphOwnedAdaptiveCandidate ? rayTracingPlan.adaptivePlan : GraphOwnedAdaptiveShadowPlan{};
    // Merge pipeline may be ready before history has an accepted frame. Bootstrap publishes the pair, but only usable history samples prior geometry.
    const b8 softShadowHistoryReadable = rayTracingPlan.softShadowHistoryReadable;
    // Opaque merge runs before the transparent tail and shares its selector. Freeze its pair while the packet owns the temporal route.
    const b8 graphOwnsOpaqueTemporalMergeEntryStates = splitSoftTransparentFold && rayTracingPlan.opaqueTemporalMergeReady;
    const b8 opaqueHistoryFrontIsA = rayTracingPlan.historyFrontIsA;

    const b8 traceGeometryStatesGraphOwned = traceGeometrySet.valid();
    ShadowVisibilityResourcePlan shadowResourcePlan(scratchArena);
    const ShadowVisibilityResourceInputs shadowResourceInputs{
        deferredTargets, rayTracingShadowResources, rayTracingResources, rayTracingPlan, hardwareSceneReads,
        worldPosition, normal, depth, shadowVisibility, currentBindlessSlots, sceneShading, lights, materialContextSlots,
        traceGeometryResources, traceGeometryStatesGraphOwned, splitSoftTransparentFold, graphOwnsOpaqueTemporalMergeEntryStates,
        softShadowHistoryReadable, opaqueHistoryFrontIsA, hardwareTransparentTrace, graphOwnedAdaptivePlan.compact
    };
    ShadowVisibilityResourcePlanner shadowResourcePlanner(context.graph, context.rayTracing);
    if(!shadowResourcePlanner.prepare(shadowResourceInputs, shadowResourcePlan))
        return false;
    const auto& sceneGeometryDomain = shadowResourcePlan.sceneGeometryDomain;
    const auto& sceneTlas = shadowResourcePlan.sceneTlas;
    const auto& hardwareTransparentScratch = shadowResourcePlan.hardwareTransparentScratch;
    const auto& adaptiveEdgeCounter = shadowResourcePlan.adaptiveEdgeCounter;
    const auto& resourceUses = shadowResourcePlan.resourceUses;

    const RendererFramePipelineDetail::TraceResourceSetUses traceResourceSets = RendererFramePipelineDetail::MakeTraceResourceSetUses(traceGeometrySet, traceMaterialSampledTextureSet);
    const Core::GpuTaskResourceSetUse traceGeometrySetUse = traceResourceSets.useCount != 0u ? traceResourceSets.uses[0u] : Core::GpuTaskResourceSetUse{};

    if(splitSoftTransparentFold){
        // The prepared soft path keeps the opaque first wavelet, resolve tail, transparent trace, and temporal/RGB resolve as separate callbacks.
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
        SoftShadowSplitResourceUseBuilder resourceUseBuilder(context.graph);
        if(!resourceUseBuilder.build(splitResourceInputs, splitResourceUses))
            return false;

        const SoftShadowSplitTaskChainInputs taskChainInputs{
            .targets = deferredTargets,
            .deferredLightingResources = deferredLightingResources,
            .rayTracingResources = rayTracingResources,
            .rayTracingPlan = rayTracingPlan,
            .shadowPrepared = context.shadowPrepared,
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
            .traceResourceSetUses = Span<const Core::GpuTaskResourceSetUse>(traceResourceSets.uses.data(), traceResourceSets.useCount),
            .traceGeometrySetUse = traceGeometrySetUse,
            .stateSources = Span<const Core::GpuTaskExternalStateSource>(shadowVisibilityStateSources.data(), shadowVisibilityStateSourceCount),
            .historyWriterDrainDependencies = Span<const Core::GpuExternalCompletionId>(laggedLightingHistoryWriterDrainDependencies, laggedLightingHistoryWriterDrainDependencyCount),
            .currentBindlessSlots = currentBindlessSlots,
            .sceneShading = sceneShading,
            .lights = lights,
            .materialContextSlots = materialContextSlots,
            .prefixTask = prefixTask,
        };
        SoftShadowSplitTasks splitTasks;
        SoftShadowSplitTaskChainBuilder taskChainBuilder(context.graph, context.graphics, context.arena, context.rayTracing);
        const b8 declared = taskChainBuilder.declare(taskChainInputs, scratchArena, splitResourceUses, splitTasks);
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
    b8 adaptivePrimitivePrecedesVisibility = false;
    if(graphOwnedAdaptivePlan.enabled){
        // The append counter is private adaptive scratch.
        // The compiler owns its CopyDest->UAV handoff before the Shadow Visibility callback, which still gates the adaptive producer.
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
            counterClearDesc.setIdentity(Name("render.shadow_visibility.adaptive_counter_clear")).setMarkerLabel("Shadow Adaptive Counter Clear").setScheduling(counterClearScheduling).setDependencies({ &shadowVisibilityDependency, 1u });
            const auto deferredShadowVisibilityAdaptiveCounterClearTaskResult = context.graph.addClearBufferTask(counterClearDesc, Core::GpuClearBufferTaskDesc{ .destination = adaptiveEdgeCounter, .clearValue = 0u, });
            if(!deferredShadowVisibilityAdaptiveCounterClearTaskResult){
                m_deferredShadowVisibilityAdaptiveCounterClearTask = {};
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned adaptive shadow counter clear"));
                return false;
            }
            m_deferredShadowVisibilityAdaptiveCounterClearTask = *deferredShadowVisibilityAdaptiveCounterClearTaskResult;
            shadowVisibilityDependency = m_deferredShadowVisibilityAdaptiveCounterClearTask;
            adaptivePrimitivePrecedesVisibility = true;
        }
    }

    // Preparedness is established while Shadow Prepare records, after this graph is declared.
    // The monolithic path opens with an all-lit clear: real producers overwrite it, a missing one keeps white with no state bridge.
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
        .setDependencies({ &shadowVisibilityDependency, 1u })
        .setExternalDependencies({ laggedLightingHistoryWriterDrainDependencies, laggedLightingHistoryWriterDrainDependencyCount })
        .setResourceUses({ &allLitClearResourceUse, 1u })
    ;
    const auto deferredShadowVisibilityAllLitClearTaskResult = context.graph.addTask<
        ECSRenderDetail::FloatTextureClearGraphTask
    >(allLitClearDesc, ECSRenderDetail::FloatTextureClearGraphTask::Payload{ .destination = shadowVisibility, .subresources = ECSRenderDetail::s_ShadowVisibilitySubresources, .floatValue = ECSRenderDetail::s_ShadowVisibilityAllLitClearColor, });
    if(!deferredShadowVisibilityAllLitClearTaskResult){
        m_deferredShadowVisibilityAllLitClearTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned all-lit shadow-visibility clear"));
        return false;
    }
    m_deferredShadowVisibilityAllLitClearTask = *deferredShadowVisibilityAllLitClearTaskResult;
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
        .setDependencies({ &shadowVisibilityDependency, 1u })
        .setExternalStateSources({ shadowVisibilityStateSourceData, shadowVisibilityStateSourceCount })
        .setResourceUses(resourceUses)
        .setResourceSetUses({ traceResourceSets.useCount != 0u ? traceResourceSets.uses.data() : nullptr, traceResourceSets.useCount })
    ;
    const auto deferredShadowVisibilityTaskResult = context.rayTracing.declareShadowVisibilityTask(
        context.graph,
        desc,
        deferredLightingResources,
        context.shadowPrepared,
        hardwareShadowSupported,
        graphOwnedAdaptivePlan,
        deferredTargets,
        timingTicket
    );
    if(!deferredShadowVisibilityTaskResult){
        m_deferredShadowVisibilityTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred shadow-visibility graph task"));
        return false;
    }
    m_deferredShadowVisibilityTask = *deferredShadowVisibilityTaskResult;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

