// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "stress_frame_graph_snapshot_writer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void StressFrameGraphSnapshotWriter::WriteJsonText(const AStringView text, OutputFileStream& output){
    constexpr AStringView s_HexDigits = "0123456789abcdef";
    constexpr u8 s_JsonControlCharLimit = 0x20u;
    constexpr u8 s_NibbleBits = 4u;
    constexpr u8 s_NibbleMask = 0x0fu;
    output << '"';
    for(const char ch : text){
        switch(ch){
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if(static_cast<u8>(ch) < s_JsonControlCharLimit)
                output << "\\u00" << s_HexDigits[static_cast<u8>(ch) >> s_NibbleBits] << s_HexDigits[static_cast<u8>(ch) & s_NibbleMask];
            else
                output << ch;
            break;
        }
    }
    output << '"';
}

[[nodiscard]] constexpr AStringView StressFrameGraphSnapshotWriter::QueueClassLabel(const Core::Telemetry::FrameGraphQueueClass::Enum queueClass)noexcept{
    switch(queueClass){
    case Core::Telemetry::FrameGraphQueueClass::Graphics: return "Graphics";
    case Core::Telemetry::FrameGraphQueueClass::Compute: return "Compute";
    case Core::Telemetry::FrameGraphQueueClass::Transfer: return "Transfer";
    default: return "Unknown";
    }
}

