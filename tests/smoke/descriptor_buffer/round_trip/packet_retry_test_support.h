// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A mutable success gate lets the packet recorder prove that an optional capture rolls back an incomplete packet
// before a retry. It intentionally records no native work beyond the task marker.
struct NativePacketCaptureRetryTask{
    struct Payload{
        const bool* shouldRecord = nullptr;
        bool* attempted = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        if(payload.attempted)
            *payload.attempted = true;
        return payload.shouldRecord && *payload.shouldRecord;
    }
};


// Records one legacy/manual scope inside the graph-owned packet scope so submission must resolve two independent
// tickets with the same native token.
struct NativePacketCompanionTimingCaptureRetryTask{
    struct Payload{
        const bool* shouldRecord = nullptr;
        bool* attempted = nullptr;
        Device* device = nullptr;
        GpuTimingRecorder* timing = nullptr;
        GpuTimingSubmissionTicket* timingTicket = nullptr;
        const GpuTimingScopeDefinition* timingScope = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.attempted)
            *payload.attempted = true;
        if(
            !payload.shouldRecord
            || !*payload.shouldRecord
            || !payload.device
            || !payload.timing
            || !payload.timingTicket
            || !payload.timingScope
        )
            return false;

        GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        GpuTimingMeasure timingMeasure(*payload.timing, *payload.timingScope, *payload.device, commandList);
        return timingMeasure.valid();
    }
};


struct NativeTaskAcceptanceOrder{
    u32 invocationCount = 0u;
    u32 markers[8] = {};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

