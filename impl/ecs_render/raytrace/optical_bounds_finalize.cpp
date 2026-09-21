// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "optical_bounds_finalize.h"

#include <impl/ecs_render/shader/shader_system.h>
#include <impl/assets/graphics/mesh/runtime_bounds_constants.h>

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/vulkan/backend.h>

namespace __hidden_optical_shader{
static constexpr char s_DefaultShaderVariant[] = "default";
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingOpticalBoundsFinalizeSnapshot::RayTracingOpticalBoundsFinalizeSnapshot(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsRuntime& graphics)
    : inputs(arena)
    , boundsBuffers(arena)
    , graphics(graphics)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingOpticalBoundsFinalizeResources::RayTracingOpticalBoundsFinalizeResources(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsRuntime& graphics)
    : m_arena(arena)
    , m_graphics(graphics)
{}

void RayTracingOpticalBoundsFinalizeResources::invalidate(){
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    const auto retire = [&](const Core::BufferHandle& buffer, const Core::GpuDescriptorHandle descriptor){
        if(heap.isInitialized() && buffer && descriptor.valid() && buffer->getDeviceGeneration() == device.getDeviceGeneration())
            heap.free(descriptor);
    };
    retire(m_inputBuffer, m_inputDescriptor);
    retire(m_outputBuffer, m_outputDescriptor);
    m_inputBuffer.reset();
    m_outputBuffer.reset();
    m_inputDescriptor = Core::GpuDescriptorHandle::invalid();
    m_outputDescriptor = Core::GpuDescriptorHandle::invalid();
    m_pipeline.reset();
    m_shader.reset();
    m_bindingLayout.reset();
}

RayTracingOpticalBoundsFinalizeHandle RayTracingOpticalBoundsFinalizeResources::prepare(
    const RayTracingOpticalSceneGather& gather,
    const Core::GpuDescriptorHandle sourceDescriptor,
    RendererShaderSystem& shaderSystem){
    if(
        gather.runtimeBounds.empty() || !sourceDescriptor.valid()
        || gather.runtimeBounds.size() > Limit<u32>::s_Max / sizeof(RayTracingOpticalRuntimeInputGpu)
        || gather.instances.size() > (Limit<u32>::s_Max - NWB_RT_OPTICAL_SCENE_HEADER_BYTES) / NWB_RT_OPTICAL_INSTANCE_BYTES
        || !ensurePipeline(shaderSystem)
    )
        return {};
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    const auto ensureBuffer = [&](
        Core::BufferHandle& buffer,
        Core::GpuDescriptorHandle& descriptor,
        const usize byteSize,
        const Name identity,
        const bool output){
        if(buffer && buffer->getDeviceGeneration() != device.getDeviceGeneration())
            return false;
        if(buffer && buffer->getCreationDescription().byteSize >= byteSize && descriptor.valid())
            return true;
        Core::BufferDesc desc;
        desc
            .setByteSize(byteSize)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(output)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(identity)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        Core::BufferHandle replacement = m_graphics.createBuffer(desc);
        if(!replacement)
            return false;
        const Core::GpuDescriptorHandle slot = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        if(!slot.valid())
            return false;
        const Core::DescriptorWriteItem write = output
            ? Core::DescriptorWriteItem::RawBuffer_UAV(0u, replacement.get())
            : Core::DescriptorWriteItem::RawBuffer_SRV(0u, replacement.get())
        ;
        if(!heap.write(slot, write)){
            heap.free(slot);
            return false;
        }
        if(descriptor.valid())
            heap.free(descriptor);
        buffer = Move(replacement);
        descriptor = slot;
        return true;
    };
    const bool inputReady = ensureBuffer(
        m_inputBuffer, m_inputDescriptor, gather.runtimeBounds.size() * sizeof(RayTracingOpticalRuntimeInputGpu),
        Name("raytrace_optical_runtime_inputs"), false
    );
    const bool outputReady = inputReady && ensureBuffer(
        m_outputBuffer, m_outputDescriptor,
        NWB_RT_OPTICAL_SCENE_HEADER_BYTES + gather.instances.size() * NWB_RT_OPTICAL_INSTANCE_BYTES,
        Name("raytrace_optical_resolved_scene"), true
    );
    if(!outputReady){
        NWB_LOGGER_ERROR(NWB_TEXT("Ray optical bounds: failed to create finalize buffers"));
        return {};
    }
    RayTracingOpticalBoundsFinalizeHandle snapshot(
        NewArenaObject<RayTracingOpticalBoundsFinalizeControl>(m_arena, m_arena, m_graphics),
        ArenaRefDeleter<RayTracingOpticalBoundsFinalizeControl, Core::Alloc::GlobalArena>(&m_arena), AdoptRef
    );
    snapshot->inputBuffer = m_inputBuffer;
    snapshot->outputBuffer = m_outputBuffer;
    snapshot->pipeline = m_pipeline;
    snapshot->inputDescriptor = m_inputDescriptor;
    snapshot->outputDescriptor = m_outputDescriptor;
    snapshot->sourceDescriptor = sourceDescriptor;
    snapshot->queue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    snapshot->instanceCount = static_cast<u32>(gather.instances.size());
    snapshot->staticBoundsComplete = gather.boundsCompleteExceptRuntime;
    snapshot->inputs.reserve(gather.runtimeBounds.size());
    snapshot->boundsBuffers.reserve(gather.runtimeBounds.size());
    u32 previousIndex = 0u;
    for(const auto& bounds : gather.runtimeBounds){
        if(
            !bounds.buffer || !bounds.descriptor.valid() || bounds.instanceIndex >= snapshot->instanceCount
            || bounds.descriptor.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
            || (!snapshot->inputs.empty() && bounds.instanceIndex <= previousIndex)
            || bounds.buffer->getDeviceGeneration() != device.getDeviceGeneration()
            || bounds.buffer->getCreationDescription().byteSize < NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE
            || (gather.instances[bounds.instanceIndex].flags & NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT) == 0u
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Ray optical bounds: invalid accepted runtime bounds binding"));
            return {};
        }
        previousIndex = bounds.instanceIndex;
        snapshot->inputs.push_back({ bounds.objectToWorld, bounds.descriptor.slot(), bounds.instanceIndex, {} });
        snapshot->boundsBuffers.push_back(bounds.buffer);
    }
    return snapshot;
}

bool RayTracingOpticalBoundsFinalizeResources::ensurePipeline(RendererShaderSystem& shaderSystem){
    if(m_pipeline)
        return true;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    if(!m_bindingLayout){
        Core::BindingLayoutDesc desc(m_arena);
        desc.setVisibility(Core::ShaderType::Compute);
        desc.addItem(Core::BindingLayoutItem::PushConstants(0u, NWB_OPTICAL_BOUNDS_FINALIZE_PUSH_BYTES));
        m_bindingLayout = device.createBindingLayout(desc);
        if(!m_bindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("Ray optical bounds: failed to create finalize binding layout"));
            return false;
        }
    }
    if(!shaderSystem.loadShader(
        m_shader, Name("engine/graphics/raytrace/optical_bounds_finalize_cs"), AStringView(::__hidden_optical_shader::s_DefaultShaderVariant),
        Core::ShaderType::Compute, Name("RayOpticalBoundsFinalize")
    ))
        return false;
    Core::ComputePipelineDesc desc;
    desc
        .setComputeShader(m_shader)
        .addBindingLayout(m_bindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_pipeline = device.createComputePipeline(desc);
    if(!m_pipeline)
        NWB_LOGGER_ERROR(NWB_TEXT("Ray optical bounds: failed to create finalize pipeline"));
    return static_cast<bool>(m_pipeline);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

