// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/compute_effect_chain_builder.h>

#include <core/graphics/backend_selection/backend.h>

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/avboit/task_graph_extinction_integration_tasks.h>
#include <impl/ecs_render/avboit/task_graph_occupancy_tasks.h>
#include <impl/ecs_render/avboit/task_graph_timing_metadata.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/task_timing_feedback.h>
#include <impl/ecs_render/kernel/timing_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AvboitComputeEffectChainBuilder::AvboitComputeEffectChainBuilder(
    Core::GpuTaskGraph& graph,
    RendererAvboitSystem& avboitSystem
)
    : m_graph(graph)
    , m_avboitSystem(avboitSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<AvboitDepthWarpStageResult> AvboitComputeEffectChainBuilder::declareDepthWarp(
    const AvboitDepthWarpStageInputs& inputs
){
    using namespace RendererTaskGraphDetail;
    AvboitDepthWarpStageResult result{};
    if(!inputs.targets)
        return MakeUnexpected(Failure{});
    if(!inputs.timingFeedback)
        return MakeUnexpected(Failure{});
    if(!inputs.depthWarpTimingTicket)
        return MakeUnexpected(Failure{});
    if(!inputs.occupancyTask.valid())
        return MakeUnexpected(Failure{});

    Core::GpuTaskSchedulingHint avboitComputeScheduling;
    avboitComputeScheduling.cost = Core::GpuTaskCostHint::Medium;
    avboitComputeScheduling.forceSubmissionBoundary = false;
    avboitComputeScheduling.allowPacketMerge = true;
    avboitComputeScheduling.mergeWithPrevious = true;
    avboitComputeScheduling.allowMergeAcrossConsumerFrontier = true;
    // Keep each effect in one recording packet while the compiler chooses its queue.
    RendererTaskGraphDetail::EnableSameFamilyComputeEffectRouting(avboitComputeScheduling);
    RendererTaskGraphDetail::EnableCrossFamilyComputeEffectRouting(avboitComputeScheduling);
    // Accepted samples use the queue class and exact transport chosen by this compile.
    avboitComputeScheduling.allowTimingFeedbackRouting = true;
    avboitComputeScheduling.allowCrossClassTimingFeedbackRouting = true;
    const Core::GpuTaskTimingMetadata avboitComputeStageTiming =
        AvboitComputeStageTimingMetadata(*inputs.targets)
    ;
    Core::GpuTaskId completionTask = inputs.occupancyTask;
    if(inputs.hasTransparentRenderers){
        if(!inputs.coverage.valid() || !inputs.depthWarp.valid() || !inputs.control.valid())
            return MakeUnexpected(Failure{});
        if(!inputs.currentBindlessSlots.valid())
            return MakeUnexpected(Failure{});
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
            .setScheduling(avboitComputeScheduling)
            .setTimingMetadata(avboitComputeStageTiming)
            .setDependencies(preDependency, LengthOf(preDependency))
            .setResourceUses(depthWarpResourceUses, LengthOf(depthWarpResourceUses))
        ;
        m_avboitSystem.taskGraphStage().m_depthWarpTask = m_graph.addTask<AvboitDepthWarpGraphTask>(
            depthWarpDesc,
            AvboitDepthWarpGraphTask::Payload{
                .avboitSystem = m_avboitSystem,
                .targets = *inputs.targets,
                .timingTicket = *inputs.depthWarpTimingTicket,
                .timingFeedback = inputs.timingFeedback,
                .timingScope = &RendererGpuTimingScope::s_AvboitDepthWarp,
            }
        );
        if(!m_avboitSystem.taskGraphStage().m_depthWarpTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT depth-warp graph task"));
            return MakeUnexpected(Failure{});
        }
        completionTask = m_avboitSystem.taskGraphStage().m_depthWarpTask;
    }

    result.completionTask = completionTask;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<AvboitIntegrationStageResult> AvboitComputeEffectChainBuilder::declareIntegration(
    const AvboitIntegrationStageInputs& inputs
){
    using namespace RendererTaskGraphDetail;
    AvboitIntegrationStageResult result{};
    if(!inputs.targets)
        return MakeUnexpected(Failure{});
    if(!inputs.timingFeedback)
        return MakeUnexpected(Failure{});
    if(!inputs.integrationTimingTicket)
        return MakeUnexpected(Failure{});
    if(!inputs.extinctionTask.valid())
        return MakeUnexpected(Failure{});
    if(!inputs.extinction.valid() || !inputs.control.valid() || !inputs.extinctionOverflow.valid())
        return MakeUnexpected(Failure{});
    if(!inputs.transmittance.valid() || !inputs.currentBindlessSlots.valid())
        return MakeUnexpected(Failure{});

    Core::GpuTaskSchedulingHint avboitComputeScheduling;
    avboitComputeScheduling.cost = Core::GpuTaskCostHint::Medium;
    avboitComputeScheduling.forceSubmissionBoundary = false;
    avboitComputeScheduling.allowPacketMerge = true;
    avboitComputeScheduling.mergeWithPrevious = true;
    avboitComputeScheduling.allowMergeAcrossConsumerFrontier = true;
    // Keep each effect in one recording packet while the compiler chooses its queue.
    RendererTaskGraphDetail::EnableSameFamilyComputeEffectRouting(avboitComputeScheduling);
    RendererTaskGraphDetail::EnableCrossFamilyComputeEffectRouting(avboitComputeScheduling);
    // Accepted samples use the queue class and exact transport chosen by this compile.
    avboitComputeScheduling.allowTimingFeedbackRouting = true;
    avboitComputeScheduling.allowCrossClassTimingFeedbackRouting = true;
    const Core::GpuTaskTimingMetadata avboitComputeStageTiming =
        AvboitComputeStageTimingMetadata(*inputs.targets)
    ;
    // Integration retains packet affinity; the compiler lowers its queue and resource states.
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
        .setScheduling(avboitComputeScheduling)
        .setTimingMetadata(avboitComputeStageTiming)
        .setDependencies(integrationDependency, LengthOf(integrationDependency))
        .setResourceUses(integrationResourceUses, LengthOf(integrationResourceUses))
    ;
    m_avboitSystem.taskGraphStage().m_integrationTask = m_graph.addTask<AvboitIntegrationGraphTask>(
        integrationDesc,
        AvboitIntegrationGraphTask::Payload{
            .avboitSystem = m_avboitSystem,
            .targets = *inputs.targets,
            .timingTicket = *inputs.integrationTimingTicket,
            .timingFeedback = inputs.timingFeedback,
            .timingScope = &RendererGpuTimingScope::s_AvboitIntegration,
        }
    );
    if(!m_avboitSystem.taskGraphStage().m_integrationTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT integration graph task"));
        return MakeUnexpected(Failure{});
    }

    result.integrationTask = m_avboitSystem.taskGraphStage().m_integrationTask;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

