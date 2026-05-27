// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/graphics/graphics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RuntimeGeometryBufferUpload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BufferFlags{
    bool canHaveUAVs = false;
    bool canHaveRawViews = false;
};

[[nodiscard]] inline bool PayloadByteCount(const usize count, const usize stride, usize& outBytes)noexcept{
    outBytes = 0u;
    if(stride == 0u || count > Limit<usize>::s_Max / stride)
        return false;

    outBytes = count * stride;
    return true;
}

template<typename PayloadT>
[[nodiscard]] inline bool PayloadByteCount(const usize count, usize& outBytes)noexcept{
    return PayloadByteCount(count, sizeof(PayloadT), outBytes);
}

template<typename PayloadT>
[[nodiscard]] inline bool PayloadByteCountFits(const usize count)noexcept{
    usize payloadBytes = 0u;
    return PayloadByteCount<PayloadT>(count, payloadBytes);
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] inline bool PayloadByteCountFits(const PayloadVector& payload)noexcept{
    return PayloadByteCountFits<PayloadT>(payload.size());
}

template<typename PayloadT>
[[nodiscard]] inline Core::BufferHandle SetupBuffer(
    Core::Graphics& graphics,
    const Name& debugName,
    const PayloadT* payload,
    const usize count,
    const BufferFlags flags = {}
){
    usize payloadBytes = 0u;
    if(!PayloadByteCount<PayloadT>(count, payloadBytes))
        return {};

    Core::Graphics::BufferSetupDesc setup;
    setup.bufferDesc
        .setByteSize(static_cast<u64>(payloadBytes))
        .setStructStride(sizeof(PayloadT))
        .setCanHaveUAVs(flags.canHaveUAVs)
        .setCanHaveRawViews(flags.canHaveRawViews)
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

