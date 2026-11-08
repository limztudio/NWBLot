// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "surfel_gi_counter_readback_builder.h"

#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SurfelGiCounterReadbackBuilder::declare(const RayTracingSurfelPersistentResourceSnapshot& rayTracingSurfelResources, const Core::GpuTaskId surfelTask, const Core::GpuTaskId timingEndTask, Core::GpuTaskId& outTask){
    using namespace RendererTaskGraphDetail;

    outTask = {};
    if(!surfelTask.valid() || !timingEndTask.valid() || !m_raytracingSystem.shouldCaptureSurfelCountReadback())
        return;

    const auto counterResult = m_graph.importBuffer(rayTracingSurfelResources.counterBuffer, BufferResourceDesc(Name(s_SurfelCounterName), s_SurfelCounterLabel));
    const auto readbackResult = m_graph.importBuffer(
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
    const Array<Core::GpuTaskId, 2u> dependencies = { surfelTask, timingEndTask, };
    Core::GpuTaskDesc desc;
    desc.setIdentity(Name(s_SurfelCounterReadbackName)).setMarkerLabel(s_SurfelCounterReadbackLabel).setScheduling(scheduling).setDependencies(dependencies);
    const auto deferredSurfelGiCounterReadbackTaskResult = m_graph.addCopyBufferTask(desc, Core::GpuCopyBufferTaskDesc{ .regions = regions.data(), .regionCount = regions.size(), });
    if(!deferredSurfelGiCounterReadbackTaskResult){
        outTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel counter-readback task"));
        return;
    }
    outTask = *deferredSurfelGiCounterReadbackTaskResult;
    // The token stays invalid until native submission accepts; this timestamp is only read when it publishes.
    m_raytracingSystem.markSurfelCountReadbackScheduled();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

