// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/rhi/resource.h>
#include <core/graphics/task_graph/task_desc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_TransferPreferredUploadMinimumBytes = 1024u * 1024u;

[[nodiscard]] inline Core::GpuQueueRequest UploadQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Transfer,
        Core::GpuQueuePreference::Transfer,
        true,
        true,
    };
}

[[nodiscard]] inline Core::GpuTaskSchedulingHint UploadScheduling(const usize byteCount){
    const bool preferDedicatedTransport = byteCount >= s_TransferPreferredUploadMinimumBytes;
    Core::GpuTaskSchedulingHint scheduling;
    // Tiny UI deltas stay on Graphics. A large font or texture refresh may use Transfer first and a dedicated
    // Compute queue second. Imported UI resources are created for all graph upload classes, so large immutable
    // updates may offload across explicitly opted-in same-class physical queues while the overlay remains on
    // primary Graphics. Built-in graph uploads capture immutable blobs onto a fresh native command list, so
    // independent packets may record together once their shared scene-output dependency is ready.
    scheduling.cost = preferDedicatedTransport ? Core::GpuTaskCostHint::Medium : Core::GpuTaskCostHint::Tiny;
    scheduling.overlapPreferred = preferDedicatedTransport;
    scheduling.avoidQueueCrossing = !preferDedicatedTransport;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.allowSameClassQueueRouting = preferDedicatedTransport;
    scheduling.preferNonPrimarySameClassQueue = preferDedicatedTransport;
    scheduling.allowCrossFamilySameClassQueueRouting = preferDedicatedTransport;
    scheduling.allowParallelRecording = true;
    return scheduling;
}

[[nodiscard]] inline Core::GpuGraphResourceDesc TextureResourceDesc(
    const Core::TextureDesc& textureDesc,
    const bool initialUploadAccepted
){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(textureDesc.name)
        .setMarkerLabel("ImGui Texture")
        .setType(Core::GpuGraphResourceType::Texture)
        .setInitialState(initialUploadAccepted ? textureDesc.initialState : Core::ResourceStates::Unknown)
        .setQueueSharing(textureDesc.queueSharing)
    ;
    return desc;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

