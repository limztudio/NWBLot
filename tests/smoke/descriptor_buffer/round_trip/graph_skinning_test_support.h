// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The active-pose skinning path has three graph stages: deformation writes all skinned streams as UAVs, bounds and
// repack consume the required streams as SRVs while producing their own UAV outputs, and a finalizer publishes every
// generated stream. This probe is getter-only so packet-lowered barriers are the entire state contract.
struct SkinningGraphStateProbeTask{
    struct Expectation{
        GpuGraphResourceId resource;
        ResourceStates::Mask state = ResourceStates::Unknown;
    };

    struct Payload{
        Expectation expectations[5u] = {};
        usize expectationCount = 0u;
        bool* recorded = nullptr;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        if(payload.expectationCount == 0u || payload.expectationCount > LengthOf(payload.expectations))
            return false;

        bool ready = true;
        for(usize expectationIndex = 0u; expectationIndex < payload.expectationCount; ++expectationIndex){
            const Expectation& expectation = payload.expectations[expectationIndex];
            Buffer* const buffer = context.declarations.bufferForResource(expectation.resource);
            if(!buffer || commandList.getBufferState(buffer) != expectation.state){
                ready = false;
                break;
            }
        }
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

