// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "telemetry_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BuildTestFrameGraph(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphNodeDescs& nodes, Telemetry::FrameGraphEdgeDescs& edges);

Telemetry::FrameGraphQueueAssignment MakeChangedFrameGraphQueueAssignment()noexcept;

Telemetry::FrameGraphQueueAssignment MakeNotAcceptedFrameGraphQueueAssignment()noexcept;

Telemetry::FrameGraphCompiledTask MakeFrameGraphCompiledTask(const u64 planGeneration, const u32 packetIndex, const Telemetry::FrameGraphTaskPacketizationDecision::Enum packetizationDecision)noexcept;

Telemetry::FrameGraphRuntimeStatistics MakeFrameGraphRuntimeStatistics()noexcept;

Telemetry::FrameGraphPhysicalQueueRuntimeStatistics MakeFrameGraphPhysicalQueueRuntimeStatistics(const u16 queueIndex)noexcept;

void BuildTestPhysicalQueueRuntimeStatistics(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphPhysicalQueueRuntimeStatisticsRecords& records);

void BuildTestAssignedFrameGraph(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphNodeDescs& nodes, Telemetry::FrameGraphEdgeDescs& edges);

void BuildTestCompiledFrameGraph(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphNodeDescs& nodes, Telemetry::FrameGraphEdgeDescs& edges);

void BuildTestRuntimeFrameGraph(Telemetry::TelemetryArena& arena, Telemetry::FrameGraphNodeDescs& nodes, Telemetry::FrameGraphEdgeDescs& edges);

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

