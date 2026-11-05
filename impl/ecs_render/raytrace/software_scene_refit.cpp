// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "software_scene_refit.h"

#include <impl/assets_graphics/components.adhd.h>
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/shader/shader_system.h>

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SoftwareSceneRefitResources::invalidate(){
    m_snapshots.clear();
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    if(heap.isInitialized() && m_inputBuffer && m_inputDescriptor.valid() && m_inputBuffer->getDeviceGeneration() == device.getDeviceGeneration())
        heap.free(m_inputDescriptor);
    m_inputBuffer.reset();
    m_inputDescriptor = Core::GpuDescriptorHandle::Invalid();
    m_pipeline.reset();
    m_shader.reset();
    m_bindingLayout.reset();
}

Expected<SoftwareSceneRefitHandle> SoftwareSceneRefitResources::prepare(
    const Span<const SoftwareSceneRefitInstanceGpu> inputs,
    const Span<const Core::BufferHandle> meshNodes,
    const Core::BufferHandle& sceneNodes,
    const Core::GpuDescriptorHandle sceneDescriptor,
    const u32 nodeCount,
    RendererShaderSystem& shaderSystem
){
    const usize instanceCount = inputs.size();
    if(
        inputs.empty() || meshNodes.size() != instanceCount || instanceCount > Limit<u32>::s_Max / sizeof(SoftwareSceneRefitInstanceGpu)
        || !sceneNodes || !sceneDescriptor.valid() || sceneDescriptor.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || static_cast<u64>(instanceCount) * 2u - 1u != nodeCount || !ensurePipeline(shaderSystem)
    )
        return MakeUnexpected(Failure{});
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    const auto& sceneDesc = sceneNodes->getCreationDescription();
    if(sceneNodes->getDeviceGeneration() != device.getDeviceGeneration() || !sceneDesc.canHaveUAVs || sceneDesc.byteSize < static_cast<u64>(nodeCount) * s_NwbBvhNodeGpuByteSize)
        return MakeUnexpected(Failure{});
    const usize byteSize = instanceCount * sizeof(SoftwareSceneRefitInstanceGpu);
    if(m_inputBuffer && m_inputBuffer->getDeviceGeneration() != device.getDeviceGeneration())
        return MakeUnexpected(Failure{});
    if(!m_inputBuffer || m_inputBuffer->getCreationDescription().byteSize < byteSize){
        Core::BufferDesc desc;
        desc.setByteSize(byteSize).setCanHaveRawViews(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name(s_SoftwareSceneRefitInputsName)).enableAutomaticStateTracking(Core::ResourceStates::Common);
        auto replacementResult = m_graphics.createBuffer(desc);
        if(!replacementResult)
            return MakeUnexpected(Failure{});
        Core::BufferHandle replacement = Move(*replacementResult);
        const Expected<Core::GpuDescriptorHandle> descriptorResult = RendererDescriptorRegisterDetail::RegisterDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::RawBufferSrv(0u, replacement.get()));
        if(!descriptorResult)
            return MakeUnexpected(descriptorResult.error());
        const Core::GpuDescriptorHandle descriptor = *descriptorResult;
        if(m_inputDescriptor.valid())
            heap.free(m_inputDescriptor);
        m_inputBuffer = Move(replacement);
        m_inputDescriptor = descriptor;
    }
    SoftwareSceneRefitHandle snapshot;
    for(const SoftwareSceneRefitHandle& cached : m_snapshots){
        if(cached->getReferenceCount() == 1u){
            AtomicThreadFence(MemoryOrder::acquire);
            snapshot = cached;
            break;
        }
    }
    if(!snapshot){
        snapshot = SoftwareSceneRefitHandle(NewArenaObject<SoftwareSceneRefitControl>(m_arena, m_arena, m_graphics), ArenaRefDeleter<SoftwareSceneRefitControl, Core::Alloc::GlobalArena>(&m_arena), s_AdoptRef);
        if(!snapshot)
            return MakeUnexpected(Failure{});
        m_snapshots.push_back(snapshot);
    }
    snapshot->inputs.clear();
    snapshot->meshNodes.clear();
    snapshot->inputBuffer = m_inputBuffer;
    snapshot->sceneNodes = sceneNodes;
    snapshot->pipeline = m_pipeline;
    snapshot->inputDescriptor = m_inputDescriptor;
    snapshot->sceneDescriptor = sceneDescriptor;
    snapshot->queue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    snapshot->nodeCount = nodeCount;
    snapshot->inputs.reserve(instanceCount);
    snapshot->meshNodes.reserve(instanceCount);
    for(usize index = 0u; index < instanceCount; ++index){
        if(
            !meshNodes[index] || inputs[index].meshNodeSlot == Limit<u32>::s_Max
            || meshNodes[index]->getDeviceGeneration() != device.getDeviceGeneration() || meshNodes[index]->getCreationDescription().byteSize < s_NwbBvhNodeGpuByteSize
        )
            return MakeUnexpected(Failure{});
        snapshot->inputs.push_back(inputs[index]);
        snapshot->meshNodes.push_back(meshNodes[index]);
    }
    return snapshot;
}

