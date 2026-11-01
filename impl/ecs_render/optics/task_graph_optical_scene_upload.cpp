// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_optical_scene_upload.h"

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_optical_scene_upload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct OpticalUploadTask{
    // Accepted optical residency belongs to the primary Graphics writer timeline.
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Transfer, true };


public:
    struct Payload{
        Core::BufferHandle destination;
        Core::GpuUploadBlobId source;
        RayTracingOpticalSceneUploadHandle upload;
        RayTracingOpticalUploadReservation reservation;
        Core::GpuPhysicalQueueId queue;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
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


Expected<RayTracingOpticalSceneGraphBuffer> ImportOpticalSceneUpload(const RayTracingOpticalSceneSnapshot& resources, const Core::GpuTaskId priorReadersDrained, Core::GpuTaskGraph& graph){
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
    const auto resourceResult = graph.importBuffer(uploadBuffer, resourceDesc);
    if(!resourceResult)
        return MakeUnexpected(Failure{});
    result.resource = *resourceResult;
    result.reused = plan.reused;
    if(result.reused)
        return result;

    const auto blobResult = graph.copyUploadData(resources.upload->bytes.data(), byteSize, alignof(u32));
    if(!blobResult)
        return MakeUnexpected(Failure{});
    const Core::GpuUploadBlobId blob = *blobResult;
    const Array<Core::GpuTaskResourceUse, 2> uses = {
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
        .setDependencies({ priorReadersDrained.valid() ? &priorReadersDrained : nullptr, priorReadersDrained.valid() ? 1u : 0u })
        .setResourceUses(uses)
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


namespace __hidden_task_graph_optical_bounds_finalize{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(OpticalBoundsFinalizePushConstants) == NWB_OPTICAL_BOUNDS_FINALIZE_PUSH_BYTES);

struct FinalizeTask{
    // Retained optical bounds are rewritten on the primary Graphics writer timeline.
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer, true };


public:
    struct Payload{
        Core::BufferHandle source;
        RayTracingOpticalBoundsFinalizeHandle finalize;
        Core::GpuUploadBlobId inputUpload;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        const auto inputs = context.declarations.uploadBlobData(payload.inputUpload);
        if(!payload.source || !payload.finalize || !inputs || context.commandIrCapture)
            return false;
        const auto& resources = *payload.finalize;
        if(context.queue != resources.queue || !resources.pipeline || !resources.inputBuffer || !resources.outputBuffer || inputs->size() != resources.inputs.size() * sizeof(RayTracingOpticalRuntimeInputGpu))
            return false;
        commandList.endRenderPass();
        commandList.setBufferState(payload.source.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(resources.inputBuffer.get(), Core::ResourceStates::CopyDest);
        commandList.setBufferState(resources.outputBuffer.get(), Core::ResourceStates::UnorderedAccess);
        for(const auto& bounds : resources.boundsBuffers)
            commandList.setBufferState(bounds.get(), Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
        if(!commandList.tryWriteBuffer(*resources.inputBuffer, inputs->data(), inputs->size(), 0u))
            return false;
        commandList.setBufferState(resources.inputBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(resources.pipeline.get());
        commandList.setComputeState(state);
        resources.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resources.pipeline);
        OpticalBoundsFinalizePushConstants push;
        push.sourceSceneSlot = resources.sourceDescriptor.slot();
        push.runtimeInputsSlot = resources.inputDescriptor.slot();
        push.outputSceneSlot = resources.outputDescriptor.slot();
        push.instanceCount = resources.instanceCount;
        push.runtimeCount = static_cast<u32>(resources.inputs.size());
        push.staticBoundsComplete = resources.staticBoundsComplete ? 1u : 0u;
        commandList.setPushConstants(&push, sizeof(push));
        {
            Core::GpuTimingMeasure timing(resources.graphics.gpuTiming(), RendererGpuTimingScope::s_OpticalBoundsFinalize, resources.graphics.getDevice(), commandList);

            commandList.dispatch(1u, 1u, 1u);
        }
        commandList.setBufferState(resources.inputBuffer.get(), Core::ResourceStates::Common);
        commandList.setBufferState(resources.outputBuffer.get(), Core::ResourceStates::Common);
        commandList.setBufferState(payload.source.get(), Core::ResourceStates::Common);
        commandList.commitBarriers();
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<RayTracingOpticalSceneGraphBuffer> ImportRayTracingOpticalSceneBuffer(
    const RayTracingOpticalSceneSnapshot& resources,
    const Core::GpuTaskId priorReadersDrained,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
){
    const auto uploaded = __hidden_task_graph_optical_scene_upload::ImportOpticalSceneUpload(resources, priorReadersDrained, graph);
    if(!uploaded || !resources.finalize)
        return uploaded;
    return DeclareRayTracingOpticalBoundsFinalize(resources, uploaded->resource, priorReadersDrained, graph, scratchArena);
}

Expected<RayTracingOpticalSceneGraphBuffer> DeclareRayTracingOpticalBoundsFinalize(
    const RayTracingOpticalSceneSnapshot& resources,
    const Core::GpuGraphResourceId source,
    const Core::GpuTaskId priorReadersDrained,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!resources.finalize || !resources.uploadBuffer || !source.valid())
        return MakeUnexpected(Failure{});
    const auto& finalize = *resources.finalize;
    if(finalize.inputs.empty() || finalize.inputs.size() != finalize.boundsBuffers.size())
        return MakeUnexpected(Failure{});
    const auto importCommon = [&](const Core::BufferHandle& buffer, const AStringView label) -> Expected<Core::GpuGraphResourceId>{
        if(!buffer || !buffer->getCreationDescription().keepInitialState || buffer->getCreationDescription().initialState != Core::ResourceStates::Common)
            return MakeUnexpected(Failure{});
        return graph.importBuffer(buffer, RendererTaskGraphDetail::BufferResourceDesc(buffer->getCreationDescription().debugName, label).setInitialState(Core::ResourceStates::Common).setExternalFinalState(Core::ResourceStates::Common));
    };
    const auto inputsResult = importCommon(finalize.inputBuffer, "Ray Optical Runtime Inputs");
    RayTracingOpticalSceneGraphBuffer result;
    const auto resourceResult = importCommon(finalize.outputBuffer, "Ray Optical Resolved Scene");
    if(!inputsResult || !resourceResult)
        return MakeUnexpected(Failure{});
    result.resource = *resourceResult;
    const Core::GpuGraphResourceId inputs = *inputsResult;

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses(scratchArena);
    uses.reserve(finalize.boundsBuffers.size() + s_OpticalBoundsUseCapacity);
    uses.push_back(RendererTaskGraphDetail::ReadUse(source, Core::ResourceStates::ShaderResource));
    uses.push_back(RendererTaskGraphDetail::ReadUse(source, Core::ResourceStates::Common));
    uses.push_back(RendererTaskGraphDetail::WriteUse(inputs, Core::ResourceStates::CopyDest));
    uses.push_back(RendererTaskGraphDetail::ReadUse(inputs, Core::ResourceStates::ShaderResource));
    uses.push_back(RendererTaskGraphDetail::WriteUse(inputs, Core::ResourceStates::Common));
    uses.push_back(RendererTaskGraphDetail::WriteUse(result.resource, Core::ResourceStates::UnorderedAccess));
    uses.push_back(RendererTaskGraphDetail::WriteUse(result.resource, Core::ResourceStates::Common));
    for(const auto& bounds : finalize.boundsBuffers){
        Core::GpuGraphResourceId resource;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            if(!declarations.valid())
                return MakeUnexpected(Failure{});
            resource = declarations.findImportedBuffer(bounds);
        }
        if(!resource.valid()){
            const auto importedResource = graph.importBuffer(
                bounds,
                RendererTaskGraphDetail::BufferResourceDesc(bounds->getCreationDescription().debugName, "Ray Optical Runtime Bounds")
                    .setInitialState(Core::ResourceStates::ShaderResource).setExternalFinalState(Core::ResourceStates::ShaderResource)
            );
            if(!importedResource)
                return MakeUnexpected(Failure{});
            resource = *importedResource;
        }
        if(!resource.valid())
            return MakeUnexpected(Failure{});
        const auto found = FindIf(uses.begin(), uses.end(), [&](const auto& use)noexcept{ return use.resource == resource; });
        if(found == uses.end())
            uses.push_back(RendererTaskGraphDetail::ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    const auto uploadResult = graph.copyUploadData(finalize.inputs.data(), finalize.inputs.size() * sizeof(RayTracingOpticalRuntimeInputGpu), alignof(u32));
    if(!uploadResult)
        return MakeUnexpected(Failure{});
    const Core::GpuUploadBlobId upload = *uploadResult;
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.raytrace.optical_bounds_finalize"))
        .setMarkerLabel("Ray Optical Bounds Finalize")
        .setScheduling(scheduling)
        .setDependencies({ priorReadersDrained.valid() ? &priorReadersDrained : nullptr, priorReadersDrained.valid() ? 1u : 0u })
        .setResourceUses(uses)
    ;
    const auto uploadTaskResult = graph.addTask<__hidden_task_graph_optical_bounds_finalize::FinalizeTask>(desc, __hidden_task_graph_optical_bounds_finalize::FinalizeTask::Payload{ resources.uploadBuffer, resources.finalize, upload });
    if(!uploadTaskResult)
        return MakeUnexpected(uploadTaskResult.error());
    result.uploadTask = *uploadTaskResult;
    if(!result.valid())
        return MakeUnexpected(Failure{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

