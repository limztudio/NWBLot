// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The probe observes a graph-owned entry state, then can deliberately perform a task-local transition. That lets
// the smoke test prove the terminal export reasserts its contract after native task-local state work.
struct NativePacketExternalFinalTextureProbeTask{
    struct Payload{
        Texture* texture = nullptr;
        ResourceStates::Mask* observedState = nullptr;
        ArraySlice observedArraySlice = 0u;
        MipLevel observedMipLevel = 0u;
        ResourceStates::Mask localFinalState = ResourceStates::Unknown;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.texture)
            return false;
        if(payload.observedState)
            *payload.observedState = commandList.getTextureSubresourceState(
                payload.texture,
                payload.observedArraySlice,
                payload.observedMipLevel
            );
        if(payload.localFinalState != ResourceStates::Unknown)
            commandList.setTextureState(payload.texture, s_AllSubresources, payload.localFinalState);
        if(payload.recorded)
            *payload.recorded = true;
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

