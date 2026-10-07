// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "stress_frame_graph_snapshot.h"

#include <loader/project_entry.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/telemetry/codec.h>
#include <core/telemetry/frame_graph_registry.h>
#include <core/telemetry/session.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_stress_frame_graph_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void WriteJsonText(OutputFileStream& output, const AStringView text){
    constexpr AStringView s_HexDigits = "0123456789abcdef";
    output << '"';
    for(const char ch : text){
        switch(ch){
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if(static_cast<u8>(ch) < 0x20u)
                output << "\\u00" << s_HexDigits[static_cast<u8>(ch) >> 4u] << s_HexDigits[static_cast<u8>(ch) & 0x0fu];
            else
                output << ch;
            break;
        }
    }
    output << '"';
}

[[nodiscard]] constexpr AStringView QueueClassLabel(const Core::Telemetry::FrameGraphQueueClass::Enum queueClass)noexcept{
    switch(queueClass){
    case Core::Telemetry::FrameGraphQueueClass::Graphics: return "Graphics";
    case Core::Telemetry::FrameGraphQueueClass::Compute: return "Compute";
    case Core::Telemetry::FrameGraphQueueClass::Transfer: return "Transfer";
    default: return "Unknown";
    }
}

void WriteQueue(OutputFileStream& output, const Core::Telemetry::FrameGraphPhysicalQueueId& queue){
    if(!queue.valid()){
        output << "null";
        return;
    }
    output << "{\"index\":" << queue.index << ",\"device_generation\":" << queue.deviceGeneration << '}';
}

[[nodiscard]] bool ValidateSnapshot(const Core::Telemetry::FrameGraphPayload& payload, const u64 sourceFrame)noexcept{
    if(
        payload.frameIndex != sourceFrame || payload.nodes.empty() || payload.edges.empty()
        || !payload.packetSubmissionStatisticsPresent || payload.packetSubmissionStatistics.empty()
        || payload.physicalQueueRuntimeStatistics.empty()
    )
        return false;
    usize runtimeOwnerCount = 0u;
    u64 planGeneration = 0u;
    u64 expectedTaskCount = 0u;
    for(const auto& node : payload.nodes){
        if(!node.runtimeStatistics.present)
            continue;
        ++runtimeOwnerCount;
        const auto& statistics = node.runtimeStatistics;
        if(
            statistics.planGeneration == 0u || statistics.compile.taskCount == 0u || statistics.compile.packetCount == 0u
            || statistics.submission.rejectedPacketCount != 0u || statistics.submission.acceptedPacketCount == 0u
        )
            return false;
        planGeneration = statistics.planGeneration;
        expectedTaskCount = statistics.compile.taskCount;
    }
    if(runtimeOwnerCount != 1u)
        return false;
    usize compiledTaskCount = 0u;
    for(const auto& node : payload.nodes){
        if(!node.compiledTask.present)
            continue;
        if(node.compiledTask.planGeneration != planGeneration)
            return false;
        ++compiledTaskCount;
    }
    if(compiledTaskCount != expectedTaskCount)
        return false;
    for(const auto& packet : payload.packetSubmissionStatistics){
        if(packet.packetGeneration != planGeneration || !packet.queue.valid() || packet.taskCount == 0u)
            return false;
    }
    for(const auto& queue : payload.physicalQueueRuntimeStatistics){
        if(queue.statistics.planGeneration != planGeneration || !queue.statistics.queue.valid())
            return false;
    }
    return true;
}