void StressFrameGraphSnapshotWriter::WriteQueue(const Core::Telemetry::FrameGraphPhysicalQueueId& queue, OutputFileStream& output){
    if(!queue.valid()){
        output << "null";
        return;
    }
    output << "{\"index\":" << queue.index << ",\"device_generation\":" << queue.deviceGeneration << '}';
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 StressFrameGraphSnapshotWriter::write(const Core::Telemetry::FrameGraphPayload& payload, const u64 graphicsFrame, const u64 completedFrames, OutputFileStream& output)const{
    output.precision(17);
    output << "{\n\"type\":\"stress_frame_graph_snapshot\",\"source_frame\":" << payload.frameIndex
        << ",\"graphics_frame_at_capture\":" << graphicsFrame << ",\"completed_frames\":" << completedFrames
        << ",\"diagnostic_only\":true,\"compiled_state_seed_ids_available\":false"
        << ",\"compiler_extra_dependency_ids_available\":false,\"edge_scope\":\"public_analysis_edges\",\n\"nodes\":[\n";
    for(usize index = 0u; index < payload.nodes.size(); ++index){
        const auto& node = payload.nodes[index];
        if(index != 0u)
            output << ",\n";
        output << "{\"index\":" << index << ",\"name\":";
        WriteJsonText(node.name.resolvedText(), output);
        output << ",\"label\":";
        WriteJsonText(node.label, output);
        output << ",\"kind\":" << static_cast<u32>(node.kind) << ",\"flags\":" << static_cast<u32>(node.flags);
        if(node.compiledTask.present){
            output << ",\"plan_generation\":" << node.compiledTask.planGeneration << ",\"packet_index\":" << node.compiledTask.packetIndex << ",\"packetization_decision\":" << static_cast<u32>(node.compiledTask.packetizationDecision);
        }
        if(node.queueAssignment.present){
            output << ",\"queue_class\":";
            WriteJsonText(QueueClassLabel(node.queueAssignment.queueClass), output);
            output << ",\"planned_queue\":";
            WriteQueue(node.queueAssignment.plannedQueue, output);
            output << ",\"accepted_queue\":";
            WriteQueue(node.queueAssignment.acceptedQueue, output);
            output << ",\"dedicated\":" << (node.queueAssignment.dedicated ? "true" : "false") << ",\"assignment_reason\":" << static_cast<u32>(node.queueAssignment.reason) << ",\"assignment_modifiers\":" << static_cast<u32>(node.queueAssignment.modifiers);
        }
        if(node.runtimeStatistics.present){
            const auto& statistics = node.runtimeStatistics;
            output << ",\"runtime\":{\"graph_generation\":" << statistics.graphGeneration
                << ",\"plan_generation\":" << statistics.planGeneration
                << ",\"recording_attempt_generation\":" << statistics.recordingAttemptGeneration
                << ",\"tasks\":" << statistics.compile.taskCount << ",\"packets\":" << statistics.compile.packetCount
                << ",\"packet_dependencies\":" << statistics.compile.packetDependencyCount
                << ",\"transitions\":" << statistics.compile.transitionBarrierCount
                << ",\"accepted_packets\":" << statistics.submission.acceptedPacketCount
                << ",\"accepted_tasks\":" << statistics.submission.acceptedTaskCount
                << ",\"rejected_packets\":" << statistics.submission.rejectedPacketCount
                << ",\"rejected_tasks\":" << statistics.submission.rejectedTaskCount
                << ",\"rejected_native_submissions\":" << statistics.submission.rejectedSubmissionCount
                << ",\"recovery_submissions\":" << statistics.submission.recoverySubmissionCount
                << ",\"planned_waits\":" << statistics.submission.plannedWaitTokenCount
                << ",\"same_queue_elisions\":" << statistics.submission.sameQueueWaitElisionCount
                << ",\"timeline_waits\":" << statistics.submission.timelineWaitCount
                << ",\"merged_timeline_waits\":" << statistics.submission.mergedTimelineWaitCount
                << ",\"inherited_timeline_wait_elisions\":" << statistics.submission.inheritedTimelineWaitElisionCount << '}';
        }
        output << '}';
    }
    output << "\n],\n\"edges\":[\n";
    for(usize index = 0u; index < payload.edges.size(); ++index){
        const auto& edge = payload.edges[index];
        if(index != 0u)
            output << ",\n";
        output << "{\"from\":" << edge.fromNodeIndex << ",\"to\":" << edge.toNodeIndex << ",\"kind\":" << static_cast<u32>(edge.kind) << ",\"flags\":" << static_cast<u32>(edge.flags) << '}';
    }
    output << "\n],\n\"physical_queues\":[\n";
    for(usize index = 0u; index < payload.physicalQueueRuntimeStatistics.size(); ++index){
        const auto& record = payload.physicalQueueRuntimeStatistics[index];
        const auto& statistics = record.statistics;
        if(index != 0u)
            output << ",\n";
        output << "{\"owner_node\":" << record.ownerNodeIndex << ",\"queue\":";
        WriteQueue(statistics.queue, output);
        output << ",\"queue_class\":";
        WriteJsonText(QueueClassLabel(statistics.queueClass), output);
        output << ",\"tasks\":" << statistics.compile.taskCount << ",\"packets\":" << statistics.compile.packetCount
            << ",\"prologue_barriers\":" << statistics.compile.prologueBarrierCount
            << ",\"epilogue_barriers\":" << statistics.compile.epilogueBarrierCount
            << ",\"accepted_packets\":" << statistics.submission.acceptedPacketCount
            << ",\"planned_waits\":" << statistics.submission.plannedWaitTokenCount
            << ",\"same_queue_elisions\":" << statistics.submission.sameQueueWaitElisionCount
            << ",\"timeline_waits\":" << statistics.submission.timelineWaitCount
            << ",\"merged_timeline_waits\":" << statistics.submission.mergedTimelineWaitCount
            << ",\"inherited_timeline_wait_elisions\":" << statistics.submission.inheritedTimelineWaitElisionCount << '}';
    }
    output << "\n],\n\"packets\":[\n";
    for(usize index = 0u; index < payload.packetSubmissionStatistics.size(); ++index){
        const auto& packet = payload.packetSubmissionStatistics[index];
        if(index != 0u)
            output << ",\n";
        output << "{\"owner_node\":" << packet.ownerNodeIndex << ",\"plan_generation\":" << packet.packetGeneration << ",\"packet_index\":" << packet.packetIndex << ",\"queue\":";
        WriteQueue(packet.queue, output);
        output << ",\"queue_class\":";
        WriteJsonText(QueueClassLabel(packet.queueClass), output);
        output << ",\"tasks\":" << packet.taskCount << ",\"command_lists\":" << packet.commandListCount
            << ",\"planned_waits\":" << packet.plannedWaitTokenCount
            << ",\"same_queue_elisions\":" << packet.sameQueueWaitElisionCount
            << ",\"timeline_waits\":" << packet.timelineWaitCount
            << ",\"merged_timeline_waits\":" << packet.mergedTimelineWaitCount
            << ",\"inherited_timeline_wait_elisions\":" << packet.inheritedTimelineWaitElisionCount
            << ",\"joins_accepted_frontier\":" << (packet.joinsAcceptedQueueFrontier ? "true" : "false")
            << ",\"recovery\":" << (packet.recoverySubmission ? "true" : "false")
            << ",\"submission_seconds\":" << packet.submissionSeconds << '}';
    }
    output << "\n]\n}\n";
    output.flush();
    return output.good();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


