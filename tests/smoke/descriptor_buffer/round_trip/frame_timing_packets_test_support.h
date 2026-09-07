// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "round_trip_fixture.h"
#include "timing_scopes_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Minimal graph-native timing endpoints used to exercise a late recovery packet in the same submission transaction.
struct NativeFrameTimingPacketTask{
    enum class Endpoint : u8{
        Begin,
        End,
    };

    struct Payload{
        Device* device = nullptr;
        GpuTimingFrameTransaction* transaction = nullptr;
        GpuTimingSubmissionTicket* timingTicket = nullptr;
        Endpoint endpoint = Endpoint::Begin;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.device || !payload.transaction || !payload.timingTicket)
            return false;
        GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        const bool ready = payload.endpoint == Endpoint::Begin
            ? payload.transaction->begin(s_FrameTransactionScope, *payload.device, commandList)
            : payload.transaction->recordEnd(commandList)
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


struct NativeFrameRecoveryPacketTask{
    struct Payload{
        GpuTimingFrameTransaction* transaction = nullptr;
        bool* armed = nullptr;
        bool* retiresTiming = nullptr;
        bool* recorded = nullptr;
        bool* accepted = nullptr;
        bool* discarded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready = payload.transaction
            && payload.armed
            && payload.retiresTiming
            && *payload.armed
            && (!*payload.retiresTiming || payload.transaction->recordEnd(commandList))
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        static_cast<void>(token);
        if(
            payload.transaction
            && payload.armed
            && payload.retiresTiming
            && *payload.armed
            && *payload.retiresTiming
        ){
            if(!payload.transaction->confirmEndSubmission(token, false))
                payload.transaction->discard();
        }
        if(payload.armed)
            *payload.armed = false;
        if(payload.retiresTiming)
            *payload.retiresTiming = false;
        if(payload.accepted)
            *payload.accepted = true;
    }

    static void discarded(Payload& payload){
        if(payload.transaction && payload.armed && *payload.armed)
            payload.transaction->discard();
        if(payload.armed)
            *payload.armed = false;
        if(payload.retiresTiming)
            *payload.retiresTiming = false;
        if(payload.discarded)
            *payload.discarded = true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

