// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "surfel_gi_lifecycle_builder.h"

#include <impl/assets/graphics/gi/surfel/surfel_binding_slots.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SurfelGiLifecycleBuilder::SurfelGiLifecycleBuilder(
    Core::GpuTaskGraph& graph,
    RendererRayTracingSystem& raytracingSystem
)
    : m_graph(graph)
    , m_raytracingSystem(raytracingSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool SurfelGiLifecycleBuilder::declare(
    const SurfelGiLifecycleInputs& inputs,
    SurfelGiLifecycleResult& outResult
){
    using namespace RendererTaskGraphDetail;
    outResult = SurfelGiLifecycleResult{};
    if(
        !inputs.targets
    )
        return false;
    Core::GpuTaskId dependency = inputs.dependency;
    outResult.dependency = inputs.dependency;
    if(!inputs.hasWork)
        return true;
    {
    if(
        !inputs.surfelPool.valid()
        || !inputs.surfelCellHead.valid()
        || !inputs.surfelCounter.valid()
        || !inputs.surfelFreeList.valid()
        || !inputs.surfelPoolSnapshot.valid()
        || !inputs.surfelCellHeadSnapshot.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel-GI snapshot resources were unavailable during graph declaration"));
        return false;
    }

    if(m_raytracingSystem.needsSurfelResourceInitialization()){
        Core::GpuTaskSchedulingHint initializationScheduling;
        initializationScheduling.cost = Core::GpuTaskCostHint::Medium;
        // Merge serial successors into the first clear's packet; never force a boundary here.
        initializationScheduling.allowPacketMerge = true;
        Core::GpuTaskDesc poolClearDesc;
        poolClearDesc
            .setIdentity(Name("render.surfel_gi.initialize_pool_clear"))
            .setMarkerLabel("Surfel GI Initialize Pool Clear")
            .setQueue(ComputeTransferQueueRequest())
            .setScheduling(initializationScheduling)
            .setDependencies(&dependency, 1u)
            .setExternalDependencies(inputs.externalDependencies, inputs.externalDependencyCount)
            .setExternalStateSources(
                inputs.computeStateSource.states ? &inputs.computeStateSource : nullptr,
                inputs.computeStateSource.states ? 1u : 0u
            )
        ;
        outResult.preparationTask = m_graph.addClearBufferTask(
            poolClearDesc,
            Core::GpuClearBufferTaskDesc{
                .destination = inputs.surfelPool,
                .clearValue = 0u,
            }
        );
        if(!outResult.preparationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI pool initialization clear"));
            return false;
        }

        Core::GpuTaskSchedulingHint chainedInitializationScheduling = initializationScheduling;
        chainedInitializationScheduling.cost = Core::GpuTaskCostHint::Tiny;
        chainedInitializationScheduling.mergeWithPrevious = true;
        // Snapshot may route to Transfer; retain the full init chain in one accepted packet.
        chainedInitializationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskId initializationDependency = outResult.preparationTask;
        const auto addInitializationClear = [&](
            const Name& identity,
            const AStringView label,
            const Core::GpuGraphResourceId destination,
            const u32 clearValue,
            const Core::GpuTaskExternalStateSource* const externalStateSources,
            const usize externalStateSourceCount
        ){
            Core::GpuTaskDesc clearDesc;
            clearDesc
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setQueue(ComputeTransferQueueRequest())
                .setScheduling(chainedInitializationScheduling)
                .setDependencies(&initializationDependency, 1u)
                .setExternalStateSources(externalStateSources, externalStateSourceCount)
            ;
            const Core::GpuTaskId clearTask = m_graph.addClearBufferTask(
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
                inputs.surfelCellHead,
                NWB_SURFEL_CELL_INVALID,
                inputs.computeStateSource.states ? &inputs.computeStateSource : nullptr,
                inputs.computeStateSource.states ? 1u : 0u
            ).valid()
            || !addInitializationClear(
                Name("render.surfel_gi.initialize_counter_clear"),
                "Surfel GI Initialize Counter Clear",
                inputs.surfelCounter,
                0u,
                inputs.counterStateSource.states ? &inputs.counterStateSource : nullptr,
                inputs.counterStateSource.states ? 1u : 0u
            ).valid()
            || !addInitializationClear(
                Name("render.surfel_gi.initialize_free_list_clear"),
                "Surfel GI Initialize Free-List Clear",
                inputs.surfelFreeList,
                0u,
                inputs.computeStateSource.states ? &inputs.computeStateSource : nullptr,
                inputs.computeStateSource.states ? 1u : 0u
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
        outResult.initializationLifecycleTask =
            m_raytracingSystem.declareSurfelResourceInitializationLifecycleTask(
                m_graph,
                initializationLifecycleDesc
            );
        if(!outResult.initializationLifecycleTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI initialization lifecycle"));
            return false;
        }
        dependency = outResult.initializationLifecycleTask;
    }

    const Core::GpuCopyBufferTaskRegion snapshotRegions[] = {
        Core::GpuCopyBufferTaskRegion{
            .source = inputs.surfelPool,
            .destination = inputs.surfelPoolSnapshot,
            .dataSizeBytes = inputs.poolSnapshotByteSize,
        },
        Core::GpuCopyBufferTaskRegion{
            .source = inputs.surfelCellHead,
            .destination = inputs.surfelCellHeadSnapshot,
            .dataSizeBytes = inputs.cellHeadSnapshotByteSize,
        },
    };
    Core::GpuTaskDesc snapshotDesc;
    snapshotDesc
        .setIdentity(Name("render.surfel_gi.snapshot_copy"))
        .setMarkerLabel("Surfel GI Snapshot Copy")
        .setQueue(TransferQueueRequest())
        .setScheduling(inputs.scheduling)
        .setDependencies(&dependency, 1u)
        .setExternalStateSources(
            inputs.computeStateSource.states ? &inputs.computeStateSource : nullptr,
            inputs.computeStateSource.states ? 1u : 0u
        )
    ;
    outResult.snapshotCopyTask = m_graph.addCopyBufferTask(
        snapshotDesc,
        Core::GpuCopyBufferTaskDesc{
            .regions = snapshotRegions,
            .regionCount = LengthOf(snapshotRegions),
        }
    );
    if(!outResult.snapshotCopyTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI snapshot-copy task"));
        return false;
    }
    if(!outResult.preparationTask.valid())
        outResult.preparationTask = outResult.snapshotCopyTask;
    dependency = outResult.snapshotCopyTask;
    }
    outResult.dependency = dependency;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