b8 SoftwareSceneRefitResources::ensurePipeline(RendererShaderSystem& shaderSystem){
    return RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        shaderSystem,
        &BvhSubsystem::sceneRefitCs,
        Core::ShaderArchive::s_DefaultVariant,
        Name("SoftwareSceneRefit"),
        NWB_SCENE_BVH_REFIT_PUSH_BYTES,
        false,
        m_bindingLayout,
        m_shader,
        m_pipeline
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_software_scene_refit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(SoftwareSceneRefitPushConstants) == NWB_SCENE_BVH_REFIT_PUSH_BYTES);

struct RefitTask{
    // The captured scene snapshot belongs to the primary Graphics writer timeline.
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute, true };


public:
    struct Payload{
        SoftwareSceneRefitHandle snapshot;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
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
        const SoftwareSceneRefitPushConstants push = MakeSoftwareSceneRefitPushConstants(
            snapshot.nodeCount,
            snapshot.sceneDescriptor.slot(),
            snapshot.inputDescriptor.slot(),
            static_cast<u32>(snapshot.inputs.size())
        );
        commandList.setPushConstants(&push, sizeof(push));
        commandList.dispatch(1u, 1u, 1u);
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SoftwareSceneRefitGraphTasks> DeclareSoftwareSceneRefit(const SoftwareSceneRefitHandle& snapshot, const Core::GpuGraphResourceId sceneNodes, const Core::GpuTaskId dependency, Core::GpuTaskGraph& graph, Core::Alloc::ScratchArena& scratchArena){
    if(!snapshot || !sceneNodes.valid() || !dependency.valid() || snapshot->inputs.empty())
        return MakeUnexpected(Failure{});
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid() || declarations.findImportedBuffer(snapshot->sceneNodes) != sceneNodes)
            return MakeUnexpected(Failure{});
    }
    const auto inputResult = graph.importBuffer(
        snapshot->inputBuffer,
        RendererTaskGraphDetail::BufferResourceDesc(Name(s_SoftwareSceneRefitInputsName), "Software Scene Refit Inputs")
            .setInitialState(Core::ResourceStates::Common).setExternalFinalState(Core::ResourceStates::Common)
    );
    if(!inputResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId input = *inputResult;
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses(scratchArena);
    // The input buffer and the scene nodes.
    uses.reserve(snapshot->meshNodes.size() + s_SoftwareSceneRefitFixedUseCapacity);
    uses.push_back(RendererTaskGraphDetail::ReadUse(input, Core::ResourceStates::ShaderResource));
    uses.push_back(RendererTaskGraphDetail::ReadWriteUse(sceneNodes, Core::ResourceStates::UnorderedAccess));
    // Reuse every root's existing import, including accepted meshes without a build in this frame.
    for(const auto& root : snapshot->meshNodes){
        Core::GpuGraphResourceId resource;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            if(!declarations.valid())
                return MakeUnexpected(Failure{});
            resource = declarations.findImportedBuffer(root);
        }
        if(!resource.valid())
            return MakeUnexpected(Failure{});
        if(FindIf(uses.begin(), uses.end(), [&](const auto& use)noexcept{ return use.resource == resource; }) == uses.end())
            uses.push_back(RendererTaskGraphDetail::ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    const auto uploadResult = graph.copyUploadData(snapshot->inputs.data(), snapshot->inputs.size() * sizeof(SoftwareSceneRefitInstanceGpu), alignof(u32));
    if(!uploadResult)
        return MakeUnexpected(Failure{});
    const Core::GpuUploadBlobId upload = *uploadResult;
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc uploadDesc;
    uploadDesc.setIdentity(Name("render.shadow_prepare.scene_refit_inputs_upload")).setMarkerLabel("Software Scene Refit Inputs Upload").setScheduling(scheduling).setDependencies({ &dependency, 1u });
    SoftwareSceneRefitGraphTasks result;
    const auto inputUploadResult = graph.addUploadBufferTask(
        uploadDesc,
        Core::GpuUploadBufferTaskDesc{
            .source = upload,
            .destination = input,
            // Retained storage restores Common; the refit read owns the following SRV transition.
            .finalState = Core::ResourceStates::Common, }
    );
    if(!inputUploadResult)
        return MakeUnexpected(Failure{});
    result.inputUpload = *inputUploadResult;
    Core::GpuTaskDesc desc;
    desc.setIdentity(Name("render.shadow_prepare.scene_refit")).setMarkerLabel("Software Scene BVH Refit").setScheduling(scheduling).setDependencies({ &result.inputUpload, 1u }).setResourceUses(uses);
    const auto refitResult = graph.addTask<__hidden_task_graph_software_scene_refit::RefitTask>(desc, __hidden_task_graph_software_scene_refit::RefitTask::Payload{ snapshot });
    if(!refitResult)
        return MakeUnexpected(refitResult.error());
    result.refit = *refitResult;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

