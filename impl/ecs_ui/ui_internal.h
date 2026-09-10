// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/rhi/resource.h>
#include <core/task/gpu/task_desc.h>


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
    // Tiny deltas stay on Graphics; large refreshes may use Transfer/Compute.
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

