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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererFramePipeline::scheduleDeferredLightingTaskGraphForExecution(Core::Alloc::ScratchArena& scratchArena){
    m_deferredLightingTaskGraphScheduled = false;
    if(!m_deferredLightingTaskGraphDeclared)
        return false;

    Core::GpuTaskGraphCompileOptions compileOptions;
    // A graphics prefix can split immediately after work that enables a different physical queue. This exposes the true cross-queue frontier while preserving the compiler's declaration-derived dependency order.
    compileOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    // Time accepted normal-rendering packets through the graph-owned presentation endpoint. Late readback, history-copy, and recovery tails retain separate diagnostic/lifecycle policy.
    compileOptions.packetTimingEnvelope.firstTask = m_deferredShadowPrepareTask;
    compileOptions.packetTimingEnvelope.lastTask = m_deferredFrameTimingEndTask;
    m_deferredTaskTimingFeedback.configureCompileOptions(compileOptions, m_graphics.getFrameIndex());
    compileOptions.declarationSeconds = m_deferredLightingTaskGraphDeclarationSeconds;

    const Core::GpuTaskScheduler& scheduler = m_graphics.gpuTasks();
    if(!scheduler.scheduleGraph(
        m_deferredLightingTaskGraph,
        m_deferredLightingTaskGraphAnalysis,
        m_deferredLightingTaskGraphQueueAssignments,
        m_deferredLightingCompiledGraph,
        m_deferredLightingRecordedGraph,
        m_deferredLightingSubmissionTransaction,
        scratchArena,
        compileOptions
    )){
        const auto& analysisDiagnostic = m_deferredLightingTaskGraphAnalysis.diagnostic();
        const auto& queueDiagnostic = m_deferredLightingTaskGraphQueueAssignments.diagnostic();
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred graph scheduling failed: analysis={} task={} resource={} queue={} queueTask={}")
            , static_cast<u32>(analysisDiagnostic.status), analysisDiagnostic.task.index, analysisDiagnostic.resource.index
            , static_cast<u32>(queueDiagnostic.status), queueDiagnostic.task.index
        );
        return false;
    }

    const Core::GpuTaskGraph::DeclarationReadView declarations(m_deferredLightingTaskGraph);
    const Core::GpuCompiledGraph::ReadView compiledPlan(m_deferredLightingCompiledGraph);
    if(!declarations.valid() || !compiledPlan.validFor(declarations)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: deferred graph scheduling lost its compiler plan"));
        return false;
    }
    if(!prepareDeferredGraphPacketEnvelopeMetrics(declarations, compiledPlan, scratchArena))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not prepare deferred graph packet metrics"));

    m_deferredLightingTaskGraphScheduled = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

