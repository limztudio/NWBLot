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
    bool isIndexBuffer = false;
};

namespace BufferSetupFailure{
    enum Enum : u8{
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
    const auto payloadBytes = TryMultiply<usize>(count, sizeof(PayloadT));
    if(!payloadBytes)
        return {};

    Core::GraphicsRuntime::BufferSetupDesc setup;
    setup.bufferDesc
        .setByteSize(static_cast<u64>(*payloadBytes))
        .setStructStride(sizeof(PayloadT))
        .setCanHaveUAVs(flags.canHaveUAVs)
        .setCanHaveRawViews(flags.canHaveRawViews)
        .setIsAccelStructBuildInput(flags.accelStructBuildInput)
        .setIsIndexBuffer(flags.isIndexBuffer)
        .setQueueSharing(flags.queueSharing)
        .setDebugName(debugName)
    ;
    setup.data = payload;
    setup.dataSize = *payloadBytes;
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
[[nodiscard]] inline Expected<Core::BufferHandle, BufferSetupFailure::Enum> SetupRequiredBuffer(
    Core::GraphicsRuntime& graphics,
    const Name& debugName,
    const PayloadVector& payload,
    const BufferFlags flags
){
    if(payload.empty())
        return MakeUnexpected(BufferSetupFailure::EmptyPayload);
    if(MultiplyOverflows<usize>(payload.size(), sizeof(PayloadT)))
        return MakeUnexpected(BufferSetupFailure::ByteSizeOverflow);

    auto buffer = SetupBuffer<PayloadT>(graphics, debugName, payload, flags);
    if(!buffer)
        return MakeUnexpected(BufferSetupFailure::CreateFailed);
    return Move(buffer);
}

template<typename PayloadVector>
[[nodiscard]] inline Expected<Core::BufferHandle, BufferSetupFailure::Enum> SetupRequiredPaddedRawByteBuffer(
    Core::GraphicsRuntime& graphics,
    Core::Alloc::GlobalArena& arena,
    const Name& debugName,
    const PayloadVector& payload,
    const BufferFlags flags
){
    if(payload.empty())
        return MakeUnexpected(BufferSetupFailure::EmptyPayload);

    const usize logicalByteCount = payload.size();
    const usize trailingByteCount =
        (s_RawByteLoadAlignmentBytes - (logicalByteCount % s_RawByteLoadAlignmentBytes))
        % s_RawByteLoadAlignmentBytes
    ;
    if(AddOverflows<usize>(logicalByteCount, trailingByteCount))
        return MakeUnexpected(BufferSetupFailure::ByteSizeOverflow);

    const usize paddedByteCount = logicalByteCount + trailingByteCount;
    if(trailingByteCount == 0u){
        auto buffer = SetupBuffer<u8>(graphics, debugName, payload, flags);
        if(!buffer)
            return MakeUnexpected(BufferSetupFailure::CreateFailed);
        return Move(buffer);
    }

    // Upload records before payload dies; explicitly zero the allocated tail.
    Vector<u8, Core::Alloc::GlobalArena> paddedPayload{arena};
    paddedPayload.assign(payload.begin(), payload.end());
    paddedPayload.resize(paddedByteCount, 0u);
    auto buffer = SetupBuffer<u8>(graphics, debugName, paddedPayload, flags);
    if(!buffer)
        return MakeUnexpected(BufferSetupFailure::CreateFailed);
    return Move(buffer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

