// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr GpuTimingScopeDefinition s_FrameTransactionScope("tests/timing_frame_transaction");


inline constexpr GpuTimingScopeDefinition s_TimerQueryUnsupportedQueueScope("tests/timing_query_unsupported_queue");


inline constexpr GpuTimingScopeDefinition s_AsyncAvboitExtinctionLifecycleScope(
    "tests/timing_async_avboit_extinction_lifecycle"
);


inline constexpr GpuTimingScopeDefinition s_UnsplitAvboitAccumulationLifecycleScope(
    "tests/timing_unsplit_avboit_accumulation_lifecycle"
);


inline constexpr GpuTimingScopeDefinition s_UnsplitAvboitOccupancyLifecycleScope(
    "tests/timing_unsplit_avboit_occupancy_lifecycle"
);


inline constexpr GpuTimingScopeDefinition s_UnsplitAvboitExtinctionLifecycleScope(
    "tests/timing_unsplit_avboit_extinction_lifecycle"
);


inline constexpr GpuTimingScopeDefinition s_UnsplitAvboitIntegrationLifecycleScope(
    "tests/timing_unsplit_avboit_integration_lifecycle"
);


inline constexpr u32 s_DiscardObserverException = 0xE104u;


inline constexpr GpuTimingScopeDefinition s_FrameTimingPreambleScope("tests/frame_timing_preamble");


inline constexpr GpuTimingScopeDefinition s_GraphCompanionSubmissionTicketScope("tests/timing_graph_companion_submission_ticket");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

