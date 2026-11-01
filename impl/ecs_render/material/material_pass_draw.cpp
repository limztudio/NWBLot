// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"

#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>

#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_pass_draw{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static MeshFrameHeapSlots ResolveMeshFrameHeapSlots(const MaterialPassDrawContext& context)noexcept{
    NWB_ASSERT(context.frameBindings.bindingValid());
    MeshFrameHeapSlots slots;
    slots.instance = context.frameBindings.instanceHeapHandle.slot();
    slots.materialTyped = context.frameBindings.materialTypedHeapHandle.slot();
    slots.view = context.frameBindings.meshView.heapHandle.slot();
    slots.generatedVertex = 0u;
    return slots;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererMaterialSystem::invalidateResources(){
    releaseMaterialResourceReferences();
    m_materialState.invalidateResources();
}

b8 RendererMaterialSystem::meshMaterialPassDrawResourcesReady(const Span<const MaterialPassDrawItem> drawItems, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)noexcept{
    for(const MaterialPassDrawItem& drawItem : drawItems){
        if(!materialPassDrawResourcesReady(drawItem.meshResources, frameBindings) || !drawItem.pipelineResources.meshletPipeline)
            return false;
    }
    return true;
}

b8 RendererMaterialSystem::computeMaterialPassDrawResourcesReady(const Span<const MaterialPassDrawItem> drawItems, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)noexcept{
    if(drawItems.empty())
        return true;

    for(const MaterialPassDrawItem& drawItem : drawItems){
        const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
        const MaterialPassPipelineResourceSnapshot& pipelineResources = drawItem.pipelineResources;
        if(
            !materialPassDrawResourcesReady(mesh, frameBindings)
            || !pipelineResources.computePipeline
            || !pipelineResources.emulationPipeline
            || !mesh.emulationVertexHeapHandle.valid()
            || !mesh.emulationVertexBuffer
            || (pipelineResources.indexedGeometryOutput && mesh.emulationIndexByteOffset == 0u)
        )
            return false;
    }
    return true;
}

