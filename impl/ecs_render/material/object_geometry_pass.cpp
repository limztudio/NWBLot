// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"

#include <impl/assets/graphics/mesh/names.h>
#include <impl/assets/graphics/mesh/object_geometry_constants.h>
#include <impl/assets_material/shader_stage_names.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/renderer_material_state.h>
#include <impl/ecs_render/mesh/mesh_compute_push_constants.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMaterialSystem::createObjectGeometryPipelineResources(
    const Name& meshShaderName,
    const AStringView variantName,
    MaterialPipelineResources& resources){
    if(!prepareMeshComputeBindingLayout())
        return false;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    // A single fixed engine decoder defines cache contents independently of the selected material or raster pass.
    if(!m_materialState.m_objectGeometryDecodePipeline){
        if(!m_shaderSystem.loadShader(
            m_materialState.m_objectGeometryDecodeShader,
            AssetsGraphicsMesh::s_ObjectGeometryDecodeShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Compute,
            "ECSRender_ObjectGeometryDecode"
        ))
            return false;
        Core::ComputePipelineDesc desc;
        desc
            .setComputeShader(m_materialState.m_objectGeometryDecodeShader)
            .addBindingLayout(m_materialState.m_computeBindingLayout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        m_materialState.m_objectGeometryDecodePipeline = device.createComputePipeline(desc);
        if(!m_materialState.m_objectGeometryDecodePipeline)
            return false;
    }
    if(!m_shaderSystem.loadShader(
        resources.objectGeometryVertexShader,
        meshShaderName,
        variantName,
        Core::ShaderType::Vertex,
        "ECSRender_ObjectGeometryVS",
        &MaterialShaderStageNames::s_MeshObjectVertexArchiveStageName
    ))
        return false;
    if(!m_materialState.m_objectGeometryInputLayout){
        Core::VertexAttributeDesc attributes[5];
        const Core::Format::Enum formats[] = {
            Core::Format::RGBA32_FLOAT, Core::Format::RGBA16_FLOAT, Core::Format::RGBA16_FLOAT,
            Core::Format::RG32_FLOAT, Core::Format::RGBA16_FLOAT,
        };
        const u32 offsets[] = {
            NWB_MESH_OBJECT_VERTEX_POSITION_BYTE_OFFSET, NWB_MESH_OBJECT_VERTEX_NORMAL_BYTE_OFFSET,
            NWB_MESH_OBJECT_VERTEX_TANGENT_BYTE_OFFSET, NWB_MESH_OBJECT_VERTEX_UV0_BYTE_OFFSET,
            NWB_MESH_OBJECT_VERTEX_COLOR_BYTE_OFFSET,
        };
        const char* const names[] = { "POSITION", "NORMAL", "TANGENT", "TEXCOORD", "COLOR" };
        for(u32 index = 0u; index < LengthOf(attributes); ++index){
            attributes[index]
                .setFormat(formats[index])
                .setBufferIndex(NWB_MESH_EMULATION_VERTEX_BUFFER_INDEX)
                .setOffset(offsets[index])
                .setElementStride(NWB_MESH_OBJECT_VERTEX_BYTE_SIZE)
                .setName(names[index])
            ;
        }
        m_materialState.m_objectGeometryInputLayout = device.createInputLayout(
            attributes,
            static_cast<u32>(LengthOf(attributes)),
            resources.objectGeometryVertexShader.get()
        );
        if(!m_materialState.m_objectGeometryInputLayout)
            return false;
    }
    resources.objectGeometryDecodePipeline = m_materialState.m_objectGeometryDecodePipeline;
    return true;
}

bool RendererMaterialSystem::recordObjectGeometryDecode(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItem& drawItem){
    const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
    const auto& cache = mesh.objectGeometryCache;
    const Core::ComputePipelineHandle& pipeline = drawItem.pipelineResources.objectGeometryDecodePipeline;
    if(
        !pipeline
        || pipeline != cache.decoderPipeline
        || !cache.valid()
        || !materialPassDrawResourcesReady(mesh, context.frameBindings)
    )
        return false;
    Core::ComputeState state;
    state.setPipeline(pipeline.get());
    context.commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(context.commandList, *pipeline.get());
    ECSRenderDetail::MeshFrameHeapSlots slots;
    slots.instance = context.frameBindings.instanceHeapHandle.slot();
    slots.materialTyped = context.frameBindings.materialTypedHeapHandle.slot();
    slots.view = context.frameBindings.meshView.heapHandle.slot();
    slots.generatedVertex = cache.heapHandle.slot();
    const ECSRenderDetail::ShaderDrivenPushConstants meshPush = ECSRenderDetail::BuildShaderDrivenPushConstants(
        mesh.meshletCount,
        drawItem.instanceIndex,
        drawItem.materialConstantByteOffset,
        context.viewportState,
        slots,
        0u
    );
    const ECSRenderDetail::MeshComputePushConstants push{
        .mesh = meshPush,
        .generatedIndexByteOffset = cache.indexByteOffset,
    };
    context.commandList.setPushConstants(&push, sizeof(push));
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_MeshDispatch, m_graphics.getDevice(), context.commandList);

        context.commandList.dispatch(Max(mesh.meshletCount, 1u));
    }
    return true;
}


bool RendererMaterialSystem::prepareIndexedMaterialPassResourceBindings(const MaterialPassDrawItemVector& drawItems){
    for(const MaterialPassDrawItem& draw : drawItems){
        const auto& cache = draw.meshResources.objectGeometryCache;
        if(
            !draw.meshResources.valid()
            || !draw.pipelineResources.indexedPipeline
            || !cache.valid()
            || cache.decoderPipeline != draw.pipelineResources.objectGeometryDecodePipeline
        )
            return false;
    }
    return true;
}

bool RendererMaterialSystem::indexedMaterialPassDrawResourcesReady(
    const MaterialPassDrawItemVector& drawItems,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings){
    return (drawItems.empty() || frameBindings.bindingValid()) && prepareIndexedMaterialPassResourceBindings(drawItems);
}

void RendererMaterialSystem::renderIndexedMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems){
    if(drawItems.empty())
        return;
    NWB_ASSERT(context.materialGeometryStatesGraphOwned);
    NWB_ASSERT(indexedMaterialPassDrawResourcesReady(drawItems, context.frameBindings));
    for(const MaterialPassDrawItem& drawItem : drawItems){
        const auto& mesh = drawItem.meshResources;
        const auto& cache = mesh.objectGeometryCache;
        const Core::GraphicsPipelineHandle& pipeline = drawItem.pipelineResources.indexedPipeline;
        NWB_ASSERT(drawItem.pipelineKey.csgMode == MaterialPipelineCsgMode::None);
        setMaterialPassDrawItemResourceStates(context, drawItem, mesh);
        Core::GraphicsState state;
        state.setPipeline(pipeline.get());
        state.setFramebuffer(context.framebuffer);
        state.setViewport(context.viewportState);
        state.addVertexBuffer(
            Core::VertexBufferBinding().setBuffer(cache.buffer.get()).setSlot(NWB_MESH_EMULATION_VERTEX_BUFFER_INDEX).setOffset(0u)
        );
        state.setIndexBuffer(
            Core::IndexBufferBinding().setBuffer(cache.buffer.get()).setFormat(Core::Format::R32_UINT).setOffset(cache.indexByteOffset)
        );
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

