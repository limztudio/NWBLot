// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/system.h>

#include <impl/ecs_render/raytrace/task_graph_surfel_tasks.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>

#include <core/graphics/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::declareDeferredSurfelGiTask(
    DeferredFrameTargets& deferredTargets,
    const Core::GpuGraphResourceId worldPosition,
    const Core::GpuGraphResourceId normal,
    const Core::GpuGraphResourceId surfelIrradiance,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId sceneShading,
    const Core::GpuGraphResourceId lights,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuGraphResourceId* const traceGeometryResources,
    const usize traceGeometryResourceCount,
    const Core::GpuGraphResourceSetId traceGeometrySet,
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet,
    const Core::GpuTaskId effectsTask,
    const Core::GpuExternalCompletionId surfelCounterReadbackCompletion,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& asyncTiming
){
    using namespace RendererTaskGraphDetail;

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
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !worldPosition.valid()
        || !normal.valid()
        || !surfelIrradiance.valid()
        || !currentBindlessSlots.valid()
        || !sceneShading.valid()
        || !lights.valid()
        || !effectsTask.valid()
        || (traceGeometryResourceCount != 0u && !traceGeometryResources)
    )
        return false;

    const bool useHwTrace = m_rayTracingState.m_surfelUseHwTrace;
    const bool hasSurfelWork = m_raytracingSystem.hasSurfelWork();
    const bool traceGeometryStatesGraphOwned = traceGeometrySet.valid();
    const Core::GpuTaskResourceSetUse traceGeometrySetUse{
        .resourceSet = traceGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse traceMaterialSampledTextureSetUse{
        .resourceSet = traceMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse traceResourceSetUses[2u] = {};
    usize traceResourceSetUseCount = 0u;
    if(traceGeometryStatesGraphOwned)
        traceResourceSetUses[traceResourceSetUseCount++] = traceGeometrySetUse;
    if(traceMaterialSampledTextureSet.valid())
        traceResourceSetUses[traceResourceSetUseCount++] = traceMaterialSampledTextureSetUse;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId surfelIrradianceHalf = importTexture(
        deferredTargets.surfelIrradianceHalf,
        Name("render.surfel_gi.irradiance_half"),
        "Surfel Irradiance Half"
    );
    const Core::GpuGraphResourceId sceneGeometryDomain = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.surfel_gi.scene_geometry"), "Scene Acceleration and Geometry")
    );
    if(!surfelIrradianceHalf.valid() || !sceneGeometryDomain.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred surfel-GI graph resources"));
        return false;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> ageFreeResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hashBuildResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> spawnResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> traceBuildArgsResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> traceResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resolveResourceUses{ scratchArena };
    resourceUses.reserve(28u + (traceGeometryStatesGraphOwned ? 0u : traceGeometryResourceCount));
    ageFreeResourceUses.reserve(4u);
    hashBuildResourceUses.reserve(3u);
    spawnResourceUses.reserve(7u);
    traceBuildArgsResourceUses.reserve(3u);
    traceResourceUses.reserve(16u + (traceGeometryStatesGraphOwned ? 0u : traceGeometryResourceCount));
    resolveResourceUses.reserve(6u);
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
    for(usize resourceIndex = 0u; resourceIndex < traceGeometryResourceCount; ++resourceIndex){
        const Core::GpuGraphResourceId resource = traceGeometryResources[resourceIndex];
        if(!resource.valid())
            return false;
        if(!traceGeometryStatesGraphOwned)
            resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }

    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state, Core::GpuGraphResourceId* const outResource = nullptr){
        if(!buffer){
            if(outResource)
                *outResource = {};
            return true;
        }
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        if(outResource)
            *outResource = resource;
        resourceUses.push_back(ReadUse(resource, state));
        return true;
    };
    const auto appendOptionalWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state, Core::GpuGraphResourceId* const outResource = nullptr){
        if(!buffer){
            if(outResource)
                *outResource = {};
            return true;
        }
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        if(outResource)
            *outResource = resource;
        resourceUses.push_back(WriteUse(resource, state));
        return true;
    };
    Core::GpuGraphResourceId shadowInstanceMaterials;
    Core::GpuGraphResourceId shadowMaterialTyped;
    Core::GpuGraphResourceId shadowInstances;
    Core::GpuGraphResourceId surfelConstants;
    Core::GpuGraphResourceId surfelPool;
    Core::GpuGraphResourceId surfelCellHead;
    Core::GpuGraphResourceId surfelCounter;
    Core::GpuGraphResourceId surfelTraceIndirectArgs;
    Core::GpuGraphResourceId surfelFreeList;
    Core::GpuGraphResourceId surfelPoolSnapshot;
    Core::GpuGraphResourceId surfelCellHeadSnapshot;
    Core::GpuGraphResourceId sceneBvhNodes;
    Core::GpuGraphResourceId sceneInstances;
    Core::GpuGraphResourceId tlas;
    bool optionalResourcesImported =
        appendOptionalReadBuffer(
            m_rayTracingState.m_shadowInstanceMaterialBuffer,
            Name("render.deferred_effects.instance_material"),
            "Shadow Instance Materials",
            Core::ResourceStates::ShaderResource,
            &shadowInstanceMaterials
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowMaterialTypedBuffer,
            Name("render.deferred_effects.material_typed"),
            "Shadow Typed Materials",
            Core::ResourceStates::ShaderResource,
            &shadowMaterialTyped
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowInstanceBuffer,
            Name("render.deferred_effects.shadow_instances"),
            "Shadow Instances",
            Core::ResourceStates::ShaderResource,
            &shadowInstances
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_surfelConstants,
            Name("render.surfel_gi.constants"),
            "Surfel Constants",
            Core::ResourceStates::ConstantBuffer,
            &surfelConstants
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelPoolBuffer,
            Name("render.surfel_gi.pool"),
            "Surfel Pool",
            Core::ResourceStates::UnorderedAccess,
            &surfelPool
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelCellHeadBuffer,
            Name("render.surfel_gi.cell_heads"),
            "Surfel Cell Heads",
            Core::ResourceStates::UnorderedAccess,
            &surfelCellHead
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelCounterBuffer,
            Name("render.surfel_gi.counter"),
            "Surfel Counter",
            Core::ResourceStates::UnorderedAccess,
            &surfelCounter
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelFreeListBuffer,
            Name("render.surfel_gi.free_list"),
            "Surfel Free List",
            Core::ResourceStates::UnorderedAccess,
            &surfelFreeList
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_surfelPoolSnapshotBuffer,
            Name("render.surfel_gi.pool_snapshot"),
            "Surfel Pool Snapshot",
            Core::ResourceStates::ShaderResource,
            &surfelPoolSnapshot
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_surfelCellHeadSnapshotBuffer,
            Name("render.surfel_gi.cell_head_snapshot"),
            "Surfel Cell Head Snapshot",
            Core::ResourceStates::ShaderResource,
            &surfelCellHeadSnapshot
        )
    ;
    if(optionalResourcesImported && m_rayTracingState.m_surfelTraceIndirectArgsBuffer){
        surfelTraceIndirectArgs = importBuffer(
            m_rayTracingState.m_surfelTraceIndirectArgsBuffer,
            Name("render.surfel_gi.trace_args"),
            "Surfel Trace Arguments"
        );
        optionalResourcesImported = surfelTraceIndirectArgs.valid();
    }
    if(optionalResourcesImported && !useHwTrace){
        optionalResourcesImported =
            appendOptionalReadBuffer(
                m_rayTracingState.m_sceneBvhNodeBuffer,
                Name("render.shadow_visibility.scene_bvh_nodes"),
                "Scene BVH Nodes",
                Core::ResourceStates::ShaderResource,
                &sceneBvhNodes
            )
            && appendOptionalReadBuffer(
                m_rayTracingState.m_sceneInstanceBuffer,
                Name("render.shadow_visibility.scene_instances"),
                "Scene Instances",
                Core::ResourceStates::ShaderResource,
                &sceneInstances
            )
        ;
    }
    if(optionalResourcesImported && useHwTrace){
        if(!m_rayTracingState.m_tlas)
            optionalResourcesImported = false;
        else{
            tlas = m_deferredLightingTaskGraph.importAccelStruct(
                m_rayTracingState.m_tlas,
                AccelStructResourceDesc(Name("render.deferred_effects.tlas"), "Scene TLAS")
                    .setInitialState(m_raytracingSystem.sceneTlasBackingInitialState())
            );
            optionalResourcesImported = tlas.valid();
            if(optionalResourcesImported){
                resourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a deferred surfel-GI dynamic resource domain"));
        return false;
    }


// A prepared normal Surfel GI frame can split its age/free, per-frame cell-head reset, hash build, Spawn,
    // trace-build-args, trace, and resolve. Keep unavailable resources or pipelines on the established monolithic
    // compatibility callback; otherwise the graph owns each handoff in one selected Compute packet.
    const bool graphOwnsSurfelGiResolve =
        hasSurfelWork
        && shadowInstanceMaterials.valid()
        && shadowMaterialTyped.valid()
        && shadowInstances.valid()
        && materialContextSlots.valid()
        && surfelConstants.valid()
        && surfelPool.valid()
        && surfelCellHead.valid()
        && surfelCounter.valid()
        && surfelTraceIndirectArgs.valid()
        && surfelFreeList.valid()
        && surfelPoolSnapshot.valid()
        && surfelCellHeadSnapshot.valid()
        && (useHwTrace ? tlas.valid() : (sceneBvhNodes.valid() && sceneInstances.valid()))
        && m_rayTracingState.m_surfelAgeFreePipeline
        && m_rayTracingState.m_surfelHashBuildPipeline
        && m_rayTracingState.m_surfelSpawnPipeline
        && (useHwTrace ? m_rayTracingState.m_surfelTraceHwPipeline : m_rayTracingState.m_surfelTracePipeline)
        && m_rayTracingState.m_surfelResolvePipeline
        && m_rayTracingState.m_surfelUpsamplePipeline
        && m_rayTracingState.m_surfelTraceBuildArgsPipeline
    ;
    if(graphOwnsSurfelGiResolve){
        resourceUses.clear();
        resourceUses.push_back(ReadUse(worldPosition));
        resourceUses.push_back(ReadUse(normal));
        resourceUses.push_back(ReadUse(surfelIrradianceHalf, Core::ResourceStates::ShaderResource));
        resourceUses.push_back(WriteUse(surfelIrradiance, Core::ResourceStates::UnorderedAccess));

        resolveResourceUses.push_back(ReadUse(worldPosition));
        resolveResourceUses.push_back(ReadUse(normal));
        resolveResourceUses.push_back(ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer));
        resolveResourceUses.push_back(ReadUse(surfelPool, Core::ResourceStates::ShaderResource));
        resolveResourceUses.push_back(ReadUse(surfelCellHead, Core::ResourceStates::ShaderResource));
        resolveResourceUses.push_back(WriteUse(surfelIrradianceHalf, Core::ResourceStates::UnorderedAccess));

        traceResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(sceneGeometryDomain));
        traceResourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(shadowMaterialTyped, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(shadowInstances, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadWriteUse(surfelPool, Core::ResourceStates::UnorderedAccess));
        traceResourceUses.push_back(ReadUse(surfelPoolSnapshot, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(surfelCellHeadSnapshot, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(surfelTraceIndirectArgs, Core::ResourceStates::IndirectArgument));
        if(!traceGeometryStatesGraphOwned){
            for(usize resourceIndex = 0u; resourceIndex < traceGeometryResourceCount; ++resourceIndex)
                traceResourceUses.push_back(ReadUse(traceGeometryResources[resourceIndex], Core::ResourceStates::ShaderResource));
        }
        if(useHwTrace){
            traceResourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
        }
        else{
            traceResourceUses.push_back(ReadUse(sceneBvhNodes, Core::ResourceStates::ShaderResource));
            traceResourceUses.push_back(ReadUse(sceneInstances, Core::ResourceStates::ShaderResource));
        }
    }
    else if(surfelTraceIndirectArgs.valid()){
        resourceUses.push_back(WriteUse(surfelTraceIndirectArgs, Core::ResourceStates::UnorderedAccess));
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Core::GpuExternalCompletionId* const surfelGiExternalDependencies =
        surfelCounterReadbackCompletion.valid() ? &surfelCounterReadbackCompletion : nullptr
    ;
    const usize surfelGiExternalDependencyCount = surfelCounterReadbackCompletion.valid() ? 1u : 0u;

    // A fresh persistent field must clear before the snapshot reads it. The four typed clear primitives and their
    // resource-free lifecycle tail form one Compute-preferred, Transfer-capable graph packet; the lifecycle task
    // publishes its CPU
    // mirror only after every clear recorded and that packet accepts. Once initialized, the two fixed regions become
    // one graph-owned copy task; compiler declarations own every CopySource/CopyDest transition and handoff.
    Core::GpuTaskId surfelGiDependency = effectsTask;
    if(hasSurfelWork){
        if(
            !surfelPool.valid()
            || !surfelCellHead.valid()
            || !surfelCounter.valid()
            || !surfelFreeList.valid()
            || !surfelPoolSnapshot.valid()
            || !surfelCellHeadSnapshot.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel-GI snapshot resources were unavailable during graph declaration"));
            return false;
        }

        if(m_raytracingSystem.needsSurfelResourceInitialization()){
            Core::GpuTaskSchedulingHint initializationScheduling;
            initializationScheduling.cost = Core::GpuTaskCostHint::Medium;
            // Explicit merging keeps the first clear as a new packet under the renderer's FrontierSafe policy, then
            // lets its serial successors share that packet. Do not force a boundary here: that would also forbid the
            // following typed clears from joining their own initialization packet.
            initializationScheduling.allowPacketMerge = true;
            Core::GpuTaskDesc poolClearDesc;
            poolClearDesc
                .setIdentity(Name("render.surfel_gi.initialize_pool_clear"))
                .setMarkerLabel("Surfel GI Initialize Pool Clear")
                .setQueue(ComputeTransferQueueRequest())
                .setScheduling(initializationScheduling)
                .setDependencies(&surfelGiDependency, 1u)
                .setExternalDependencies(surfelGiExternalDependencies, surfelGiExternalDependencyCount)
            ;
            m_deferredSurfelGiPreparationTask = m_deferredLightingTaskGraph.addClearBufferTask(
                poolClearDesc,
                Core::GpuClearBufferTaskDesc{
                    .destination = surfelPool,
                    .clearValue = 0u,
                }
            );
            if(!m_deferredSurfelGiPreparationTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI pool initialization clear"));
                return false;
            }

            Core::GpuTaskSchedulingHint chainedInitializationScheduling = initializationScheduling;
            chainedInitializationScheduling.cost = Core::GpuTaskCostHint::Tiny;
            chainedInitializationScheduling.mergeWithPrevious = true;
            // The subsequent snapshot can route to a dedicated Transfer queue and therefore creates a consumer
            // frontier for the first two clears. Every tail is an explicit immediate successor, so retain the
            // complete initialization chain in its single accepted packet rather than splitting before that wait.
            chainedInitializationScheduling.allowMergeAcrossConsumerFrontier = true;
            Core::GpuTaskId initializationDependency = m_deferredSurfelGiPreparationTask;
            const auto addInitializationClear = [&](
                const Name& identity,
                const AStringView label,
                const Core::GpuGraphResourceId destination,
                const u32 clearValue
            ){
                Core::GpuTaskDesc clearDesc;
                clearDesc
                    .setIdentity(identity)
                    .setMarkerLabel(label)
                    .setQueue(ComputeTransferQueueRequest())
                    .setScheduling(chainedInitializationScheduling)
                    .setDependencies(&initializationDependency, 1u)
                ;
                const Core::GpuTaskId clearTask = m_deferredLightingTaskGraph.addClearBufferTask(
                    clearDesc,
                    Core::GpuClearBufferTaskDesc{
                        .destination = destination,
                        .clearValue = clearValue,
                    }
                );
                if(clearTask.valid())
                    initializationDependency = clearTask;
                return clearTask;
            };
            if(
                !addInitializationClear(
                    Name("render.surfel_gi.initialize_cell_head_clear"),
                    "Surfel GI Initialize Cell-Head Clear",
                    surfelCellHead,
                    NWB_SURFEL_CELL_INVALID
                ).valid()
                || !addInitializationClear(
                    Name("render.surfel_gi.initialize_counter_clear"),
                    "Surfel GI Initialize Counter Clear",
                    surfelCounter,
                    0u
                ).valid()
                || !addInitializationClear(
                    Name("render.surfel_gi.initialize_free_list_clear"),
                    "Surfel GI Initialize Free-List Clear",
                    surfelFreeList,
                    0u
                ).valid()
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI initialization clears"));
                return false;
            }

            Core::GpuTaskDesc initializationLifecycleDesc;
            initializationLifecycleDesc
                .setIdentity(Name("render.surfel_gi.initialize_lifecycle"))
                .setMarkerLabel("Surfel GI Initialize Lifecycle")
                .setQueue(ComputeTransferQueueRequest())
                .setScheduling(chainedInitializationScheduling)
                .setDependencies(&initializationDependency, 1u)
            ;
            m_deferredSurfelGiInitializationLifecycleTask =
                m_raytracingSystem.declareSurfelResourceInitializationLifecycleTask(
                    m_deferredLightingTaskGraph,
                    initializationLifecycleDesc
                );
            if(!m_deferredSurfelGiInitializationLifecycleTask.valid()){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI initialization lifecycle"));
                return false;
            }
            surfelGiDependency = m_deferredSurfelGiInitializationLifecycleTask;
        }

        const Core::GpuCopyBufferTaskRegion snapshotRegions[] = {
            Core::GpuCopyBufferTaskRegion{
                .source = surfelPool,
                .destination = surfelPoolSnapshot,
                .dataSizeBytes = m_rayTracingState.m_surfelPoolBuffer->getDescription().byteSize,
            },
            Core::GpuCopyBufferTaskRegion{
                .source = surfelCellHead,
                .destination = surfelCellHeadSnapshot,
                .dataSizeBytes = m_rayTracingState.m_surfelCellHeadBuffer->getDescription().byteSize,
            },
        };
        Core::GpuTaskDesc snapshotDesc;
        snapshotDesc
            .setIdentity(Name("render.surfel_gi.snapshot_copy"))
            .setMarkerLabel("Surfel GI Snapshot Copy")
            .setQueue(TransferQueueRequest())
            .setScheduling(scheduling)
            .setDependencies(&surfelGiDependency, 1u)
        ;
        m_deferredSurfelGiSnapshotCopyTask = m_deferredLightingTaskGraph.addCopyBufferTask(
            snapshotDesc,
            Core::GpuCopyBufferTaskDesc{
                .regions = snapshotRegions,
                .regionCount = LengthOf(snapshotRegions),
            }
        );
        if(!m_deferredSurfelGiSnapshotCopyTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI snapshot-copy task"));
            return false;
        }
        if(!m_deferredSurfelGiPreparationTask.valid())
            m_deferredSurfelGiPreparationTask = m_deferredSurfelGiSnapshotCopyTask;
        surfelGiDependency = m_deferredSurfelGiSnapshotCopyTask;
    }


// Zero coverage is the deferred-lighting no-op. Keep the typed CopyDest clear at the front of the same Compute
    // packet as GI: it does not merge with the preceding effects packet, while GI explicitly merges with it below.
    // This preserves the established semantic GI boundary on every Compute/Graphics fallback route.
    Core::GpuTaskSchedulingHint surfelIrradianceClearScheduling;
    surfelIrradianceClearScheduling.cost = Core::GpuTaskCostHint::Tiny;
    surfelIrradianceClearScheduling.allowPacketMerge = true;
    // This starts the independent Surfel GI effect packet after its optional Transfer snapshot, so it may choose
    // an explicitly opted-in same-class auxiliary lane, including an alternate Compute family when every
    // declared resource can legally cross it. The GI chain below retains that exact queue through its direct
    // dependencies.
    EnableSameFamilyComputeEffectRouting(surfelIrradianceClearScheduling, false);
    EnableCrossFamilyComputeEffectRouting(surfelIrradianceClearScheduling);
    const Core::GpuTaskResourceUse surfelIrradianceClearResourceUse = WriteTextureUse(
        surfelIrradiance,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::CopyDest
    );
    Core::GpuTaskDesc surfelIrradianceClearDesc;
    surfelIrradianceClearDesc
        .setIdentity(Name("render.surfel_gi.irradiance_clear"))
        .setMarkerLabel("Surfel Irradiance Clear")
        .setQueue(ComputePacketQueueRequest())
        .setScheduling(surfelIrradianceClearScheduling)
        .setDependencies(&surfelGiDependency, 1u)
        .setResourceUses(&surfelIrradianceClearResourceUse, 1u)
    ;
    const Core::GpuTaskId surfelIrradianceClearTask = m_deferredLightingTaskGraph.addTask<ECSRenderDetail::SurfelIrradianceClearGraphTask>(
        surfelIrradianceClearDesc,
        ECSRenderDetail::SurfelIrradianceClearGraphTask::Payload{
            .destination = surfelIrradiance,
        }
    );
    if(!surfelIrradianceClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred surfel-irradiance clear"));
        return false;
    }
    m_deferredSurfelGiIrradianceClearTask = surfelIrradianceClearTask;
    surfelGiDependency = surfelIrradianceClearTask;

    Core::GpuTaskSchedulingHint surfelGiScheduling = scheduling;
    surfelGiScheduling.forceSubmissionBoundary = false;
    surfelGiScheduling.allowPacketMerge = true;
    surfelGiScheduling.mergeWithPrevious = true;
    // Every GI preparation stage directly succeeds the output clear or its predecessor and shares one accepted
    // Compute timing packet; later cross-queue consumers wait for the completed packet.
    surfelGiScheduling.allowMergeAcrossConsumerFrontier = true;
    EnableSameFamilyComputeEffectRouting(surfelGiScheduling);
    EnableCrossFamilyComputeEffectRouting(surfelGiScheduling);
    if(graphOwnsSurfelGiResolve){
        ageFreeResourceUses.push_back(ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer));
        ageFreeResourceUses.push_back(WriteUse(surfelPool, Core::ResourceStates::UnorderedAccess));
        ageFreeResourceUses.push_back(WriteUse(surfelCounter, Core::ResourceStates::UnorderedAccess));
        ageFreeResourceUses.push_back(WriteUse(surfelFreeList, Core::ResourceStates::UnorderedAccess));

        Core::GpuTaskDesc ageFreeDesc;
        ageFreeDesc
            .setIdentity(Name("render.surfel_gi.age_free"))
            .setMarkerLabel("Surfel GI Age Free")
            .setQueue(ComputeQueueRequest())
            .setScheduling(surfelGiScheduling)
            .setDependencies(&surfelGiDependency, 1u)
            .setExternalDependencies(surfelGiExternalDependencies, surfelGiExternalDependencyCount)
            .setResourceUses(ageFreeResourceUses.data(), ageFreeResourceUses.size())
        ;
        m_deferredSurfelGiAgeFreeTask = m_raytracingSystem.declareSurfelGiAgeFreeTask(
            m_deferredLightingTaskGraph,
            ageFreeDesc,
            deferredTargets,
            timingTicket,
            asyncTiming,
            true
        );
        if(!m_deferredSurfelGiAgeFreeTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI age/free graph task"));
            return false;
        }

        Core::GpuTaskDesc cellHeadClearDesc;
        cellHeadClearDesc
            .setIdentity(Name("render.surfel_gi.cell_head_clear"))
            .setMarkerLabel("Surfel GI Cell Head Clear")
            .setQueue(ComputeTransferQueueRequest())
            .setScheduling(surfelGiScheduling)
            .setDependencies(&m_deferredSurfelGiAgeFreeTask, 1u)
        ;
        m_deferredSurfelGiCellHeadClearTask = m_deferredLightingTaskGraph.addClearBufferTask(
            cellHeadClearDesc,
            Core::GpuClearBufferTaskDesc{
                .destination = surfelCellHead,
                .clearValue = NWB_SURFEL_CELL_INVALID,
            }
        );
        if(!m_deferredSurfelGiCellHeadClearTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned deferred surfel cell-head clear"));
            return false;
        }

        hashBuildResourceUses.push_back(ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer));
        hashBuildResourceUses.push_back(ReadWriteUse(surfelPool, Core::ResourceStates::UnorderedAccess));
        hashBuildResourceUses.push_back(ReadWriteUse(surfelCellHead, Core::ResourceStates::UnorderedAccess));
        Core::GpuTaskDesc hashBuildDesc;
        hashBuildDesc
            .setIdentity(Name("render.surfel_gi.hash_build"))
            .setMarkerLabel("Surfel GI Hash Build")
            .setQueue(ComputeQueueRequest())
            .setScheduling(surfelGiScheduling)
            .setDependencies(&m_deferredSurfelGiCellHeadClearTask, 1u)
            .setResourceUses(hashBuildResourceUses.data(), hashBuildResourceUses.size())
        ;
        m_deferredSurfelGiHashBuildTask = m_raytracingSystem.declareSurfelGiHashBuildTask(
            m_deferredLightingTaskGraph,
            hashBuildDesc,
            deferredTargets,
            timingTicket,
            &asyncTiming,
            true
        );
        if(!m_deferredSurfelGiHashBuildTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI hash-build graph task"));
            return false;
        }

        spawnResourceUses.push_back(ReadUse(worldPosition));
        spawnResourceUses.push_back(ReadUse(normal));
        spawnResourceUses.push_back(ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer));
        spawnResourceUses.push_back(ReadWriteUse(surfelPool, Core::ResourceStates::UnorderedAccess));
        spawnResourceUses.push_back(ReadWriteUse(surfelCellHead, Core::ResourceStates::UnorderedAccess));
        spawnResourceUses.push_back(ReadWriteUse(surfelCounter, Core::ResourceStates::UnorderedAccess));
        spawnResourceUses.push_back(ReadWriteUse(surfelFreeList, Core::ResourceStates::UnorderedAccess));
        Core::GpuTaskDesc spawnDesc;
        spawnDesc
            .setIdentity(Name("render.surfel_gi.spawn"))
            .setMarkerLabel("Surfel GI Spawn")
            .setQueue(ComputeQueueRequest())
            .setScheduling(surfelGiScheduling)
            .setDependencies(&m_deferredSurfelGiHashBuildTask, 1u)
            .setResourceUses(spawnResourceUses.data(), spawnResourceUses.size())
        ;
        m_deferredSurfelGiSpawnTask = m_raytracingSystem.declareSurfelGiSpawnTask(
            m_deferredLightingTaskGraph,
            spawnDesc,
            deferredTargets,
            timingTicket,
            &asyncTiming,
            true
        );
        if(!m_deferredSurfelGiSpawnTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI spawn graph task"));
            return false;
        }

        traceBuildArgsResourceUses.push_back(ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer));
        traceBuildArgsResourceUses.push_back(ReadUse(surfelCounter, Core::ResourceStates::UnorderedAccess));
        traceBuildArgsResourceUses.push_back(WriteUse(surfelTraceIndirectArgs, Core::ResourceStates::UnorderedAccess));
        Core::GpuTaskDesc traceBuildArgsDesc;
        traceBuildArgsDesc
            .setIdentity(Name("render.surfel_gi.trace_build_args"))
            .setMarkerLabel("Surfel GI Trace Build Args")
            .setQueue(ComputeQueueRequest())
            .setScheduling(surfelGiScheduling)
            .setDependencies(&m_deferredSurfelGiSpawnTask, 1u)
            .setResourceUses(traceBuildArgsResourceUses.data(), traceBuildArgsResourceUses.size())
        ;
        m_deferredSurfelGiTraceBuildArgsTask = m_raytracingSystem.declareSurfelGiTraceBuildArgsTask(
            m_deferredLightingTaskGraph,
            traceBuildArgsDesc,
            deferredTargets,
            timingTicket,
            &asyncTiming,
            true
        );
        if(!m_deferredSurfelGiTraceBuildArgsTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI trace-build-args graph task"));
            return false;
        }

        Core::GpuTaskDesc traceDesc;
        traceDesc
            .setIdentity(Name("render.surfel_gi.trace"))
            .setMarkerLabel("Surfel GI Trace")
            .setQueue(ComputeQueueRequest())
            .setScheduling(surfelGiScheduling)
            .setDependencies(&m_deferredSurfelGiTraceBuildArgsTask, 1u)
            .setResourceUses(traceResourceUses.data(), traceResourceUses.size())
            .setResourceSetUses(
                traceResourceSetUseCount != 0u ? traceResourceSetUses : nullptr,
                traceResourceSetUseCount
            )
        ;
        m_deferredSurfelGiTraceTask = m_raytracingSystem.declareSurfelGiTraceTask(
            m_deferredLightingTaskGraph,
            traceDesc,
            deferredTargets,
            timingTicket,
            &asyncTiming,
            true
        );
        if(!m_deferredSurfelGiTraceTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI trace graph task"));
            return false;
        }

        Core::GpuTaskDesc resolveDesc;
        resolveDesc
            .setIdentity(Name("render.surfel_gi.resolve"))
            .setMarkerLabel("Surfel GI Resolve")
            .setQueue(ComputeQueueRequest())
            .setScheduling(surfelGiScheduling)
            .setDependencies(&m_deferredSurfelGiTraceTask, 1u)
            .setResourceUses(resolveResourceUses.data(), resolveResourceUses.size())
        ;
        m_deferredSurfelGiResolveTask = m_raytracingSystem.declareSurfelGiResolveTask(
            m_deferredLightingTaskGraph,
            resolveDesc,
            deferredTargets,
            timingTicket,
            &asyncTiming,
            true
        );
        if(!m_deferredSurfelGiResolveTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI resolve graph task"));
            return false;
        }
        surfelGiDependency = m_deferredSurfelGiResolveTask;
    }

    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.surfel_gi"))
        .setMarkerLabel("Surfel GI")
        .setQueue(ComputeQueueRequest())
        .setScheduling(surfelGiScheduling)
        .setDependencies(&surfelGiDependency, 1u)
        .setExternalDependencies(
            graphOwnsSurfelGiResolve ? nullptr : surfelGiExternalDependencies,
            graphOwnsSurfelGiResolve ? 0u : surfelGiExternalDependencyCount
        )
        .setResourceUses(resourceUses.data(), resourceUses.size())
        .setResourceSetUses(
            !graphOwnsSurfelGiResolve && traceResourceSetUseCount != 0u ? traceResourceSetUses : nullptr,
            !graphOwnsSurfelGiResolve ? traceResourceSetUseCount : 0u
        )
    ;
    m_deferredSurfelGiTask = m_raytracingSystem.declareSurfelGiTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        timingTicket,
        true,
        graphOwnsSurfelGiResolve,
        graphOwnsSurfelGiResolve,
        graphOwnsSurfelGiResolve,
        graphOwnsSurfelGiResolve,
        graphOwnsSurfelGiResolve,
        graphOwnsSurfelGiResolve,
        graphOwnsSurfelGiResolve ? &asyncTiming : nullptr
    );
    if(!m_deferredSurfelGiTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI graph task"));
        return false;
    }
    return true;


}


