// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "stress_frame_graph_snapshot.h"
#include "stress_frame_graph_snapshot_writer.h"
#include <tests/common/frame_graph_snapshot_validation.h>

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


[[nodiscard]] b8 ValidateSnapshot(const Core::Telemetry::FrameGraphPayload& payload, const u64 sourceFrame)noexcept{
    if(payload.frameIndex != sourceFrame || payload.nodes.empty() || payload.edges.empty() || !payload.packetSubmissionStatisticsPresent || payload.packetSubmissionStatistics.empty() || payload.physicalQueueRuntimeStatistics.empty()){
        usize runtimeOwners = 0u;
        usize compiledTasks = 0u;
        for(const auto& node : payload.nodes){
            runtimeOwners += node.runtimeStatistics.present ? 1u : 0u;
            compiledTasks += node.compiledTask.present ? 1u : 0u;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressFrameGraphSnapshot: invalid payload source_frame={} payload_frame={} nodes={} edges={} runtime_owners={} compiled_tasks={} packet_statistics_present={} packets={} physical_queues={}")
            , sourceFrame, payload.frameIndex, payload.nodes.size(), payload.edges.size(), runtimeOwners, compiledTasks, payload.packetSubmissionStatisticsPresent, payload.packetSubmissionStatistics.size(), payload.physicalQueueRuntimeStatistics.size()
        );
        return false;
    }
    usize runtimeOwnerCount = 0u;
    u64 planGeneration = 0u;
    u64 expectedTaskCount = 0u;
    for(const auto& node : payload.nodes){
        if(!node.runtimeStatistics.present)
            continue;
        ++runtimeOwnerCount;
        const auto& statistics = node.runtimeStatistics;
        if(statistics.planGeneration == 0u || statistics.compile.taskCount == 0u || statistics.compile.packetCount == 0u || statistics.submission.acceptedPacketCount == 0u || !ValidateNormalFrameSnapshotCompletion(payload, statistics)){
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("StressFrameGraphSnapshot: invalid normal completion graph={} plan={} attempt={} tasks={} packets={} rejected_packets={} rejected_tasks={} rejected_submissions={} recovery_submissions={} accepted_packets={} accepted_tasks={}")
                , statistics.graphGeneration, statistics.planGeneration, statistics.recordingAttemptGeneration, statistics.compile.taskCount, statistics.compile.packetCount
                , statistics.submission.rejectedPacketCount, statistics.submission.rejectedTaskCount, statistics.submission.rejectedSubmissionCount, statistics.submission.recoverySubmissionCount
                , statistics.submission.acceptedPacketCount, statistics.submission.acceptedTaskCount
            );
            return false;
        }
        planGeneration = statistics.planGeneration;
        expectedTaskCount = statistics.compile.taskCount;
    }
    if(runtimeOwnerCount != 1u){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressFrameGraphSnapshot: runtime owner count expected=1 observed={} source_frame={} nodes={} packets={} physical_queues={}")
            , runtimeOwnerCount, sourceFrame, payload.nodes.size(), payload.packetSubmissionStatistics.size(), payload.physicalQueueRuntimeStatistics.size()
        );
        return false;
    }
    usize compiledTaskCount = 0u;
    for(const auto& node : payload.nodes){
        if(!node.compiledTask.present)
            continue;
        if(node.compiledTask.planGeneration != planGeneration){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressFrameGraphSnapshot: compiled task plan expected={} observed={} packet_index={}"), planGeneration, node.compiledTask.planGeneration, node.compiledTask.packetIndex);
            return false;
        }
        ++compiledTaskCount;
    }
    if(compiledTaskCount != expectedTaskCount){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressFrameGraphSnapshot: compiled task count expected={} observed={} plan={}"), expectedTaskCount, compiledTaskCount, planGeneration);
        return false;
    }
    for(const auto& packet : payload.packetSubmissionStatistics){
        if(packet.packetGeneration != planGeneration || !packet.queue.valid() || packet.taskCount == 0u){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressFrameGraphSnapshot: invalid packet expected_plan={} observed_plan={} packet_index={} queue_index={} queue_generation={} tasks={}")
                , planGeneration, packet.packetGeneration, packet.packetIndex, packet.queue.index, packet.queue.deviceGeneration, packet.taskCount
            );
            return false;
        }
    }
    for(const auto& queue : payload.physicalQueueRuntimeStatistics){
        if(queue.statistics.planGeneration != planGeneration || !queue.statistics.queue.valid()){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressFrameGraphSnapshot: invalid physical queue expected_plan={} observed_plan={} queue_index={} queue_generation={}")
                , planGeneration, queue.statistics.planGeneration, queue.statistics.queue.index, queue.statistics.queue.deviceGeneration
            );
            return false;
        }
    }
    return true;
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CaptureStressFrameGraphSnapshot(const AStringView outputPath, const u64 completedFrames, ProjectRuntimeContext& context){
    const u64 graphicsFrame = context.graphics.getFrameIndex();
    if(outputPath.empty() || completedFrames == 0u || graphicsFrame == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("StressFrameGraphSnapshot: no completed frame or output path"));
        return false;
    }
    // Capture the completed frame before runFrame advances the frame index.
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
    if(!event || event->header.kind != Core::Telemetry::EventKind::FrameGraphFrame || event->header.frameIndex != sourceFrame){
        NWB_LOGGER_ERROR(NWB_TEXT("StressFrameGraphSnapshot: missing runtime data or inconsistent completed-frame identity"));
        return false;
    }
    Core::Alloc::ScratchArena scratchArena(Name("tests/smoke/frame_graph_snapshot_scratch"));
    const auto payload = Core::Telemetry::ParseFrameGraphPayload(arena, scratchArena, event->payload.data(), event->payload.size());
    if(!payload){
        NWB_LOGGER_ERROR(NWB_TEXT("StressFrameGraphSnapshot: frame graph payload parse failed source_frame={} payload_bytes={}"), sourceFrame, event->payload.size());
        return false;
    }
    if(!__hidden_stress_frame_graph_snapshot::ValidateSnapshot(*payload, sourceFrame)){
        NWB_LOGGER_ERROR(NWB_TEXT("StressFrameGraphSnapshot: missing runtime data or inconsistent completed-frame identity"));
        return false;
    }
    Core::Telemetry::TelemetryBytes encoded(arena);
    if(!Core::Telemetry::EncodeEventStream(session.view(), encoded) || encoded.size() > static_cast<usize>(Limit<StreamSize>::s_Max)){
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
    binary.close();
    if(!binary.good()){
        NWB_LOGGER_ERROR(NWB_TEXT("StressFrameGraphSnapshot: encoded stream output failed"));
        return false;
    }
    AString<Core::Telemetry::TelemetryArena> jsonPath(arena);
    jsonPath.assign(outputPath);
    OutputFileStream output(jsonPath.c_str(), s_FileOpenTruncate);
    if(!output.is_open()){
        NWB_LOGGER_ERROR(NWB_TEXT("StressFrameGraphSnapshot: readable snapshot output failed"));
        return false;
    }
    const b8 snapshotWritten = StressFrameGraphSnapshotWriter().write(*payload, graphicsFrame, completedFrames, output);
    output.close();
    if(!snapshotWritten || !output.good()){
        NWB_LOGGER_ERROR(NWB_TEXT("StressFrameGraphSnapshot: readable snapshot output failed"));
        return false;
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressFrameGraphSnapshot: complete source_frame={} completed_frames={} nodes={} edges={} packets={}")
        , sourceFrame
        , completedFrames
        , payload->nodes.size()
        , payload->edges.size()
        , payload->packetSubmissionStatistics.size()
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

