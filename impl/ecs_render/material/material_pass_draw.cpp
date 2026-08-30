// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"

#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/material_pass_csg_private.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/ecs_render/shared/renderer_state.h>

#include <core/graphics/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_pass_draw{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void SetCsgHeapResourceStates(
    RendererCsgSystem& csgSystem,
    Core::CommandList& commandList,
    const DeferredFrameTargets& deferredTargets,
    const ECSRenderDetail::CsgGraphResourceSnapshot* const csgResources,
    const MaterialPipelineCsgBindingUse& csgBindingUse,
    const bool receiverSurfaceImageStatesGraphOwned,
    const bool intervalSampleImageStatesGraphOwned,
    const bool csgClipBufferStatesGraphOwned
){
    if(!csgBindingUse.clip)
        return;

    NWB_ASSERT(csgResources);
    if(!csgResources)
        return;

    if(!csgClipBufferStatesGraphOwned)
        csgSystem.setCsgClipBufferStates(commandList, *csgResources);
    if(csgBindingUse.receiverSurface && !receiverSurfaceImageStatesGraphOwned){
        // Compatibility callers still stage the heap-selected receiver-event images themselves. The normal graph
        // declares this exact StorageImage pair before its receiver-surface task records.
        csgSystem.setCsgReceiverSurfaceImageStates(commandList, deferredTargets);
    }
    if(csgBindingUse.intervalSample && !intervalSampleImageStatesGraphOwned){
        // Cap/interval sampling loads through StorageImage aliases, so its heap descriptors require GENERAL rather
        // than the sampled-image shader-read layout. The normal opaque graph declares the same UAV use at its
        // combine-to-sample boundary; AVBOIT and direct compatibility callers retain this native bridge.
        csgSystem.setCsgIntervalSampleImageStates(commandList, deferredTargets);
    }
}