void RendererMaterialSystem::setMaterialPassCommonBufferStates(const MaterialPassDrawContext& context, const MaterialPassMeshResourceSnapshot& mesh){
    if(!context.materialGeometryStatesGraphOwned){
        ForEachMaterialPassMeshSourceBuffer(mesh, [&](const Core::BufferHandle& buffer){
            context.commandList.setBufferState(buffer.get(), Core::ResourceStates::ShaderResource);
        });
    }
    if(!context.materialFrameStatesGraphOwned){
        NWB_ASSERT(context.frameBindings.bindingValid());
        context.commandList.setBufferState(context.frameBindings.instanceBuffer.get(), Core::ResourceStates::ShaderResource);
        context.commandList.setBufferState(context.frameBindings.meshView.buffer.get(), Core::ResourceStates::ConstantBuffer);
        context.commandList.setBufferState(context.frameBindings.materialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
    }
}

b8 RendererMaterialSystem::setMaterialPassDrawPushConstants(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MaterialPassMeshResourceSnapshot& mesh){
    const u32 dispatchFlags = ResolveMaterialPassDrawDispatchFlags(
        mesh.runtimeMesh,
        mesh.dynamicMeshletBoundsFresh,
        mesh.dynamicMeshletConesFresh,
        context.pass,
        drawItem.pipelineKey.twoSided,
        drawItem.meshletConeCullScaleSafe,
        drawItem.pipelineKey.csgMode,
        drawItem.shadingModelId
    );
    const MaterialPipelineCsgBindingUse csgBindingUse = MaterialPipelineResolveCsgBindingUse(drawItem.pipelineKey.csgMode, context.pass);
    u32 csgContextHeapSlot = 0u;
    if(csgBindingUse.clip){
        if(!context.csgResources)
            return false;
        const auto resolvedClipContext = context.csgResources->findClipContextHeapSlot();
        NWB_ASSERT(resolvedClipContext);
        if(!resolvedClipContext)
            return false;
        csgContextHeapSlot = *resolvedClipContext;
    }
    MeshFrameHeapSlots frameHeapSlots = __hidden_material_pass_draw::ResolveMeshFrameHeapSlots(context);
    if(MaterialPipelinePassUsesRendererAvboit(context.pass)){
        ECSRenderDetail::SetTransparentDrawPushConstants(
            context.commandList,
            mesh.meshletCount,
            drawItem.instanceIndex,
            drawItem.materialConstantByteOffset,
            context.viewportState,
            *context.avboitTargets,
            frameHeapSlots,
            dispatchFlags,
            m_graphics.isHDR10OutputActive(),
            csgContextHeapSlot,
            context.pass == MaterialPipelinePass::AvboitRefractionCapture
        );
        return true;
    }

    // Pixel-only CSG variants reuse the unused generated-vertex lane as the context selector; geometry reads it per instance.
    frameHeapSlots.generatedVertex = csgContextHeapSlot;

    ECSRenderDetail::SetShaderDrivenPushConstants(context.commandList, mesh.meshletCount, drawItem.instanceIndex, drawItem.materialConstantByteOffset, context.viewportState, frameHeapSlots, dispatchFlags);
    return true;
}

void RendererMaterialSystem::dispatchComputeMaterialPassDrawItem(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MaterialPassMeshResourceSnapshot& mesh, const MaterialPassPipelineResourceSnapshot& pipelineResources){
    Core::ComputeState computeState;
    computeState.setPipeline(pipelineResources.computePipeline.get());
    // Set 0 contains only the compute push range; the unified generated vertex/index UAV uses one global heap selector.

    context.commandList.setComputeState(computeState);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(context.commandList, *pipelineResources.computePipeline.get());

    MeshFrameHeapSlots frameHeapSlots = __hidden_material_pass_draw::ResolveMeshFrameHeapSlots(context);
    frameHeapSlots.generatedVertex = mesh.emulationVertexHeapHandle.slot();
    const b8 hasViewport = !context.viewportState.viewports.empty();
    const Core::Viewport viewport = hasViewport ? context.viewportState.viewports[0] : Core::Viewport{};
    const Core::Rect scissorRect = hasViewport ? (!context.viewportState.scissorRects.empty() ? context.viewportState.scissorRects[0] : Core::Rect::FromViewport(viewport)) : Core::Rect{};
    ShaderDrivenPushConstants pushConstants = ResolveShaderDrivenPushConstants(
        mesh.meshletCount,
        drawItem.instanceIndex,
        drawItem.materialConstantByteOffset,
        hasViewport,
        Float4(viewport.minX, viewport.minY, viewport.maxX, viewport.maxY),
        Float4(static_cast<f32>(scissorRect.minX), static_cast<f32>(scissorRect.minY), static_cast<f32>(scissorRect.maxX), static_cast<f32>(scissorRect.maxY)),
        frameHeapSlots,
        ResolveMaterialPassDrawDispatchFlags(
            mesh.runtimeMesh,
            mesh.dynamicMeshletBoundsFresh,
            mesh.dynamicMeshletConesFresh,
            context.pass,
            drawItem.pipelineKey.twoSided,
            drawItem.meshletConeCullScaleSafe,
            drawItem.pipelineKey.csgMode,
            drawItem.shadingModelId
        )
    );
    if(context.conservativeGeometryScissor){
        NWB_ASSERT(pipelineResources.sharedGeometryComputeProgram);
        pushConstants.dispatchFlags &= ~s_MeshDispatchFlagScissorCull;
    }
    if(pipelineResources.indexedGeometryOutput){
        NWB_ASSERT(mesh.emulationIndexByteOffset != 0u);
        const MeshComputePushConstants computePush{ .mesh = pushConstants, .generatedIndexByteOffset = mesh.emulationIndexByteOffset, };
        context.commandList.setPushConstants(&computePush, sizeof(computePush));
    }
    else
        context.commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_MeshDispatch, m_graphics.getDevice(), context.commandList);

        context.commandList.dispatch(mesh.meshletCount);
    }
}

void RendererMaterialSystem::drawComputeMaterialPassDrawItem(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MaterialPassMeshResourceSnapshot& mesh, const MaterialPassPipelineResourceSnapshot& pipelineResources){
    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(pipelineResources.emulationPipeline.get());
    graphicsState.setFramebuffer(context.framebuffer);
    graphicsState.setViewport(context.viewportState);
    graphicsState.addVertexBuffer(Core::VertexBufferBinding().setBuffer(mesh.emulationVertexBuffer.get()).setSlot(NWB_MESH_EMULATION_VERTEX_BUFFER_INDEX).setOffset(0));
    if(pipelineResources.indexedGeometryOutput){
        NWB_ASSERT(mesh.emulationIndexByteOffset != 0u);
        graphicsState.setIndexBuffer(Core::IndexBufferBinding().setBuffer(mesh.emulationVertexBuffer.get()).setFormat(Core::Format::R32_UINT).setOffset(mesh.emulationIndexByteOffset));
    }

    context.commandList.setGraphicsState(graphicsState);
    m_graphics.getDevice().getDescriptorHeap().bindGraphics(context.commandList, *pipelineResources.emulationPipeline.get());

    if(!setMaterialPassDrawPushConstants(context, drawItem, mesh))
        return;

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(mesh.meshletPrimitiveIndexCount);
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_Raster, m_graphics.getDevice(), context.commandList);

        if(pipelineResources.indexedGeometryOutput)
            context.commandList.drawIndexed(drawArgs);
        else
            context.commandList.draw(drawArgs);
    }
}

void RendererMaterialSystem::renderMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemsView& drawItems){
    renderMeshMaterialPassDrawItems(context, drawItems.meshDrawItems);
    renderIndexedMaterialPassDrawItems(context, drawItems.indexedDrawItems);
    if(context.emulationOutputEntryStateGraphOwned)
        renderComputeMaterialPassDrawItemsRasterOnly(context, drawItems.computeDrawItems);
    else
        renderComputeMaterialPassDrawItems(context, drawItems.computeDrawItems);
}

