// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_refraction_resolve.h"

#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RefractionClearTileArgumentsGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    if(context.commandIrCapture && !context.commandIrCapture->captureClearBuffer(context.task, context.packet, context.queue, payload.resource, 0u))
        return false;
    commandList.endRenderPass();
    Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_RefractionClearTileArguments, payload.graphics.getDevice(), commandList);

    commandList.clearBufferUInt(*payload.destination, 0u);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RefractionClearStatisticsGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    if(!payload.destination)
        return false;
    if(context.commandIrCapture && !context.commandIrCapture->captureClearBuffer(context.task, context.packet, context.queue, payload.resource, 0u))
        return false;
    commandList.endRenderPass();
    commandList.clearBufferUInt(*payload.destination, 0u);
    return !commandList.commandRecordingFailed();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RefractionStatisticsReadbackGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
    if(!payload.source || !payload.destination || !payload.reservation.valid())
        return false;
    if(context.commandIrCapture && !context.commandIrCapture->captureCopyBuffer(context.task, context.packet, context.queue, payload.sourceResource, 0u, payload.destinationResource, 0u, NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE))
        return false;
    commandList.endRenderPass();
    commandList.copyBuffer(*payload.destination, 0u, *payload.source, 0u, NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE);
    return !commandList.commandRecordingFailed();
}

void RefractionStatisticsReadbackGraphTask::Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
    const b8 hardwareReady = payload.hardwareEnabled && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
    payload.reservation.acceptReservation(token, hardwareReady);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RefractionResolveGraphTask::Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
    RayTracingRefractionGraphResources resources = payload.resources;
    if(resources.usesHardwareTrace && (!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady)){
        // A nonfatal TLAS miss still accepts. Use the screen pipeline (its inputs subset this task's) instead of a stale TLAS.
        resources.pipeline = resources.screenFallbackPipeline;
        resources.densePipeline = resources.denseScreenFallbackPipeline;
        resources.usesHardwareTrace = false;
        resources.tlasHeapHandle = Core::GpuDescriptorHandle::Invalid();
        resources.materialContextSlotsHeapSlot = 0u;
    }
    return payload.system.recordRefractionResolve(commandList, payload.targets, resources);
}