static bool ResolveMeshFrameHeapSlots(
    RendererMeshSystem& meshSystem,
    const MaterialPassDrawContext& context,
    ECSRenderDetail::MeshFrameHeapSlots& outSlots
){
    if(context.frameBindings){
        if(!context.frameBindings->bindingValid())
            return false;
        outSlots.instance = context.frameBindings->instanceHeapHandle.slot();
        outSlots.materialTyped = context.frameBindings->materialTypedHeapHandle.slot();
        outSlots.view = context.frameBindings->meshView.heapHandle.slot();
        outSlots.generatedVertex = 0u;
        return true;
    }

    // A null captured binding is reserved for native compatibility recording. Graph-owned entry states always
    // carry their retained generation and must never fall through to mutable RendererDrawState.
    if(context.materialFrameStatesGraphOwned || !meshSystem.meshFrameHeapHandlesReady())
        return false;
    meshSystem.populateMeshFrameHeapSlots(outSlots);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererMaterialSystem::setMaterialPassCommonBufferStates(
    const MaterialPassDrawContext& context,
    const MeshResources& mesh
){
    if(!context.materialGeometryStatesGraphOwned){
        RendererMeshSystem::forEachMeshSourceBuffer(mesh, [&](const u32, const Core::BufferHandle& buffer, const bool){
            context.commandList.setBufferState(buffer.get(), Core::ResourceStates::ShaderResource);
        });
    }
    if(!context.materialFrameStatesGraphOwned){
        if(context.frameBindings){
            NWB_ASSERT(context.frameBindings->bindingValid());
            context.commandList.setBufferState(
                context.frameBindings->instanceBuffer.get(),
                Core::ResourceStates::ShaderResource
            );
            context.commandList.setBufferState(
                context.frameBindings->meshView.buffer.get(),
                Core::ResourceStates::ConstantBuffer
            );
            context.commandList.setBufferState(
                context.frameBindings->materialTypedBuffer.get(),
                Core::ResourceStates::ShaderResource
            );
        }
        else{
            // Native compatibility recording deliberately follows the current Mesh-owned generation.
            context.commandList.setBufferState(m_drawState.m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
            context.commandList.setBufferState(m_drawState.m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
            context.commandList.setBufferState(m_drawState.m_materialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
        }
    }
}

bool RendererMaterialSystem::materialPassDrawResourcesReady(const MeshResources& mesh)const{
    return
        mesh.valid()
        && m_meshSystem.meshGeometryHeapHandlesReady(mesh)
        && m_meshSystem.meshFrameHeapHandlesReady()
        && m_drawState.m_instanceBuffer
        && m_drawState.m_meshViewBuffer
        && m_drawState.m_materialTypedBuffer
    ;
}

bool RendererMaterialSystem::materialPassDrawResourcesReady(
    const MeshResources& mesh,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings
)const{
    return
        mesh.valid()
        && m_meshSystem.meshGeometryHeapHandlesReady(mesh)
        && frameBindings.bindingValid()
    ;
}

bool RendererMaterialSystem::materialPassDrawResourcesReady(const MaterialPassDrawItems& drawItems){
    return meshMaterialPassDrawResourcesReady(drawItems.meshDrawItems)
        && computeMaterialPassDrawResourcesReady(drawItems.computeDrawItems)
    ;
}

bool RendererMaterialSystem::materialPassDrawResourcesReady(
    const MaterialPassDrawItems& drawItems,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings
){
    return meshMaterialPassDrawResourcesReady(drawItems.meshDrawItems, frameBindings)
        && computeMaterialPassDrawResourcesReady(drawItems.computeDrawItems, frameBindings)
    ;
}

bool RendererMaterialSystem::meshMaterialPassDrawResourcesReady(const MaterialPassDrawItemVector& drawItems){
    for(const MaterialPassDrawItem& drawItem : drawItems){
        MeshResources* mesh = nullptr;
        MaterialPipelineResources* pipelineResources = nullptr;
        if(!findMaterialPassDrawItemResources(drawItem, mesh, pipelineResources))
            return false;
        if(!materialPassDrawResourcesReady(*mesh) || !pipelineResources->meshletPipeline){
            return false;
        }
    }
    return true;
}

bool RendererMaterialSystem::meshMaterialPassDrawResourcesReady(
    const MaterialPassDrawItemVector& drawItems,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings
){
    for(const MaterialPassDrawItem& drawItem : drawItems){
        MeshResources* mesh = nullptr;
        MaterialPipelineResources* pipelineResources = nullptr;
        if(!findMaterialPassDrawItemResources(drawItem, mesh, pipelineResources))
            return false;
        if(!materialPassDrawResourcesReady(*mesh, frameBindings) || !pipelineResources->meshletPipeline)
            return false;
    }
    return true;
}

bool RendererMaterialSystem::computeMaterialPassDrawResourcesReady(const MaterialPassDrawItemVector& drawItems){
    if(drawItems.empty())
        return true;

    for(const MaterialPassDrawItem& drawItem : drawItems){
        MeshResources* mesh = nullptr;
        MaterialPipelineResources* pipelineResources = nullptr;
        if(!findMaterialPassDrawItemResources(drawItem, mesh, pipelineResources))
            return false;
        if(
            !materialPassDrawResourcesReady(*mesh)
            || !pipelineResources->computePipeline
            || !pipelineResources->emulationPipeline
            || !mesh->emulationVertexHeapHandle.valid()
            || !mesh->emulationVertexBuffer
        ){
            return false;
        }
    }
    return true;
}

bool RendererMaterialSystem::computeMaterialPassDrawResourcesReady(
    const MaterialPassDrawItemVector& drawItems,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings
){
    if(drawItems.empty())
        return true;

    for(const MaterialPassDrawItem& drawItem : drawItems){
        MeshResources* mesh = nullptr;
        MaterialPipelineResources* pipelineResources = nullptr;
        if(!findMaterialPassDrawItemResources(drawItem, mesh, pipelineResources))
            return false;
        if(
            !materialPassDrawResourcesReady(*mesh, frameBindings)
            || !pipelineResources->computePipeline
            || !pipelineResources->emulationPipeline
            || !mesh->emulationVertexHeapHandle.valid()
            || !mesh->emulationVertexBuffer
        )
            return false;
    }
    return true;
}

u32 RendererMaterialSystem::meshDispatchFlags(
    const MeshResources& mesh,
    const MaterialPipelinePass::Enum pass,
    const bool twoSided,
    const bool meshletConeCullScaleSafe
)const{
    u32 flags = 0u;
    const bool meshletBoundsFresh = !mesh.runtimeMesh || mesh.dynamicMeshletBoundsFresh;
    const bool meshletConesFresh = !mesh.runtimeMesh || mesh.dynamicMeshletConesFresh;
    // Runtime mesh providers own dynamic culling policy; the renderer only consumes the published freshness flags.
    if(meshletBoundsFresh)
        flags |= ECSRenderDetail::s_MeshDispatchFlagMeshletFrustumCull;
    if(meshletConesFresh && pass == MaterialPipelinePass::Opaque && !twoSided && meshletConeCullScaleSafe)
        flags |= ECSRenderDetail::s_MeshDispatchFlagMeshletConeCull;
    if(!mesh.runtimeMesh)
        flags |= ECSRenderDetail::s_MeshDispatchFlagCsgMeshletFullyRemovedCull;
    return flags;
}

u32 RendererMaterialSystem::materialPassDrawDispatchFlags(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItem& drawItem,
    const MeshResources& mesh
)const{
    u32 flags = meshDispatchFlags(
        mesh,
        context.pass,
        drawItem.pipelineKey.twoSided,
        drawItem.meshletConeCullScaleSafe
    );
    if(drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None)
        flags &= ~ECSRenderDetail::s_MeshDispatchFlagCsgMeshletFullyRemovedCull;
    // Carry the owning material's shading-model id in the high bits of the per-draw dispatch word; the G-buffer
    // pixel shader reads it back out and writes it into the base-color alpha for the deferred lighting dispatch.
    flags = (flags & NWB_MESH_DISPATCH_FLAG_MASK)
        | ((drawItem.shadingModelId & NWB_MESH_DISPATCH_SHADING_MODEL_MASK) << NWB_MESH_DISPATCH_SHADING_MODEL_SHIFT);
    return flags;
}

void RendererMaterialSystem::setMaterialPassDrawPushConstants(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItem& drawItem,
    const MeshResources& mesh
){
    const u32 dispatchFlags = materialPassDrawDispatchFlags(context, drawItem, mesh);
    const MaterialPipelineCsgBindingUse csgBindingUse =
        MaterialPipelineResolveCsgBindingUse(drawItem.pipelineKey, context.pass);
    u32 csgContextHeapSlot = 0u;
    const bool csgContextHeapSlotReady = !csgBindingUse.clip
        || (
            context.csgResources
            && context.csgResources->findClipContextHeapSlot(csgContextHeapSlot)
        )
    ;
    NWB_ASSERT(!csgBindingUse.clip || csgContextHeapSlotReady);
    if(!csgBindingUse.clip)
        csgContextHeapSlot = 0u;
    ECSRenderDetail::MeshFrameHeapSlots frameHeapSlots;
    const bool frameHeapSlotsReady = __hidden_material_pass_draw::ResolveMeshFrameHeapSlots(
        m_meshSystem,
        context,
        frameHeapSlots
    );
    NWB_ASSERT(frameHeapSlotsReady);
    if(!frameHeapSlotsReady)
        return;
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
            csgContextHeapSlot
        );
        return;
    }

    // CSG pixel-only variants reuse the otherwise-unused generated-vertex lane as their global context selector.
    // Mesh/compute geometry still reads the same selector from its per-instance retained slot.
    frameHeapSlots.generatedVertex = csgContextHeapSlot;

    ECSRenderDetail::SetShaderDrivenPushConstants(
        context.commandList,
        mesh.meshletCount,
        drawItem.instanceIndex,
        drawItem.materialConstantByteOffset,
        context.viewportState,
        frameHeapSlots,
        dispatchFlags
    );
}

void RendererMaterialSystem::setMaterialPassDrawItemResourceStates(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItem& drawItem,
    const MeshResources& mesh
){
    const MaterialPipelineCsgBindingUse csgBindingUse =
        MaterialPipelineResolveCsgBindingUse(drawItem.pipelineKey, context.pass);

    setMaterialPassCommonBufferStates(context, mesh);
    __hidden_material_pass_draw::SetCsgHeapResourceStates(
        m_csgSystem,
        context.commandList,
        context.deferredTargets,
        context.csgResources,
        csgBindingUse,
        context.csgReceiverSurfaceImageStatesGraphOwned,
        context.csgIntervalSampleImageStatesGraphOwned,
        context.csgClipBufferStatesGraphOwned
    );
}

void RendererMaterialSystem::dispatchComputeMaterialPassDrawItem(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItem& drawItem,
    const MeshResources& mesh,
    MaterialPipelineResources& pipelineResources
){
    Core::ComputeState computeState;
    computeState.setPipeline(pipelineResources.computePipeline.get());
    // Set 0 contains the shared push range only; every resource, including this mesh's graph-provided writable
    // generated-vertex buffer, is selected through the global descriptor heap.

    context.commandList.setComputeState(computeState);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(context.commandList, *pipelineResources.computePipeline.get());

    ECSRenderDetail::MeshFrameHeapSlots frameHeapSlots;
    const bool frameHeapSlotsReady = __hidden_material_pass_draw::ResolveMeshFrameHeapSlots(
        m_meshSystem,
        context,
        frameHeapSlots
    );
    NWB_ASSERT(frameHeapSlotsReady);
    if(!frameHeapSlotsReady)
        return;
    frameHeapSlots.generatedVertex = mesh.emulationVertexHeapHandle.slot();
    ECSRenderDetail::SetShaderDrivenPushConstants(
        context.commandList,
        mesh.meshletCount,
        drawItem.instanceIndex,
        drawItem.materialConstantByteOffset,
        context.viewportState,
        frameHeapSlots,
        materialPassDrawDispatchFlags(context, drawItem, mesh)
    );
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_MeshDispatch, m_graphics.getDevice(), context.commandList);

        context.commandList.dispatch(mesh.meshletCount);
    }
}

