// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "opaque_emulation_merge_validator.h"

#include <impl/ecs_render/renderer_frame_pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpaqueEmulationMergeValidator::validate(const Core::GpuCompiledGraph::ReadView& compiledPlan, const Span<const Core::GpuTaskId> timingTasks, b8 timingBindingsValid, b8 hasOpaqueCsgFrameWork, OpaqueEmulationMergeValidationResult& outResult)const noexcept{
    const RendererFramePipeline& pipeline = m_pipeline;
    outResult = OpaqueEmulationMergeValidationResult{};
    const auto taskIsCompiled = [&](const Core::GpuTaskId task)noexcept{
        return compiledPlan.findTask(task).valid();
    };
    const Core::GpuPhysicalQueueInfo* const opaqueComputeEmulationQueue = pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueComputeEmulationTask.valid() ? compiledPlan.queueInfoForTask(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueComputeEmulationTask) : nullptr;
    const Core::GpuPhysicalQueueInfo* const opaqueSharedComputeEmulationQueue =
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount != 0u
        && pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u].valid()
            ? compiledPlan.queueInfoForTask(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u])
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const opaqueCsgReceiverComputeEmulationQueue =
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask.valid()
            ? compiledPlan.queueInfoForTask(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask)
            : nullptr
    ;
    const Core::GpuPhysicalQueueInfo* const opaqueCsgIntervalSampleComputeEmulationQueue =
        pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask.valid()
            ? compiledPlan.queueInfoForTask(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask)
            : nullptr
    ;
    outResult.packetsAreGraphics = timingBindingsValid;
    for(usize prefixTaskIndex = 0u; outResult.packetsAreGraphics && prefixTaskIndex < timingTasks.size(); ++prefixTaskIndex){
        const Core::GpuPhysicalQueueInfo* const queue = compiledPlan.queueInfoForTask(timingTasks[prefixTaskIndex]);
        outResult.packetsAreGraphics = queue && queue->queueClass == Core::CommandQueue::Graphics;
    }
    // The producer shares G-buffer's packet; its boundary stays packet-local.
    outResult.opaqueComputeEmulationMerged =
        !pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueComputeEmulationTask.valid()
        || (
            taskIsCompiled(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueComputeEmulationTask)
            && compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueComputeEmulationTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask)
            && opaqueComputeEmulationQueue
            && opaqueComputeEmulationQueue->queueClass == Core::CommandQueue::Graphics
        )
    ;
    // Shared outputs need exact packet order; keep the G-buffer prelude first.
    outResult.opaqueSharedComputeEmulationMerged = [&]()noexcept{
        const usize phaseCount = pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTaskCount;
        if(phaseCount == 0u){
            for(const Core::GpuTaskId& task : pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks){
                if(task.valid())
                    return false;
            }
            return true;
        }
        if(!IsSupportedSharedComputeEmulationPhaseCount(phaseCount))
            return false;
        if(
            !opaqueSharedComputeEmulationQueue
            || opaqueSharedComputeEmulationQueue->queueClass != Core::CommandQueue::Graphics
            || !compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u])
        )
            return false;
        for(usize phaseIndex = 0u; phaseIndex < phaseCount; ++phaseIndex){
            const Core::GpuTaskId task = pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks[phaseIndex];
            const Core::GpuPhysicalQueueInfo* const queue = compiledPlan.queueInfoForTask(task);
            if(!task.valid() || !queue || queue->queueClass != Core::CommandQueue::Graphics || !compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask, task))
                return false;
        }
        for(usize phaseIndex = phaseCount; phaseIndex < pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks.size(); ++phaseIndex){
            if(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks[phaseIndex].valid())
                return false;
        }
        return
            compiledPlan.tasksFormContiguousPacketSequence(Span<const Core::GpuTaskId>(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks.data(), phaseCount))
            && compiledPlan.taskPrecedesInSamePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueSharedComputeEmulationTasks[0u])
        ;
    }();
    // Declared producers must share G-buffer's packet for the same handoff.
    outResult.opaqueCsgReceiverComputeEmulationMerged =
        !pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask.valid()
        || (
            taskIsCompiled(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask)
            && compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgReceiverComputeEmulationTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask)
            && opaqueCsgReceiverComputeEmulationQueue
            && opaqueCsgReceiverComputeEmulationQueue->queueClass == Core::CommandQueue::Graphics
        )
    ;
    // The producer/raster pair must stay contiguous in one packet.
    outResult.opaqueCsgIntervalSampleComputeEmulationMerged = [&]()noexcept{
        if(!pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask.valid())
            return true;
        if(
            !pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalCombineTask.valid()
            || !pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalSampleTask.valid()
            || !opaqueCsgIntervalSampleComputeEmulationQueue
            || opaqueCsgIntervalSampleComputeEmulationQueue->queueClass
                != Core::CommandQueue::Graphics
            || !compiledPlan.taskPrecedesOrSharesPacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalCombineTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask)
            || !compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalSampleTask)
        )
            return false;
        const Array<Core::GpuTaskId, 2> sequence = { pipeline.m_graphicsPrefixGraph.m_graphicsPrefixOpaqueCsgIntervalSampleComputeEmulationTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalSampleTask, };
        return compiledPlan.tasksFormContiguousPacketSequence(sequence);
    }();
    // Keep the clear with G-buffer's ticket; a split would rebind ownership.
    const Core::GpuPhysicalQueueInfo* const csgIntervalClearQueue = pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask.valid() ? compiledPlan.queueInfoForTask(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask) : nullptr;
    outResult.csgIntervalClearBundleMerged = !hasOpaqueCsgFrameWork
        ? (!pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearFirstTask.valid() && !pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask.valid())
        : (
            pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearFirstTask.valid()
            && pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask.valid()
            && taskIsCompiled(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearFirstTask)
            && taskIsCompiled(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask)
            && compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearFirstTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask)
            && compiledPlan.tasksSharePacket(pipeline.m_graphicsPrefixGraph.m_graphicsPrefixCsgIntervalClearTask, pipeline.m_graphicsPrefixGraph.m_graphicsPrefixGbufferTask)
            && csgIntervalClearQueue
            && csgIntervalClearQueue->queueClass == Core::CommandQueue::Graphics
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

