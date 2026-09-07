// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "telemetry_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TelemetryTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Telemetry::EncodedFrameGraphRuntimeStatistics EncodeTestFrameGraphRuntimeStatistics(
    const Telemetry::FrameGraphRuntimeStatistics& statistics,
    const u32 nodeIndex,
    const u16 reserved
);

Telemetry::EncodedFrameGraphRuntimeStatisticsV6 EncodeTestFrameGraphRuntimeStatisticsV6(
    const Telemetry::FrameGraphRuntimeStatistics& statistics,
    const u32 nodeIndex,
    const u16 reserved
);

Telemetry::EncodedFrameGraphRuntimeStatisticsV8 EncodeTestFrameGraphRuntimeStatisticsV8(
    const Telemetry::FrameGraphRuntimeStatistics& statistics,
    const u32 nodeIndex,
    const u16 reserved
);

Telemetry::EncodedFrameGraphRuntimeStatisticsV6 DowngradeFrameGraphRuntimeStatisticsV8(
    const Telemetry::EncodedFrameGraphRuntimeStatisticsV8& statistics
);

Telemetry::EncodedFrameGraphRuntimeStatistics DowngradeFrameGraphRuntimeStatisticsV6(
    const Telemetry::EncodedFrameGraphRuntimeStatisticsV6& statistics
);

Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatistics
DowngradeFrameGraphPhysicalQueueRuntimeStatisticsV6(
    const Telemetry::EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6& statistics
);

bool ConvertFrameGraphPayloadV8ToLegacy(
    const Telemetry::TelemetryBytes& source,
    const u16 legacyVersion,
    Telemetry::TelemetryBytes& outPayload
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

