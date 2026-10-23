// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "soft_shadow_split_task_chain.h"

#include <impl/ecs_render/renderer_frame_pipeline_graph_shared.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/shadow/task_graph_light_space_shadow.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SoftShadowSplitTaskChainBuilder::declare(Core::Alloc::ScratchArena& scratchArena, const SoftShadowSplitTaskChainInputs& inputs, SoftShadowSplitResourceUses& uses, SoftShadowSplitTasks& tasks){
    using namespace RendererTaskGraphDetail;

    Core::GpuTaskId shadowTraceDependency = inputs.prefixTask;
    Core::GpuGraphResourceId csgShadowContext;
    if(inputs.rayTracingPlan.lightSpace.ready && !declareLightSpaceShadowMaps(scratchArena, inputs, uses, shadowTraceDependency, csgShadowContext))
        return false;

    Core::GpuTaskSchedulingHint tailScheduling;
    tailScheduling.cost = Core::GpuTaskCostHint::Medium;
    tailScheduling.forceSubmissionBoundary = false;
    tailScheduling.allowPacketMerge = true;
    tailScheduling.mergeWithPrevious = true;
    tailScheduling.allowMergeAcrossConsumerFrontier = inputs.route.csgShadow;
    EnableSameFamilyComputeEffectRouting(tailScheduling);
    EnableCrossFamilyComputeEffectRouting(tailScheduling);
    if(!declareOpaqueTasks(inputs, uses, shadowTraceDependency, tailScheduling, tasks))
        return false;
    return declareTransparentTasks(scratchArena, inputs, uses, tailScheduling, csgShadowContext, tasks);
}

