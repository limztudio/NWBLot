// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/material/task_graph_object_geometry_cache.h>
#include <impl/ecs_render/reflection/scene_content_stamp.h>

#include <global/hash_utils.h>

#include <impl/ecs_render/raytrace/task_graph_post_gbuffer_normalize_task.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_finalize_task.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_prepare_tasks.h>
#include <impl/ecs_render/raytrace/task_graph_shadow_visibility_tasks.h>
#include <impl/ecs_render/raytrace/task_graph_surfel_tasks.h>
#include <impl/ecs_render/raytrace/task_graph_refraction_resolve.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/reflection/task_graph_reflection.h>
#include <impl/ecs_render/avboit/task_graph_refraction_capture.h>
#include <impl/ecs_render/avboit/generated_geometry_reuse.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>

#include <core/task/gpu/capture/command_ir.h>
#include <core/task/gpu/scheduler.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>

#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>
#include <impl/ecs_render/kernel/task_graph_frame_recovery_task.h>
#include <impl/ecs_render/kernel/task_graph_frame_timing_end_task.h>
#include <impl/ecs_render/kernel/task_graph_queue_lookup.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <impl/ecs_render/deferred/task_graph_prefix_tasks.h>
#include <impl/ecs_render/deferred/task_graph_gbuffer_task.h>
#include <impl/ecs_render/deferred/task_graph_present_task.h>
#include <impl/ecs_render/deferred/task_graph_suffix_builder.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>

#include <impl/ecs_render/mesh/task_graph_prefix_tasks.h>
#include <impl/ecs_render/material/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_tasks.h>
#include <impl/ecs_render/csg/task_graph_opaque_interval_tasks.h>
#include <impl/ecs_render/csg/task_graph_transparent_interval_tasks.h>
#include <impl/ecs_render/csg/transparent_csg_interval_builder.h>
#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/deferred/lighting_stage_builder.h>
#include <impl/ecs_render/deferred/frame_tail_builder.h>
#include <impl/ecs_render/raytrace/hardware_caustics_stage_builder.h>

#include <impl/ecs_render/avboit/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/avboit/task_graph_occupancy_tasks.h>
#include <impl/ecs_render/avboit/task_graph_extinction_integration_tasks.h>
#include <impl/ecs_render/avboit/task_graph_accumulation_tasks.h>
#include <impl/ecs_render/avboit/task_graph_timing_metadata.h>
#include <impl/ecs_render/avboit/clear_chain_builder.h>
#include <impl/ecs_render/avboit/compute_effect_chain_builder.h>
#include <impl/ecs_render/avboit/occupancy_record_builder.h>
#include <impl/ecs_render/avboit/extinction_record_builder.h>
#include <impl/ecs_render/avboit/accumulation_record_builder.h>
#include <impl/ecs_render/avboit/avboit_pass_upload_helper.h>
#include <impl/ecs_render/avboit/material_upload_builder.h>
#include <impl/ecs_render/avboit/geometry_preparation_builder.h>
#include <impl/ecs_render/avboit/compute_emulation_capture.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN



namespace __hidden_task_graph_deferred_lighting{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool PreparePacketEnvelopeMetrics(
    const Core::GpuTaskGraph::DeclarationReadView& graph,
    const Core::GpuCompiledGraph::ReadView& compiledGraph,
    Core::GpuTimingRecorder& timingRecorder,
    const u64 sourceFrameIndex,
    Core::Alloc::ScratchArena& scratchArena
){
    const Core::GpuSubmissionPacketRange range = compiledGraph.packetTimingEnvelopeRange();
    if(!range.valid() || !compiledGraph.validPacketRange(range))
        return false;

    Vector<Core::GpuPacketEnvelopeMetricScope, Core::Alloc::ScratchArena> packetScopes{scratchArena};
    Vector<Core::GpuPacketEnvelopeMetricQueueOutput, Core::Alloc::ScratchArena> queueOutputs{scratchArena};
    packetScopes.reserve(range.packetCount);
    queueOutputs.reserve(range.packetCount);
    for(usize packetOffset = 0u; packetOffset < range.packetCount; ++packetOffset){
        const Core::GpuSubmissionPacketId packetID = compiledGraph.packetIdAt(range.first.index + packetOffset);
        const Core::GpuCompiledPacketView packetView = compiledGraph.packet(packetID);
        if(!packetView.valid() || !packetView.plan->recordsPacketEnvelopeTiming || packetView.plan->taskCount == 0u)
            return false;

        const Name packetScopeName = Core::GpuTaskPacketTimingScopeName(graph.taskAt(packetView.tasks[0u].index).identity);
        if(!packetScopeName)
            return false;
        packetScopes.push_back(Core::GpuPacketEnvelopeMetricScope{
            .scopeName = packetScopeName,
            .physicalQueue = packetView.plan->queue,
        });

        bool hasQueueOutput = false;
        for(const Core::GpuPacketEnvelopeMetricQueueOutput& output : queueOutputs)
            hasQueueOutput = hasQueueOutput || output.physicalQueue == packetView.plan->queue;
        if(hasQueueOutput)
            continue;

        const Name internalIdleScopeName = RendererGpuTimingScope::DeferredGraphQueueInternalIdle(packetView.plan->queue, scratchArena);
        if(!internalIdleScopeName)
            return false;
        queueOutputs.push_back(Core::GpuPacketEnvelopeMetricQueueOutput{
            .physicalQueue = packetView.plan->queue,
            .internalIdleScopeName = internalIdleScopeName,
        });
    }

    return timingRecorder.preparePacketEnvelopeMetrics(
        sourceFrameIndex,
        MakeNotNull(static_cast<const Core::GpuPacketEnvelopeMetricScope*>(packetScopes.data())),
        packetScopes.size(),
        RendererGpuTimingScope::s_DeferredGraphQueueOverlap.identity,
        MakeNotNull(static_cast<const Core::GpuPacketEnvelopeMetricQueueOutput*>(queueOutputs.data())),
        queueOutputs.size()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool RendererFramePipeline::prepareDeferredGraphPacketEnvelopeMetrics(
    const Core::GpuTaskGraph::DeclarationReadView& graph,
    const Core::GpuCompiledGraph::ReadView& compiledGraph,
    Core::Alloc::ScratchArena& scratchArena
){
    return __hidden_task_graph_deferred_lighting::PreparePacketEnvelopeMetrics(
        graph,
        compiledGraph,
        m_graphics.gpuTiming(),
        m_graphics.getFrameIndex(),
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

