// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "software_scene_refit.h"

#include <impl/ecs_render/shader/shader_system.h>

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/vulkan/backend.h>

namespace __hidden_refit_shader{
static constexpr char s_DefaultShaderVariant[] = "default";
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SoftwareSceneRefitSnapshot::SoftwareSceneRefitSnapshot(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics)
    : inputs(arena)
    , meshNodes(arena)
    , graphics(graphics)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SoftwareSceneRefitResources::SoftwareSceneRefitResources(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics)
    : m_arena(arena)
    , m_graphics(graphics)
{}

void SoftwareSceneRefitResources::invalidate(){
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    if(heap.isInitialized() && m_inputBuffer && m_inputDescriptor.valid() && m_inputBuffer->getDeviceGeneration() == device.getDeviceGeneration())
        heap.free(m_inputDescriptor);
    m_inputBuffer.reset();
    m_inputDescriptor = Core::GpuDescriptorHandle::invalid();
    m_pipeline.reset();
    m_shader.reset();
    m_bindingLayout.reset();
}

SoftwareSceneRefitHandle SoftwareSceneRefitResources::prepare(
    const SoftwareSceneRefitInstanceGpu* const inputs, const Core::BufferHandle* const meshNodes, const usize instanceCount,
    const Core::BufferHandle& sceneNodes, const Core::GpuDescriptorHandle sceneDescriptor, const u32 nodeCount,
    RendererShaderSystem& shaderSystem){
    if(
        !inputs || !meshNodes || instanceCount == 0u || instanceCount > Limit<u32>::s_Max / sizeof(SoftwareSceneRefitInstanceGpu)
        || !sceneNodes || !sceneDescriptor.valid() || sceneDescriptor.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || static_cast<u64>(instanceCount) * 2u - 1u != nodeCount || !ensurePipeline(shaderSystem)
    )
        return {};
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    const auto& sceneDesc = sceneNodes->getCreationDescription();
    if(
        sceneNodes->getDeviceGeneration() != device.getDeviceGeneration() || !sceneDesc.canHaveUAVs
        || sceneDesc.byteSize < static_cast<u64>(nodeCount) * 32u
    )
        return {};
    const usize byteSize = instanceCount * sizeof(SoftwareSceneRefitInstanceGpu);
    if(m_inputBuffer && m_inputBuffer->getDeviceGeneration() != device.getDeviceGeneration())
        return {};
    if(!m_inputBuffer || m_inputBuffer->getCreationDescription().byteSize < byteSize){
        Core::BufferDesc desc;
        desc
            .setByteSize(byteSize)
            .setCanHaveRawViews(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name("software_scene_refit_inputs"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        Core::BufferHandle replacement = m_graphics.createBuffer(desc);
        if(!replacement)
            return {};
        const Core::GpuDescriptorHandle descriptor = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        if(!descriptor.valid())
            return {};
        if(!heap.write(descriptor, Core::DescriptorWriteItem::RawBuffer_SRV(0u, replacement.get()))){
            heap.free(descriptor);
            return {};
        }
        if(m_inputDescriptor.valid())
            heap.free(m_inputDescriptor);
        m_inputBuffer = Move(replacement);
        m_inputDescriptor = descriptor;
    }
    SoftwareSceneRefitHandle snapshot(
        NewArenaObject<SoftwareSceneRefitControl>(m_arena, m_arena, m_graphics),
        ArenaRefDeleter<SoftwareSceneRefitControl, Core::Alloc::GlobalArena>(&m_arena), AdoptRef
    );
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
            || meshNodes[index]->getDeviceGeneration() != device.getDeviceGeneration()
            || meshNodes[index]->getCreationDescription().byteSize < 32u
        )
            return {};
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
        m_bindingLayout = device.createBindingLayout(desc);
        if(!m_bindingLayout)
            return false;
    }
    if(!shaderSystem.loadShader(m_shader, Name("engine/graphics/bvh/scene_refit_cs"), AStringView(::__hidden_refit_shader::s_DefaultShaderVariant), Core::ShaderType::Compute, Name("SoftwareSceneRefit")))
        return false;
    Core::ComputePipelineDesc desc;
    desc
        .setComputeShader(m_shader)
        .addBindingLayout(m_bindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_pipeline = device.createComputePipeline(desc);
    return static_cast<bool>(m_pipeline);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

