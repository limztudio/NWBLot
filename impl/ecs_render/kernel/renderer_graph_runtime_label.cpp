// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_graph_runtime_label.h"

#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/shared/frame_push_constants.adhd.h>

#include <core/graphics/backend_selection.h>

#include <global/simd/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


namespace __hidden_renderer_frame_pipeline_telemetry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static AStringView PhysicalQueueClassLabel(const Core::CommandQueue::Enum queueClass)noexcept{
    return ResolveTelemetryPhysicalQueueClassLabel(static_cast<u8>(queueClass));
}

[[nodiscard]] static AStringView OwnershipTransferRouteLabel(const Core::GpuOwnershipTransferRoute::Enum route)noexcept{
    return ResolveTelemetryOwnershipTransferRouteLabel(static_cast<u8>(route));
}

[[nodiscard]] static AStringView ResourceQueueSharingLabel(const Core::ResourceQueueSharing::Mask queueSharing)noexcept{
    return ResolveTelemetryResourceQueueSharingLabel(static_cast<u8>(queueSharing));
}

static void AppendCommandArenaWorkerStatistics(const Core::GpuCommandArenaWorkerStatistics& statistics, AString<Core::Alloc::GlobalArena>& label){
    StringAppendFormat(
        label,
        "\n    Worker arena domain={} index={}: epochs={} pending epochs={} command buffers current/high-water={}/{} reusable={} leased={} pending={} growth={} resets={} native handle storage lower bound={} bytes",
        statistics.recordingWorkerDomain,
        statistics.recordingWorkerIndex,
        statistics.commandPoolEpochCount,
        statistics.pendingCommandPoolEpochCount,
        statistics.currentCommandBufferCount,
        statistics.highWaterCommandBufferCount,
        statistics.reusableCommandBufferCount,
        statistics.leasedCommandBufferCount,
        statistics.pendingCommandBufferCount,
        statistics.growthEventCount,
        statistics.resetEventCount,
        statistics.nativeHandleStorageLowerBoundBytes
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererGraphRuntimeLabel::append(
    const Core::GpuTaskGraphRuntimeStatistics& deferredRuntimeStatistics,
    const Core::GpuPhysicalQueueTopology& runtimeQueueTopology,
    const Span<const Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatistics> physicalQueueRuntimeStatistics,
    const Core::GpuCompiledGraph::ReadView& deferredCompiledPlan,
    const Core::GpuRecordedGraph& recordedGraph,
    Core::Device& device,
    AString<Core::Alloc::GlobalArena>& label
){
    if(deferredRuntimeStatistics.valid()){
        const Core::GpuTaskGraphCompileStatistics& compileStatistics = deferredRuntimeStatistics.compile;
        const Core::GpuTaskGraphRecordingStatistics& recordingStatistics = deferredRuntimeStatistics.recording;
        const Core::GpuTaskGraphSubmissionStatistics& submissionStatistics = deferredRuntimeStatistics.submission;
        const SIMDVectorDouble millisecondsScale = VectorDoubleReplicate(s_MillisecondsPerSecondTelemetry);
        const SIMDVectorDouble frameTimingMs = VectorDoubleMultiply(VectorDoubleSet(compileStatistics.declarationSeconds, compileStatistics.totalSeconds, recordingStatistics.recordingElapsedSeconds, submissionStatistics.submissionSeconds), millisecondsScale);
        const SIMDVectorDouble compileTimingMs = VectorDoubleMultiply(VectorDoubleSet(compileStatistics.analysisSeconds, compileStatistics.queueAssignmentSeconds, compileStatistics.planningSeconds, compileStatistics.validationSeconds), millisecondsScale);
        const SIMDVectorDouble analysisPlanningTimingMs = VectorDoubleMultiply(
            VectorDoubleSet(compileStatistics.dependencyAnalysisSeconds, compileStatistics.hazardAnalysisSeconds, compileStatistics.topologicalOrderSeconds, compileStatistics.packetizationSeconds),
            millisecondsScale
        );
        const SIMDVectorDouble planningRecordingTimingMs = VectorDoubleMultiply(
            VectorDoubleSet(compileStatistics.resourceStatePlanningSeconds, compileStatistics.packetDependencyPlanningSeconds, recordingStatistics.recordingSeconds, recordingStatistics.commandListAcquisitionSeconds),
            millisecondsScale
        );
        const SIMDVectorDouble recordingFrontierTimingMs = VectorDoubleMultiply(
            VectorDoubleSet(recordingStatistics.graphBarrierRecordingSeconds, recordingStatistics.taskRecordSeconds, recordingStatistics.readyFrontierElapsedSeconds, recordingStatistics.readyFrontierWorkerBusySeconds),
            millisecondsScale
        );
        const f64 frontierCapacityMs = recordingStatistics.readyFrontierWorkerCapacitySeconds * s_MillisecondsPerSecondTelemetry;
        const f64 frontierUtilizationPercent = recordingStatistics.readyFrontierWorkerUtilization() * s_PercentScaleTelemetry;
        StringAppendFormat(
            label,
            "Renderer Frame\n"
            "Task graph: tasks={} resources={} resource versions={} version edges={} packets={} deps={} transitions={}\n"
            "Declarations: resource sets={} resource-set members={} direct uses={} declared set uses={} expanded set-member uses={} materialized uses={}\n"
            "Data: payload objects={} payload object bytes={} upload blobs={} upload blob bytes={}\n"
            "Raw barriers: transitions={} UAV={} ownership releases={} ownership acquires={} state exports={}\n"
            "Logical ownership transfers: records={} signatures={} repeated signatures={} concurrent-sharing candidate records={} advised repeated resources={} route records internal/import/export={}/{}/{}\n"
            "Recording: packets={} tasks={} command lists={} barriers={} worker-routed={} overlapped={}\n"
            "Submission: accepted packets={} accepted tasks={} rejected packets={} rejected tasks={} submissions={} accepted frontier={} recovery submissions={} command lists={} waits={} inherited wait elisions={} failed submissions={}\n"
            "CPU: declaration={:.3f} ms compile={:.3f} ms native recording elapsed={:.3f} ms submit={:.3f} ms\n"
            "CPU compile phases: analysis={:.3f} ms queue assignment={:.3f} ms planning={:.3f} ms\n"
            "CPU analysis detail: validation={:.3f} ms dependencies={:.3f} ms hazards={:.3f} ms cycles/topology={:.3f} ms\n"
            "CPU planning detail: packetization={:.3f} ms resource states/barriers={:.3f} ms packet dependencies={:.3f} ms\n"
            "CPU recording summed spans: packet={:.3f} ms command-list acquisition={:.3f} ms graph barrier lowering={:.3f} ms task={:.3f} ms\n"
            "Ready-frontier recording: elapsed={:.3f} ms logical-worker busy={:.3f} ms logical-worker capacity={:.3f} ms utilization={:.1f}%",
            compileStatistics.taskCount,
            compileStatistics.resourceCount,
            compileStatistics.resourceVersionCount,
            compileStatistics.resourceVersionEdgeCount,
            compileStatistics.packetCount,
            compileStatistics.packetDependencyCount,
            compileStatistics.transitionBarrierCount,
            compileStatistics.resourceSetCount,
            compileStatistics.resourceSetMemberCount,
            compileStatistics.directResourceUseCount,
            compileStatistics.declaredResourceSetUseCount,
            compileStatistics.expandedResourceSetMemberUseCount,
            compileStatistics.resourceUseCount,
            compileStatistics.payloadObjectCount,
            compileStatistics.payloadObjectBytes,
            compileStatistics.uploadBlobCount,
            compileStatistics.uploadBlobBytes,
            compileStatistics.transitionBarrierCount,
            compileStatistics.uavBarrierCount,
            compileStatistics.ownershipReleaseBarrierCount,
            compileStatistics.ownershipAcquireBarrierCount,
            compileStatistics.stateExportBarrierCount,
            compileStatistics.logicalOwnershipTransferCount,
            compileStatistics.logicalOwnershipTransferSignatureCount,
            compileStatistics.repeatedOwnershipTransferSignatureCount,
            compileStatistics.concurrentSharingCouldAvoidTransferCount,
            compileStatistics.concurrentSharingAdviceResourceCount,
            compileStatistics.logicalOwnershipTransferCountByRoute[Core::GpuOwnershipTransferRoute::Internal],
            compileStatistics.logicalOwnershipTransferCountByRoute[Core::GpuOwnershipTransferRoute::ExternalImport],
            compileStatistics.logicalOwnershipTransferCountByRoute[Core::GpuOwnershipTransferRoute::ExternalExport],
            recordingStatistics.packetCount,
            recordingStatistics.taskCount,
            recordingStatistics.commandListCount,
            recordingStatistics.barrierCount,
            recordingStatistics.workerRoutedPacketCount,
            recordingStatistics.parallelPacketCount,
            submissionStatistics.acceptedPacketCount,
            submissionStatistics.acceptedTaskCount,
            submissionStatistics.rejectedPacketCount,
            submissionStatistics.rejectedTaskCount,
            submissionStatistics.nativeSubmissionCount,
            submissionStatistics.acceptedFrontierSubmissionCount,
            submissionStatistics.recoverySubmissionCount,
            submissionStatistics.nativeCommandListCount,
            submissionStatistics.timelineWaitCount,
            submissionStatistics.inheritedTimelineWaitElisionCount,
            submissionStatistics.rejectedSubmissionCount,
            VectorDoubleGetX(frameTimingMs),
            VectorDoubleGetY(frameTimingMs),
            VectorDoubleGetZ(frameTimingMs),
            VectorDoubleGetW(frameTimingMs),
            VectorDoubleGetX(compileTimingMs),
            VectorDoubleGetY(compileTimingMs),
            VectorDoubleGetZ(compileTimingMs),
            VectorDoubleGetW(compileTimingMs),
            VectorDoubleGetX(analysisPlanningTimingMs),
            VectorDoubleGetY(analysisPlanningTimingMs),
            VectorDoubleGetZ(analysisPlanningTimingMs),
            VectorDoubleGetW(analysisPlanningTimingMs),
            VectorDoubleGetX(planningRecordingTimingMs),
            VectorDoubleGetY(planningRecordingTimingMs),
            VectorDoubleGetZ(planningRecordingTimingMs),
            VectorDoubleGetW(planningRecordingTimingMs),
            VectorDoubleGetX(recordingFrontierTimingMs),
            VectorDoubleGetY(recordingFrontierTimingMs),
            VectorDoubleGetZ(recordingFrontierTimingMs),
            VectorDoubleGetW(recordingFrontierTimingMs),
            frontierCapacityMs,
            frontierUtilizationPercent
        );

        // Borrows plan topology + packet slots under the serialization contract; never the registry (recompile can move the plan).
        for(usize queueIndex = 0u; queueIndex < physicalQueueRuntimeStatistics.size(); ++queueIndex){
            const Core::GpuPhysicalQueueInfo& queueInfo = runtimeQueueTopology.queues[queueIndex];
            const Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatistics& queueRuntimeStatistics = physicalQueueRuntimeStatistics[queueIndex];
            const Core::Telemetry::FrameGraphPhysicalQueueSubmissionRuntimeStatistics& queueStatistics = queueRuntimeStatistics.submission;
            const Core::Telemetry::FrameGraphPhysicalQueueCompileRuntimeStatistics& queueCompileStatistics = queueRuntimeStatistics.compile;
            const b8 hasTerminalSubmissionWork = queueStatistics.acceptedPacketCount != 0u || queueStatistics.rejectedPacketCount != 0u;
            const b8 hasLogicalOwnershipTelemetry =
                queueCompileStatistics.incomingLogicalOwnershipTransferCount != 0u
                || queueCompileStatistics.outgoingLogicalOwnershipTransferCount != 0u
                || queueCompileStatistics.incomingLogicalOwnershipTransferSignatureCount != 0u
                || queueCompileStatistics.outgoingLogicalOwnershipTransferSignatureCount != 0u
                || queueCompileStatistics.incomingRepeatedOwnershipTransferSignatureCount != 0u
                || queueCompileStatistics.outgoingRepeatedOwnershipTransferSignatureCount != 0u
                || queueCompileStatistics.concurrentSharingAdviceResourceCount != 0u
            ;
            if(!hasTerminalSubmissionWork && !hasLogicalOwnershipTelemetry)
                continue;
            const Core::Telemetry::FrameGraphPhysicalQueueRecordingRuntimeStatistics& queueRecordingStatistics = queueRuntimeStatistics.recording;
            const Core::GpuCommandArenaStatistics commandArenaStatistics = device.getCommandArenaStatistics(queueInfo.id);
            if(!commandArenaStatistics.valid())
                continue;

            const f64 queueSubmissionMs = queueStatistics.submissionSeconds * s_MillisecondsPerSecondTelemetry;
            const f64 queueRecordingMs = queueRecordingStatistics.recordingSeconds * s_MillisecondsPerSecondTelemetry;
            const SIMDVectorDouble queueRecordingBreakdownMs = VectorDoubleMultiply(
                VectorDoubleSet(queueRecordingStatistics.commandListAcquisitionSeconds, queueRecordingStatistics.graphBarrierRecordingSeconds, queueRecordingStatistics.taskRecordSeconds, 0.0),
                millisecondsScale
            );
            StringAppendFormat(
                label,
                "\nPhysical queue index={} generation={} class={} family index={} native queue index={} dedicated={}: accepted packets={} accepted tasks={} rejected packets={} rejected tasks={} "
                "native submissions={} rejected submit paths={} command lists={} planned waits={} same-queue elisions={} timeline waits={} merged timeline waits={} inherited wait elisions={} accepted frontier={} recovery submissions={} CPU={:.3f} ms"
                "\n  Compile plan: tasks={} packets={} merged tasks={} prologue barriers={} epilogue barriers={} raw ownership release barriers (subset)={} raw ownership acquire barriers (subset)={}"
                "\n  Logical ownership: incoming/outgoing records={}/{} signatures={}/{} repeated signatures={}/{} attributed advice resources={}"
                "\n  Recording: packets={} tasks={} command lists={} barriers={} worker-routed={} overlapped={} CPU summed spans: command-list acquisition={:.3f} ms graph barrier lowering={:.3f} ms task={:.3f} ms packet={:.3f} ms"
                "\n  Command arena: workers={} epochs={} pending epochs={} command buffers current/high-water={}/{} reusable={} leased={} pending={} growth={} resets={} native handle storage lower bound={} bytes",
                queueRuntimeStatistics.queue.index,
                queueRuntimeStatistics.queue.deviceGeneration,
                __hidden_renderer_frame_pipeline_telemetry::PhysicalQueueClassLabel(queueInfo.queueClass),
                queueInfo.familyIndex,
                queueInfo.queueIndex,
                queueInfo.dedicated,
                queueStatistics.acceptedPacketCount,
                queueStatistics.acceptedTaskCount,
                queueStatistics.rejectedPacketCount,
                queueStatistics.rejectedTaskCount,
                queueStatistics.nativeSubmissionCount,
                queueStatistics.rejectedSubmissionCount,
                queueStatistics.nativeCommandListCount,
                queueStatistics.plannedWaitTokenCount,
                queueStatistics.sameQueueWaitElisionCount,
                queueStatistics.timelineWaitCount,
                queueStatistics.mergedTimelineWaitCount,
                queueStatistics.inheritedTimelineWaitElisionCount,
                queueStatistics.acceptedFrontierSubmissionCount,
                queueStatistics.recoverySubmissionCount,
                queueSubmissionMs,
                queueCompileStatistics.taskCount,
                queueCompileStatistics.packetCount,
                queueCompileStatistics.mergedTaskCount,
                queueCompileStatistics.prologueBarrierCount,
                queueCompileStatistics.epilogueBarrierCount,
                queueCompileStatistics.ownershipReleaseBarrierCount,
                queueCompileStatistics.ownershipAcquireBarrierCount,
                queueCompileStatistics.incomingLogicalOwnershipTransferCount,
                queueCompileStatistics.outgoingLogicalOwnershipTransferCount,
                queueCompileStatistics.incomingLogicalOwnershipTransferSignatureCount,
                queueCompileStatistics.outgoingLogicalOwnershipTransferSignatureCount,
                queueCompileStatistics.incomingRepeatedOwnershipTransferSignatureCount,
                queueCompileStatistics.outgoingRepeatedOwnershipTransferSignatureCount,
                queueCompileStatistics.concurrentSharingAdviceResourceCount,
                queueRecordingStatistics.packetCount,
                queueRecordingStatistics.taskCount,
                queueRecordingStatistics.commandListCount,
                queueRecordingStatistics.barrierCount,
                queueRecordingStatistics.workerRoutedPacketCount,
                queueRecordingStatistics.parallelPacketCount,
                VectorDoubleGetX(queueRecordingBreakdownMs),
                VectorDoubleGetY(queueRecordingBreakdownMs),
                VectorDoubleGetZ(queueRecordingBreakdownMs),
                queueRecordingMs,
                commandArenaStatistics.workerArenaCount,
                commandArenaStatistics.commandPoolEpochCount,
                commandArenaStatistics.pendingCommandPoolEpochCount,
                commandArenaStatistics.currentCommandBufferCount,
                commandArenaStatistics.highWaterCommandBufferCount,
                commandArenaStatistics.reusableCommandBufferCount,
                commandArenaStatistics.leasedCommandBufferCount,
                commandArenaStatistics.pendingCommandBufferCount,
                commandArenaStatistics.growthEventCount,
                commandArenaStatistics.resetEventCount,
                commandArenaStatistics.nativeHandleStorageLowerBoundBytes
            );

            const Core::GpuCommandArenaWorkerStatistics directCommandArenaStatistics = device.getCommandArenaWorkerStatistics(queueInfo.id, 0u, 0u);
            if(directCommandArenaStatistics.valid())
                __hidden_renderer_frame_pipeline_telemetry::AppendCommandArenaWorkerStatistics(directCommandArenaStatistics, label);

            // Recorded packets retain the exact worker domain/index used for native allocation.
            // Walk compiled order, reject identities already seen on this queue; packet counts are small, so no persistent storage.
            for(usize packetIndex = 0u; packetIndex < deferredCompiledPlan.packetCount(); ++packetIndex){
                const Core::GpuSubmissionPacketId packet = deferredCompiledPlan.packetIdAt(packetIndex);
                const Core::GpuCompiledPacketView packetView = deferredCompiledPlan.packet(packet);
                if(!packetView.valid())
                    return false;
                if(packetView.plan->queue != queueInfo.id)
                    continue;
                const Optional<Core::GpuRecordedPacket> recordedPacket = recordedGraph.packetSnapshot(packet);
                if(!recordedPacket || recordedPacket->recordingWorkerIndex == 0u)
                    continue;

                b8 alreadyAppended = false;
                for(usize previousPacketIndex = 0u; previousPacketIndex < packetIndex; ++previousPacketIndex){
                    const Core::GpuSubmissionPacketId previousPacket = deferredCompiledPlan.packetIdAt(previousPacketIndex);
                    const Core::GpuCompiledPacketView previousPacketView = deferredCompiledPlan.packet(previousPacket);
                    if(!previousPacketView.valid())
                        return false;
                    if(previousPacketView.plan->queue != queueInfo.id)
                        continue;
                    const Optional<Core::GpuRecordedPacket> previousRecordedPacket = recordedGraph.packetSnapshot(previousPacket);
                    if(previousRecordedPacket && previousRecordedPacket->recordingWorkerDomain == recordedPacket->recordingWorkerDomain && previousRecordedPacket->recordingWorkerIndex == recordedPacket->recordingWorkerIndex){
                        alreadyAppended = true;
                        break;
                    }
                }
                if(alreadyAppended)
                    continue;

                const Core::GpuCommandArenaWorkerStatistics workerCommandArenaStatistics = device.getCommandArenaWorkerStatistics(queueInfo.id, recordedPacket->recordingWorkerDomain, recordedPacket->recordingWorkerIndex);
                if(workerCommandArenaStatistics.valid())
                    __hidden_renderer_frame_pipeline_telemetry::AppendCommandArenaWorkerStatistics(workerCommandArenaStatistics, label);
            }
        }

        const usize logicalOwnershipTransferCount = deferredCompiledPlan.logicalOwnershipTransferCount();
        const Core::GpuCompiledOwnershipTransfer* const logicalOwnershipTransfers = deferredCompiledPlan.logicalOwnershipTransfers();
        if(logicalOwnershipTransfers){
            for(usize transferIndex = 0u; transferIndex < logicalOwnershipTransferCount; ++transferIndex){
                const Core::GpuCompiledOwnershipTransfer& transfer = logicalOwnershipTransfers[transferIndex];
                NWB_ASSERT(transfer.valid());

                StringAppendFormat(
                    label,
                    "\nLogical ownership transfer {}: resource identity={} route={} source physical queue index={} generation={} family={} "
                    "destination physical queue index={} generation={} family={} declared sharing={} mask={} concurrent sharing could avoid={}",
                    transferIndex,
                    transfer.resourceIdentity.resolvedText(),
                    __hidden_renderer_frame_pipeline_telemetry::OwnershipTransferRouteLabel(transfer.route),
                    transfer.sourceQueue.index,
                    transfer.sourceQueue.deviceGeneration,
                    transfer.sourceQueueFamilyIndex,
                    transfer.destinationQueue.index,
                    transfer.destinationQueue.deviceGeneration,
                    transfer.destinationQueueFamilyIndex,
                    __hidden_renderer_frame_pipeline_telemetry::ResourceQueueSharingLabel(transfer.declaredQueueSharing),
                    static_cast<u32>(transfer.declaredQueueSharing),
                    transfer.concurrentSharingCouldAvoid
                );
            }
        }
    }
    else
        label += s_RendererFrameTitle;
    return true;
}


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
