// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../../global.h"

#include <impl/ecs_mesh/runtime/types.adhd.h>

#include <core/alloc/scratch.h>
#include <core/graphics/runtime/runtime.h>

#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RuntimeMeshBufferUpload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BufferFlags{
    b8 canHaveUAVs = false;
    b8 canHaveRawViews = false;
    b8 accelStructBuildInput = false;
    Core::ResourceQueueSharing::Mask queueSharing = Core::ResourceQueueSharing::Exclusive;
    b8 isIndexBuffer = false;
};




template<typename PayloadT>
[[nodiscard]] inline Expected<Core::BufferHandle, BufferSetupFailure::Enum> SetupBuffer(Core::GraphicsRuntime& graphics, const Name& debugName, const Span<const PayloadT> payload, const BufferFlags flags = {}){
    const auto payloadBytes = TryMultiply<usize>(payload.size(), sizeof(PayloadT));
    if(!payloadBytes)
        return MakeUnexpected(BufferSetupFailure::ByteSizeOverflow);

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
    setup.data = payload.data();
    setup.dataSize = *payloadBytes;
    auto buffer = graphics.setupBuffer(setup);
    if(!buffer)
        return MakeUnexpected(BufferSetupFailure::CreateFailed);
    return Move(*buffer);
}

template<typename PayloadT>
[[nodiscard]] inline Expected<Core::BufferHandle, BufferSetupFailure::Enum> SetupRequiredBuffer(Core::GraphicsRuntime& graphics, const Name& debugName, const Span<const PayloadT> payload, const BufferFlags flags){
    if(payload.empty())
        return MakeUnexpected(BufferSetupFailure::EmptyPayload);
    if(MultiplyOverflows<usize>(payload.size(), sizeof(PayloadT)))
        return MakeUnexpected(BufferSetupFailure::ByteSizeOverflow);

    return SetupBuffer<PayloadT>(graphics, debugName, payload, flags);
}

[[nodiscard]] inline Expected<Core::BufferHandle, BufferSetupFailure::Enum> SetupRequiredPaddedRawByteBuffer(
    Core::GraphicsRuntime& graphics,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& debugName,
    const Span<const u8> payload,
    const BufferFlags flags
){
    if(payload.empty())
        return MakeUnexpected(BufferSetupFailure::EmptyPayload);

    const usize logicalByteCount = payload.size();
    const usize trailingByteCount = (s_RawByteLoadAlignmentBytes - (logicalByteCount % s_RawByteLoadAlignmentBytes)) % s_RawByteLoadAlignmentBytes;
    if(AddOverflows<usize>(logicalByteCount, trailingByteCount))
        return MakeUnexpected(BufferSetupFailure::ByteSizeOverflow);

    const usize paddedByteCount = logicalByteCount + trailingByteCount;
    if(trailingByteCount == 0u)
        return SetupBuffer<u8>(graphics, debugName, payload, flags);

    // Upload records before payload dies; explicitly zero the allocated tail.
    Vector<u8, Core::Alloc::ScratchArena> paddedPayload{scratchArena};
    paddedPayload.reserve(paddedByteCount);
    paddedPayload.assign(payload.begin(), payload.end());
    paddedPayload.resize(paddedByteCount, 0u);
    return SetupBuffer<u8>(graphics, debugName, paddedPayload, flags);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