void RendererMaterialSystem::drawComputeMaterialPassDrawItem(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItem& drawItem,
    const MeshResources& mesh,
    MaterialPipelineResources& pipelineResources
){
    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(pipelineResources.emulationPipeline.get());
    graphicsState.setFramebuffer(context.framebuffer);
    graphicsState.setViewport(context.viewportState);
    graphicsState.addVertexBuffer(
        Core::VertexBufferBinding()
            .setBuffer(mesh.emulationVertexBuffer.get())
            .setSlot(NWB_MESH_EMULATION_VERTEX_BUFFER_INDEX)
            .setOffset(0)
    );

    context.commandList.setGraphicsState(graphicsState);
    m_graphics.getDevice().getDescriptorHeap().bindGraphics(context.commandList, *pipelineResources.emulationPipeline.get());

    setMaterialPassDrawPushConstants(context, drawItem, mesh);

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(mesh.meshletPrimitiveIndexCount);
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_Raster, m_graphics.getDevice(), context.commandList);

        context.commandList.draw(drawArgs);
    }
}

void RendererMaterialSystem::renderMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItems& drawItems
){
    renderMeshMaterialPassDrawItems(context, drawItems.meshDrawItems);
    if(context.emulationOutputEntryStateGraphOwned)
        renderComputeMaterialPassDrawItemsRasterOnly(context, drawItems.computeDrawItems);
    else
        renderComputeMaterialPassDrawItems(context, drawItems.computeDrawItems);
}

