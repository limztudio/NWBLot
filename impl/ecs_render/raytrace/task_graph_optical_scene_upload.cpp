// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_optical_scene_upload.h"

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/common/log.h>
#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_optical_scene_upload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OpticalUploadTask{
    struct Payload{
        Core::BufferHandle destination;
        Core::GpuUploadBlobId source;
        RayTracingOpticalSceneUploadHandle upload;
        RayTracingOpticalUploadReservation reservation;
        Core::GpuPhysicalQueueId queue;
    };

    [[nodiscard]] static bool record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        usize byteSize = 0u;
        const void* const bytes = context.declarations.uploadBlobData(payload.source, byteSize);
        if(
            !payload.destination || !payload.upload || !bytes || byteSize == 0u
            || byteSize != payload.upload->bytes.size() || !payload.reservation.valid() || context.commandIrCapture
            || context.queue != payload.queue
        )
            return false;

        commandList.endRenderPass();
        const Core::BufferRange range(0u, byteSize);
        commandList.setBufferState(payload.destination.get(), Core::ResourceStates::CopyDest, false, range);
        commandList.commitBarriers();
        if(!commandList.tryWriteBuffer(*payload.destination, bytes, byteSize, 0u))
            return false;
        commandList.setBufferState(payload.destination.get(), Core::ResourceStates::Common, false, range);
        commandList.commitBarriers();
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(!payload.reservation.accept(token))
            NWB_LOGGER_ERROR(NWB_TEXT("Ray optical scene: accepted upload has invalid residency provenance"));
    }

    static void discarded(Payload& payload){
        payload.reservation.discard();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingOpticalSceneGraphBuffer ImportRayTracingOpticalSceneBuffer(
    Core::GpuTaskGraph& graph,
    const RayTracingOpticalSceneSnapshot& resources){
    if(!resources.valid())
        return {};
    const Core::BufferDesc& bufferDesc = resources.buffer->getCreationDescription();
    const usize byteSize = resources.upload->bytes.size();
    if(
        !bufferDesc.debugName || byteSize == 0u || byteSize > bufferDesc.byteSize || (byteSize % sizeof(u32)) != 0u
        || bufferDesc.isVolatile || !bufferDesc.keepInitialState || bufferDesc.initialState != Core::ResourceStates::Common
        || resources.buffer->resolveTaskGraphImportInitialState() != Core::ResourceStates::Common
    )
        return {};

    const RayTracingOpticalUploadPlan plan = resources.uploadState->plan(resources.upload);
    if(!plan.valid() || plan.buffer.get() != resources.buffer.get())
        return {};
    RayTracingOpticalUploadReservation reservation(resources.uploadState, plan);
    if(!plan.reused && !reservation.valid())
        return {};

    Core::GpuGraphResourceDesc resourceDesc = RendererTaskGraphDetail::BufferResourceDesc(bufferDesc.debugName, "Ray Optical Scene Buffer");
    resourceDesc.setInitialState(Core::ResourceStates::Common).setExternalFinalState(Core::ResourceStates::Common);
    if(plan.acceptedToken.valid()){
        const Core::GpuExternalCompletionId ready = graph.importExternalCompletion(
            Core::GpuExternalCompletionDesc{}
                .setIdentity(Name("render.raytrace.optical_scene_ready"))
                .setMarkerLabel("Ray Optical Scene Ready")
                .setToken(plan.acceptedToken)
        );
        if(!ready.valid())
            return {};
        resourceDesc.setInitialAvailabilityCompletion(ready);
    }
    RayTracingOpticalSceneGraphBuffer result;
    result.resource = graph.importBuffer(resources.buffer, resourceDesc);
    if(!result.resource.valid())
        return {};
    result.reused = plan.reused;
    if(result.reused)
        return result;

    const Core::GpuUploadBlobId blob = graph.copyUploadData(resources.upload->bytes.data(), byteSize, alignof(u32));
    if(!blob.valid())
        return {};
    const Core::GpuTaskResourceUse uses[] = {
        Core::GpuTaskResourceUse{
            .resource = result.resource,
            .range = Core::GpuTaskResourceRange{ .bufferRange = Core::BufferRange(0u, byteSize) },
            .requiredState = Core::ResourceStates::CopyDest,
            .access = Core::GpuTaskResourceAccess::Write,
        },
        Core::GpuTaskResourceUse{
            .resource = result.resource,
            .range = Core::GpuTaskResourceRange{ .bufferRange = Core::BufferRange(0u, byteSize) },
            .requiredState = Core::ResourceStates::Common,
            .access = Core::GpuTaskResourceAccess::Write,
        },
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
        .setQueue(RendererTaskGraphDetail::GraphicsUploadQueueRequest())
        .setScheduling(scheduling)
        .setResourceUses(uses, LengthOf(uses))
    ;
    result.uploadTask = graph.addTask<__hidden_task_graph_optical_scene_upload::OpticalUploadTask>(
        desc,
        __hidden_task_graph_optical_scene_upload::OpticalUploadTask::Payload{
            resources.buffer, blob, resources.upload, Move(reservation), plan.queue,
        }
    );
    return result.valid() ? result : RayTracingOpticalSceneGraphBuffer{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