bool SoftShadowSplitTaskChainBuilder::declareLightSpaceShadowMaps(
    Core::Alloc::ScratchArena& scratchArena,
    const SoftShadowSplitTaskChainInputs& inputs,
    SoftShadowSplitResourceUses& uses,
    Core::GpuTaskId& shadowTraceDependency,
    Core::GpuGraphResourceId& csgShadowContext
){
    using namespace RendererTaskGraphDetail;

    const SoftShadowSplitRoute& route = inputs.route;
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_graph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };

    if((inputs.rayTracingPlan.lightSpace.push.csgFlags & NWB_CSG_SHADOW_FLAG_CURRENT_RECEIVER_CAPS) != 0u){
        const Core::GpuTaskGraph::DeclarationReadView declarations(m_graph);
        const Core::GpuGraphResourceId receiverView = declarations.findImportedBuffer(inputs.rayTracingPlan.lightSpace.receiverView);
        const Core::GpuGraphResourceId receiverCapState = declarations.findImportedBuffer(inputs.rayTracingPlan.lightSpace.receiverCsgSampleState);
        const Core::GpuGraphResourceId receiverCapDepth = declarations.findImportedTexture(inputs.targets.csgRemovedIntervalDepth);
        const Core::GpuGraphResourceId receiverCapData = declarations.findImportedTexture(inputs.targets.csgRemovedIntervalData);
        const Core::GpuGraphResourceId receiverCapCount = declarations.findImportedTexture(inputs.targets.csgRemovedIntervalCount);
        if(!receiverView.valid() || !receiverCapState.valid() || !receiverCapDepth.valid() || !receiverCapData.valid() || !receiverCapCount.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not find the current light-space shadow receiver cap resources"));
            return false;
        }
        const Core::TextureSubresourceSet layers(0u, 1u, 0u, inputs.targets.csgRemovedIntervalLayerCount);
        const Core::GpuTaskResourceUse receiverReads[]{
            ReadUse(receiverView, Core::ResourceStates::ConstantBuffer),
            ReadUse(receiverCapState, Core::ResourceStates::ConstantBuffer),
            ReadTextureUse(receiverCapDepth, layers, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(receiverCapData, layers, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(receiverCapCount, Core::TextureSubresourceSet(0u, 1u, 0u, 1u), Core::ResourceStates::UnorderedAccess),
        };
        for(const auto& use : receiverReads){
            uses.opaque.push_back(use);
            uses.transparentTrace.push_back(use);
        }
    }
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> lightSpaceReads{ scratchArena };
    constexpr usize s_LightSpaceReadCapacity = 9u;
    lightSpaceReads.reserve(s_LightSpaceReadCapacity + inputs.rayTracingPlan.lightSpace.csgDynamicBoundsCount + (route.traceGeometryStatesGraphOwned ? 0u : inputs.traceGeometryResourceCount));
    if(!RendererFramePipelineDetail::AppendRayTracingSceneShadowBuffers(inputs.rayTracingResources, importBuffer, [&](const Core::GpuGraphResourceId resource, const Core::ResourceStates::Mask state)noexcept{
            lightSpaceReads.push_back(ReadUse(resource, state));
        }
    ))
        return false;
    lightSpaceReads.push_back(ReadUse(inputs.materialContextSlots, Core::ResourceStates::ConstantBuffer));
    lightSpaceReads.push_back(ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    lightSpaceReads.push_back(ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer));
    lightSpaceReads.push_back(ReadUse(inputs.lights, Core::ResourceStates::ShaderResource));
    if(!route.traceGeometryStatesGraphOwned){
        for(usize index = 0u; index < inputs.traceGeometryResourceCount; ++index)
            lightSpaceReads.push_back(ReadUse(inputs.traceGeometryResources[index], Core::ResourceStates::ShaderResource));
    }
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> csgBoundsResources{ scratchArena };
    csgBoundsResources.reserve(inputs.rayTracingPlan.lightSpace.csgDynamicBoundsCount);
    for(usize index = 0u; index < inputs.rayTracingPlan.lightSpace.csgDynamicBoundsCount; ++index){
        const Core::BufferHandle& buffer = inputs.rayTracingPlan.lightSpace.csgDynamicBounds[index];
        Core::GpuGraphResourceId resource;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(m_graph);
            resource = declarations.findImportedBuffer(buffer);
        }
        if(!resource.valid()){
            const auto importedResource = importBuffer(buffer, buffer->getCreationDescription().debugName, "CSG Shadow Dynamic Bounds");
            if(!importedResource){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import CSG shadow dynamic bounds"));
                return false;
            }
            resource = *importedResource;
        }
        if(!resource.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import CSG shadow dynamic bounds"));
            return false;
        }
        if(FindIf(csgBoundsResources.begin(), csgBoundsResources.end(), [&](const auto existing)noexcept{ return existing == resource; }) != csgBoundsResources.end())
            continue;
        csgBoundsResources.push_back(resource);
        lightSpaceReads.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        uses.opaque.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    auto mapsResult = DeclareLightSpaceShadowMaps(m_graph, LightSpaceShadowGraphInputs{
        .graphics = m_graphics,
        .arena = m_arena,
        .scratchArena = scratchArena,
        .shadowPrepared = inputs.shadowPrepared,
        .snapshot = inputs.rayTracingPlan.lightSpace,
        .dependency = inputs.prefixTask,
        .sceneReads = lightSpaceReads.data(),
        .sceneReadCount = lightSpaceReads.size(),
        .sceneReadSets = inputs.traceResourceSetUses,
        .sceneReadSetCount = inputs.traceResourceSetUseCount,
        .stateSources = inputs.stateSources,
        .stateSourceCount = inputs.stateSourceCount,
    }
    );
    if(!mapsResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare light-space shadow maps"));
        return false;
    }
    const LightSpaceShadowGraph maps = Move(*mapsResult);
    const Core::GpuGraphResourceId mapBuffers[] = { maps.counts, maps.events, maps.views };
    for(const auto resource : mapBuffers){
        uses.opaque.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    if(maps.csgContext.valid()){
        csgShadowContext = maps.csgContext;
        uses.opaque.push_back(ReadWriteUse(maps.csgContext, Core::ResourceStates::UnorderedAccess));
        uses.transparentTrace.push_back(ReadWriteUse(maps.csgContext, Core::ResourceStates::UnorderedAccess));
        uses.opaque.push_back(ReadUse(maps.csgOpaqueDepth, Core::ResourceStates::ShaderResource));
        uses.transparentTrace.push_back(ReadUse(maps.csgOpaqueDepth, Core::ResourceStates::ShaderResource));
    }
    const Core::TextureSubresourceSet layers{ 0u, 1u, 0u, inputs.rayTracingPlan.lightSpace.plan.viewCount };
    uses.opaque.push_back(ReadTextureUse(maps.depth, layers, Core::ResourceStates::ShaderResource));
    uses.transparentTrace.push_back(ReadTextureUse(maps.depth, layers, Core::ResourceStates::ShaderResource));
    shadowTraceDependency = maps.ready;
    return true;
}

bool SoftShadowSplitTaskChainBuilder::declareOpaqueTasks(
    const SoftShadowSplitTaskChainInputs& inputs,
    const SoftShadowSplitResourceUses& uses,
    const Core::GpuTaskId& shadowTraceDependency,
    const Core::GpuTaskSchedulingHint& tailScheduling,
    SoftShadowSplitTasks& tasks
){
    using namespace RendererTaskGraphDetail;
    const SoftShadowSplitRoute& route = inputs.route;

    Core::GpuTaskSchedulingHint opaqueScheduling;
    opaqueScheduling.cost = Core::GpuTaskCostHint::Large;
    opaqueScheduling.forceSubmissionBoundary = false;
    opaqueScheduling.allowPacketMerge = true;
    opaqueScheduling.mergeWithPrevious = false;
    EnableSameFamilyComputeEffectRouting(opaqueScheduling, false);
    EnableCrossFamilyComputeEffectRouting(opaqueScheduling);
    Core::GpuTaskDesc opaqueDesc;
    opaqueDesc
        .setIdentity(Name("render.shadow_visibility.opaque"))
        .setMarkerLabel("Shadow Visibility Opaque")
        .setScheduling(opaqueScheduling)
        .setDependencies(&shadowTraceDependency, 1u)
        .setExternalDependencies(inputs.historyWriterDrainDependencies, inputs.historyWriterDrainDependencyCount)
        .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
        .setResourceUses(uses.opaque.data(), uses.opaque.size())
        .setResourceSetUses(
            route.hardwareCsgShadow ? inputs.traceResourceSetUses
                : (!route.hardwareShadowSupported && route.traceGeometryStatesGraphOwned ? &inputs.traceGeometrySetUse : nullptr),
            route.hardwareCsgShadow ? inputs.traceResourceSetUseCount : (!route.hardwareShadowSupported && route.traceGeometryStatesGraphOwned ? 1u : 0u)
        )
    ;
    const auto opaqueTaskResult = m_raytracingSystem.declareShadowVisibilityOpaqueTask(
        m_graph,
        opaqueDesc,
        inputs.deferredLightingResources,
        inputs.shadowPrepared,
        route.hardwareShadowSupported,
        route.graphOwnsOpaqueTemporalMergeEntryStates,
        &inputs.rayTracingPlan.lightSpace,
        inputs.targets,
        inputs.timingTicket,
        inputs.asyncTiming,
        inputs.shadowVisibilityTiming,
        inputs.opaqueProduced,
        inputs.opaqueFrameIndex
    );
    if(!opaqueTaskResult){
        tasks.opaque = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred opaque shadow-visibility graph task"));
        return false;
    }
    tasks.opaque = *opaqueTaskResult;

    if(!route.combinedSoftTemporal){
        const Core::GpuTaskId opaqueFirstWaveletDependencies[] = { tasks.opaque };
        Core::GpuTaskDesc opaqueFirstWaveletDesc;
        opaqueFirstWaveletDesc
            .setIdentity(route.combinedSoftWavelet ? Name("render.shadow_visibility.opaque_temporal_merge") : Name("render.shadow_visibility.opaque_first_wavelet"))
            .setMarkerLabel(route.combinedSoftWavelet ? "Shadow Opaque Temporal Merge" : "Shadow Opaque First Wavelet")
            .setScheduling(tailScheduling)
            .setDependencies(opaqueFirstWaveletDependencies, LengthOf(opaqueFirstWaveletDependencies))
            .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
            .setResourceUses(uses.opaqueFirstWavelet.data(), uses.opaqueFirstWavelet.size())
        ;
        const auto opaqueFirstWaveletTaskResult = m_raytracingSystem.declareShadowVisibilityOpaqueFirstWaveletTask(
            m_graph,
            opaqueFirstWaveletDesc,
            inputs.deferredLightingResources,
            inputs.opaqueFrameIndex,
            route.hardwareShadowSupported,
            route.graphOwnsOpaqueTemporalMergeEntryStates,
            route.combinedSoftUpsample,
            route.combinedSoftWavelet,
            inputs.targets,
            inputs.timingTicket,
            inputs.asyncTiming,
            inputs.shadowVisibilityTiming,
            inputs.opaqueResolveTiming,
            inputs.opaqueProduced
        );
        if(!opaqueFirstWaveletTaskResult){
            tasks.opaqueFirstWavelet = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred opaque soft-shadow first-wavelet graph task"));
            return false;
        }
        tasks.opaqueFirstWavelet = *opaqueFirstWaveletTaskResult;
    }

    if(!route.combinedSoftUpsample){
        const Core::GpuTaskId opaqueResolveDependencies[] = { tasks.opaqueFirstWavelet };
        Core::GpuTaskDesc opaqueResolveDesc;
        opaqueResolveDesc
            .setIdentity(Name("render.shadow_visibility.opaque_soft_resolve"))
            .setMarkerLabel("Shadow Opaque Soft Resolve")
            .setScheduling(tailScheduling)
            .setDependencies(opaqueResolveDependencies, LengthOf(opaqueResolveDependencies))
            .setResourceUses(uses.opaqueResolve.data(), uses.opaqueResolve.size())
        ;
        const auto opaqueResolveTaskResult = m_raytracingSystem.declareShadowVisibilityOpaqueResolveTailTask(
            m_graph,
            opaqueResolveDesc,
            inputs.deferredLightingResources,
            inputs.opaqueFrameIndex,
            route.hardwareShadowSupported,
            inputs.targets,
            inputs.timingTicket,
            inputs.asyncTiming,
            inputs.shadowVisibilityTiming,
            inputs.opaqueResolveTiming,
            inputs.opaqueProduced
        );
        if(!opaqueResolveTaskResult){
            tasks.opaqueResolve = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred opaque soft-shadow resolve-tail graph task"));
            return false;
        }
        tasks.opaqueResolve = *opaqueResolveTaskResult;
    }
    return true;
}

bool SoftShadowSplitTaskChainBuilder::declareTransparentTasks(
    Core::Alloc::ScratchArena& scratchArena,
    const SoftShadowSplitTaskChainInputs& inputs,
    const SoftShadowSplitResourceUses& uses,
    const Core::GpuTaskSchedulingHint& tailScheduling,
    const Core::GpuGraphResourceId csgShadowContext,
    SoftShadowSplitTasks& tasks
){
    const SoftShadowSplitRoute& route = inputs.route;

    const Core::GpuTaskId opaqueCompletionTask = route.combinedSoftTemporal ? tasks.opaque : route.combinedSoftUpsample ? tasks.opaqueFirstWavelet : tasks.opaqueResolve;
    const Core::GpuTaskId traceDependencies[] = { opaqueCompletionTask };
    Core::GpuTaskDesc traceDesc;
    traceDesc
        .setIdentity(Name("render.shadow_visibility.soft_transparent_trace"))
        .setMarkerLabel("Shadow Transparent Soft Trace")
        .setScheduling(tailScheduling)
        .setDependencies(traceDependencies, LengthOf(traceDependencies))
        .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
        .setResourceUses(uses.transparentTrace.data(), uses.transparentTrace.size())
        .setResourceSetUses(inputs.traceResourceSetUseCount != 0u ? inputs.traceResourceSetUses : nullptr, inputs.traceResourceSetUseCount)
    ;
    const auto transparentTraceTaskResult = m_raytracingSystem.declareShadowTransparentSoftTraceTask(
        m_graph,
        traceDesc,
        inputs.deferredLightingResources,
        inputs.opaqueProduced,
        inputs.opaqueFrameIndex,
        &inputs.rayTracingPlan.lightSpace,
        inputs.targets,
        inputs.timingTicket,
        inputs.transparentTraceProduced
    );
    if(!transparentTraceTaskResult){
        tasks.transparentTrace = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred transparent soft-shadow trace graph task"));
        return false;
    }
    tasks.transparentTrace = *transparentTraceTaskResult;

    Core::GpuTaskId transparentFirstWaveletDependency = tasks.transparentTrace;
    if(uses.graphOwnsTransparentTemporalMergeEntryStates){
        const Core::GpuTaskId transparentTemporalMergeDependencies[] = { transparentFirstWaveletDependency, };
        Core::GpuTaskDesc transparentTemporalMergeDesc;
        transparentTemporalMergeDesc
            .setIdentity(route.combinedSoftTemporal ? Name("render.shadow_visibility.combined_temporal_merge") : Name("render.shadow_visibility.transparent_temporal_merge"))
            .setMarkerLabel(route.combinedSoftTemporal ? "Shadow Combined Temporal Merge" : "Shadow Transparent Temporal Merge")
            .setScheduling(tailScheduling)
            .setDependencies(transparentTemporalMergeDependencies, LengthOf(transparentTemporalMergeDependencies))
            .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
            .setResourceUses(uses.transparentTemporalMerge.data(), uses.transparentTemporalMerge.size())
        ;
        const auto transparentTemporalMergeTaskResult = m_raytracingSystem.declareShadowTransparentSoftTemporalMergeTask(
            m_graph,
            transparentTemporalMergeDesc,
            inputs.deferredLightingResources,
            inputs.opaqueProduced,
            inputs.opaqueFrameIndex,
            true,
            route.combinedSoftTemporal,
            route.hardwareShadowSupported,
            inputs.targets,
            inputs.timingTicket,
            inputs.transparentResolveTiming,
            inputs.transparentTraceProduced
        );
        if(!transparentTemporalMergeTaskResult){
            tasks.transparentTemporalMerge = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred transparent soft-shadow temporal-merge graph task"));
            return false;
        }
        tasks.transparentTemporalMerge = *transparentTemporalMergeTaskResult;
        transparentFirstWaveletDependency = tasks.transparentTemporalMerge;
    }

    const Core::GpuTaskId transparentFirstWaveletDependencies[] = { transparentFirstWaveletDependency };
    Core::GpuTaskDesc transparentFirstWaveletDesc;
    transparentFirstWaveletDesc
        .setIdentity(route.combinedSoftWavelet ? Name("render.shadow_visibility.combined_first_wavelet") : Name("render.shadow_visibility.transparent_first_wavelet"))
        .setMarkerLabel(route.combinedSoftWavelet ? "Shadow Combined First Wavelet" : "Shadow Transparent First Wavelet")
        .setScheduling(tailScheduling)
        .setDependencies(transparentFirstWaveletDependencies, LengthOf(transparentFirstWaveletDependencies))
        .setResourceUses(uses.transparentFirstWavelet.data(), uses.transparentFirstWavelet.size())
    ;
    const auto transparentFirstWaveletTaskResult = m_raytracingSystem.declareShadowTransparentSoftFirstWaveletTask(
        m_graph,
        transparentFirstWaveletDesc,
        inputs.deferredLightingResources,
        inputs.opaqueProduced,
        inputs.opaqueFrameIndex,
        true,
        !uses.graphOwnsTransparentTemporalMergeEntryStates,
        route.combinedSoftWavelet,
        inputs.targets,
        inputs.timingTicket,
        inputs.transparentResolveTiming,
        inputs.transparentTraceProduced
    );
    if(!transparentFirstWaveletTaskResult){
        tasks.transparentFirstWavelet = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred transparent soft-shadow first-wavelet graph task"));
        return false;
    }
    tasks.transparentFirstWavelet = *transparentFirstWaveletTaskResult;

    const Core::GpuTaskId foldDependencies[] = { tasks.transparentFirstWavelet };
    Core::GpuTaskDesc foldDesc;
    foldDesc
        .setIdentity(Name("render.shadow_visibility.soft_transparent_fold"))
        .setMarkerLabel("Shadow Transparent Soft Fold")
        .setScheduling(tailScheduling)
        .setDependencies(foldDependencies, LengthOf(foldDependencies))
        .setResourceUses(uses.transparentFold.data(), uses.transparentFold.size())
    ;
    // The terminal fold owns effect completion, so lighting, recovery and acceptance observe fully folded visibility.
    const auto foldTaskResult = m_raytracingSystem.declareShadowTransparentSoftFoldTask(
        m_graph,
        foldDesc,
        inputs.deferredLightingResources,
        inputs.opaqueProduced,
        inputs.opaqueFrameIndex,
        route.combinedSoftUpsample,
        inputs.targets,
        inputs.timingTicket,
        inputs.asyncTiming,
        inputs.shadowVisibilityTiming,
        inputs.transparentResolveTiming,
        inputs.transparentTraceProduced
    );
    if(!foldTaskResult){
        tasks.fold = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred soft-transparent shadow-fold graph task"));
        return false;
    }
    tasks.fold = *foldTaskResult;
    if(inputs.rayTracingPlan.lightSpace.diagnosticReadback){
        const auto diagnosticsTaskResult = DeclareLightSpaceShadowDiagnostics(m_graph, LightSpaceShadowGraphInputs{
            .graphics = m_graphics,
            .arena = m_arena,
            .scratchArena = scratchArena,
            .shadowPrepared = inputs.shadowPrepared,
            .snapshot = inputs.rayTracingPlan.lightSpace,
            .dependency = tasks.fold,
            .stateSources = inputs.stateSources,
            .stateSourceCount = inputs.stateSourceCount,
        }, csgShadowContext
        );
        if(!diagnosticsTaskResult){
            tasks.diagnostics = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare CSG shadow diagnostics readback"));
            return false;
        }
        tasks.diagnostics = *diagnosticsTaskResult;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

