// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>

#include <impl/ecs_render/raytrace/task_graph_surfel_tasks.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>

#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererFramePipeline::declareDeferredSurfelCountReadbackTask(
    const RayTracingSurfelPersistentResourceSnapshot& rayTracingSurfelResources
){
    using namespace RendererTaskGraphDetail;

    m_deferredSurfelGiCounterReadbackTask = {};
    if(
        !m_deferredSurfelGiTask.valid()
        || !m_deferredFrameTimingEndTask.valid()
        || !m_raytracingSystem.shouldCaptureSurfelCountReadback()
    )
        return;

    const Core::GpuGraphResourceId counter = m_deferredLightingTaskGraph.importBuffer(
        rayTracingSurfelResources.counterBuffer,
        BufferResourceDesc(Name("render.surfel_gi.counter"), "Surfel Counter")
    );
    const Core::GpuGraphResourceId readback = m_deferredLightingTaskGraph.importBuffer(
        rayTracingSurfelResources.counterReadbackBuffer,
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
    // Infrequent diagnostic after presentation; small copy so Transfer absorbs it.
    scheduling.cost = Core::GpuTaskCostHint::Small;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Core::GpuTaskId dependencies[] = {
        m_deferredSurfelGiTask,
        m_deferredFrameTimingEndTask,
    };
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

