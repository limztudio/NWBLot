// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../../global.h"

#include <core/alloc/general.h>
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

// ByteAddressBuffer::Load reads one aligned 32-bit word even when a meshlet needs only one byte from that word.
// Give every raw byte stream a zero-filled tail through the end of its final word so terminal u8 decodes stay within
// the descriptor range.  The caller continues to own and publish the logical byte count separately.
inline constexpr usize s_RawByteLoadAlignmentBytes = sizeof(u32);

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
        .setQueueSharing(flags.queueSharing)
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

template<typename PayloadVector>
[[nodiscard]] inline BufferSetupFailure::Enum SetupRequiredPaddedRawByteBuffer(
    Core::Graphics& graphics,
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

    // Graphics::setupBuffer records its upload before this temporary payload is destroyed.  Explicitly zero the
    // physically allocated tail rather than relying on backend allocation contents.
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