[[nodiscard]] bool WriteSnapshot(
    OutputFileStream& output,
    const Core::Telemetry::FrameGraphPayload& payload,
    const u64 graphicsFrame,
    const u64 completedFrames
){
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
        WriteJsonText(output, node.name.resolvedText());
        output << ",\"label\":";
        WriteJsonText(output, node.label);
        output << ",\"kind\":" << static_cast<u32>(node.kind) << ",\"flags\":" << static_cast<u32>(node.flags);
        if(node.compiledTask.present){
            output << ",\"plan_generation\":" << node.compiledTask.planGeneration
                << ",\"packet_index\":" << node.compiledTask.packetIndex
                << ",\"packetization_decision\":" << static_cast<u32>(node.compiledTask.packetizationDecision);
        }
        if(node.queueAssignment.present){
            output << ",\"queue_class\":";
            WriteJsonText(output, QueueClassLabel(node.queueAssignment.queueClass));
            output << ",\"planned_queue\":";
            WriteQueue(output, node.queueAssignment.plannedQueue);
            output << ",\"accepted_queue\":";
            WriteQueue(output, node.queueAssignment.acceptedQueue);
            output << ",\"dedicated\":" << (node.queueAssignment.dedicated ? "true" : "false")
                << ",\"assignment_reason\":" << static_cast<u32>(node.queueAssignment.reason)
                << ",\"assignment_modifiers\":" << static_cast<u32>(node.queueAssignment.modifiers);
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
        output << "{\"from\":" << edge.fromNodeIndex << ",\"to\":" << edge.toNodeIndex
            << ",\"kind\":" << static_cast<u32>(edge.kind) << ",\"flags\":" << static_cast<u32>(edge.flags) << '}';
    }
    output << "\n],\n\"physical_queues\":[\n";
    for(usize index = 0u; index < payload.physicalQueueRuntimeStatistics.size(); ++index){
        const auto& record = payload.physicalQueueRuntimeStatistics[index];
        const auto& statistics = record.statistics;
        if(index != 0u)
            output << ",\n";
        output << "{\"owner_node\":" << record.ownerNodeIndex << ",\"queue\":";
        WriteQueue(output, statistics.queue);
        output << ",\"queue_class\":";
        WriteJsonText(output, QueueClassLabel(statistics.queueClass));
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
        output << "{\"owner_node\":" << packet.ownerNodeIndex << ",\"plan_generation\":" << packet.packetGeneration
            << ",\"packet_index\":" << packet.packetIndex << ",\"queue\":";
        WriteQueue(output, packet.queue);
        output << ",\"queue_class\":";
        WriteJsonText(output, QueueClassLabel(packet.queueClass));
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


bool CaptureStressFrameGraphSnapshot(ProjectRuntimeContext& context, const AStringView outputPath, const u64 completedFrames){
    const u64 graphicsFrame = context.graphics.getFrameIndex();
    if(outputPath.empty() || completedFrames == 0u || graphicsFrame == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("StressFrameGraphSnapshot: no completed frame or output path"));
        return false;
    }
    // runFrame increments its index after render/present; the contributor retains the preceding completed frame.
    const u64 sourceFrame = graphicsFrame - 1u;
    Core::Telemetry::TelemetryArena arena(Name("tests/smoke/frame_graph_snapshot"));
    Core::Telemetry::CaptureSession session(arena);
    session.setCaptureOptions(Core::Telemetry::CaptureOptions::FrameGraphOnly());
    session.setFrameIndex(sourceFrame);
    if(!context.frameGraphRegistry.record(session) || session.eventCount() != 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("StressFrameGraphSnapshot: public frame graph capture failed"));
        return false;
    }
    const auto* const event = session.view().eventAt(0u);
    Core::Telemetry::FrameGraphPayload payload(arena);
    if(
        !event || event->header.kind != Core::Telemetry::EventKind::FrameGraphFrame || event->header.frameIndex != sourceFrame
        || !Core::Telemetry::ParseFrameGraphPayload(arena, event->payload.data(), event->payload.size(), payload)
        || !__hidden_stress_frame_graph_snapshot::ValidateSnapshot(payload, sourceFrame)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("StressFrameGraphSnapshot: missing runtime data or inconsistent completed-frame identity"));
        return false;
    }
    Core::Telemetry::TelemetryBytes encoded(arena);
    if(
        !Core::Telemetry::EncodeEventStream(session.view(), encoded)
        || encoded.size() > static_cast<usize>(Limit<StreamSize>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("StressFrameGraphSnapshot: event stream encoding failed"));
        return false;
    }
    AString<Core::Telemetry::TelemetryArena> binaryPath(arena);
    binaryPath.assign(outputPath);
    binaryPath.append(".nwbs");
    OutputFileStream binary(binaryPath.c_str(), s_FileOpenBinary | s_FileOpenTruncate);
    if(!binary.is_open()){
        NWB_LOGGER_ERROR(NWB_TEXT("StressFrameGraphSnapshot: encoded stream output could not be opened"));
        return false;
    }
    binary.write(reinterpret_cast<const char*>(encoded.data()), static_cast<StreamSize>(encoded.size()));
    binary.flush();
    if(!binary.good()){
        NWB_LOGGER_ERROR(NWB_TEXT("StressFrameGraphSnapshot: encoded stream output failed"));
        return false;
    }
    AString<Core::Telemetry::TelemetryArena> jsonPath(arena);
    jsonPath.assign(outputPath);
    OutputFileStream output(jsonPath.c_str(), s_FileOpenTruncate);
    if(!output.is_open() || !__hidden_stress_frame_graph_snapshot::WriteSnapshot(output, payload, graphicsFrame, completedFrames)){
        NWB_LOGGER_ERROR(NWB_TEXT("StressFrameGraphSnapshot: readable snapshot output failed"));
        return false;
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressFrameGraphSnapshot: complete source_frame={} completed_frames={} nodes={} edges={} packets={}")
        , sourceFrame
        , completedFrames
        , payload.nodes.size()
        , payload.edges.size()
        , payload.packetSubmissionStatistics.size()
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