void RendererMaterialSystem::renderMeshMaterialPassDrawItems(const MaterialPassDrawContext& context, const Span<const MaterialPassDrawItem> drawItems){
    for(const MaterialPassDrawItem& drawItem : drawItems){
        const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
        const MaterialPassPipelineResourceSnapshot& pipelineResources = drawItem.pipelineResources;
        NWB_ASSERT(materialPassDrawResourcesReady(mesh, context.frameBindings));
        NWB_ASSERT(pipelineResources.meshletPipeline);
        setMaterialPassCommonBufferStates(context, mesh);

        Core::MeshletState meshletState;
        meshletState.setPipeline(pipelineResources.meshletPipeline.get());
        meshletState.setFramebuffer(context.framebuffer);
        meshletState.setViewport(context.viewportState);

        context.commandList.setMeshletState(meshletState);
        // Geometry streams are heap-backed for every raster material pass (opaque and AVBOIT alike).
        m_graphics.getDevice().getDescriptorHeap().bindGraphics(context.commandList, *pipelineResources.meshletPipeline.get());

        if(!setMaterialPassDrawPushConstants(context, drawItem, mesh))
            continue;
        {
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_MeshDispatch, m_graphics.getDevice(), context.commandList);

            context.commandList.dispatchMesh(mesh.meshletCount);
        }
    }
}

void RendererMaterialSystem::generateComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, const Span<const MaterialPassDrawItem> drawItems){
    if(drawItems.empty())
        return;
    // No local output transition here. A graph producer must have declared every selected buffer as a UAV first.
    NWB_ASSERT(context.emulationOutputEntryStateGraphOwned);
    NWB_ASSERT(context.frameBindings.meshView.buffer);

    for(const MaterialPassDrawItem& drawItem : drawItems){
        const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
        const MaterialPassPipelineResourceSnapshot& pipelineResources = drawItem.pipelineResources;
        NWB_ASSERT(materialPassDrawResourcesReady(mesh, context.frameBindings));
        NWB_ASSERT(pipelineResources.computePipeline);
        NWB_ASSERT(mesh.emulationVertexHeapHandle.valid());
        NWB_ASSERT(mesh.emulationVertexBuffer);
        setMaterialPassCommonBufferStates(context, mesh);
        dispatchComputeMaterialPassDrawItem(context, drawItem, mesh, pipelineResources);
    }
}

void RendererMaterialSystem::renderComputeMaterialPassDrawItemsRasterOnly(const MaterialPassDrawContext& context, const Span<const MaterialPassDrawItem> drawItems){
    if(drawItems.empty())
        return;
    // This half has no local output transition. The graph declares each unified generated buffer for vertex and index reads.
    NWB_ASSERT(context.emulationOutputEntryStateGraphOwned);
    NWB_ASSERT(context.frameBindings.meshView.buffer);

    for(const MaterialPassDrawItem& drawItem : drawItems){
        const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
        const MaterialPassPipelineResourceSnapshot& pipelineResources = drawItem.pipelineResources;
        NWB_ASSERT(materialPassDrawResourcesReady(mesh, context.frameBindings));
        NWB_ASSERT(pipelineResources.emulationPipeline);
        NWB_ASSERT(mesh.emulationVertexBuffer);
        setMaterialPassCommonBufferStates(context, mesh);
        drawComputeMaterialPassDrawItem(context, drawItem, mesh, pipelineResources);
    }
}

void RendererMaterialSystem::renderComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, const Span<const MaterialPassDrawItem> drawItems){
    if(drawItems.empty())
        return;
    NWB_ASSERT(!context.emulationOutputEntryStateGraphOwned);
    NWB_ASSERT(context.frameBindings.meshView.buffer);

    for(const MaterialPassDrawItem& drawItem : drawItems){
        const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;
        const MaterialPassPipelineResourceSnapshot& pipelineResources = drawItem.pipelineResources;
        NWB_ASSERT(materialPassDrawResourcesReady(mesh, context.frameBindings));
        NWB_ASSERT(pipelineResources.computePipeline);
        NWB_ASSERT(pipelineResources.emulationPipeline);
        NWB_ASSERT(mesh.emulationVertexHeapHandle.valid());
        NWB_ASSERT(mesh.emulationVertexBuffer);

        setMaterialPassCommonBufferStates(context, mesh);
        context.commandList.setBufferState(mesh.emulationVertexBuffer.get(), Core::ResourceStates::UnorderedAccess);
        // Compute-emulation runs the same heap-backed mesh runtime before the generated vertex buffer reaches the ordinary graphics raster stage.
        dispatchComputeMaterialPassDrawItem(context, drawItem, mesh, pipelineResources);

        context.commandList.setBufferState(mesh.emulationVertexBuffer.get(), ECSRenderDetail::s_ObjectGeometryRasterState);
        drawComputeMaterialPassDrawItem(context, drawItem, mesh, pipelineResources);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

