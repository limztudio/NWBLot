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


// This deliberately does no native state setup.  It lets an async AVBOIT chain prove every packet prologue and the
// two-callback Extinction timing span together: producer opens the persistent measure and closes its marker, raster
// finishes the timestamp under the same ticket, while the surrounding dedicated-Compute packets stay independent.
struct NativePacketAsyncAvboitExtinctionLifecycleTask{
    struct BufferExpectation{
        Buffer* buffer = nullptr;
        ResourceStates::Mask state = ResourceStates::Unknown;
    };
    struct TextureExpectation{
        Texture* texture = nullptr;
        ResourceStates::Mask state = ResourceStates::Unknown;
    };

    struct Payload{
        BufferExpectation expectations[7u] = {};
        usize expectationCount = 0u;
        TextureExpectation textureExpectations[8u] = {};
        usize textureExpectationCount = 0u;
        u32* recordOrdinal = nullptr;
        u32 expectedOrdinal = 0u;
        Device* device = nullptr;
        GpuTimingRecorder* timing = nullptr;
        GpuTimingSubmissionTicket* timingTicket = nullptr;
        Optional<GpuTimingMeasure>* sharedTiming = nullptr;
        const GpuTimingScopeDefinition* timingScope = &s_AsyncAvboitExtinctionLifecycleScope;
        bool startTiming = false;
        bool finishTiming = false;
        bool recordTiming = false;
        bool* timingStarted = nullptr;
        bool* timingFinished = nullptr;
        bool* recorded = nullptr;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            payload.expectationCount == 0u
            || payload.expectationCount > LengthOf(payload.expectations)
            || payload.textureExpectationCount > LengthOf(payload.textureExpectations)
            || !payload.recordOrdinal
        )
            return false;

        const auto recordWithActiveTicket = [&](){
            bool ready = *payload.recordOrdinal == payload.expectedOrdinal;
            for(usize expectationIndex = 0u; ready && expectationIndex < payload.expectationCount; ++expectationIndex){
                const BufferExpectation& expectation = payload.expectations[expectationIndex];
                ready = expectation.buffer && commandList.getBufferState(expectation.buffer) == expectation.state;
            }
            for(usize expectationIndex = 0u; ready && expectationIndex < payload.textureExpectationCount; ++expectationIndex){
                const TextureExpectation& expectation = payload.textureExpectations[expectationIndex];
                ready = expectation.texture
                    && commandList.getTextureSubresourceState(expectation.texture, 0u, 0u) == expectation.state
                ;
            }
            if(ready && payload.recordTiming){
                if(!payload.device || !payload.timing || !payload.timingScope)
                    ready = false;
                else{
                    GpuTimingMeasure timingMeasure(
                        *payload.timing,
                        *payload.timingScope,
                        *payload.device,
                        commandList
                    );
                }
            }
            if(ready && (payload.startTiming || payload.finishTiming)){
                if(
                    !payload.timingTicket
                    || !payload.device
                    || !payload.timing
                    || !payload.sharedTiming
                    || !payload.timingScope
                    || payload.startTiming == payload.finishTiming
                )
                    ready = false;
                else if(payload.startTiming){
                    if(payload.sharedTiming->has_value())
                        ready = false;
                    else{
                        payload.sharedTiming->emplace(
                            *payload.timing,
                            *payload.timingScope,
                            *payload.device,
                            commandList
                        );
                        ready = payload.sharedTiming->value().finishMarker();
                        if(payload.timingStarted)
                            *payload.timingStarted = ready;
                    }
                }
                else if(!payload.sharedTiming->has_value())
                    ready = false;
                else{
                    payload.sharedTiming->value().finishTiming(commandList);
                    payload.sharedTiming->reset();
                    if(payload.timingFinished)
                        *payload.timingFinished = true;
                }
            }
            if(ready)
                ++*payload.recordOrdinal;
            if(payload.recorded)
                *payload.recorded = ready;
            return ready;
        };

        if(payload.timingTicket){
            GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
            return recordWithActiveTicket();
        }
        return recordWithActiveTicket();
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }

    static void discarded(Payload& payload){
        if(!payload.sharedTiming || !payload.sharedTiming->has_value())
            return;
        payload.sharedTiming->value().discardTiming();
        payload.sharedTiming->reset();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