void RendererMaterialSystem::renderMeshMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
        NWB_ASSERT(context.frameBindings
            ? materialPassDrawResourcesReady(mesh, *context.frameBindings)
            : materialPassDrawResourcesReady(mesh));
        NWB_ASSERT(pipelineResources.meshletPipeline);
        setMaterialPassDrawItemResourceStates(context, drawItem, mesh);

        Core::MeshletState meshletState;
        meshletState.setPipeline(pipelineResources.meshletPipeline.get());
        meshletState.setFramebuffer(context.framebuffer);
        meshletState.setViewport(context.viewportState);

        context.commandList.setMeshletState(meshletState);
        // Geometry streams are heap-backed for every raster material pass (opaque and AVBOIT alike).
        m_graphics.getDevice().getDescriptorHeap().bindGraphics(context.commandList, *pipelineResources.meshletPipeline.get());

        setMaterialPassDrawPushConstants(context, drawItem, mesh);
        {
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_MeshDispatch, m_graphics.getDevice(), context.commandList);

            context.commandList.dispatchMesh(mesh.meshletCount);
        }
    });
}

void RendererMaterialSystem::generateComputeMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    if(drawItems.empty())
        return;
    // This half deliberately has no local output transition. A graph producer must have declared every selected
    // generated-vertex buffer as a UAV before recording it.
    NWB_ASSERT(context.emulationOutputEntryStateGraphOwned);
    NWB_ASSERT(context.frameBindings ? context.frameBindings->meshView.buffer : m_drawState.m_meshViewBuffer);

    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
        NWB_ASSERT(context.frameBindings
            ? materialPassDrawResourcesReady(mesh, *context.frameBindings)
            : materialPassDrawResourcesReady(mesh));
        NWB_ASSERT(pipelineResources.computePipeline);
        NWB_ASSERT(mesh.emulationVertexHeapHandle.valid());
        NWB_ASSERT(mesh.emulationVertexBuffer);
        setMaterialPassDrawItemResourceStates(context, drawItem, mesh);
        dispatchComputeMaterialPassDrawItem(context, drawItem, mesh, pipelineResources);
    });
}

