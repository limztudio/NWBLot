// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NativePacketPrefixTask{
    struct Payload{
        Buffer* buffer = nullptr;
        ResourceStates::Mask expectedState = ResourceStates::Unknown;
        Buffer* additionalBuffer = nullptr;
        ResourceStates::Mask expectedAdditionalBufferState = ResourceStates::Unknown;
        Texture* texture = nullptr;
        ResourceStates::Mask expectedTextureState = ResourceStates::Unknown;
        Texture* additionalTexture = nullptr;
        ResourceStates::Mask expectedAdditionalTextureState = ResourceStates::Unknown;
        Texture* thirdTexture = nullptr;
        ResourceStates::Mask expectedThirdTextureState = ResourceStates::Unknown;
        bool* recorded = nullptr;
        QueueSubmissionToken* acceptedToken = nullptr;
        u32* discardedCount = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.buffer)
            return false;
        bool ready = commandList.getBufferState(payload.buffer) == payload.expectedState;
        if(payload.additionalBuffer)
            ready = ready && commandList.getBufferState(payload.additionalBuffer) == payload.expectedAdditionalBufferState;
        if(payload.texture)
            ready = ready && commandList.getTextureSubresourceState(payload.texture, 0u, 0u) == payload.expectedTextureState;
        if(payload.additionalTexture)
            ready = ready && commandList.getTextureSubresourceState(payload.additionalTexture, 0u, 0u) == payload.expectedAdditionalTextureState;
        if(payload.thirdTexture)
            ready = ready && commandList.getTextureSubresourceState(payload.thirdTexture, 0u, 0u) == payload.expectedThirdTextureState;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }

    static void discarded(Payload& payload){
        if(payload.discardedCount)
            ++*payload.discardedCount;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

