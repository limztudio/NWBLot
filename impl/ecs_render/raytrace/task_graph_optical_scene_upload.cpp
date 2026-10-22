// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_optical_scene_upload.h"
#include "task_graph_optical_bounds_finalize.h"

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_optical_scene_upload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OpticalUploadTask{
    // Accepted optical residency belongs to the primary Graphics writer timeline.
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Transfer, true };

    struct Payload{
        Core::BufferHandle destination;
        Core::GpuUploadBlobId source;
        RayTracingOpticalSceneUploadHandle upload;
        RayTracingOpticalUploadReservation reservation;
        Core::GpuPhysicalQueueId queue;
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        const auto bytes = context.declarations.uploadBlobData(payload.source);
        if(!payload.destination || !payload.upload || !bytes || bytes->size() == 0u || bytes->size() != payload.upload->bytes.size() || !payload.reservation.valid() || context.commandIrCapture || context.queue != payload.queue)
            return false;

        commandList.endRenderPass();
        const Core::BufferRange range(0u, bytes->size());
        commandList.setBufferState(payload.destination.get(), Core::ResourceStates::CopyDest, false, range);
        commandList.commitBarriers();
        if(!commandList.tryWriteBuffer(*payload.destination, bytes->data(), bytes->size(), 0u))
            return false;
        commandList.setBufferState(payload.destination.get(), Core::ResourceStates::Common, false, range);
        commandList.commitBarriers();
        return true;
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(!payload.reservation.accept(token))
            NWB_LOGGER_ERROR(NWB_TEXT("Ray optical scene: accepted upload has invalid residency provenance"));
    }

    static void Discarded(Payload& payload)noexcept{
        payload.reservation.discard();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<RayTracingOpticalSceneGraphBuffer> ImportOpticalSceneUpload(Core::GpuTaskGraph& graph, const RayTracingOpticalSceneSnapshot& resources, const Core::GpuTaskId priorReadersDrained){
    if(!resources.valid())
        return MakeUnexpected(Failure{});
    const Core::BufferHandle& uploadBuffer = resources.uploadBuffer ? resources.uploadBuffer : resources.buffer;
    const Core::BufferDesc& bufferDesc = uploadBuffer->getCreationDescription();
    const usize byteSize = resources.upload->bytes.size();
    if(
        !bufferDesc.debugName || byteSize == 0u || byteSize > bufferDesc.byteSize || (byteSize % sizeof(u32)) != 0u
        || bufferDesc.isVolatile || !bufferDesc.keepInitialState || bufferDesc.initialState != Core::ResourceStates::Common
        || uploadBuffer->resolveTaskGraphImportInitialState() != Core::ResourceStates::Common
    )
        return MakeUnexpected(Failure{});

    const RayTracingOpticalUploadPlan plan = resources.uploadState->plan(resources.upload);
    if(!plan.valid() || plan.buffer.get() != uploadBuffer.get())
        return MakeUnexpected(Failure{});
    RayTracingOpticalUploadReservation reservation(resources.uploadState, plan);
    if(!plan.reused && !reservation.valid())
        return MakeUnexpected(Failure{});

    Core::GpuGraphResourceDesc resourceDesc = RendererTaskGraphDetail::BufferResourceDesc(bufferDesc.debugName, "Ray Optical Scene Buffer");
    resourceDesc.setInitialState(Core::ResourceStates::Common).setExternalFinalState(Core::ResourceStates::Common);
    if(plan.acceptedToken.valid()){
        const auto readyResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(Name("render.raytrace.optical_scene_ready")).setMarkerLabel("Ray Optical Scene Ready").setToken(plan.acceptedToken));
        if(!readyResult)
            return MakeUnexpected(Failure{});
        const Core::GpuExternalCompletionId ready = *readyResult;
        resourceDesc.setInitialAvailabilityCompletion(ready);
    }
    RayTracingOpticalSceneGraphBuffer result;
    const auto resultResourceResult = graph.importBuffer(uploadBuffer, resourceDesc);
    if(!resultResourceResult){
        result.resource = {};
        return MakeUnexpected(Failure{});
    }
    result.resource = *resultResourceResult;
    result.reused = plan.reused;
    if(result.reused)
        return result;

    const auto blobResult = graph.copyUploadData(resources.upload->bytes.data(), byteSize, alignof(u32));
    if(!blobResult)
        return MakeUnexpected(Failure{});
    const Core::GpuUploadBlobId blob = *blobResult;
    const Core::GpuTaskResourceUse uses[] = {
        Core::GpuTaskResourceUse{ .resource = result.resource, .range = Core::GpuTaskResourceRange{ .bufferRange = Core::BufferRange(0u, byteSize) }, .requiredState = Core::ResourceStates::CopyDest, .access = Core::GpuTaskResourceAccess::Write, },
        Core::GpuTaskResourceUse{ .resource = result.resource, .range = Core::GpuTaskResourceRange{ .bufferRange = Core::BufferRange(0u, byteSize) }, .requiredState = Core::ResourceStates::Common, .access = Core::GpuTaskResourceAccess::Write, },
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.raytrace.optical_scene_upload"))
        .setMarkerLabel("Ray Optical Scene Upload")
        .setScheduling(scheduling)
        .setDependencies(priorReadersDrained.valid() ? &priorReadersDrained : nullptr, priorReadersDrained.valid() ? 1u : 0u)
        .setResourceUses(uses, LengthOf(uses))
    ;
    const auto uploadTaskResult = graph.addTask<__hidden_task_graph_optical_scene_upload::OpticalUploadTask>(desc, __hidden_task_graph_optical_scene_upload::OpticalUploadTask::Payload{ uploadBuffer, blob, resources.upload, Move(reservation), plan.queue, });
    if(!uploadTaskResult)
        return MakeUnexpected(uploadTaskResult.error());
    result.uploadTask = *uploadTaskResult;
    if(!result.valid())
        return MakeUnexpected(Failure{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<RayTracingOpticalSceneGraphBuffer> ImportRayTracingOpticalSceneBuffer(Core::GpuTaskGraph& graph, const RayTracingOpticalSceneSnapshot& resources, const Core::GpuTaskId priorReadersDrained, Core::Alloc::ScratchArena& scratchArena){
    const auto uploaded = __hidden_task_graph_optical_scene_upload::ImportOpticalSceneUpload(graph, resources, priorReadersDrained);
    if(!uploaded || !resources.finalize)
        return uploaded;
    return DeclareRayTracingOpticalBoundsFinalize(graph, resources, uploaded->resource, priorReadersDrained, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

