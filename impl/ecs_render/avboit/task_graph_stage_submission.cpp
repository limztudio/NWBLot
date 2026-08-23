// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_system.h"

#include <impl/ecs_render/kernel/arena_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_task_graph_stage_submission{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_TimingTicketCapacity = 7u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererAvboitTaskGraphSubmission RendererAvboitSystem::submitTaskGraphStage(
    RendererAvboitTaskGraphSubmitContext& context,
    const RendererAvboitTaskGraphValidation& validation
)const{
    RendererAvboitTaskGraphSubmission submission;
    if(!validation.valid())
        return submission;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphSubmitter submitter(context.m_device);
    Core::GpuTaskGraphTaskTimingTicket timingTickets[
        __hidden_avboit_task_graph_stage_submission::s_TimingTicketCapacity
    ] = {};
    usize timingTicketCount = 0u;
    const auto appendTimingTicket = [&timingTickets, &timingTicketCount](
        const Core::GpuTaskId task,
        Core::GpuTimingSubmissionTicket& timingTicket
    ){
        NWB_ASSERT(timingTicketCount < LengthOf(timingTickets));
        timingTickets[timingTicketCount++] = Core::GpuTaskGraphTaskTimingTicket{
            .task = task,
            .timingTicket = &timingTicket,
        };
    };

    appendTimingTicket(m_taskGraphStage.m_preTask, context.m_timingTickets.m_pre);
    if(validation.stage().asynchronous){
        appendTimingTicket(m_taskGraphStage.m_depthWarpTask, context.m_timingTickets.m_depthWarp);
        if(m_taskGraphStage.m_extinctionComputeEmulationTask.valid()){
            // Both callbacks resolve to one packet and deliberately share the one Extinction timing ticket.
            appendTimingTicket(
                m_taskGraphStage.m_extinctionComputeEmulationTask,
                context.m_timingTickets.m_extinction
            );
        }
        appendTimingTicket(m_taskGraphStage.m_extinctionTask, context.m_timingTickets.m_extinction);
        appendTimingTicket(m_taskGraphStage.m_integrationTask, context.m_timingTickets.m_integration);
        if(m_taskGraphStage.m_accumulationComputeEmulationTask.valid()){
            // Both callbacks resolve to one packet and deliberately share the one Accumulation timing ticket.
            appendTimingTicket(
                m_taskGraphStage.m_accumulationComputeEmulationTask,
                context.m_timingTickets.m_accumulation
            );
        }
        appendTimingTicket(m_taskGraphStage.m_accumulationTask, context.m_timingTickets.m_accumulation);
    }

    submission.m_submitterAccepted = submitter.submitTaskRangeInCompileOrderFromTasks(
        context.m_graph,
        context.m_compiledGraph,
        context.m_recordedGraph,
        validation.stage().firstTask,
        validation.submissionCompletionTask(),
        nullptr,
        0u,
        timingTickets,
        timingTicketCount,
        context.m_submissionTransaction,
        scratchArena
    );
    submission.m_preToken = context.m_submissionTransaction.taskToken(
        context.m_compiledGraph,
        validation.stage().firstTask
    );
    submission.m_completionToken = context.m_submissionTransaction.taskToken(
        context.m_compiledGraph,
        validation.submissionCompletionTask()
    );
    return submission;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

