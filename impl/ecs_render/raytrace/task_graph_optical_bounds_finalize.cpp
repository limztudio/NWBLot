// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_optical_bounds_finalize.h"

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/alloc/scratch.h>
#include <core/graphics/vulkan/backend.h>
#include <core/graphics/runtime/runtime.h>
#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_optical_bounds_finalize{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PushConstants{
    u32 sourceSceneSlot;
    u32 runtimeInputsSlot;
    u32 outputSceneSlot;
    u32 instanceCount;
    u32 runtimeCount;
    u32 staticBoundsComplete;
};
static_assert(sizeof(PushConstants) == NWB_OPTICAL_BOUNDS_FINALIZE_PUSH_BYTES);

struct FinalizeTask{
    struct Payload{
        Core::BufferHandle source;
        RayTracingOpticalBoundsFinalizeHandle finalize;
        Core::GpuUploadBlobId inputUpload;
    };

    [[nodiscard]] static bool record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        usize inputBytes = 0u;
        const void* const inputs = context.declarations.uploadBlobData(payload.inputUpload, inputBytes);
        if(!payload.source || !payload.finalize || !inputs || context.commandIrCapture)
            return false;
        const auto& resources = *payload.finalize;
        if(
            context.queue != resources.queue || !resources.pipeline
            || !resources.inputBuffer || !resources.outputBuffer
            || inputBytes != resources.inputs.size() * sizeof(RayTracingOpticalRuntimeInputGpu)
        )
            return false;
        commandList.endRenderPass();
        commandList.setBufferState(payload.source.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(resources.inputBuffer.get(), Core::ResourceStates::CopyDest);
        commandList.setBufferState(resources.outputBuffer.get(), Core::ResourceStates::UnorderedAccess);
        for(const auto& bounds : resources.boundsBuffers)
            commandList.setBufferState(bounds.get(), Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
        if(!commandList.tryWriteBuffer(*resources.inputBuffer, inputs, inputBytes, 0u))
            return false;
        commandList.setBufferState(resources.inputBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(resources.pipeline.get());
        commandList.setComputeState(state);
        resources.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resources.pipeline);
        const PushConstants push{
            resources.sourceDescriptor.slot(), resources.inputDescriptor.slot(), resources.outputDescriptor.slot(),
            resources.instanceCount, static_cast<u32>(resources.inputs.size()), resources.staticBoundsComplete ? 1u : 0u,
        };
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


RayTracingOpticalSceneGraphBuffer DeclareRayTracingOpticalBoundsFinalize(
    Core::GpuTaskGraph& graph,
    const RayTracingOpticalSceneSnapshot& resources,
    const Core::GpuGraphResourceId source,
    Core::Alloc::ScratchArena& scratchArena){
    if(!resources.finalize || !resources.uploadBuffer || !source.valid())
        return {};
    const auto& finalize = *resources.finalize;
    if(finalize.inputs.empty() || finalize.inputs.size() != finalize.boundsBuffers.size())
        return {};
    const auto importCommon = [&](const Core::BufferHandle& buffer, const AStringView label){
        if(
            !buffer || !buffer->getCreationDescription().keepInitialState
            || buffer->getCreationDescription().initialState != Core::ResourceStates::Common
        )
            return Core::GpuGraphResourceId{};
        return graph.importBuffer(
            buffer,
            RendererTaskGraphDetail::BufferResourceDesc(buffer->getCreationDescription().debugName, label)
                .setInitialState(Core::ResourceStates::Common).setExternalFinalState(Core::ResourceStates::Common)
        );
    };
    const Core::GpuGraphResourceId inputs = importCommon(finalize.inputBuffer, "Ray Optical Runtime Inputs");
    RayTracingOpticalSceneGraphBuffer result;
    result.resource = importCommon(finalize.outputBuffer, "Ray Optical Resolved Scene");
    if(!inputs.valid() || !result.resource.valid())
        return {};

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses(scratchArena);
    uses.reserve(finalize.boundsBuffers.size() + 8u);
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
                return {};
            resource = declarations.findImportedBuffer(bounds);
        }
        if(!resource.valid()){
            resource = graph.importBuffer(
                bounds,
                RendererTaskGraphDetail::BufferResourceDesc(bounds->getCreationDescription().debugName, "Ray Optical Runtime Bounds")
                    .setInitialState(Core::ResourceStates::ShaderResource).setExternalFinalState(Core::ResourceStates::ShaderResource)
            );
        }
        if(!resource.valid())
            return {};
        const auto found = FindIf(uses.begin(), uses.end(), [&](const auto& use){ return use.resource == resource; });
        if(found == uses.end())
            uses.push_back(RendererTaskGraphDetail::ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    const Core::GpuUploadBlobId upload = graph.copyUploadData(
        finalize.inputs.data(), finalize.inputs.size() * sizeof(RayTracingOpticalRuntimeInputGpu), alignof(u32)
    );
    if(!upload.valid())
        return {};
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
        .setQueue(RendererTaskGraphDetail::GraphicsComputeUploadQueueRequest())
        .setScheduling(scheduling)
        .setResourceUses(uses.data(), uses.size())
    ;
    result.uploadTask = graph.addTask<__hidden_task_graph_optical_bounds_finalize::FinalizeTask>(
        desc, __hidden_task_graph_optical_bounds_finalize::FinalizeTask::Payload{ resources.uploadBuffer, resources.finalize, upload }
    );
    return result.valid() ? result : RayTracingOpticalSceneGraphBuffer{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

