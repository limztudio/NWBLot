// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/renderer_private.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/material_pass_csg_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_pass_draw{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void SetCsgHeapResourceStates(
    RendererCsgSystem& csgSystem,
    Core::CommandList& commandList,
    const DeferredFrameTargets& targets,
    const MaterialPipelineCsgBindingUse& csgBindingUse
){
    if(!csgBindingUse.clip)
        return;

    csgSystem.setCsgClipBufferStates(commandList);
    if(csgBindingUse.receiverSurface){
        // Receiver-event images are heap-selected, so preserve their pixel-UAV states explicitly.
        commandList.setTextureState(targets.csgReceiverEventData.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.csgReceiverEventCount.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    }
    if(csgBindingUse.intervalSample){
        // Cap/interval sampling loads through StorageImage aliases, so its heap descriptors require GENERAL rather
        // than the sampled-image shader-read layout.
        commandList.setTextureState(targets.csgRemovedIntervalDepth.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.csgRemovedIntervalCapNormal.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.csgRemovedIntervalData.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.csgRemovedIntervalCount.get(), Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererMaterialSystem::setMaterialPassCommonBufferStates(
    Core::CommandList& commandList,
    const MeshResources& mesh
){
    RendererMeshSystem::forEachMeshSourceBuffer(mesh, [&](const u32, const Core::BufferHandle& buffer, const bool){
        commandList.setBufferState(buffer.get(), Core::ResourceStates::ShaderResource);
    });
    commandList.setBufferState(drawState().m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(drawState().m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.setBufferState(drawState().m_materialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
}

bool RendererMaterialSystem::materialPassDrawResourcesReady(const MeshResources& mesh)const{
    return
        mesh.valid()
        && m_renderer.meshSystem().meshGeometryHeapHandlesReady(mesh)
        && m_renderer.meshSystem().meshFrameHeapHandlesReady()
        && drawState().m_instanceBuffer
        && drawState().m_meshViewBuffer
        && drawState().m_materialTypedBuffer
    ;
}

bool RendererMaterialSystem::materialPassDrawResourcesReady(const MaterialPassDrawItems& drawItems){
    return meshMaterialPassDrawResourcesReady(drawItems.meshDrawItems)
        && computeMaterialPassDrawResourcesReady(drawItems.computeDrawItems)
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
    const u32 csgContextHeapSlot = csgBindingUse.clip
        ? csgState().m_clipContextSlotsHeapHandle.slot()
        : 0u
    ;
    NWB_ASSERT(!csgBindingUse.clip || csgState().m_clipContextSlotsHeapHandle.valid());
    ECSRenderDetail::MeshFrameHeapSlots frameHeapSlots;
    m_renderer.meshSystem().populateMeshFrameHeapSlots(frameHeapSlots);
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

void RendererMaterialSystem::renderMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItems& drawItems
){
    renderMeshMaterialPassDrawItems(context, drawItems.meshDrawItems);
    renderComputeMaterialPassDrawItems(context, drawItems.computeDrawItems);
}

void RendererMaterialSystem::renderMeshMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
        NWB_ASSERT(materialPassDrawResourcesReady(mesh));
        NWB_ASSERT(pipelineResources.meshletPipeline);
        const MaterialPipelineCsgBindingUse csgBindingUse =
            MaterialPipelineResolveCsgBindingUse(drawItem.pipelineKey, context.pass);

        setMaterialPassCommonBufferStates(context.commandList, mesh);
        __hidden_material_pass_draw::SetCsgHeapResourceStates(
            m_renderer.csgSystem(),
            context.commandList,
            deferredState().m_targets,
            csgBindingUse
        );

        Core::MeshletState meshletState;
        meshletState.setPipeline(pipelineResources.meshletPipeline.get());
        meshletState.setFramebuffer(context.framebuffer);
        meshletState.setViewport(context.viewportState);

        context.commandList.setMeshletState(meshletState);
        // Geometry streams are heap-backed for every raster material pass (opaque and AVBOIT alike).
        graphics().getDevice()->getDescriptorHeap().bindGraphics(context.commandList, *pipelineResources.meshletPipeline.get());

        setMaterialPassDrawPushConstants(context, drawItem, mesh);
        {
            Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_MeshDispatch, graphics().getDevice(), context.commandList);

            context.commandList.dispatchMesh(mesh.meshletCount);
        }
    });
}

void RendererMaterialSystem::renderComputeMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    if(drawItems.empty())
        return;
    NWB_ASSERT(drawState().m_meshViewBuffer);

    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
        NWB_ASSERT(materialPassDrawResourcesReady(mesh));
        NWB_ASSERT(pipelineResources.computePipeline);
        NWB_ASSERT(pipelineResources.emulationPipeline);
        const MaterialPipelineCsgBindingUse csgBindingUse =
            MaterialPipelineResolveCsgBindingUse(drawItem.pipelineKey, context.pass);
        NWB_ASSERT(mesh.emulationVertexHeapHandle.valid());
        NWB_ASSERT(mesh.emulationVertexBuffer);

        setMaterialPassCommonBufferStates(context.commandList, mesh);
        __hidden_material_pass_draw::SetCsgHeapResourceStates(
            m_renderer.csgSystem(),
            context.commandList,
            deferredState().m_targets,
            csgBindingUse
        );
        context.commandList.setBufferState(mesh.emulationVertexBuffer.get(), Core::ResourceStates::UnorderedAccess);

        Core::ComputeState computeState;
        computeState.setPipeline(pipelineResources.computePipeline.get());
        // Set 0 contains the shared push range only; every resource, including this mesh's writable generated-vertex
        // buffer, is selected through the global descriptor heap.

        context.commandList.setComputeState(computeState);
        // Compute-emulation runs the same heap-backed mesh runtime before the generated vertex buffer reaches the
        // ordinary graphics raster stage.
        graphics().getDevice()->getDescriptorHeap().bindCompute(context.commandList, *pipelineResources.computePipeline.get());

        ECSRenderDetail::MeshFrameHeapSlots frameHeapSlots;
        m_renderer.meshSystem().populateMeshFrameHeapSlots(frameHeapSlots);
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
            Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_MeshDispatch, graphics().getDevice(), context.commandList);

            context.commandList.dispatch(mesh.meshletCount);
        }

        context.commandList.setBufferState(mesh.emulationVertexBuffer.get(), Core::ResourceStates::VertexBuffer);

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
        graphics().getDevice()->getDescriptorHeap().bindGraphics(context.commandList, *pipelineResources.emulationPipeline.get());

        setMaterialPassDrawPushConstants(context, drawItem, mesh);

        Core::DrawArguments drawArgs;
        drawArgs.setVertexCount(mesh.meshletPrimitiveIndexCount);
        {
            Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_Raster, graphics().getDevice(), context.commandList);

            context.commandList.draw(drawArgs);
        }
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

