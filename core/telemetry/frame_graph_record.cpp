// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph.h"

#include <core/alloc/scratch.h>
#include <global/algorithm.h>
#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RecordFrameGraph(
    Recorder& recorder,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const u32 streamId
){
    return recorder.recordBuiltPayload(
        EventKind::FrameGraphFrame,
        frameIndex,
        streamId,
        [frameIndex, &nodes, &edges](TelemetryArena& arena, TelemetryBytes& payload){
            return BuildFrameGraphPayload(arena, frameIndex, nodes, edges, payload);
        }
    );
}

bool RecordFrameGraph(
    Recorder& recorder,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const u32 streamId
){
    return recorder.recordBuiltPayload(
        EventKind::FrameGraphFrame,
        frameIndex,
        streamId,
        [frameIndex, &nodes, &edges, &physicalQueueRuntimeStatistics](
            TelemetryArena& arena,
            TelemetryBytes& payload
        ){
            return BuildFrameGraphPayload(
                arena,
                frameIndex,
                nodes,
                edges,
                physicalQueueRuntimeStatistics,
                payload
            );
        }
    );
}

bool RecordFrameGraph(
    Recorder& recorder,
    const u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics,
    const u32 streamId
){
    return recorder.recordBuiltPayload(
        EventKind::FrameGraphFrame,
        frameIndex,
        streamId,
        [frameIndex, &nodes, &edges, &physicalQueueRuntimeStatistics, &packetSubmissionStatistics](
            TelemetryArena& arena,
            TelemetryBytes& payload
        ){
            return BuildFrameGraphPayload(
                arena,
                frameIndex,
                nodes,
                edges,
                physicalQueueRuntimeStatistics,
                packetSubmissionStatistics,
                payload
            );
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

