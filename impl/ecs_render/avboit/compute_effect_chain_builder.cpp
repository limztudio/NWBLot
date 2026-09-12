// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/compute_effect_chain_builder.h>


#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/avboit/task_graph_extinction_integration_tasks.h>
#include <impl/ecs_render/avboit/task_graph_occupancy_tasks.h>
#include <impl/ecs_render/avboit/task_graph_timing_metadata.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/task_timing_feedback.h>
#include <impl/ecs_render/kernel/timing_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_compute_effect_chain{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Core::GpuTaskSchedulingHint MakeComputeEffectScheduling(){
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    // Depth Warp and Integration prefer Compute; preserve affinity after Graphics collapse.
    RendererTaskGraphDetail::EnableSameFamilyComputeEffectRouting(scheduling);
    RendererTaskGraphDetail::EnableCrossFamilyComputeEffectRouting(scheduling);
    // Accepted samples use the queue class and exact transport chosen by this compile.
    scheduling.allowTimingFeedbackRouting = true;
    scheduling.allowCrossClassTimingFeedbackRouting = true;
    return scheduling;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AvboitComputeEffectChainBuilder::AvboitComputeEffectChainBuilder(
    Core::GpuTaskGraph& graph,
    RendererAvboitSystem& avboitSystem
)
    : m_graph(graph)
    , m_avboitSystem(avboitSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool AvboitComputeEffectChainBuilder::declareDepthWarp(
    const AvboitDepthWarpStageInputs& inputs,
    AvboitDepthWarpStageResult& outResult
){
    using namespace RendererTaskGraphDetail;
    outResult = AvboitDepthWarpStageResult{};
    if(!inputs.targets)
        return false;
    if(!inputs.timingFeedback)
        return false;
    if(!inputs.depthWarpTimingTicket)
        return false;
    if(!inputs.occupancyTask.valid())
        return false;

    const Core::GpuTaskSchedulingHint avboitComputeScheduling = __hidden_avboit_compute_effect_chain::MakeComputeEffectScheduling();
    const Core::GpuTaskTimingMetadata avboitComputeStageTiming =
        AvboitComputeStageTimingMetadata(*inputs.targets)
    ;
    Core::GpuTaskId completionTask = inputs.occupancyTask;
    if(inputs.hasTransparentRenderers){
        if(!inputs.coverage.valid() || !inputs.depthWarp.valid() || !inputs.control.valid())
            return false;
        if(!inputs.currentBindlessSlots.valid())
            return false;
        const Core::GpuTaskResourceUse depthWarpResourceUses[] = {
            ReadUse(inputs.coverage, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(inputs.depthWarp, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(inputs.control, Core::ResourceStates::UnorderedAccess),
            ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
        };
        const Core::GpuTaskId preDependency[] = { inputs.occupancyTask };
        Core::GpuTaskDesc depthWarpDesc;
        depthWarpDesc
            .setIdentity(Name("render.avboit.depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(RendererTaskGraphDetail::ComputeQueueRequest())
            .setScheduling(avboitComputeScheduling)
            .setTimingMetadata(avboitComputeStageTiming)
            .setDependencies(preDependency, LengthOf(preDependency))
            .setResourceUses(depthWarpResourceUses, LengthOf(depthWarpResourceUses))
        ;
        m_avboitSystem.taskGraphStage().m_depthWarpTask = m_graph.addTask<AvboitDepthWarpGraphTask>(
            depthWarpDesc,
            AvboitDepthWarpGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = inputs.targets,
                .timingTicket = inputs.depthWarpTimingTicket,
                .timingFeedback = inputs.timingFeedback,
                .timingScope = &RendererGpuTimingScope::s_AvboitDepthWarp,
            }
        );
        if(!m_avboitSystem.taskGraphStage().m_depthWarpTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT depth-warp graph task"));
            return false;
        }
        completionTask = m_avboitSystem.taskGraphStage().m_depthWarpTask;
    }

    outResult.completionTask = completionTask;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool AvboitComputeEffectChainBuilder::declareIntegration(
    const AvboitIntegrationStageInputs& inputs,
    AvboitIntegrationStageResult& outResult
){
    using namespace RendererTaskGraphDetail;
    outResult = AvboitIntegrationStageResult{};
    if(!inputs.targets)
        return false;
    if(!inputs.timingFeedback)
        return false;
    if(!inputs.integrationTimingTicket)
        return false;
    if(!inputs.extinctionTask.valid())
        return false;
    if(!inputs.extinction.valid() || !inputs.control.valid() || !inputs.extinctionOverflow.valid())
        return false;
    if(!inputs.transmittance.valid() || !inputs.currentBindlessSlots.valid())
        return false;

    const Core::GpuTaskSchedulingHint avboitComputeScheduling = __hidden_avboit_compute_effect_chain::MakeComputeEffectScheduling();
    const Core::GpuTaskTimingMetadata avboitComputeStageTiming =
        AvboitComputeStageTimingMetadata(*inputs.targets)
    ;
    // Integration is a Compute-preferred successor; compiler owns queue and state lowering.
    const Core::GpuTaskResourceUse integrationResourceUses[] = {
        ReadUse(inputs.extinction),
        ReadUse(inputs.control),
        ReadUse(inputs.extinctionOverflow),
        ReadWriteUse(inputs.transmittance, Core::ResourceStates::UnorderedAccess),
        ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
    };
    const Core::GpuTaskId integrationDependency[] = { inputs.extinctionTask };
    Core::GpuTaskDesc integrationDesc;
    integrationDesc
        .setIdentity(Name("render.avboit.integration"))
        .setMarkerLabel("AVBOIT Integration")
        .setQueue(RendererTaskGraphDetail::ComputeQueueRequest())
        .setScheduling(avboitComputeScheduling)
        .setTimingMetadata(avboitComputeStageTiming)
        .setDependencies(integrationDependency, LengthOf(integrationDependency))
        .setResourceUses(integrationResourceUses, LengthOf(integrationResourceUses))
    ;
    m_avboitSystem.taskGraphStage().m_integrationTask = m_graph.addTask<AvboitIntegrationGraphTask>(
        integrationDesc,
        AvboitIntegrationGraphTask::Payload{
            .avboitSystem = &m_avboitSystem,
            .targets = inputs.targets,
            .timingTicket = inputs.integrationTimingTicket,
            .timingFeedback = inputs.timingFeedback,
            .timingScope = &RendererGpuTimingScope::s_AvboitIntegration,
        }
    );
    if(!m_avboitSystem.taskGraphStage().m_integrationTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT integration graph task"));
        return false;
    }

    outResult.integrationTask = m_avboitSystem.taskGraphStage().m_integrationTask;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