void RendererMaterialSystem::renderComputeMaterialPassDrawItemsRasterOnly(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    if(drawItems.empty())
        return;
    // This half deliberately has no local output transition. A graph consumer must have declared every selected
    // generated-vertex buffer as a VertexBuffer before recording it.
    NWB_ASSERT(context.emulationOutputEntryStateGraphOwned);
    NWB_ASSERT(context.frameBindings ? context.frameBindings->meshView.buffer : m_drawState.m_meshViewBuffer);

    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
        NWB_ASSERT(context.frameBindings
            ? materialPassDrawResourcesReady(mesh, *context.frameBindings)
            : materialPassDrawResourcesReady(mesh));
        NWB_ASSERT(pipelineResources.emulationPipeline);
        NWB_ASSERT(mesh.emulationVertexBuffer);
        setMaterialPassDrawItemResourceStates(context, drawItem, mesh);
        drawComputeMaterialPassDrawItem(context, drawItem, mesh, pipelineResources);
    });
}

void RendererMaterialSystem::renderComputeMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    if(drawItems.empty())
        return;
    NWB_ASSERT(!context.emulationOutputEntryStateGraphOwned);
    NWB_ASSERT(context.frameBindings ? context.frameBindings->meshView.buffer : m_drawState.m_meshViewBuffer);

    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
        NWB_ASSERT(context.frameBindings
            ? materialPassDrawResourcesReady(mesh, *context.frameBindings)
            : materialPassDrawResourcesReady(mesh));
        NWB_ASSERT(pipelineResources.computePipeline);
        NWB_ASSERT(pipelineResources.emulationPipeline);
        NWB_ASSERT(mesh.emulationVertexHeapHandle.valid());
        NWB_ASSERT(mesh.emulationVertexBuffer);

        setMaterialPassDrawItemResourceStates(context, drawItem, mesh);
        context.commandList.setBufferState(mesh.emulationVertexBuffer.get(), Core::ResourceStates::UnorderedAccess);
        // Compute-emulation runs the same heap-backed mesh runtime before the generated vertex buffer reaches the
        // ordinary graphics raster stage.
        dispatchComputeMaterialPassDrawItem(context, drawItem, mesh, pipelineResources);

        context.commandList.setBufferState(mesh.emulationVertexBuffer.get(), Core::ResourceStates::VertexBuffer);
        drawComputeMaterialPassDrawItem(context, drawItem, mesh, pipelineResources);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