void RendererSystem::declareDeferredSurfelCountReadbackTask(){
    using namespace RendererTaskGraphDetail;

    m_deferredSurfelGiCounterReadbackTask = {};
    if(
        !m_deferredSurfelGiTask.valid()
        || !m_raytracingSystem.shouldCaptureSurfelCountReadback()
    )
        return;

    const Core::GpuGraphResourceId counter = m_deferredLightingTaskGraph.importBuffer(
        m_rayTracingState.m_surfelCounterBuffer,
        BufferResourceDesc(Name("render.surfel_gi.counter"), "Surfel Counter")
    );
    const Core::GpuGraphResourceId readback = m_deferredLightingTaskGraph.importBuffer(
        m_rayTracingState.m_surfelCounterReadback,
        BufferResourceDesc(Name("render.surfel_gi.counter_readback"), "Surfel Counter Readback")
    );
    if(!counter.valid() || !readback.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import surfel counter-readback graph resources"));
        return;
    }

    const Core::GpuCopyBufferTaskRegion regions[] = {
        Core::GpuCopyBufferTaskRegion{
            .source = counter,
            .destination = readback,
            .dataSizeBytes = static_cast<u64>(sizeof(u32)) * NWB_SURFEL_COUNTER_SIZE,
        },
    };
    Core::GpuTaskSchedulingHint scheduling;
    // This infrequent diagnostic is independent of the deferred suffix. Treat it as a small copy so a dedicated
    // Transfer transport can absorb it after Present, while Compute/Graphics remain valid fallbacks.
    scheduling.cost = Core::GpuTaskCostHint::Small;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Core::GpuTaskId dependencies[] = { m_deferredSurfelGiTask };
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.surfel_gi.counter_readback"))
        .setMarkerLabel("Surfel Counter Readback")
        .setQueue(TransferQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(dependencies, LengthOf(dependencies))
    ;
    m_deferredSurfelGiCounterReadbackTask = m_deferredLightingTaskGraph.addCopyBufferTask(
        desc,
        Core::GpuCopyBufferTaskDesc{
            .regions = regions,
            .regionCount = LengthOf(regions),
            .acceptedToken = &m_rayTracingState.m_surfelCountReadbackSubmissionToken,
        }
    );
    if(!m_deferredSurfelGiCounterReadbackTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel counter-readback task"));
        return;
    }
    // The token stays invalid until native submission accepts; this timestamp is only read when it publishes.
    m_raytracingSystem.markSurfelCountReadbackScheduled();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

