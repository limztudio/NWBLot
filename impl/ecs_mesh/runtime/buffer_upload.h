// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../../global.h"

#include <core/alloc/general.h>
#include <core/graphics/runtime/runtime.h>
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
    Core::ResourceQueueSharing::Mask queueSharing = Core::ResourceQueueSharing::Exclusive;
};

namespace BufferSetupFailure{
    enum Enum : u8{
        None,
        EmptyPayload,
        ByteSizeOverflow,
        CreateFailed,
    };
};

// ByteAddress loads whole words; zero-fill tails so terminal u8 decodes stay in range.
inline constexpr usize s_RawByteLoadAlignmentBytes = sizeof(u32);

template<typename PayloadT>
[[nodiscard]] inline Core::BufferHandle SetupBuffer(
    Core::GraphicsRuntime& graphics,
    const Name& debugName,
    const PayloadT* payload,
    const usize count,
    const BufferFlags flags = {}
){
    usize payloadBytes = 0u;
    if(!TryMultiply<usize>(count, sizeof(PayloadT), payloadBytes))
        return {};

    Core::GraphicsRuntime::BufferSetupDesc setup;
    setup.bufferDesc
        .setByteSize(static_cast<u64>(payloadBytes))
        .setStructStride(sizeof(PayloadT))
        .setCanHaveUAVs(flags.canHaveUAVs)
        .setCanHaveRawViews(flags.canHaveRawViews)
        .setIsAccelStructBuildInput(flags.accelStructBuildInput)
        .setQueueSharing(flags.queueSharing)
        .setDebugName(debugName)
    ;
    setup.data = payload;
    setup.dataSize = payloadBytes;
    return graphics.setupBuffer(setup);
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] inline Core::BufferHandle SetupBuffer(
    Core::GraphicsRuntime& graphics,
    const Name& debugName,
    const PayloadVector& payload,
    const BufferFlags flags = {}
){
    return SetupBuffer<PayloadT>(graphics, debugName, payload.data(), payload.size(), flags);
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] inline BufferSetupFailure::Enum SetupRequiredBuffer(
    Core::GraphicsRuntime& graphics,
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

template<typename PayloadVector>
[[nodiscard]] inline BufferSetupFailure::Enum SetupRequiredPaddedRawByteBuffer(
    Core::GraphicsRuntime& graphics,
    Core::Alloc::GlobalArena& arena,
    const Name& debugName,
    const PayloadVector& payload,
    const BufferFlags flags,
    Core::BufferHandle& outBuffer
){
    outBuffer = nullptr;
    if(payload.empty())
        return BufferSetupFailure::EmptyPayload;

    const usize logicalByteCount = payload.size();
    const usize trailingByteCount =
        (s_RawByteLoadAlignmentBytes - (logicalByteCount % s_RawByteLoadAlignmentBytes))
        % s_RawByteLoadAlignmentBytes
    ;
    if(AddOverflows<usize>(logicalByteCount, trailingByteCount))
        return BufferSetupFailure::ByteSizeOverflow;

    const usize paddedByteCount = logicalByteCount + trailingByteCount;
    if(trailingByteCount == 0u){
        outBuffer = SetupBuffer<u8>(graphics, debugName, payload, flags);
        return outBuffer ? BufferSetupFailure::None : BufferSetupFailure::CreateFailed;
    }

    // Upload records before payload dies; explicitly zero the allocated tail.
    Vector<u8, Core::Alloc::GlobalArena> paddedPayload{arena};
    paddedPayload.assign(payload.begin(), payload.end());
    paddedPayload.resize(paddedByteCount, 0u);
    outBuffer = SetupBuffer<u8>(graphics, debugName, paddedPayload, flags);
    return outBuffer ? BufferSetupFailure::None : BufferSetupFailure::CreateFailed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

