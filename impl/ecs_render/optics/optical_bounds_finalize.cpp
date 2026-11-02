// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "optical_bounds_finalize.h"

#include <impl/assets/graphics/mesh/runtime_bounds_constants.h>
#include <impl/assets_graphics/components.adhd.h>
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/shader/shader_system.h>

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RayTracingOpticalBoundsFinalizeResources::invalidate(){
    m_snapshots.clear();
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
    m_inputDescriptor = Core::GpuDescriptorHandle::Invalid();
    m_outputDescriptor = Core::GpuDescriptorHandle::Invalid();
    m_pipeline.reset();
    m_shader.reset();
    m_bindingLayout.reset();
}

Expected<RayTracingOpticalBoundsFinalizeHandle> RayTracingOpticalBoundsFinalizeResources::prepare(const RayTracingOpticalSceneGather& gather, const Core::GpuDescriptorHandle sourceDescriptor, RendererShaderSystem& shaderSystem){
    if(
        gather.runtimeBounds.empty() || !sourceDescriptor.valid()
        || gather.runtimeBounds.size() > Limit<u32>::s_Max / sizeof(RayTracingOpticalRuntimeInputGpu)
        || gather.instances.size() > (Limit<u32>::s_Max - NWB_RT_OPTICAL_SCENE_HEADER_BYTES) / NWB_RT_OPTICAL_INSTANCE_BYTES
        || !ensurePipeline(shaderSystem)
    )
        return MakeUnexpected(Failure{});
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    const auto ensureBuffer = [&](Core::BufferHandle& buffer, Core::GpuDescriptorHandle& descriptor, const usize byteSize, const Name identity, const b8 output){
        if(buffer && buffer->getDeviceGeneration() != device.getDeviceGeneration())
            return false;
        if(buffer && buffer->getCreationDescription().byteSize >= byteSize && descriptor.valid())
            return true;
        Core::BufferDesc desc;
        desc.setByteSize(byteSize).setCanHaveRawViews(true).setCanHaveUAVs(output).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName(identity).enableAutomaticStateTracking(Core::ResourceStates::Common);
        auto replacementResult = m_graphics.createBuffer(desc);
        if(!replacementResult)
            return false;
        Core::BufferHandle replacement = Move(*replacementResult);
        const Core::DescriptorWriteItem write = output ? Core::DescriptorWriteItem::RawBufferUav(0u, replacement.get()) : Core::DescriptorWriteItem::RawBufferSrv(0u, replacement.get());
        const Expected<Core::GpuDescriptorHandle> slotResult = RendererDescriptorRegisterDetail::RegisterDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, write);
        if(!slotResult)
            return false;
        const Core::GpuDescriptorHandle slot = *slotResult;
        if(descriptor.valid())
            heap.free(descriptor);
        buffer = Move(replacement);
        descriptor = slot;
        return true;
    };
    const b8 inputReady = ensureBuffer(m_inputBuffer, m_inputDescriptor, gather.runtimeBounds.size() * sizeof(RayTracingOpticalRuntimeInputGpu), Name("raytrace_optical_runtime_inputs"), false);
    const b8 outputReady = inputReady && ensureBuffer(m_outputBuffer, m_outputDescriptor, NWB_RT_OPTICAL_SCENE_HEADER_BYTES + gather.instances.size() * NWB_RT_OPTICAL_INSTANCE_BYTES, Name("raytrace_optical_resolved_scene"), true);
    if(!outputReady){
        NWB_LOGGER_ERROR(NWB_TEXT("Ray optical bounds: failed to create finalize buffers"));
        return MakeUnexpected(Failure{});
    }
    RayTracingOpticalBoundsFinalizeHandle snapshot;
    for(const RayTracingOpticalBoundsFinalizeHandle& cached : m_snapshots){
        if(cached->getReferenceCount() == 1u){
            AtomicThreadFence(MemoryOrder::acquire);
            snapshot = cached;
            break;
        }
    }
    if(!snapshot){
        snapshot = RayTracingOpticalBoundsFinalizeHandle(NewArenaObject<RayTracingOpticalBoundsFinalizeControl>(m_arena, m_arena, m_graphics), ArenaRefDeleter<RayTracingOpticalBoundsFinalizeControl, Core::Alloc::GlobalArena>(&m_arena), s_AdoptRef);
        if(!snapshot)
            return MakeUnexpected(Failure{});
        m_snapshots.push_back(snapshot);
    }
    snapshot->inputs.clear();
    snapshot->boundsBuffers.clear();
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
            return MakeUnexpected(Failure{});
        }
        previousIndex = bounds.instanceIndex;
        snapshot->inputs.push_back({ bounds.objectToWorld, bounds.descriptor.slot(), bounds.instanceIndex, {} });
        snapshot->boundsBuffers.push_back(bounds.buffer);
    }
    return snapshot;
}

b8 RayTracingOpticalBoundsFinalizeResources::ensurePipeline(RendererShaderSystem& shaderSystem){
    if(m_pipeline)
        return true;
    if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        shaderSystem,
        &TransparentSubsystem::opticalBoundsFinalizeCs,
        Core::ShaderArchive::s_DefaultVariant,
        Name("RayOpticalBoundsFinalize"),
        NWB_OPTICAL_BOUNDS_FINALIZE_PUSH_BYTES,
        false,
        m_bindingLayout,
        m_shader,
        m_pipeline
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Ray optical bounds: failed to create finalize pipeline"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

