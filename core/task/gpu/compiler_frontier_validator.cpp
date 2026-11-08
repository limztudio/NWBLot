// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_frontier_validator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


b8 GpuTaskGraphFrontierValidator::Validate(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphCompilerDetail::GpuTaskGraphCompiledPlanStorage& compiledPlan, GpuTaskGraphAnalysisDiagnostic& outDiagnostic){
    const auto failAcceptedQueueFrontierPlan = [&](const GpuTaskId task, const GpuTaskId relatedTask = {}, const GpuGraphResourceId resource = {})noexcept{
        outDiagnostic = GpuTaskGraphAnalysisDiagnostic{ .task = task, .relatedTask = relatedTask, .resource = resource, .resourceVersion = {}, .status = GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask, };
        return false;
    };
    for(const GpuCompiledTask& compiledTask : compiledPlan.tasks){
        const GpuTaskGraphTaskView task = graph.taskAt(compiledTask.task.index);
        if(!task.scheduling.joinsAcceptedQueueFrontier)
            continue;

        if(!compiledTask.packet.valid() || compiledTask.packet.generation != compiledPlan.planGeneration || compiledTask.packet.index >= compiledPlan.packets.size())
            return failAcceptedQueueFrontierPlan(compiledTask.task);

        const GpuSubmissionPacket& packet = compiledPlan.packets[compiledTask.packet.index];
        const b8 invalidAcceptedQueueFrontierPlan = packet.taskCount != 1u
            || packet.dependencyCount != 0u
            || packet.externalDependencyCount != 0u
            || !packet.joinsAcceptedQueueFrontier
            || packet.isRecoverySubmission != task.scheduling.isRecoverySubmission
            || compiledTask.prologueStateSeedCount != 0u
            || compiledTask.prologueBarrierCount != 0u
            || compiledTask.epilogueBarrierCount != 0u
        ;
        if(!invalidAcceptedQueueFrontierPlan)
            continue;

        GpuTaskId relatedTask;
        GpuGraphResourceId resource;
        if(packet.taskCount > 1u && packet.taskOffset <= compiledPlan.packetTasks.size() && packet.taskCount <= compiledPlan.packetTasks.size() - packet.taskOffset){
            for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
                const GpuTaskId packetTask = compiledPlan.packetTasks[packet.taskOffset + taskIndex];
                if(packetTask != compiledTask.task){
                    relatedTask = packetTask;
                    break;
                }
            }
        }
        if(packet.dependencyCount != 0u && packet.dependencyOffset < compiledPlan.packetDependencies.size()){
            const GpuSubmissionPacketId producerPacket = compiledPlan.packetDependencies[packet.dependencyOffset].producer;
            if(producerPacket.index < compiledPlan.packets.size()){
                const GpuSubmissionPacket& producer = compiledPlan.packets[producerPacket.index];
                if(producer.taskCount != 0u && producer.taskOffset < compiledPlan.packetTasks.size())
                    relatedTask = compiledPlan.packetTasks[producer.taskOffset];
            }
        }
        if(compiledTask.prologueStateSeedCount != 0u && compiledTask.prologueStateSeedOffset < compiledPlan.prologueStateSeeds.size())
            resource = compiledPlan.prologueStateSeeds[compiledTask.prologueStateSeedOffset].resource;
        else if(compiledTask.prologueBarrierCount != 0u && compiledTask.prologueBarrierOffset < compiledPlan.prologueBarriers.size())
            resource = compiledPlan.prologueBarriers[compiledTask.prologueBarrierOffset].resource;
        else if(compiledTask.epilogueBarrierCount != 0u && compiledTask.epilogueBarrierOffset < compiledPlan.epilogueBarriers.size())
            resource = compiledPlan.epilogueBarriers[compiledTask.epilogueBarrierOffset].resource;

        return failAcceptedQueueFrontierPlan(compiledTask.task, relatedTask, resource);
    }
    return true;
}


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
