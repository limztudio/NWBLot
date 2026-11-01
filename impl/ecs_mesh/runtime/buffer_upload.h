// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

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

    return SetupBuffer<PayloadT>(graphics, debugName, payload, flags);
}

[[nodiscard]] Expected<Core::BufferHandle, BufferSetupFailure::Enum> SetupRequiredPaddedRawByteBuffer(
    Core::GraphicsRuntime& graphics,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& debugName,
    Span<const u8> payload,
    BufferFlags flags
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