void RefractionResolveGraphTask::Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
    // Only an accepted packet publishes diagnostics. Rejected retries leave the latch; selection/recording stays stateless.
    const b8 usedHardware = payload.resources.usesHardwareTrace && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
    if(usedHardware)
        payload.system.confirmRayTraceContextReadSubmission(token);
    b8* const dispatchLogged = payload.resources.usesHardwareTrace && !usedHardware ? payload.screenFallbackDispatchLogged : payload.dispatchLogged;
    if(dispatchLogged && !*dispatchLogged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AVBOIT refraction resolve: {}"), usedHardware ? NWB_TEXT("hardware") : NWB_TEXT("screen-space"));
        *dispatchLogged = true;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> FrameGraphRefractionResolve::declare(const FrameGraphRefractionResolveInputs& inputs, Core::Alloc::ScratchArena& scratchArena, RayTracingSceneGraphReads& sceneReads){
    using namespace RendererTaskGraphDetail;
    DeferredFrameTargets& deferredTargets = inputs.targets;
    const RayTracingSceneGraphResources& sceneResources = inputs.sceneResources;
    const RayTracingRefractionGraphResources& refractionResources = inputs.refractionResources;
    const b8 refractionActive = inputs.refractionActive;
    const Core::GpuGraphResourceId depth = inputs.depth;
    const Core::GpuGraphResourceId worldPosition = inputs.worldPosition;
    const Core::GpuGraphResourceId refractionDepth = inputs.refractionDepth;
    const Core::GpuGraphResourceId refractionNormalIor = inputs.refractionNormalIor;
    const Core::GpuGraphResourceId refractionTintCoverage = inputs.refractionTintCoverage;
    const Core::GpuGraphResourceId refractionInstance = inputs.refractionInstance;
    const Core::GpuGraphResourceId refractionResolve = inputs.refractionResolve;
    const Core::GpuGraphResourceId opaqueColor = inputs.opaqueColor;
    const Core::GpuGraphResourceId opaqueReflectionRadiance = inputs.opaqueReflectionRadiance;
    const Core::GpuGraphResourceId meshView = inputs.meshView;
    const Core::GpuGraphResourceId currentBindlessSlots = inputs.currentBindlessSlots;
    const Core::GpuGraphResourceId sceneShading = inputs.sceneShading;
    const Core::GpuGraphResourceId lights = inputs.lights;
    const Core::GpuGraphResourceId avboitAccumColor = inputs.avboitAccumColor;
    const Core::GpuGraphResourceId avboitAccumExtinction = inputs.avboitAccumExtinction;
    const Core::GpuGraphResourceId avboitForegroundColor = inputs.avboitForegroundColor;
    const Core::GpuGraphResourceId avboitForegroundExtinction = inputs.avboitForegroundExtinction;
    const Core::GpuGraphResourceSetId hardwareTraceGeometrySet = inputs.hardwareTraceGeometrySet;
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet = inputs.traceMaterialSampledTextureSet;

    if(!refractionActive || !refractionResources.valid()){
        Core::GpuTaskDesc clearDesc;
        clearDesc.setIdentity(Name("render.avboit.refraction_resolve_clear")).setMarkerLabel("AVBOIT Refraction Resolve Clear").setDependencies({ &inputs.avboitFinalTask, 1u });
        Core::GpuClearTextureTaskDesc clear;
        clear.destination = refractionResolve;
        clear.subresources = ECSRenderDetail::s_FramebufferSubresources;
        clear.valueType = Core::GpuClearTextureTaskValueType::Float;
        clear.floatValue = Core::Color(0.f, 0.f, 0.f, 0.f);
        return m_graph.addClearTextureTask(clearDesc, clear);
    }

    if(!sceneReads.valid() && refractionResources.usesHardwareTrace){
        auto sceneReadsResult = ImportRayTracingSceneGraphReads(sceneResources, m_raytracingSystem.sceneTlasBackingInitialState(), inputs.traceContextUploadTask, m_graph, scratchArena);
        if(!sceneReadsResult){
            sceneReads = RayTracingSceneGraphReads(scratchArena);
            return MakeUnexpected(Failure{});
        }
        sceneReads = Move(*sceneReadsResult);
    }

    const auto tileListResult = m_graph.importBuffer(deferredTargets.avboit.refractionTileList, BufferResourceDesc(Name("render.refraction.tile_list"), "Refraction Tile List"));
    const auto tileArgumentsResult = m_graph.importBuffer(deferredTargets.avboit.refractionTileArguments, BufferResourceDesc(Name("render.refraction.tile_arguments"), "Refraction Tile Arguments"));
    if(!tileListResult || !tileArgumentsResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId tileList = *tileListResult;
    const Core::GpuGraphResourceId tileArguments = *tileArgumentsResult;
    const Core::GpuTaskResourceUse tileClearUse = WriteUse(tileArguments, Core::ResourceStates::CopyDest);
    Core::GpuTaskDesc tileClearDesc;
    tileClearDesc.setIdentity(Name("render.avboit.refraction_tile_arguments_clear")).setMarkerLabel("Refraction Tile Arguments Clear").setDependencies({ &inputs.avboitFinalTask, 1u }).setResourceUses({ &tileClearUse, 1u });
    const auto tileClearTask = m_graph.addTask<RefractionClearTileArgumentsGraphTask>(tileClearDesc, RefractionClearTileArgumentsGraphTask::Payload{ m_graphics, deferredTargets.avboit.refractionTileArguments, tileArguments });
    if(!tileClearTask)
        return MakeUnexpected(Failure{});
    const Array<Core::GpuTaskResourceUse, 4> tileUses = {
        ReadUse(refractionDepth),
        ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteUse(tileList, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(tileArguments, Core::ResourceStates::UnorderedAccess),
    };
    Core::GpuTaskSchedulingHint refractionTileScheduling;
    refractionTileScheduling.mergeWithPrevious = true;
    refractionTileScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc tileDesc;
    tileDesc.setIdentity(Name("render.avboit.refraction_classify_tiles")).setMarkerLabel("Refraction Classify Tiles").setScheduling(refractionTileScheduling).setDependencies({ &*tileClearTask, 1u }).setResourceUses(tileUses);
    const auto tileTask = m_graph.addTask<RefractionClassifyTilesGraphTask>(tileDesc, RefractionClassifyTilesGraphTask::Payload{ m_raytracingSystem, deferredTargets, refractionResources });
    if(!tileTask)
        return MakeUnexpected(Failure{});
    Core::GpuGraphResourceId refractionCounters;
    Core::GpuTaskId refractionDiagnosticClear;
    if(refractionResources.statistics.control){
        const auto counters = m_graph.importBuffer(refractionResources.statistics.counters, BufferResourceDesc(Name("render.refraction.diagnostic_counters"), "Refraction Diagnostic Counters").setExternalFinalState(Core::ResourceStates::UnorderedAccess));
        if(!counters)
            return MakeUnexpected(Failure{});
        refractionCounters = *counters;
        const Core::GpuTaskResourceUse clearUse = WriteUse(refractionCounters, Core::ResourceStates::CopyDest);
        Core::GpuTaskDesc clearDesc;
        clearDesc.setIdentity(Name("render.refraction.diagnostics_clear")).setMarkerLabel("Refraction Diagnostics Clear").setScheduling(refractionTileScheduling).setDependencies({ &*tileTask, 1u }).setResourceUses({ &clearUse, 1u });
        const auto clearTask = m_graph.addTask<RefractionClearStatisticsGraphTask>(clearDesc, RefractionClearStatisticsGraphTask::Payload{ refractionResources.statistics.counters, refractionCounters });
        if(!clearTask)
            return MakeUnexpected(Failure{});
        refractionDiagnosticClear = *clearTask;
    }
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> refractionUses{scratchArena};
    const Array<Core::GpuGraphResourceId, 12> refractionInputs = {
        refractionDepth,
        refractionNormalIor,
        refractionTintCoverage,
        refractionInstance,
        opaqueColor,
        opaqueReflectionRadiance,
        worldPosition,
        depth,
        avboitAccumColor,
        avboitAccumExtinction,
        avboitForegroundColor,
        avboitForegroundExtinction,
    };
    // Mesh view, bindless slots, scene shading, lights, the resolve output, and tile scheduling buffers.
    refractionUses.reserve(refractionInputs.size() + s_RefractionFixedUseCapacity + sceneReads.uses.size() + sceneReads.csgUses.size());
    for(const auto input : refractionInputs)
        refractionUses.push_back(ReadUse(input));
    refractionUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
    refractionUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    refractionUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    refractionUses.push_back(ReadUse(lights));
    refractionUses.push_back(WriteUse(refractionResolve, Core::ResourceStates::UnorderedAccess));
    refractionUses.push_back(ReadUse(tileList));
    refractionUses.push_back(ReadUse(tileArguments, Core::ResourceStates::ShaderResource | Core::ResourceStates::IndirectArgument));
    if(refractionCounters.valid())
        refractionUses.push_back(ReadWriteUse(refractionCounters, Core::ResourceStates::UnorderedAccess));
    RendererFramePipelineDetail::TraceResourceSetUses refractionSets{};
    if(refractionResources.usesHardwareTrace){
        for(const Core::GpuTaskResourceUse& read : sceneReads.uses)
            refractionUses.push_back(read);
        for(const Core::GpuTaskResourceUse& read : sceneReads.csgUses)
            refractionUses.push_back(read);
        refractionSets = RendererFramePipelineDetail::MakeTraceResourceSetUses(hardwareTraceGeometrySet, traceMaterialSampledTextureSet);
    }
    const Array<Core::GpuTaskId, 3> refractionDependencies = { inputs.reflectionCompletionTask, refractionDiagnosticClear.valid() ? refractionDiagnosticClear : *tileTask, inputs.surfelGiTask };
    Core::GpuTaskDesc refractionDesc;
    refractionDesc.setIdentity(Name("render.avboit.refraction_resolve"))
        .setMarkerLabel("AVBOIT Refraction Resolve")
        .setScheduling(refractionTileScheduling)
        .setDependencies(refractionDependencies)
        .setResourceUses(refractionUses)
        .setResourceSetUses({ refractionSets.uses.data(), refractionSets.useCount })
    ;
    RefractionResolveGraphTask::Payload resolvePayload{
        .system = m_raytracingSystem,
        .targets = deferredTargets,
        .resources = refractionResources,
        .hardwarePreparationReady = &inputs.hardwarePreparationReady,
        .dispatchLogged = refractionResources.usesHardwareTrace ? &inputs.refractionHardwareLogged : &inputs.refractionScreenLogged,
        .screenFallbackDispatchLogged = &inputs.refractionScreenLogged,
    };
    Expected<Core::GpuTaskId> refractionResolveTask = refractionCounters.valid()
        ? m_graph.addTask<RefractionDiagnosticResolveGraphTask>(refractionDesc, Move(resolvePayload))
        : m_graph.addTask<RefractionResolveGraphTask>(refractionDesc, Move(resolvePayload))
    ;
    if(!refractionResolveTask)
        return MakeUnexpected(Failure{});
    if(refractionCounters.valid()){
        RefractionStatisticsReservation reservation(refractionResources.statistics.control, refractionResources.statistics.metadata);
        // All slots may still be in flight; diagnostic collection is lossy rather than blocking rendering.
        if(reservation.valid()){
            const Core::BufferHandle& readback = refractionResources.statistics.buffers[reservation.slotIndex()];
            if(!readback)
                return MakeUnexpected(Failure{});
            const auto readbackResource = m_graph.importBuffer(readback, BufferResourceDesc(readback->getCreationDescription().debugName, "Refraction Statistics Readback").setExternalFinalState(Core::ResourceStates::CopyDest));
            if(!readbackResource)
                return MakeUnexpected(Failure{});
            const Array<Core::GpuTaskResourceUse, 2> copyUses = {
                ReadUse(refractionCounters, Core::ResourceStates::CopySource),
                WriteUse(*readbackResource, Core::ResourceStates::CopyDest),
            };
            Core::GpuTaskDesc copyDesc;
            copyDesc.setIdentity(Name("render.refraction.statistics")).setMarkerLabel("Refraction Statistics Readback").setScheduling(refractionTileScheduling).setDependencies({ &*refractionResolveTask, 1u }).setResourceUses(copyUses);
            refractionResolveTask = m_graph.addTask<RefractionStatisticsReadbackGraphTask>(
                copyDesc,
                RefractionStatisticsReadbackGraphTask::Payload{
                    refractionResources.statistics.counters,
                    readback,
                    refractionCounters,
                    *readbackResource,
                    Move(reservation),
                    &inputs.hardwarePreparationReady,
                    refractionResources.usesHardwareTrace,
                }
            );
        }
    }
    return refractionResolveTask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

