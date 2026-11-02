// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/kernel/task_graph_float_clear_tasks.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/gi/surfel_gi_lifecycle_builder.h>
#include <impl/ecs_render/gi/surfel_gi_task_builder.h>

#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererFramePipeline::declareDeferredSurfelGiTask(
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

    SurfelGiTaskBuilder surfelGiTasks(m_raytracingSystem, m_deferredLightingTaskGraph);
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
    const b8 hasSurfelWork = m_raytracingSystem.hasSurfelWork();
    const b8 traceGeometryStatesGraphOwned = traceGeometrySet.valid();
    const RendererFramePipelineDetail::TraceResourceSetUses traceResourceSets = RendererFramePipelineDetail::MakeTraceResourceSetUses(traceGeometrySet, traceMaterialSampledTextureSet);

    const Core::GpuTaskExternalStateSource surfelGiComputeStateSource{ .states = m_surfelGiComputePersistentState.source(), };
    const Core::GpuTaskExternalStateSource surfelGiCounterStateSource{ .states = m_surfelGiCounterPersistentState.source(), };
    const Core::GpuTaskExternalStateSource surfelIrradianceReturnStateSource{ .states = m_surfelIrradianceReturnState.source(), .applicableConsumerQueueClass = Core::CommandQueue::Compute, };
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

    const auto surfelIrradianceHalfResult = ImportExternalTexture(m_deferredLightingTaskGraph, deferredTargets.surfelIrradianceHalf, Name("render.surfel_gi.irradiance_half"), "Surfel Irradiance Half");
    const auto sceneGeometryDomainResult = m_deferredLightingTaskGraph.importHazardDomain(HazardDomainDesc(Name("render.surfel_gi.scene_geometry"), s_SceneGeometryDomainLabel));
    if(!surfelIrradianceHalfResult || !sceneGeometryDomainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred surfel-GI graph resources"));
        return false;
    }
    const Core::GpuGraphResourceId surfelIrradianceHalf = *surfelIrradianceHalfResult;
    const Core::GpuGraphResourceId sceneGeometryDomain = *sceneGeometryDomainResult;

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> traceResourceUses{ scratchArena };
    const b8 hasCsgTraceContext = rayTracingResources.csgTraceContextBuffer.get() != nullptr;
    resourceUses.reserve(s_SurfelResourceUseCapacity + (hasCsgTraceContext ? 1u : 0u) + (traceGeometryStatesGraphOwned ? 0u : traceGeometryResources.size()));
    traceResourceUses.reserve(s_SurfelTraceResourceUseCapacity + (hasCsgTraceContext ? s_SurfelCsgTraceContextUseCapacity : 0u) + rayTracingResources.csgTraceBoundsBuffers.size() + (traceGeometryStatesGraphOwned ? 0u : traceGeometryResources.size()));
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(normal));
    resourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(WriteUse(surfelIrradianceHalf, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(WriteUse(surfelIrradiance, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));
    if(materialContextSlots.valid())
        resourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    for(const Core::GpuGraphResourceId resource : traceGeometryResources){
        if(!resource.valid())
            return false;
        if(!traceGeometryStatesGraphOwned)
            resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }

    const auto appendOptionalBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state, const b8 writable = false) -> Expected<Core::GpuGraphResourceId>{
        if(!buffer)
            return Core::GpuGraphResourceId{};
        const auto resourceResult = ImportExternalBuffer(m_deferredLightingTaskGraph, buffer, identity, label);
        if(!resourceResult)
            return MakeUnexpected(Failure{});
        const Core::GpuGraphResourceId resource = *resourceResult;
        resourceUses.push_back(writable ? WriteUse(resource, state) : ReadUse(resource, state));
        return resource;
    };
    Core::GpuGraphResourceId shadowInstanceMaterials;
    Core::GpuGraphResourceId shadowMaterialTyped;
    Core::GpuGraphResourceId shadowInstances;
    Core::GpuGraphResourceId csgTraceContext;
    Core::GpuGraphResourceId surfelConstants;
    Core::GpuGraphResourceId surfelPool;
    Core::GpuGraphResourceId surfelGuidePool;
    Core::GpuGraphResourceId surfelCellHead;
    Core::GpuGraphResourceId surfelCounter;
    Core::GpuGraphResourceId surfelTraceIndirectArgs;
    Core::GpuGraphResourceId surfelFreeList;
    Core::GpuGraphResourceId surfelPoolSnapshot;
    Core::GpuGraphResourceId surfelCellHeadSnapshot;
    Core::GpuGraphResourceId sceneBvhNodes;
    Core::GpuGraphResourceId sceneInstances;
    Core::GpuGraphResourceId tlas;
    const auto importOptionalResources = [&]() -> Expected<void>{
        const auto shadowInstanceMaterialsResult = appendOptionalBuffer(
            rayTracingResources.shadowInstanceMaterialBuffer,
            Name(s_SceneShadowInstanceMaterialName),
            s_SceneShadowInstanceMaterialLabel,
            Core::ResourceStates::ShaderResource
        );
        if(!shadowInstanceMaterialsResult)
            return MakeUnexpected(Failure{});
        shadowInstanceMaterials = *shadowInstanceMaterialsResult;
        const auto shadowMaterialTypedResult = appendOptionalBuffer(
            rayTracingResources.shadowMaterialTypedBuffer,
            Name(s_SceneShadowMaterialTypedName),
            s_SceneShadowMaterialTypedLabel,
            Core::ResourceStates::ShaderResource
        );
        if(!shadowMaterialTypedResult)
            return MakeUnexpected(Failure{});
        shadowMaterialTyped = *shadowMaterialTypedResult;
        const auto shadowInstancesResult = appendOptionalBuffer(
            rayTracingResources.shadowInstanceBuffer,
            Name(s_SceneShadowInstancesName),
            s_SceneShadowInstancesLabel,
            Core::ResourceStates::ShaderResource
        );
        if(!shadowInstancesResult)
            return MakeUnexpected(Failure{});
        shadowInstances = *shadowInstancesResult;
        const auto surfelConstantsResult = appendOptionalBuffer(rayTracingSurfelResources.constantsBuffer, Name(s_SurfelConstantsName), s_SurfelConstantsLabel, Core::ResourceStates::ConstantBuffer);
        if(!surfelConstantsResult)
            return MakeUnexpected(Failure{});
        surfelConstants = *surfelConstantsResult;
        const auto surfelPoolResult = appendOptionalBuffer(rayTracingSurfelResources.poolBuffer, Name("render.surfel_gi.pool"), "Surfel Pool", Core::ResourceStates::UnorderedAccess, true);
        if(!surfelPoolResult)
            return MakeUnexpected(Failure{});
        surfelPool = *surfelPoolResult;
        const auto surfelGuidePoolResult = appendOptionalBuffer(rayTracingSurfelResources.guidePoolBuffer, Name("render.surfel_gi.guide_pool"), "Surfel Guide Pool", Core::ResourceStates::UnorderedAccess, true);
        if(!surfelGuidePoolResult)
            return MakeUnexpected(Failure{});
        surfelGuidePool = *surfelGuidePoolResult;
        const auto surfelCellHeadResult = appendOptionalBuffer(rayTracingSurfelResources.cellHeadBuffer, Name("render.surfel_gi.cell_heads"), "Surfel Cell Heads", Core::ResourceStates::UnorderedAccess, true);
        if(!surfelCellHeadResult)
            return MakeUnexpected(Failure{});
        surfelCellHead = *surfelCellHeadResult;
        const auto surfelCounterResult = appendOptionalBuffer(rayTracingSurfelResources.counterBuffer, Name(s_SurfelCounterName), s_SurfelCounterLabel, Core::ResourceStates::UnorderedAccess, true);
        if(!surfelCounterResult)
            return MakeUnexpected(Failure{});
        surfelCounter = *surfelCounterResult;
        const auto surfelFreeListResult = appendOptionalBuffer(rayTracingSurfelResources.freeListBuffer, Name("render.surfel_gi.free_list"), "Surfel Free List", Core::ResourceStates::UnorderedAccess, true);
        if(!surfelFreeListResult)
            return MakeUnexpected(Failure{});
        surfelFreeList = *surfelFreeListResult;
        const auto surfelPoolSnapshotResult = appendOptionalBuffer(rayTracingSurfelResources.poolSnapshotBuffer, Name("render.surfel_gi.pool_snapshot"), "Surfel Pool Snapshot", Core::ResourceStates::ShaderResource);
        if(!surfelPoolSnapshotResult)
            return MakeUnexpected(Failure{});
        surfelPoolSnapshot = *surfelPoolSnapshotResult;
        const auto surfelCellHeadSnapshotResult = appendOptionalBuffer(rayTracingSurfelResources.cellHeadSnapshotBuffer, Name("render.surfel_gi.cell_head_snapshot"), "Surfel Cell Head Snapshot", Core::ResourceStates::ShaderResource);
        if(!surfelCellHeadSnapshotResult)
            return MakeUnexpected(Failure{});
        surfelCellHeadSnapshot = *surfelCellHeadSnapshotResult;
        return {};
    };
    b8 optionalResourcesImported = importOptionalResources().has_value();
    if(optionalResourcesImported && rayTracingResources.csgTraceContextBuffer){
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(m_deferredLightingTaskGraph);
            csgTraceContext = declarations.findImportedBuffer(rayTracingResources.csgTraceContextBuffer);
        }
        optionalResourcesImported = csgTraceContext.valid();
        if(optionalResourcesImported)
            resourceUses.push_back(ReadUse(csgTraceContext, Core::ResourceStates::ShaderResource));
    }
    if(optionalResourcesImported && rayTracingSurfelResources.traceIndirectArgsBuffer){
        const auto surfelTraceIndirectArgsResult = ImportExternalBuffer(m_deferredLightingTaskGraph, rayTracingSurfelResources.traceIndirectArgsBuffer, Name("render.surfel_gi.trace_args"), "Surfel Trace Arguments");
        if(surfelTraceIndirectArgsResult)
            surfelTraceIndirectArgs = *surfelTraceIndirectArgsResult;
        optionalResourcesImported = surfelTraceIndirectArgsResult.has_value();
    }
    if(optionalResourcesImported && !useHwTrace){
        const auto sceneBvhNodesResult = appendOptionalBuffer(rayTracingResources.sceneBvhNodeBuffer, Name(s_SceneBvhNodesName), s_SceneBvhNodesLabel, Core::ResourceStates::ShaderResource);
        if(sceneBvhNodesResult)
            sceneBvhNodes = *sceneBvhNodesResult;
        optionalResourcesImported = sceneBvhNodesResult.has_value();
    }
    if(optionalResourcesImported && !useHwTrace){
        const auto sceneInstancesResult = appendOptionalBuffer(rayTracingResources.sceneInstanceBuffer, Name(s_SceneInstancesName), s_SceneInstancesLabel, Core::ResourceStates::ShaderResource);
        if(sceneInstancesResult)
            sceneInstances = *sceneInstancesResult;
        optionalResourcesImported = sceneInstancesResult.has_value();
    }
    if(optionalResourcesImported && useHwTrace){
        if(!rayTracingResources.sceneTlas)
            optionalResourcesImported = false;
        else{
            const auto tlasResult = m_deferredLightingTaskGraph.importAccelStruct(
                rayTracingResources.sceneTlas,
                AccelStructResourceDesc(Name(s_SceneTlasResourceName), s_SceneTlasResourceLabel)
                    .setInitialState(m_raytracingSystem.sceneTlasBackingInitialState())
            );
            if(tlasResult)
                tlas = *tlasResult;
            optionalResourcesImported = tlasResult.has_value();
            if(optionalResourcesImported)
                resourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
        }
    }
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a deferred surfel-GI dynamic resource domain"));
        return false;
    }

    const b8 graphOwnsSurfelGiResolve =
        hasSurfelWork
        && shadowInstanceMaterials.valid()
        && shadowMaterialTyped.valid()
        && shadowInstances.valid()
        && materialContextSlots.valid()
        && surfelConstants.valid()
        && surfelPool.valid()
        && surfelGuidePool.valid()
        && surfelCellHead.valid()
        && surfelCounter.valid()
        && surfelTraceIndirectArgs.valid()
        && surfelFreeList.valid()
        && surfelPoolSnapshot.valid()
        && surfelCellHeadSnapshot.valid()
        && (useHwTrace ? tlas.valid() : (sceneBvhNodes.valid() && sceneInstances.valid()))
        && rayTracingResources.surfelSplitGraphPipelinesReady
    ;
    if(hasSurfelWork && !graphOwnsSurfelGiResolve){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: active surfel GI has no complete prepared graph resources"));
        return false;
    }
    if(graphOwnsSurfelGiResolve){
        resourceUses.clear();
        resourceUses.push_back(ReadUse(worldPosition));
        resourceUses.push_back(ReadUse(normal));
        resourceUses.push_back(ReadUse(surfelIrradianceHalf, Core::ResourceStates::ShaderResource));
        resourceUses.push_back(WriteUse(surfelIrradiance, Core::ResourceStates::UnorderedAccess));

        traceResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(sceneGeometryDomain));
        traceResourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(shadowMaterialTyped, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(shadowInstances, Core::ResourceStates::ShaderResource));
        if(csgTraceContext.valid()){
            if(!RendererFramePipelineDetail::AppendCurrentCsgRayGeometry(
                rayTracingResources.csgTraceContextBuffer,
                rayTracingResources.csgTraceBoundsBuffers,
                m_deferredLightingTaskGraph,
                traceResourceUses
            ))
                return false;
            traceResourceUses.push_back(ReadWriteUse(surfelCounter, Core::ResourceStates::UnorderedAccess));
        }
        traceResourceUses.push_back(ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadWriteUse(surfelPool, Core::ResourceStates::UnorderedAccess));
        traceResourceUses.push_back(ReadWriteUse(surfelGuidePool, Core::ResourceStates::UnorderedAccess));
        traceResourceUses.push_back(ReadUse(surfelPoolSnapshot, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(surfelCellHeadSnapshot, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(surfelTraceIndirectArgs, Core::ResourceStates::IndirectArgument));
        if(!traceGeometryStatesGraphOwned){
            for(const Core::GpuGraphResourceId resource : traceGeometryResources)
                traceResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
        if(useHwTrace)
            traceResourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
        else{
            traceResourceUses.push_back(ReadUse(sceneBvhNodes, Core::ResourceStates::ShaderResource));
            traceResourceUses.push_back(ReadUse(sceneInstances, Core::ResourceStates::ShaderResource));
        }
    }
    else if(surfelTraceIndirectArgs.valid())
        resourceUses.push_back(WriteUse(surfelTraceIndirectArgs, Core::ResourceStates::UnorderedAccess));

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    // Live Lighting joins both effects; lagged Lighting retains their independent publication boundaries.
    scheduling.forceSubmissionBoundary = useLaggedLightingHistory;
    scheduling.allowPacketMerge = !useLaggedLightingHistory;
    scheduling.mergeWithPrevious = !useLaggedLightingHistory;
    scheduling.allowMergeAcrossConsumerFrontier = !useLaggedLightingHistory;
    const Core::GpuExternalCompletionId* const surfelGiExternalDependencies = surfelCounterReadbackCompletion.valid() ? &surfelCounterReadbackCompletion : nullptr;
    const usize surfelGiExternalDependencyCount = surfelCounterReadbackCompletion.valid() ? 1u : 0u;

    SurfelGiLifecycleBuilder surfelGiLifecycleBuilder(m_deferredLightingTaskGraph, m_raytracingSystem);
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
        .setIdentity(Name("render.surfel_gi.irradiance_clear"))
        .setMarkerLabel("Surfel Irradiance Clear")
        .setScheduling(surfelIrradianceClearScheduling)
        .setDependencies({ &surfelGiDependency, 1u })
        .setResourceUses({ &surfelIrradianceClearResourceUse, 1u })
    ;
    const auto surfelIrradianceClearTaskResult = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::FloatTextureClearGraphTask>(
        surfelIrradianceClearDesc,
        ECSRenderDetail::FloatTextureClearGraphTask::Payload{ .destination = surfelIrradiance, .subresources = ECSRenderDetail::s_FramebufferSubresources, .floatValue = ECSRenderDetail::s_SurfelIrradianceClearColor, }
    );
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
            .setIdentity(Name("render.surfel_gi.age_free"))
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
            .setIdentity(Name("render.surfel_gi.cell_head_clear"))
            .setMarkerLabel("Surfel GI Cell Head Clear")
            .setScheduling(surfelGiScheduling)
            .setDependencies({ &m_deferredSurfelGiAgeFreeTask, 1u })
            .setExternalStateSources({ surfelGiComputeStateSource.states ? &surfelGiComputeStateSource : nullptr, surfelGiComputeStateSource.states ? 1u : 0u })
        ;
        const auto deferredSurfelGiCellHeadClearTaskResult = m_deferredLightingTaskGraph.addClearBufferTask(cellHeadClearDesc, Core::GpuClearBufferTaskDesc{ .destination = surfelCellHead, .clearValue = NWB_SURFEL_CELL_INVALID, });
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
            .setIdentity(Name("render.surfel_gi.hash_build"))
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
        spawnDesc.setIdentity(Name("render.surfel_gi.spawn")).setMarkerLabel("Surfel GI Spawn").setScheduling(surfelGiScheduling).setDependencies({ &m_deferredSurfelGiHashBuildTask, 1u }).setResourceUses(spawnResourceUses);
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
            .setIdentity(Name("render.surfel_gi.trace_build_args"))
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
            .setIdentity(Name("render.surfel_gi.trace"))
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
            .setIdentity(Name("render.surfel_gi.resolve"))
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
        .setIdentity(Name("render.surfel_gi"))
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

void RendererFramePipeline::declareDeferredSurfelCountReadbackTask(const RayTracingSurfelPersistentResourceSnapshot& rayTracingSurfelResources){
    using namespace RendererTaskGraphDetail;

    m_deferredSurfelGiCounterReadbackTask = {};
    if(!m_deferredSurfelGiTask.valid() || !m_deferredFrameTimingEndTask.valid() || !m_raytracingSystem.shouldCaptureSurfelCountReadback())
        return;

    const auto counterResult = m_deferredLightingTaskGraph.importBuffer(rayTracingSurfelResources.counterBuffer, BufferResourceDesc(Name(s_SurfelCounterName), s_SurfelCounterLabel));
    const auto readbackResult = m_deferredLightingTaskGraph.importBuffer(
        rayTracingSurfelResources.counterReadbackBuffer,
        BufferResourceDesc(Name(s_SurfelCounterReadbackName), s_SurfelCounterReadbackLabel)
    );
    if(!counterResult || !readbackResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import surfel counter-readback graph resources"));
        return;
    }
    const Core::GpuGraphResourceId counter = *counterResult;
    const Core::GpuGraphResourceId readback = *readbackResult;

    const Array<Core::GpuCopyBufferTaskRegion, 1u> regions = { Core::GpuCopyBufferTaskRegion{ .source = counter, .destination = readback, .dataSizeBytes = static_cast<u64>(sizeof(u32)) * NWB_SURFEL_COUNTER_SIZE, }, };
    Core::GpuTaskSchedulingHint scheduling;
    // Infrequent diagnostic after presentation; small copy so Transfer absorbs it.
    scheduling.cost = Core::GpuTaskCostHint::Small;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Array<Core::GpuTaskId, 2u> dependencies = { m_deferredSurfelGiTask, m_deferredFrameTimingEndTask, };
    Core::GpuTaskDesc desc;
    desc.setIdentity(Name(s_SurfelCounterReadbackName)).setMarkerLabel(s_SurfelCounterReadbackLabel).setScheduling(scheduling).setDependencies(dependencies);
    const auto deferredSurfelGiCounterReadbackTaskResult = m_deferredLightingTaskGraph.addCopyBufferTask(desc, Core::GpuCopyBufferTaskDesc{ .regions = regions.data(), .regionCount = regions.size(), });
    if(!deferredSurfelGiCounterReadbackTaskResult){
        m_deferredSurfelGiCounterReadbackTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel counter-readback task"));
        return;
    }
    m_deferredSurfelGiCounterReadbackTask = *deferredSurfelGiCounterReadbackTaskResult;
    // The token stays invalid until native submission accepts; this timestamp is only read when it publishes.
    m_raytracingSystem.markSurfelCountReadbackScheduled();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

