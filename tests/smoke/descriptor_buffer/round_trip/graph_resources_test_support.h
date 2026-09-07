// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline TextureHandle CreateConcurrentTestTexture(
    GraphicsBackend::Device& device,
    const bool keepInitialState = false
){
    TextureDesc desc;
    desc
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UNORM)
        .setInUAV(true)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;
    if(keepInitialState)
        desc.setKeepInitialState(true);
    return device.createTexture(desc);
}


// Graph topology IDs must be the identities emitted by the concrete Device, not local array positions. This keeps
// the native packet smoke paths honest across device recreation and distinct Compute/Transfer transports.
[[nodiscard]] inline GpuPhysicalQueueId BackendQueueId(
    GraphicsBackend::Device& device,
    const CommandQueue::Enum queue
){
    return device.getPrimaryPhysicalQueue(queue);
}


[[nodiscard]] inline bool HasDedicatedComputeQueue(GraphicsBackend::Device& device){
    const GpuPhysicalQueueId computeQueue = BackendQueueId(device, CommandQueue::Compute);
    const GpuPhysicalQueueInfo* const queueInfo = device.getPhysicalQueueInfo(computeQueue);
    return queueInfo && queueInfo->queueClass == CommandQueue::Compute && queueInfo->dedicated;
}


// A graph declaration can name a known imported state only after native work has materialized that state. Fresh
// Vulkan images begin UNDEFINED, so tests that intentionally model an external first reader use this small producer
// instead of asserting a descriptor's logical initialState as physical truth.
[[nodiscard]] inline bool PrimeTextureStatesForGraph(
    GraphicsBackend::Device& device,
    Texture* const* const textures,
    const usize textureCount,
    const ResourceStates::Mask state
){
    if(!textures || textureCount == 0u || state == ResourceStates::Unknown)
        return false;
    for(usize textureIndex = 0u; textureIndex < textureCount; ++textureIndex){
        if(!textures[textureIndex])
            return false;
    }

    const auto commandList = device.createCommandList();
    if(!commandList)
        return false;
    commandList->open();
    for(usize textureIndex = 0u; textureIndex < textureCount; ++textureIndex)
        commandList->setTextureState(textures[textureIndex], s_AllSubresources, state);
    commandList->close();

    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    return token.valid() && device.waitForIdle();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

