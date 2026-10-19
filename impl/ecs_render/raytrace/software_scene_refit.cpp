// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "software_scene_refit.h"

#include <impl/ecs_render/shader/shader_system.h>

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/backend_selection/backend.h>

namespace __hidden_refit_shader{
static constexpr StringView s_DefaultShaderVariant = "default";
static constexpr u64 s_SoftwareNodeByteSize = 32u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SoftwareSceneRefitSnapshot::SoftwareSceneRefitSnapshot(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics)noexcept
    : inputs(arena)
    , meshNodes(arena)
    , graphics(graphics)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SoftwareSceneRefitResources::SoftwareSceneRefitResources(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics)noexcept
    : m_arena(arena)
    , m_graphics(graphics)
    , m_snapshots(arena)
{}

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
    const SoftwareSceneRefitInstanceGpu* const inputs, const Core::BufferHandle* const meshNodes, const usize instanceCount,
    const Core::BufferHandle& sceneNodes, const Core::GpuDescriptorHandle sceneDescriptor, const u32 nodeCount,
    RendererShaderSystem& shaderSystem
){
    if(
        !inputs || !meshNodes || instanceCount == 0u || instanceCount > Limit<u32>::s_Max / sizeof(SoftwareSceneRefitInstanceGpu)
        || !sceneNodes || !sceneDescriptor.valid() || sceneDescriptor.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || static_cast<u64>(instanceCount) * 2u - 1u != nodeCount || !ensurePipeline(shaderSystem)
    )
        return MakeUnexpected(Failure{});
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    const auto& sceneDesc = sceneNodes->getCreationDescription();
    if(sceneNodes->getDeviceGeneration() != device.getDeviceGeneration() || !sceneDesc.canHaveUAVs || sceneDesc.byteSize < static_cast<u64>(nodeCount) * __hidden_refit_shader::s_SoftwareNodeByteSize)
        return MakeUnexpected(Failure{});
    const usize byteSize = instanceCount * sizeof(SoftwareSceneRefitInstanceGpu);
    if(m_inputBuffer && m_inputBuffer->getDeviceGeneration() != device.getDeviceGeneration())
        return MakeUnexpected(Failure{});
    if(!m_inputBuffer || m_inputBuffer->getCreationDescription().byteSize < byteSize){
        Core::BufferDesc desc;
        desc.setByteSize(byteSize).setCanHaveRawViews(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName(Name("software_scene_refit_inputs")).enableAutomaticStateTracking(Core::ResourceStates::Common);
        auto replacementResult = m_graphics.createBuffer(desc);
        if(!replacementResult)
            return MakeUnexpected(Failure{});
        Core::BufferHandle replacement = Move(*replacementResult);
        const Core::GpuDescriptorHandle descriptor = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        if(!descriptor.valid())
            return MakeUnexpected(Failure{});
        if(!heap.write(descriptor, Core::DescriptorWriteItem::RawBufferSrv(0u, replacement.get()))){
            heap.free(descriptor);
            return MakeUnexpected(Failure{});
        }
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
        if(!meshNodes[index] || inputs[index].meshNodeSlot == Limit<u32>::s_Max || meshNodes[index]->getDeviceGeneration() != device.getDeviceGeneration() || meshNodes[index]->getCreationDescription().byteSize < __hidden_refit_shader::s_SoftwareNodeByteSize)
            return MakeUnexpected(Failure{});
        snapshot->inputs.push_back(inputs[index]);
        snapshot->meshNodes.push_back(meshNodes[index]);
    }
    return snapshot;
}

bool SoftwareSceneRefitResources::ensurePipeline(RendererShaderSystem& shaderSystem){
    if(m_pipeline)
        return true;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    if(!m_bindingLayout){
        Core::BindingLayoutDesc desc(m_arena);
        desc.setVisibility(Core::ShaderType::Compute);
        desc.addItem(Core::BindingLayoutItem::PushConstants(0u, NWB_SCENE_BVH_REFIT_PUSH_BYTES));
        auto bindingLayoutResult = device.createBindingLayout(desc);
        if(bindingLayoutResult)
            m_bindingLayout = Move(*bindingLayoutResult);
        else
            m_bindingLayout.reset();
        if(!bindingLayoutResult)
            return false;
    }
    if(!shaderSystem.loadShader<ComputeShader>(m_shader, Name("engine/graphics/bvh/scene_refit_cs"), ::__hidden_refit_shader::s_DefaultShaderVariant, Name("SoftwareSceneRefit")))
        return false;
    Core::ComputePipelineDesc desc;
    desc.setComputeShader(m_shader).addBindingLayout(m_bindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto pipelineResult = device.createComputePipeline(desc);
    if(pipelineResult)
        m_pipeline = Move(*pipelineResult);
    else
        m_pipeline.reset();
    return static_cast<bool>(pipelineResult);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

