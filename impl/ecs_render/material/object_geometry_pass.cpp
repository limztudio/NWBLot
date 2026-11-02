// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"

#include <impl/assets/graphics/mesh/object_geometry_constants.h>
#include <impl/assets_graphics/components.adhd.h>
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/renderer_material_state.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererMaterialSystem::createObjectGeometryPipelineResources(MaterialPipelineResources& resources){
    if(!prepareMeshComputeBindingLayout())
        return false;
    auto& device = m_graphics.getDevice();
    // A single fixed engine decoder defines cache contents independently of the selected material or raster pass.
    if(!m_materialState.m_objectGeometryDecodePipeline){
        if(!m_shaderSystem.loadShader<ComputeShader>(&MeshSubsystem::objectDecodeCs, Core::ShaderArchive::s_DefaultVariant, s_ObjectGeometryDecodeDebugName, m_materialState.m_objectGeometryDecodeShader))
            return false;
        if(!RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_materialState.m_objectGeometryDecodeShader, m_materialState.m_computeBindingLayout, false, m_materialState.m_objectGeometryDecodePipeline))
            return false;
    }
    if(!m_shaderSystem.loadShader<VertexShader>(&MeshSubsystem::objectVs, Core::ShaderArchive::s_DefaultVariant, s_ObjectGeometryVsDebugName, resources.objectGeometryVertexShader))
        return false;
    if(!m_materialState.m_objectGeometryInputLayout){
        Array<Core::VertexAttributeDesc, s_ObjectGeometryVertexAttributeCount> attributes{};
        const Array<Core::Format::Enum, 5u> formats = { Core::Format::RGBA32_FLOAT, Core::Format::RGBA16_FLOAT, Core::Format::RGBA16_FLOAT, Core::Format::RG32_FLOAT, Core::Format::RGBA16_FLOAT, };
        const Array<u32, 5u> offsets = { NWB_MESH_OBJECT_VERTEX_POSITION_BYTE_OFFSET, NWB_MESH_OBJECT_VERTEX_NORMAL_BYTE_OFFSET, NWB_MESH_OBJECT_VERTEX_TANGENT_BYTE_OFFSET, NWB_MESH_OBJECT_VERTEX_UV0_BYTE_OFFSET, NWB_MESH_OBJECT_VERTEX_COLOR_BYTE_OFFSET, };
        const Array<Name, 5u> names = { s_VertexAttributePositionName, s_VertexAttributeNormalName, s_VertexAttributeTangentName, s_VertexAttributeTexCoordName, s_VertexAttributeColorName };
        for(u32 index = 0u; index < attributes.size(); ++index)
            attributes[index].setFormat(formats[index]).setBufferIndex(NWB_MESH_EMULATION_VERTEX_BUFFER_INDEX).setOffset(offsets[index]).setElementStride(NWB_MESH_OBJECT_VERTEX_BYTE_SIZE).setName(names[index]);
        auto objectGeometryInputLayoutResult = device.createInputLayout(attributes.data(), static_cast<u32>(attributes.size()), resources.objectGeometryVertexShader.get());
        if(objectGeometryInputLayoutResult)
            m_materialState.m_objectGeometryInputLayout = Move(*objectGeometryInputLayoutResult);
        else
            m_materialState.m_objectGeometryInputLayout.reset();
        if(!objectGeometryInputLayoutResult)
            return false;
    }
    resources.objectGeometryDecodePipeline = m_materialState.m_objectGeometryDecodePipeline;
    return true;
}

b8 RendererMaterialSystem::prepareIndexedMaterialPassResourceBindings(const Span<const MaterialPassDrawItem> drawItems)noexcept{
    for(const MaterialPassDrawItem& draw : drawItems){
        const auto& cache = draw.meshResources.objectGeometryCache;
        if(!draw.meshResources.valid() || !draw.pipelineResources.indexedPipeline || !cache.valid() || cache.decoderPipeline != draw.pipelineResources.objectGeometryDecodePipeline)
            return false;
    }
    return true;
}

void RendererMaterialSystem::renderIndexedMaterialPassDrawItems(const MaterialPassDrawContext& context, const Span<const MaterialPassDrawItem> drawItems){
    if(drawItems.empty())
        return;
    NWB_ASSERT(context.materialGeometryStatesGraphOwned);
    NWB_ASSERT(indexedMaterialPassDrawResourcesReady(drawItems, context.frameBindings));
    for(const MaterialPassDrawItem& drawItem : drawItems){
        const auto& mesh = drawItem.meshResources;
        const auto& cache = mesh.objectGeometryCache;
        const Core::GraphicsPipelineHandle& pipeline = drawItem.pipelineResources.indexedPipeline;
        setMaterialPassCommonBufferStates(context, mesh);
        Core::GraphicsState state;
        state.setPipeline(pipeline.get());
        state.setFramebuffer(context.framebuffer);
        state.setViewport(context.viewportState);
        state.addVertexBuffer(Core::VertexBufferBinding().setBuffer(cache.buffer.get()).setSlot(NWB_MESH_EMULATION_VERTEX_BUFFER_INDEX).setOffset(0u));
        state.setIndexBuffer(Core::IndexBufferBinding().setBuffer(cache.buffer.get()).setFormat(Core::Format::R32_UINT).setOffset(cache.indexByteOffset));
        context.commandList.setGraphicsState(state);
        m_graphics.getDevice().getDescriptorHeap().bindGraphics(context.commandList, *pipeline.get());
        if(!setMaterialPassDrawPushConstants(context, drawItem, mesh))
            continue;
        Core::DrawArguments arguments;
        arguments.setVertexCount(cache.indexCount);
        {
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_Raster, m_graphics.getDevice(), context.commandList);

            context.commandList.drawIndexed(arguments);
        }
    }
}

b8 RendererMaterialSystem::recordObjectGeometryDecode(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem){
    const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
    const auto& cache = mesh.objectGeometryCache;
    const Core::ComputePipelineHandle& pipeline = drawItem.pipelineResources.objectGeometryDecodePipeline;
    if(!pipeline || pipeline != cache.decoderPipeline || !cache.valid() || !materialPassDrawResourcesReady(mesh, context.frameBindings))
        return false;
    Core::ComputeState state;
    state.setPipeline(pipeline.get());
    context.commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(context.commandList, *pipeline.get());
    const MeshFrameHeapSlots slots = ECSRenderDetail::ResolveMeshFrameHeapSlots(context.frameBindings, cache.heapHandle.slot());
    const ShaderDrivenPushConstants meshPush = ECSRenderDetail::BuildShaderDrivenPushConstants(
        mesh.meshletCount,
        drawItem.instanceIndex,
        drawItem.materialConstantByteOffset,
        context.viewportState,
        slots,
        0u
    );
    const MeshComputePushConstants push{ .mesh = meshPush, .generatedIndexByteOffset = cache.indexByteOffset, };
    context.commandList.setPushConstants(&push, sizeof(push));
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_MeshDispatch, m_graphics.getDevice(), context.commandList);

        context.commandList.dispatch(Max(mesh.meshletCount, 1u));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

