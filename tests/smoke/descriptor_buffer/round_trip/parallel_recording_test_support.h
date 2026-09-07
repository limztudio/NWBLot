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


// The ready-frontier recorder may execute one chunk on its caller and another on a worker. Hold both task thunks
// at a latch after they observe their command-list description, proving each parallel packet received a distinct
// nonzero worker-affined native lease rather than borrowing the serial/default command arena.
struct WorkerAffinedPacketTask{
    struct Payload{
        Latch* recordingStarted = nullptr;
        u32* observedWorkerIndex = nullptr;
        Buffer* expectedBuffer = nullptr;
        ResourceStates::Mask expectedBufferState = ResourceStates::Unknown;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext&
    ){
        if(
            !payload.recordingStarted
            || !payload.observedWorkerIndex
            || !payload.expectedBuffer
            || commandList.getBufferState(payload.expectedBuffer) != payload.expectedBufferState
        )
            return false;
        *payload.observedWorkerIndex = commandList.getDescription().recordingWorkerIndex;
        payload.recordingStarted->count_down();
        payload.recordingStarted->wait();
        return commandList.isRecording();
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }
};


struct DiscardObserverExceptionState{
    u32 discardCount = 0u;
    u32 destructionCount = 0u;
};


struct DiscardObserverExceptionTask{
    struct Payload{
        DiscardObserverExceptionState* state = nullptr;


        explicit Payload(DiscardObserverExceptionState& value)noexcept
            : state(&value)
        {}
        Payload(Payload&& other)noexcept
            : state(other.state)
        {
            other.state = nullptr;
        }
        Payload(const Payload&) = delete;
        ~Payload(){
            if(state)
                ++state->destructionCount;
        }
    };

    [[nodiscard]] static bool record(
        const Payload&,
        CommandList& commandList,
        const GpuTaskRecordContext&
    ){
        return commandList.isRecording();
    }

    static void discarded(Payload& payload){
        if(!payload.state)
            return;
        ++payload.state->discardCount;
        throw s_DiscardObserverException;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

