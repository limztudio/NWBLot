// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "telemetry_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BuildTestFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges
);

Telemetry::FrameGraphQueueAssignment MakeChangedFrameGraphQueueAssignment();

Telemetry::FrameGraphQueueAssignment MakeNotAcceptedFrameGraphQueueAssignment();

Telemetry::FrameGraphCompiledTask MakeFrameGraphCompiledTask(
    const u64 planGeneration,
    const u32 packetIndex,
    const Telemetry::FrameGraphTaskPacketizationDecision::Enum packetizationDecision
);

Telemetry::FrameGraphRuntimeStatistics MakeFrameGraphRuntimeStatistics();

Telemetry::FrameGraphPhysicalQueueRuntimeStatistics MakeFrameGraphPhysicalQueueRuntimeStatistics(
    const u16 queueIndex
);

void BuildTestPhysicalQueueRuntimeStatistics(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords& records
);

void BuildTestAssignedFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges
);

void BuildTestCompiledFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges
);

void BuildTestRuntimeFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges
);

void BuildTestPacketSubmissionFrameGraph(
    Telemetry::TelemetryArena& arena,
    Telemetry::FrameGraphNodeDescs& nodes,
    Telemetry::FrameGraphEdgeDescs& edges,
    Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    Telemetry::FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

