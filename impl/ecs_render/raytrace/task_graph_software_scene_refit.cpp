// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_software_scene_refit.h"

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/vulkan/backend.h>
#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_software_scene_refit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PushConstants{
    u32 nodeCount;
    u32 sceneNodeSlot;
    u32 instanceInputSlot;
    u32 instanceCount;
};
static_assert(sizeof(PushConstants) == NWB_SCENE_BVH_REFIT_PUSH_BYTES);

struct RefitTask{
    struct Payload{
        SoftwareSceneRefitHandle snapshot;
    };

    [[nodiscard]] static bool record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(!payload.snapshot || context.commandIrCapture)
            return false;
        const auto& snapshot = *payload.snapshot;
        if(context.queue != snapshot.queue || !snapshot.pipeline)
            return false;
        commandList.endRenderPass();
        Core::ComputeState state;
        state.setPipeline(snapshot.pipeline.get());
        commandList.setComputeState(state);
        snapshot.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *snapshot.pipeline);
        const PushConstants push{
            snapshot.nodeCount, snapshot.sceneDescriptor.slot(), snapshot.inputDescriptor.slot(), static_cast<u32>(snapshot.inputs.size()),
        };
        commandList.setPushConstants(&push, sizeof(push));
        commandList.dispatch(1u, 1u, 1u);
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SoftwareSceneRefitGraphTasks DeclareSoftwareSceneRefit(
    Core::GpuTaskGraph& graph, const SoftwareSceneRefitHandle& snapshot,
    const Core::GpuGraphResourceId sceneNodes, const Core::GpuTaskId dependency, Core::Alloc::ScratchArena& scratchArena){
    if(!snapshot || !sceneNodes.valid() || !dependency.valid() || snapshot->inputs.empty())
        return {};
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid() || declarations.findImportedBuffer(snapshot->sceneNodes) != sceneNodes)
            return {};
    }
    const Core::GpuGraphResourceId input = graph.importBuffer(
        snapshot->inputBuffer,
        RendererTaskGraphDetail::BufferResourceDesc(Name("software_scene_refit_inputs"), "Software Scene Refit Inputs")
            .setInitialState(Core::ResourceStates::Common).setExternalFinalState(Core::ResourceStates::Common)
    );
    if(!input.valid())
        return {};
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses(scratchArena);
    uses.reserve(snapshot->meshNodes.size() + 2u);
    uses.push_back(RendererTaskGraphDetail::ReadUse(input, Core::ResourceStates::ShaderResource));
    uses.push_back(RendererTaskGraphDetail::ReadWriteUse(sceneNodes, Core::ResourceStates::UnorderedAccess));
    // Reuse every root's existing import, including accepted meshes without a build in this frame.
    for(const auto& root : snapshot->meshNodes){
        Core::GpuGraphResourceId resource;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            if(!declarations.valid())
                return {};
            resource = declarations.findImportedBuffer(root);
        }
        if(!resource.valid())
            return {};
        if(FindIf(uses.begin(), uses.end(), [&](const auto& use){ return use.resource == resource; }) == uses.end())
            uses.push_back(RendererTaskGraphDetail::ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    const Core::GpuUploadBlobId upload = graph.copyUploadData(snapshot->inputs.data(), snapshot->inputs.size() * sizeof(SoftwareSceneRefitInstanceGpu), alignof(u32));
    if(!upload.valid())
        return {};
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("render.shadow_prepare.scene_refit_inputs_upload"))
        .setMarkerLabel("Software Scene Refit Inputs Upload")
        .setQueue(RendererTaskGraphDetail::GraphicsUploadQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(&dependency, 1u)
    ;
    SoftwareSceneRefitGraphTasks result;
    result.inputUpload = graph.addUploadBufferTask(
        uploadDesc,
        Core::GpuUploadBufferTaskDesc{
            .source = upload,
            .destination = input,
            // Retained storage restores Common; the refit read owns the following SRV transition.
            .finalState = Core::ResourceStates::Common,
        }
    );
    if(!result.inputUpload.valid())
        return {};
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.shadow_prepare.scene_refit"))
        .setMarkerLabel("Software Scene BVH Refit")
        .setQueue(RendererTaskGraphDetail::GraphicsPreferredComputeQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(&result.inputUpload, 1u)
        .setResourceUses(uses.data(), uses.size())
    ;
    result.refit = graph.addTask<__hidden_task_graph_software_scene_refit::RefitTask>(
        desc, __hidden_task_graph_software_scene_refit::RefitTask::Payload{ snapshot }
    );
    return result.refit.valid() ? result : SoftwareSceneRefitGraphTasks{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

