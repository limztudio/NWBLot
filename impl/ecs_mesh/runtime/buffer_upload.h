// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../../global.h"

#include <core/graphics/module.h>
#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RuntimeMeshBufferUpload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BufferFlags{
    bool canHaveUAVs = false;
    bool canHaveRawViews = false;
    bool accelStructBuildInput = false;
};

namespace BufferSetupFailure{
    enum Enum : u8{
        None,
        EmptyPayload,
        ByteSizeOverflow,
        CreateFailed,
    };
};

template<typename PayloadT>
[[nodiscard]] inline Core::BufferHandle SetupBuffer(
    Core::Graphics& graphics,
    const Name& debugName,
    const PayloadT* payload,
    const usize count,
    const BufferFlags flags = {}
){
    usize payloadBytes = 0u;
    if(!TryMultiply<usize>(count, sizeof(PayloadT), payloadBytes))
        return {};

    Core::Graphics::BufferSetupDesc setup;
    setup.bufferDesc
        .setByteSize(static_cast<u64>(payloadBytes))
        .setStructStride(sizeof(PayloadT))
        .setCanHaveUAVs(flags.canHaveUAVs)
        .setCanHaveRawViews(flags.canHaveRawViews)
        .setIsAccelStructBuildInput(flags.accelStructBuildInput)
        .setDebugName(debugName)
    ;
    setup.data = payload;
    setup.dataSize = payloadBytes;
    return graphics.setupBuffer(setup);
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] inline Core::BufferHandle SetupBuffer(
    Core::Graphics& graphics,
    const Name& debugName,
    const PayloadVector& payload,
    const BufferFlags flags = {}
){
    return SetupBuffer<PayloadT>(graphics, debugName, payload.data(), payload.size(), flags);
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] inline BufferSetupFailure::Enum SetupRequiredBuffer(
    Core::Graphics& graphics,
    const Name& debugName,
    const PayloadVector& payload,
    const BufferFlags flags,
    Core::BufferHandle& outBuffer
){
    outBuffer = nullptr;
    if(payload.empty())
        return BufferSetupFailure::EmptyPayload;
    if(MultiplyOverflows<usize>(payload.size(), sizeof(PayloadT)))
        return BufferSetupFailure::ByteSizeOverflow;

    outBuffer = SetupBuffer<PayloadT>(graphics, debugName, payload, flags);
    return outBuffer ? BufferSetupFailure::None : BufferSetupFailure::CreateFailed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

