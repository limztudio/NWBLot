// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"

#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/material/renderer_material_state.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/assets_graphics/components.adhd.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::BindingLayoutHandle> RendererMaterialSystem::prepareMaterialPassBindingLayout(){
    // Every material pass uses this push-only layout; transparent consumes the full payload.
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, m_graphics.getDevice(), sizeof(TransparentDrawPushConstants), Core::ShaderType::All, m_materialState.m_materialPassBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create the shared material-pass push-constant layout"));
        return MakeUnexpected(Failure{});
    }
    return m_materialState.m_materialPassBindingLayout;
}


b8 RendererMaterialSystem::prepareMeshComputeBindingLayout(){
    // The unified UAV keeps its existing heap selector; only the compute tail adds the generated-index byte offset.
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, m_graphics.getDevice(), sizeof(MeshComputePushConstants), Core::ShaderType::Compute, m_materialState.m_computeBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation binding layout"));
        return false;
    }

    return true;
}

b8 RendererMaterialSystem::createComputeEmulationResources(){
    if(!prepareMeshComputeBindingLayout())
        return false;

    if(!m_materialState.m_emulationVertexShader){
        if(!m_shaderSystem.loadShader<VertexShader>(&MeshSubsystem::emulationVs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_MeshEmulationVS", m_materialState.m_emulationVertexShader))
            return false;
    }

    if(!m_materialState.m_emulationInputLayout){
        Array<Core::VertexAttributeDesc, NWB_MESH_EMULATION_VERTEX_ATTRIBUTE_COUNT> attributes{};
        ECSRenderDetail::SetEmulatedVertexAttribute(Core::Format::RGBA32_FLOAT, NWB_MESH_EMULATION_VERTEX_POSITION_BYTE_OFFSET, "POSITION", attributes[NWB_MESH_EMULATION_VERTEX_POSITION_LOCATION]);
        ECSRenderDetail::SetEmulatedVertexAttribute(Core::Format::RGBA16_FLOAT, NWB_MESH_EMULATION_VERTEX_NORMAL_BYTE_OFFSET, "NORMAL", attributes[NWB_MESH_EMULATION_VERTEX_NORMAL_LOCATION]);
        ECSRenderDetail::SetEmulatedVertexAttribute(Core::Format::RGBA16_FLOAT, NWB_MESH_EMULATION_VERTEX_TANGENT_BYTE_OFFSET, "TANGENT", attributes[NWB_MESH_EMULATION_VERTEX_TANGENT_LOCATION]);
        ECSRenderDetail::SetEmulatedVertexAttribute(Core::Format::RG32_FLOAT, NWB_MESH_EMULATION_VERTEX_UV0_BYTE_OFFSET, "TEXCOORD", attributes[NWB_MESH_EMULATION_VERTEX_UV0_LOCATION]);
        ECSRenderDetail::SetEmulatedVertexAttribute(Core::Format::RGBA16_FLOAT, NWB_MESH_EMULATION_VERTEX_COLOR_BYTE_OFFSET, "COLOR", attributes[NWB_MESH_EMULATION_VERTEX_COLOR_LOCATION]);
        ECSRenderDetail::SetEmulatedVertexAttribute(Core::Format::RGBA32_FLOAT, NWB_MESH_EMULATION_VERTEX_WORLD_POSITION_BYTE_OFFSET, "POSITION1", attributes[NWB_MESH_EMULATION_VERTEX_WORLD_POSITION_LOCATION]);

        auto& device = m_graphics.getDevice();
        auto emulationInputLayoutResult = device.createInputLayout(attributes.data(), NWB_MESH_EMULATION_VERTEX_ATTRIBUTE_COUNT, m_materialState.m_emulationVertexShader.get());
        if(emulationInputLayoutResult)
            m_materialState.m_emulationInputLayout = Move(*emulationInputLayoutResult);
        else
            m_materialState.m_emulationInputLayout.reset();
        if(!emulationInputLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation input layout"));
            return false;
        }
    }

    return true;
}

b8 RendererMaterialSystem::reserveInstanceBufferCapacity(const usize instanceCount){
    if(instanceCount == 0)
        return true;
    NWB_ASSERT(instanceCount <= static_cast<usize>(Limit<u32>::s_Max));
    if(m_materialState.m_instanceBuffer && m_materialState.m_instanceBufferCapacity >= instanceCount)
        return true;

    const usize capacity = ::NextGrowingCapacity(m_materialState.m_instanceBufferCapacity, instanceCount);
#if defined(NWB_DEBUG)
    if(capacity > Limit<usize>::s_Max / sizeof(InstanceGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance buffer capacity overflows addressable memory"));
        return false;
    }
#endif

    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(s_InstanceBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto instanceBufferResult = m_graphics.createBuffer(instanceBufferDesc);
    if(!instanceBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create instance data buffer"));
        return false;
    }
    Core::BufferHandle instanceBuffer = Move(*instanceBufferResult);

    m_materialState.m_instanceBuffer = Move(instanceBuffer);
    m_materialState.m_instanceBufferCapacity = capacity;
    return true;
}

b8 RendererMaterialSystem::reserveMaterialTypedBufferCapacity(const usize byteCount){
    usize requiredByteCount = Max<usize>(byteCount, sizeof(u32));
#if defined(NWB_DEBUG)
    const auto alignedByteCount = AlignUpChecked(requiredByteCount, sizeof(u32));
    if(!alignedByteCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed buffer request overflows alignment"));
        return false;
    }
    requiredByteCount = *alignedByteCount;
    if(requiredByteCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed buffer request exceeds u32 byte-offset limits"));
        return false;
    }
#else
    requiredByteCount = AlignUp(requiredByteCount, sizeof(u32));
#endif
    if(m_materialState.m_materialTypedBuffer && m_materialState.m_materialTypedBufferCapacity >= requiredByteCount)
        return true;

    const usize capacity = ::NextGrowingCapacity(m_materialState.m_materialTypedBufferCapacity, requiredByteCount);
    Core::BufferDesc materialTypedBufferDesc;
    materialTypedBufferDesc
        .setByteSize(static_cast<u64>(capacity))
        .setStructStride(sizeof(u32))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(s_MaterialTypedBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto materialTypedBufferResult = m_graphics.createBuffer(materialTypedBufferDesc);
    if(!materialTypedBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create material typed buffer"));
        return false;
    }
    Core::BufferHandle materialTypedBuffer = Move(*materialTypedBufferResult);

    m_materialState.m_materialTypedBuffer = Move(materialTypedBuffer);
    m_materialState.m_materialTypedBufferCapacity = capacity;
    return true;
}

b8 RendererMaterialSystem::prepareMaterialPassDrawBuffers(const Span<const InstanceGpuData> instanceData, const Span<const u8> materialTypedBytes){
    const usize uploadBytes = ECSRenderDetail::ResolveMaterialTypedUploadByteCount(materialTypedBytes);

    return reserveInstanceBufferCapacity(instanceData.size()) && reserveMaterialTypedBufferCapacity(uploadBytes);
}

b8 RendererMaterialSystem::materialPassDrawBuffersReady(const Span<const InstanceGpuData> instanceData, const Span<const u8> materialTypedBytes)const noexcept{
    const usize uploadBytes = ECSRenderDetail::ResolveMaterialTypedUploadByteCount(materialTypedBytes);

    return materialPassDrawBuffersReady(instanceData.size(), uploadBytes);
}

b8 RendererMaterialSystem::materialPassDrawBuffersReady(const usize instanceCount, const usize materialTypedByteCount)const noexcept{
    return IsMaterialPassDrawBuffersReady(
        instanceCount,
        materialTypedByteCount,
        static_cast<b8>(m_materialState.m_instanceBuffer),
        m_materialState.m_instanceBufferCapacity,
        static_cast<b8>(m_materialState.m_materialTypedBuffer),
        m_materialState.m_materialTypedBufferCapacity
    );
}

ECSRenderDetail::MaterialPassBufferSnapshot RendererMaterialSystem::materialPassBufferSnapshot()const noexcept{
    return {
        .instanceBuffer = m_materialState.m_instanceBuffer,
        .materialTypedBuffer = m_materialState.m_materialTypedBuffer,
        .instanceBufferCapacity = m_materialState.m_instanceBufferCapacity,
        .materialTypedBufferCapacity = m_materialState.m_materialTypedBufferCapacity,
    };
}

void RendererMaterialSystem::prepareMaterialPassInstanceUploadData(const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources, const Span<InstanceGpuData> instanceData)noexcept{
    // Slot 6 carries CSG context for every raster instance; avoids a second local descriptor.
    const auto csgContextHeapSlot = csgResources.findClipContextHeapSlot();
    for(InstanceGpuData& instance : instanceData)
        instance.geometryHeapSlots[NWB_MESH_INSTANCE_CSG_CONTEXT_HEAP_SLOT] = csgContextHeapSlot.value_or(0u);
}

b8 RendererMaterialSystem::prepareMaterialPassResourceBindingsImpl(const Span<const MaterialPassDrawItem> drawItems, const b8 computeEmulation)noexcept{
    if(drawItems.empty())
        return true;

    b8 ready = true;
    for(const MaterialPassDrawItem& drawItem : drawItems){
        if(!ready)
            break;

        const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
        const MaterialPassPipelineResourceSnapshot& pipelineResources = drawItem.pipelineResources;

        if(!computeEmulation){
            ready = pipelineResources.meshletPipeline && mesh.valid();
            continue;
        }

        ready = pipelineResources.computePipeline
            && pipelineResources.emulationPipeline
            && mesh.valid()
            && mesh.emulationVertexBuffer
            && mesh.emulationVertexHeapHandle.valid()
            && mesh.emulationVertexHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
            && (!pipelineResources.indexedGeometryOutput || mesh.emulationIndexByteOffset != 0u)
        ;
    }
    return ready;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

