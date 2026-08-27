// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_system.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererAvboitSystem::appendTaskGraphTimingTickets(
    const RendererAvboitTaskGraphValidation& validation,
    RendererAvboitTaskGraphTimingTickets& timingTickets,
    Core::GpuTaskGraphTaskTimingTicket* const bindings,
    const usize bindingCapacity,
    usize& bindingCount
)const{
    if(!validation.valid() || !bindings || bindingCount > bindingCapacity)
        return false;

    usize requiredBindingCount = 1u;
    if(validation.stage().hasTransparentTasks){
        requiredBindingCount += 4u;
        if(m_taskGraphStage.m_extinctionComputeEmulationTask.valid())
            ++requiredBindingCount;
        if(m_taskGraphStage.m_accumulationComputeEmulationTask.valid())
            ++requiredBindingCount;
    }
    if(
        requiredBindingCount > s_AvboitTaskGraphTimingTicketCapacity
        || requiredBindingCount > bindingCapacity - bindingCount
    )
        return false;

    const auto appendTimingTicket = [bindings, &bindingCount](
        const Core::GpuTaskId task,
        Core::GpuTimingSubmissionTicket& timingTicket
    ){
        bindings[bindingCount++] = Core::GpuTaskGraphTaskTimingTicket{
            .task = task,
            .timingTicket = &timingTicket,
        };
    };

    appendTimingTicket(m_taskGraphStage.m_preTask, timingTickets.m_pre);
    if(validation.stage().hasTransparentTasks){
        appendTimingTicket(m_taskGraphStage.m_depthWarpTask, timingTickets.m_depthWarp);
        if(m_taskGraphStage.m_extinctionComputeEmulationTask.valid()){
            // Both callbacks resolve to one packet and deliberately share the one Extinction timing ticket.
            appendTimingTicket(m_taskGraphStage.m_extinctionComputeEmulationTask, timingTickets.m_extinction);
        }
        appendTimingTicket(m_taskGraphStage.m_extinctionTask, timingTickets.m_extinction);
        appendTimingTicket(m_taskGraphStage.m_integrationTask, timingTickets.m_integration);
        if(m_taskGraphStage.m_accumulationComputeEmulationTask.valid()){
            // Both callbacks resolve to one packet and deliberately share the one Accumulation timing ticket.
            appendTimingTicket(m_taskGraphStage.m_accumulationComputeEmulationTask, timingTickets.m_accumulation);
        }
        appendTimingTicket(m_taskGraphStage.m_accumulationTask, timingTickets.m_accumulation);
    }
    else{
        timingTickets.m_depthWarp.discard();
        timingTickets.m_extinction.discard();
        timingTickets.m_integration.discard();
        timingTickets.m_accumulation.discard();
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

