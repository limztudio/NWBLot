// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/gi/surfel_gi_graph_coordinator.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/kernel/task_graph_float_clear_tasks.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/gi/surfel_gi_lifecycle_builder.h>
#include <impl/ecs_render/gi/surfel_gi_resource_builder.h>
#include <impl/ecs_render/gi/surfel_gi_counter_readback_builder.h>
#include <impl/ecs_render/gi/surfel_gi_task_builder.h>
#include <impl/ecs_render/gi/quality_settings.adhd.h>

#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SurfelGiGraphCoordinator::declareDeferredSurfelGiTask(
    const SurfelGiGraphContext& context,
    const DeferredLightingGraphResources& deferredLightingResources,
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
    const RayTracingSurfelPersistentResourceSnapshot& rayTracingSurfelResources,
    const Core::GpuGraphResourceId worldPosition,
    const Core::GpuGraphResourceId normal,
    const Core::GpuGraphResourceId surfelIrradiance,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId sceneShading,
    const Core::GpuGraphResourceId lights,
    const Core::GpuGraphResourceId materialContextSlots,
    const Span<const Core::GpuGraphResourceId> traceGeometryResources,
    const Core::GpuGraphResourceSetId traceGeometrySet,
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
    const Core::GpuTaskId effectsTask,
    const b8 useLaggedLightingHistory,
    const Core::GpuExternalCompletionId surfelCounterReadbackCompletion,
    Core::Alloc::ScratchArena& scratchArena,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming
){
    using namespace RendererTaskGraphDetail;

    SurfelGiTaskBuilder surfelGiTasks(context.rayTracing, context.graph);
    m_deferredSurfelGiPreparationTask = {};
    m_deferredSurfelGiInitializationLifecycleTask = {};
    m_deferredSurfelGiSnapshotCopyTask = {};
    m_deferredSurfelGiIrradianceClearTask = {};
    m_deferredSurfelGiAgeFreeTask = {};
    m_deferredSurfelGiCellHeadClearTask = {};
    m_deferredSurfelGiHashBuildTask = {};
    m_deferredSurfelGiSpawnTask = {};
    m_deferredSurfelGiTraceBuildArgsTask = {};
    m_deferredSurfelGiTraceTask = {};
    m_deferredSurfelGiResolveTask = {};
    asyncTiming.reset();
    m_deferredSurfelGiTask = {};
    m_deferredSurfelGiCounterReadbackTask = {};
    if(!deferredTargets.valid() || !deferredTargets.bindless.valid() || !worldPosition.valid() || !normal.valid() || !surfelIrradiance.valid() || !currentBindlessSlots.valid() || !sceneShading.valid() || !lights.valid() || !effectsTask.valid())
        return false;

    const b8 useHwTrace = rayTracingResources.surfelUsesHardwareTrace;
    const b8 hasSurfelWork = context.rayTracing.hasSurfelWork();
    const b8 traceGeometryStatesGraphOwned = traceGeometrySet.valid();
    const RendererFramePipelineDetail::TraceResourceSetUses traceResourceSets = RendererFramePipelineDetail::MakeTraceResourceSetUses(traceGeometrySet, traceMaterialSampledTextureSet);

    const Core::GpuTaskExternalStateSource surfelGiComputeStateSource{ .states = context.computeState.source(), };
    const Core::GpuTaskExternalStateSource surfelGiCounterStateSource{ .states = context.counterState.source(), };
    const Core::GpuTaskExternalStateSource surfelIrradianceReturnStateSource{ .states = context.irradianceReturnState.source(), .applicableConsumerQueueClass = Core::CommandQueue::Compute, };
    struct SurfelGiTaskStateSourceSet{
        Array<Core::GpuTaskExternalStateSource, s_SurfelGiStateSourceCapacity> values{};
        usize count = 0u;
    };
    const auto makeSurfelGiTaskStateSources = [&](const b8 includeCompute, const b8 includeCounter, const b8 includeIrradianceReturn)noexcept{
        SurfelGiTaskStateSourceSet result;
        if(includeCompute && surfelGiComputeStateSource.states){
            result.values[result.count] = surfelGiComputeStateSource;
            ++result.count;
        }
        if(includeCounter && surfelGiCounterStateSource.states){
            result.values[result.count] = surfelGiCounterStateSource;
            ++result.count;
        }
        if(includeIrradianceReturn && surfelIrradianceReturnStateSource.states){
            result.values[result.count] = surfelIrradianceReturnStateSource;
            ++result.count;
        }
        return result;
    };
    const SurfelGiTaskStateSourceSet surfelGiComputeCounterStateSources = makeSurfelGiTaskStateSources(true, true, false);
    const SurfelGiTaskStateSourceSet surfelGiComputeReturnStateSources = makeSurfelGiTaskStateSources(true, false, true);
    const SurfelGiTaskStateSourceSet surfelGiAllStateSources = makeSurfelGiTaskStateSources(true, true, true);

    SurfelGiResourcePlan surfelGiResourcePlan(scratchArena);
    const SurfelGiResourceInputs surfelGiResourceInputs{
        deferredTargets, rayTracingResources, rayTracingSurfelResources,
        worldPosition, normal, surfelIrradiance, currentBindlessSlots, sceneShading, lights, materialContextSlots,
        traceGeometryResources, traceGeometryStatesGraphOwned, useHwTrace, hasSurfelWork
    };
    SurfelGiResourceBuilder surfelGiResourceBuilder(context.graph, context.rayTracing);
    if(!surfelGiResourceBuilder.prepare(surfelGiResourceInputs, surfelGiResourcePlan))
        return false;
    const auto& surfelIrradianceHalf = surfelGiResourcePlan.surfelIrradianceHalf;
    const auto& csgTraceContext = surfelGiResourcePlan.csgTraceContext;
    const auto& surfelConstants = surfelGiResourcePlan.surfelConstants;
    const auto& surfelPool = surfelGiResourcePlan.surfelPool;
    const auto& surfelGuidePool = surfelGiResourcePlan.surfelGuidePool;
    const auto& surfelCellHead = surfelGiResourcePlan.surfelCellHead;
    const auto& surfelCounter = surfelGiResourcePlan.surfelCounter;
    const auto& surfelTraceIndirectArgs = surfelGiResourcePlan.surfelTraceIndirectArgs;
    const auto& surfelFreeList = surfelGiResourcePlan.surfelFreeList;
    const auto& surfelPoolSnapshot = surfelGiResourcePlan.surfelPoolSnapshot;
    const auto& surfelCellHeadSnapshot = surfelGiResourcePlan.surfelCellHeadSnapshot;
    const auto& graphOwnsSurfelGiResolve = surfelGiResourcePlan.graphOwnsSurfelGiResolve;
    const auto& resourceUses = surfelGiResourcePlan.resourceUses;
    const auto& traceResourceUses = surfelGiResourcePlan.traceResourceUses;

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    // Live Lighting joins both effects; lagged Lighting retains their independent publication boundaries.
    scheduling.forceSubmissionBoundary = useLaggedLightingHistory;
    scheduling.allowPacketMerge = !useLaggedLightingHistory;
    scheduling.mergeWithPrevious = !useLaggedLightingHistory;
    scheduling.allowMergeAcrossConsumerFrontier = !useLaggedLightingHistory;
    const Core::GpuExternalCompletionId* const surfelGiExternalDependencies = surfelCounterReadbackCompletion.valid() ? &surfelCounterReadbackCompletion : nullptr;
    const usize surfelGiExternalDependencyCount = surfelCounterReadbackCompletion.valid() ? 1u : 0u;

    SurfelGiLifecycleBuilder surfelGiLifecycleBuilder(context.graph, context.rayTracing);
    SurfelGiLifecycleInputs surfelGiLifecycleInputs;
    surfelGiLifecycleInputs.targets = &deferredTargets;
    surfelGiLifecycleInputs.dependency = effectsTask;
    surfelGiLifecycleInputs.surfelPool = surfelPool;
    surfelGiLifecycleInputs.surfelCellHead = surfelCellHead;
    surfelGiLifecycleInputs.surfelCounter = surfelCounter;
    surfelGiLifecycleInputs.surfelFreeList = surfelFreeList;
    surfelGiLifecycleInputs.surfelPoolSnapshot = surfelPoolSnapshot;
    surfelGiLifecycleInputs.surfelCellHeadSnapshot = surfelCellHeadSnapshot;
    surfelGiLifecycleInputs.poolSnapshotByteSize = rayTracingSurfelResources.poolBuffer->getCreationDescription().byteSize;
    surfelGiLifecycleInputs.cellHeadSnapshotByteSize = rayTracingSurfelResources.cellHeadBuffer->getCreationDescription().byteSize;
    surfelGiLifecycleInputs.scheduling = scheduling;
    surfelGiLifecycleInputs.externalDependencies = { surfelGiExternalDependencies, surfelGiExternalDependencyCount };
    surfelGiLifecycleInputs.computeStateSource = surfelGiComputeStateSource;
    surfelGiLifecycleInputs.counterStateSource = surfelGiCounterStateSource;
    surfelGiLifecycleInputs.hasWork = hasSurfelWork;
    auto surfelGiLifecycleResult = surfelGiLifecycleBuilder.declare(surfelGiLifecycleInputs);
    if(!surfelGiLifecycleResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI lifecycle"));
        return false;
    }
    m_deferredSurfelGiPreparationTask = surfelGiLifecycleResult->preparationTask;
    m_deferredSurfelGiInitializationLifecycleTask = surfelGiLifecycleResult->initializationLifecycleTask;
    m_deferredSurfelGiSnapshotCopyTask = surfelGiLifecycleResult->snapshotCopyTask;
    Core::GpuTaskId surfelGiDependency = surfelGiLifecycleResult->dependency;

    // Zero coverage is the no-op; keep its clear at the front of the GI packet.
    Core::GpuTaskSchedulingHint surfelIrradianceClearScheduling;
    surfelIrradianceClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    surfelIrradianceClearScheduling.allowPacketMerge = true;
    surfelIrradianceClearScheduling.mergeWithPrevious = !useLaggedLightingHistory;
    surfelIrradianceClearScheduling.allowMergeAcrossConsumerFrontier = !useLaggedLightingHistory;
    // Compatible live effects share the snapshot packet; the GI chain retains its selected queue.
    EnableSameFamilyComputeEffectRouting(surfelIrradianceClearScheduling, false);
    EnableCrossFamilyComputeEffectRouting(surfelIrradianceClearScheduling);
    const Core::GpuTaskResourceUse surfelIrradianceClearResourceUse = WriteTextureUse(surfelIrradiance, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    Core::GpuTaskDesc surfelIrradianceClearDesc;
    surfelIrradianceClearDesc
        .setIdentity(Name(s_SurfelGraphIrradianceClear))
        .setMarkerLabel("Surfel Irradiance Clear")
        .setScheduling(surfelIrradianceClearScheduling)
        .setDependencies({ &surfelGiDependency, 1u })
        .setResourceUses({ &surfelIrradianceClearResourceUse, 1u })
    ;
    const auto surfelIrradianceClearTaskResult = context.graph.addTask<ECSRenderDetail::FloatTextureClearGraphTask>(
        surfelIrradianceClearDesc,
        ECSRenderDetail::FloatTextureClearGraphTask::Payload{ .destination = surfelIrradiance, .subresources = ECSRenderDetail::s_FramebufferSubresources, .floatValue = ECSRenderDetail::s_SurfelIrradianceClearColor, });
    if(!surfelIrradianceClearTaskResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred surfel-irradiance clear"));
        return false;
    }
    const Core::GpuTaskId surfelIrradianceClearTask = *surfelIrradianceClearTaskResult;
    m_deferredSurfelGiIrradianceClearTask = surfelIrradianceClearTask;
    surfelGiDependency = surfelIrradianceClearTask;

    Core::GpuTaskSchedulingHint surfelGiScheduling = scheduling;
    surfelGiScheduling.forceSubmissionBoundary = false;
    surfelGiScheduling.allowPacketMerge = true;
    surfelGiScheduling.mergeWithPrevious = true;
    // GI stages share one accepted timing packet; cross-queue consumers wait for it.
    surfelGiScheduling.allowMergeAcrossConsumerFrontier = true;
    EnableSameFamilyComputeEffectRouting(surfelGiScheduling);
    EnableCrossFamilyComputeEffectRouting(surfelGiScheduling);
    if(graphOwnsSurfelGiResolve){
        const Array<Core::GpuTaskResourceUse, 4u> ageFreeResourceUses = {
            ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer),
            WriteUse(surfelPool, Core::ResourceStates::UnorderedAccess),
            WriteUse(surfelCounter, Core::ResourceStates::UnorderedAccess),
            WriteUse(surfelFreeList, Core::ResourceStates::UnorderedAccess),
        };

        Core::GpuTaskDesc ageFreeDesc;
        ageFreeDesc
            .setIdentity(Name(s_SurfelGraphAgeFree))
            .setMarkerLabel("Surfel GI Age Free")
            .setScheduling(surfelGiScheduling)
            .setDependencies({ &surfelGiDependency, 1u })
            .setExternalDependencies({ surfelGiExternalDependencies, surfelGiExternalDependencyCount })
            .setExternalStateSources({ surfelGiComputeCounterStateSources.count != 0u ? surfelGiComputeCounterStateSources.values.data() : nullptr, surfelGiComputeCounterStateSources.count })
            .setResourceUses(ageFreeResourceUses)
        ;
        const auto deferredSurfelGiAgeFreeTaskResult = surfelGiTasks.declareAgeFreeTask(ageFreeDesc, deferredLightingResources, deferredTargets, timingTicket, asyncTiming);
        if(!deferredSurfelGiAgeFreeTaskResult){
            m_deferredSurfelGiAgeFreeTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI age/free graph task"));
            return false;
        }
        m_deferredSurfelGiAgeFreeTask = *deferredSurfelGiAgeFreeTaskResult;

        Core::GpuTaskDesc cellHeadClearDesc;
        cellHeadClearDesc
            .setIdentity(Name(s_SurfelGraphCellHeadClear))
            .setMarkerLabel("Surfel GI Cell Head Clear")
            .setScheduling(surfelGiScheduling)
            .setDependencies({ &m_deferredSurfelGiAgeFreeTask, 1u })
            .setExternalStateSources({ surfelGiComputeStateSource.states ? &surfelGiComputeStateSource : nullptr, surfelGiComputeStateSource.states ? 1u : 0u })
        ;
        const auto deferredSurfelGiCellHeadClearTaskResult = context.graph.addClearBufferTask(cellHeadClearDesc, Core::GpuClearBufferTaskDesc{ .destination = surfelCellHead, .clearValue = NWB_SURFEL_CELL_INVALID, });
        if(!deferredSurfelGiCellHeadClearTaskResult){
            m_deferredSurfelGiCellHeadClearTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred surfel cell-head clear"));
            return false;
        }
        m_deferredSurfelGiCellHeadClearTask = *deferredSurfelGiCellHeadClearTaskResult;

        const Array<Core::GpuTaskResourceUse, 3u> hashBuildResourceUses = {
            ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer),
            ReadWriteUse(surfelPool, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(surfelCellHead, Core::ResourceStates::UnorderedAccess),
        };
        Core::GpuTaskDesc hashBuildDesc;
        hashBuildDesc
            .setIdentity(Name(s_SurfelGraphHashBuild))
            .setMarkerLabel("Surfel GI Hash Build")
            .setScheduling(surfelGiScheduling)
            .setDependencies({ &m_deferredSurfelGiCellHeadClearTask, 1u })
            .setResourceUses(hashBuildResourceUses)
        ;
        const auto deferredSurfelGiHashBuildTaskResult = surfelGiTasks.declareHashBuildTask(hashBuildDesc, deferredLightingResources, deferredTargets, timingTicket, asyncTiming);
        if(!deferredSurfelGiHashBuildTaskResult){
            m_deferredSurfelGiHashBuildTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI hash-build graph task"));
            return false;
        }
        m_deferredSurfelGiHashBuildTask = *deferredSurfelGiHashBuildTaskResult;

        const Array<Core::GpuTaskResourceUse, 10u> spawnResourceUses = {
            ReadUse(worldPosition),
            ReadUse(normal),
            ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer),
            ReadWriteUse(surfelPool, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(surfelGuidePool, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(surfelCellHead, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(surfelCounter, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(surfelFreeList, Core::ResourceStates::UnorderedAccess),
            ReadUse(surfelPoolSnapshot, Core::ResourceStates::ShaderResource),
            ReadUse(surfelCellHeadSnapshot, Core::ResourceStates::ShaderResource),
        };
        Core::GpuTaskDesc spawnDesc;
        spawnDesc.setIdentity(Name(s_SurfelGraphSpawn)).setMarkerLabel("Surfel GI Spawn").setScheduling(surfelGiScheduling).setDependencies({ &m_deferredSurfelGiHashBuildTask, 1u }).setResourceUses(spawnResourceUses);
        const auto deferredSurfelGiSpawnTaskResult = surfelGiTasks.declareSpawnTask(spawnDesc, deferredLightingResources, deferredTargets, timingTicket, asyncTiming);
        if(!deferredSurfelGiSpawnTaskResult){
            m_deferredSurfelGiSpawnTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI spawn graph task"));
            return false;
        }
        m_deferredSurfelGiSpawnTask = *deferredSurfelGiSpawnTaskResult;

        const Array<Core::GpuTaskResourceUse, 3u> traceBuildArgsResourceUses = {
            ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer),
            ReadUse(surfelCounter, Core::ResourceStates::UnorderedAccess),
            WriteUse(surfelTraceIndirectArgs, Core::ResourceStates::UnorderedAccess),
        };
        Core::GpuTaskDesc traceBuildArgsDesc;
        traceBuildArgsDesc
            .setIdentity(Name(s_SurfelGraphTraceBuildArgs))
            .setMarkerLabel("Surfel GI Trace Build Args")
            .setScheduling(surfelGiScheduling)
            .setDependencies({ &m_deferredSurfelGiSpawnTask, 1u })
            .setExternalStateSources({ surfelGiComputeCounterStateSources.count != 0u ? surfelGiComputeCounterStateSources.values.data() : nullptr, surfelGiComputeCounterStateSources.count })
            .setResourceUses(traceBuildArgsResourceUses)
        ;
        const auto deferredSurfelGiTraceBuildArgsTaskResult = surfelGiTasks.declareTraceBuildArgsTask(traceBuildArgsDesc, deferredLightingResources, deferredTargets, timingTicket, asyncTiming);
        if(!deferredSurfelGiTraceBuildArgsTaskResult){
            m_deferredSurfelGiTraceBuildArgsTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI trace-build-args graph task"));
            return false;
        }
        m_deferredSurfelGiTraceBuildArgsTask = *deferredSurfelGiTraceBuildArgsTaskResult;

        Core::GpuTaskDesc traceDesc;
        const SurfelGiTaskStateSourceSet traceStateSources = makeSurfelGiTaskStateSources(true, csgTraceContext.valid(), false);
        traceDesc
            .setIdentity(Name(s_SurfelGraphTrace))
            .setMarkerLabel("Surfel GI Trace")
            .setScheduling(surfelGiScheduling)
            .setDependencies({ &m_deferredSurfelGiTraceBuildArgsTask, 1u })
            .setExternalStateSources({ traceStateSources.count != 0u ? traceStateSources.values.data() : nullptr, traceStateSources.count })
            .setResourceUses(traceResourceUses)
            .setResourceSetUses({ traceResourceSets.useCount != 0u ? traceResourceSets.uses.data() : nullptr, traceResourceSets.useCount })
        ;
        const auto deferredSurfelGiTraceTaskResult = surfelGiTasks.declareTraceTask(traceDesc, deferredLightingResources, deferredTargets, timingTicket, asyncTiming);
        if(!deferredSurfelGiTraceTaskResult){
            m_deferredSurfelGiTraceTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI trace graph task"));
            return false;
        }
        m_deferredSurfelGiTraceTask = *deferredSurfelGiTraceTaskResult;

        const Array<Core::GpuTaskResourceUse, 6u> resolveResourceUses = {
            ReadUse(worldPosition),
            ReadUse(normal),
            ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer),
            ReadUse(surfelPool, Core::ResourceStates::ShaderResource),
            ReadUse(surfelCellHead, Core::ResourceStates::ShaderResource),
            WriteUse(surfelIrradianceHalf, Core::ResourceStates::UnorderedAccess),
        };
        Core::GpuTaskDesc resolveDesc;
        resolveDesc
            .setIdentity(Name(s_SurfelGraphResolve))
            .setMarkerLabel("Surfel GI Resolve")
            .setScheduling(surfelGiScheduling)
            .setDependencies({ &m_deferredSurfelGiTraceTask, 1u })
            .setExternalStateSources({ surfelGiComputeStateSource.states ? &surfelGiComputeStateSource : nullptr, surfelGiComputeStateSource.states ? 1u : 0u })
            .setResourceUses(resolveResourceUses)
        ;
        const auto deferredSurfelGiResolveTaskResult = surfelGiTasks.declareResolveTask(resolveDesc, deferredLightingResources, deferredTargets, timingTicket, asyncTiming);
        if(!deferredSurfelGiResolveTaskResult){
            m_deferredSurfelGiResolveTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI resolve graph task"));
            return false;
        }
        m_deferredSurfelGiResolveTask = *deferredSurfelGiResolveTaskResult;
        surfelGiDependency = m_deferredSurfelGiResolveTask;
    }

    const SurfelGiTaskStateSourceSet& terminalStateSources = graphOwnsSurfelGiResolve ? surfelGiComputeReturnStateSources : surfelGiAllStateSources;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name(s_SurfelGraphPass))
        .setMarkerLabel("Surfel GI")
        .setScheduling(surfelGiScheduling)
        .setDependencies({ &surfelGiDependency, 1u })
        .setExternalDependencies({ graphOwnsSurfelGiResolve ? nullptr : surfelGiExternalDependencies, graphOwnsSurfelGiResolve ? 0u : surfelGiExternalDependencyCount })
        .setExternalStateSources({ terminalStateSources.count != 0u ? terminalStateSources.values.data() : nullptr, terminalStateSources.count })
        .setResourceUses(resourceUses)
        .setResourceSetUses({ !graphOwnsSurfelGiResolve && traceResourceSets.useCount != 0u ? traceResourceSets.uses.data() : nullptr, !graphOwnsSurfelGiResolve ? traceResourceSets.useCount : 0u })
    ;
    const auto deferredSurfelGiTaskResult = surfelGiTasks.declareTerminalTask(desc, deferredLightingResources, deferredTargets, timingTicket, graphOwnsSurfelGiResolve ? &asyncTiming : nullptr);
    if(!deferredSurfelGiTaskResult){
        m_deferredSurfelGiTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI graph task"));
        return false;
    }
    m_deferredSurfelGiTask = *deferredSurfelGiTaskResult;
    return true;
}

void SurfelGiGraphCoordinator::declareDeferredSurfelCountReadbackTask(const SurfelGiGraphContext& context, const RayTracingSurfelPersistentResourceSnapshot& rayTracingSurfelResources){
    SurfelGiCounterReadbackBuilder builder(context.graph, context.rayTracing);
    builder.declare(rayTracingSurfelResources, m_deferredSurfelGiTask, context.frameTimingEndTask, m_deferredSurfelGiCounterReadbackTask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

