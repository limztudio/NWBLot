// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererAvboitSystem::appendTaskGraphTimingTickets(
    const RendererAvboitTaskGraphValidation& validation,
    const usize bindingCapacity,
    RendererAvboitTaskGraphTimingTickets& timingTickets,
    Core::GpuTaskGraphTaskTimingTicket* const bindings,
    usize& bindingCount
)const{
    if(!validation.valid() || !bindings || bindingCount > bindingCapacity)
        return false;

    // Depth warp, extinction, integration, and accumulation tickets.
    constexpr usize s_TransparentTimingTicketCount = 4u;
    usize requiredBindingCount = 1u;
    if(validation.stage().hasTransparentTasks){
        requiredBindingCount += s_TransparentTimingTicketCount;
        if(m_taskGraphStage.m_extinctionComputeEmulationTask.valid())
            ++requiredBindingCount;
        if(m_taskGraphStage.m_accumulationComputeEmulationTask.valid())
            ++requiredBindingCount;
    }
    if(requiredBindingCount > s_AvboitTaskGraphTimingTicketCapacity || requiredBindingCount > bindingCapacity - bindingCount)
        return false;

    const auto appendTimingTicket = [bindings, &bindingCount](const Core::GpuTaskId task, Core::GpuTimingSubmissionTicket& timingTicket)noexcept{
        bindings[bindingCount++] = Core::GpuTaskGraphTaskTimingTicket{ .task = task, .timingTicket = &timingTicket, };
    };

    appendTimingTicket(m_taskGraphStage.m_preTask, timingTickets.m_pre);
    if(validation.stage().hasTransparentTasks){
        appendTimingTicket(m_taskGraphStage.m_depthWarpTask, timingTickets.m_depthWarp);
        if(m_taskGraphStage.m_extinctionComputeEmulationTask.valid()){
            // Shares the one Extinction ticket.
            appendTimingTicket(m_taskGraphStage.m_extinctionComputeEmulationTask, timingTickets.m_extinction);
        }
        appendTimingTicket(m_taskGraphStage.m_extinctionTask, timingTickets.m_extinction);
        appendTimingTicket(m_taskGraphStage.m_integrationTask, timingTickets.m_integration);
        if(m_taskGraphStage.m_accumulationComputeEmulationTask.valid()){
            // Shares the one Accumulation ticket.
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

