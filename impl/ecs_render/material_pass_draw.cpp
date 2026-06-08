// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"
#include "timing_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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
#if defined(NWB_DEBUG)
    return mesh.valid() && drawState().m_instanceBuffer && drawState().m_meshViewBuffer && drawState().m_materialTypedBuffer;
#else
    static_cast<void>(mesh);
    return true;
#endif
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
        NWB_ASSERT(mesh);
        NWB_ASSERT(pipelineResources);
        if(!materialPassDrawResourcesReady(*mesh) || !pipelineResources->meshletPipeline || !mesh->meshBindingSet){
            return false;
        }

        const bool csgClipDraw = drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None;
        const bool readsCsgIntervalSample =
            csgClipDraw
            && MaterialPipelinePassUsesRendererCsgIntervalSample(drawItem.pipelineKey.pass)
        ;
        if(csgClipDraw && !csgState().m_clipBindingSet)
            return false;
        if(readsCsgIntervalSample && !csgState().m_intervalSampleBindingSet)
            return false;
        if(
            csgClipDraw
            && MaterialPipelinePassUsesRendererCsgReceiverSurfaceMask(drawItem.pipelineKey.pass)
            && !csgState().m_receiverSurfaceBindingSet
        )
            return false;
    }
    return true;
}

bool RendererMaterialSystem::computeMaterialPassDrawResourcesReady(const MaterialPassDrawItemVector& drawItems){
    if(drawItems.empty())
        return true;
    if(!drawState().m_emulationViewBindingSet)
        return false;

    for(const MaterialPassDrawItem& drawItem : drawItems){
        MeshResources* mesh = nullptr;
        MaterialPipelineResources* pipelineResources = nullptr;
        if(!findMaterialPassDrawItemResources(drawItem, mesh, pipelineResources))
            return false;
        NWB_ASSERT(mesh);
        NWB_ASSERT(pipelineResources);
        if(
            !materialPassDrawResourcesReady(*mesh)
            || !pipelineResources->computePipeline
            || !pipelineResources->emulationPipeline
            || !mesh->computeBindingSet
            || !mesh->emulationVertexBuffer
        ){
            return false;
        }

        const bool csgClipDraw = drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None;
        const bool readsCsgIntervalSample =
            csgClipDraw
            && MaterialPipelinePassUsesRendererCsgIntervalSample(drawItem.pipelineKey.pass)
        ;
        if(csgClipDraw && !csgState().m_clipBindingSet)
            return false;
        if(readsCsgIntervalSample && !csgState().m_intervalSampleBindingSet)
            return false;
        if(
            csgClipDraw
            && MaterialPipelinePassUsesRendererCsgReceiverSurfaceMask(drawItem.pipelineKey.pass)
            && !csgState().m_receiverSurfaceBindingSet
        )
            return false;
    }
    return true;
}

bool RendererMaterialSystem::prepareMaterialPassDrawResources(const MaterialPassDrawItems& drawItems){
    return prepareMeshMaterialPassDrawResources(drawItems.meshDrawItems)
        && prepareComputeMaterialPassDrawResources(drawItems.computeDrawItems)
    ;
}

bool RendererMaterialSystem::prepareMeshMaterialPassDrawResources(const MaterialPassDrawItemVector& drawItems){
    bool ready = true;
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
        if(!ready)
            return;
        NWB_ASSERT(pipelineResources.meshletPipeline);
        if(!pipelineResources.meshletPipeline){
            ready = false;
            return;
        }
        if(!mesh.meshBindingSet && !m_renderer.meshSystem().createMeshBindingSet(mesh)){
            ready = false;
            return;
        }

        const bool csgClipDraw = drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None;
        if(csgClipDraw && !csgState().m_clipBindingSet)
            ready = false;
        if(
            csgClipDraw
            && MaterialPipelinePassUsesRendererCsgReceiverSurfaceMask(drawItem.pipelineKey.pass)
            && !csgState().m_receiverSurfaceBindingSet
        )
            ready = false;
    });
    return ready;
}

bool RendererMaterialSystem::prepareComputeMaterialPassDrawResources(const MaterialPassDrawItemVector& drawItems){
    if(drawItems.empty())
        return true;
    if(!drawState().m_emulationViewBindingSet && !createEmulationViewResources())
        return false;

    bool ready = true;
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
        if(!ready)
            return;
        NWB_ASSERT(pipelineResources.computePipeline);
        NWB_ASSERT(pipelineResources.emulationPipeline);
        if(!pipelineResources.computePipeline || !pipelineResources.emulationPipeline){
            ready = false;
            return;
        }
        if(!mesh.computeBindingSet && !m_renderer.meshSystem().createComputeBindingSet(mesh)){
            ready = false;
            return;
        }

        const bool csgClipDraw = drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None;
        if(csgClipDraw && !csgState().m_clipBindingSet)
            ready = false;
        if(
            csgClipDraw
            && MaterialPipelinePassUsesRendererCsgReceiverSurfaceMask(drawItem.pipelineKey.pass)
            && !csgState().m_receiverSurfaceBindingSet
        )
            ready = false;
    });
    return ready;
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
    return flags;
}

void RendererMaterialSystem::setMaterialPassDrawPushConstants(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItem& drawItem,
    const MeshResources& mesh
){
    const u32 dispatchFlags = materialPassDrawDispatchFlags(context, drawItem, mesh);
    if(MaterialPipelinePassUsesRendererAvboit(context.pass)){
        ECSRenderDetail::SetTransparentDrawPushConstants(
            context.commandList,
            mesh.meshletCount,
            drawItem.instanceIndex,
            drawItem.materialConstantByteOffset,
            context.viewportState,
            *context.avboitTargets,
            dispatchFlags
        );
        return;
    }

    ECSRenderDetail::SetShaderDrivenPushConstants(
        context.commandList,
        mesh.meshletCount,
        drawItem.instanceIndex,
        drawItem.materialConstantByteOffset,
        context.viewportState,
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
        const bool csgClipDraw = drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None;
        const bool writesCsgReceiverSurfaceMask =
            csgClipDraw
            && MaterialPipelinePassUsesRendererCsgReceiverSurfaceMask(context.pass)
        ;
        const bool readsCsgIntervalSample =
            csgClipDraw
            && MaterialPipelinePassUsesRendererCsgIntervalSample(context.pass)
        ;
        const bool usesAvboit = MaterialPipelinePassUsesRendererAvboit(context.pass);
        NWB_ASSERT(mesh.meshBindingSet);
        NWB_ASSERT(!csgClipDraw || csgState().m_clipBindingSet);
        NWB_ASSERT(!writesCsgReceiverSurfaceMask || csgState().m_receiverSurfaceBindingSet);
        NWB_ASSERT(!readsCsgIntervalSample || csgState().m_intervalSampleBindingSet);

        setMaterialPassCommonBufferStates(context.commandList, mesh);
        if(csgClipDraw){
            m_renderer.csgSystem().setCsgClipBufferStates(context.commandList);
            if(writesCsgReceiverSurfaceMask)
                context.commandList.setResourceStatesForBindingSet(csgState().m_receiverSurfaceBindingSet.get());
            if(readsCsgIntervalSample)
                context.commandList.setResourceStatesForBindingSet(csgState().m_intervalSampleBindingSet.get());
        }

        Core::MeshletState meshletState;
        meshletState.setPipeline(pipelineResources.meshletPipeline.get());
        meshletState.setFramebuffer(context.framebuffer);
        meshletState.setViewport(context.viewportState);
        meshletState.addBindingSet(mesh.meshBindingSet.get());
        if(context.passBindingSet)
            meshletState.addBindingSet(context.passBindingSet);
        else if(csgClipDraw && usesAvboit)
            meshletState.addBindingSet(nullptr);
        if(csgClipDraw)
            meshletState.addBindingSet(csgState().m_clipBindingSet.get());
        if(writesCsgReceiverSurfaceMask)
            meshletState.addBindingSet(csgState().m_receiverSurfaceBindingSet.get());
        if(readsCsgIntervalSample)
            meshletState.addBindingSet(csgState().m_intervalSampleBindingSet.get());

        context.commandList.setMeshletState(meshletState);

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
    NWB_ASSERT(drawState().m_emulationViewBindingSet);

    const bool usesAvboit = MaterialPipelinePassUsesRendererAvboit(context.pass);
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
        NWB_ASSERT(materialPassDrawResourcesReady(mesh));
        NWB_ASSERT(pipelineResources.computePipeline);
        NWB_ASSERT(pipelineResources.emulationPipeline);
        const bool csgClipDraw = drawItem.pipelineKey.csgMode != MaterialPipelineCsgMode::None;
        const bool writesCsgReceiverSurfaceMask =
            csgClipDraw
            && MaterialPipelinePassUsesRendererCsgReceiverSurfaceMask(context.pass)
        ;
        const bool readsCsgIntervalSample =
            csgClipDraw
            && MaterialPipelinePassUsesRendererCsgIntervalSample(context.pass)
        ;
        NWB_ASSERT(mesh.computeBindingSet);
        NWB_ASSERT(mesh.emulationVertexBuffer);
        NWB_ASSERT(!csgClipDraw || csgState().m_clipBindingSet);
        NWB_ASSERT(!writesCsgReceiverSurfaceMask || csgState().m_receiverSurfaceBindingSet);
        NWB_ASSERT(!readsCsgIntervalSample || csgState().m_intervalSampleBindingSet);

        setMaterialPassCommonBufferStates(context.commandList, mesh);
        if(csgClipDraw){
            m_renderer.csgSystem().setCsgClipBufferStates(context.commandList);
            if(writesCsgReceiverSurfaceMask)
                context.commandList.setResourceStatesForBindingSet(csgState().m_receiverSurfaceBindingSet.get());
            if(readsCsgIntervalSample)
                context.commandList.setResourceStatesForBindingSet(csgState().m_intervalSampleBindingSet.get());
        }
        context.commandList.setBufferState(mesh.emulationVertexBuffer.get(), Core::ResourceStates::UnorderedAccess);

        Core::ComputeState computeState;
        computeState.setPipeline(pipelineResources.computePipeline.get());
        computeState.addBindingSet(mesh.computeBindingSet.get());
        if(csgClipDraw && usesAvboit)
            computeState.addBindingSet(nullptr);
        if(csgClipDraw)
            computeState.addBindingSet(csgState().m_clipBindingSet.get());

        context.commandList.setComputeState(computeState);

        ECSRenderDetail::SetShaderDrivenPushConstants(
            context.commandList,
            mesh.meshletCount,
            drawItem.instanceIndex,
            drawItem.materialConstantByteOffset,
            context.viewportState,
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
        if(usesAvboit){
            graphicsState.addBindingSet(csgClipDraw ? drawState().m_emulationViewBindingSet.get() : nullptr);
            graphicsState.addBindingSet(context.passBindingSet);
            if(csgClipDraw)
                graphicsState.addBindingSet(csgState().m_clipBindingSet.get());
            if(writesCsgReceiverSurfaceMask)
                graphicsState.addBindingSet(csgState().m_receiverSurfaceBindingSet.get());
            if(readsCsgIntervalSample)
                graphicsState.addBindingSet(csgState().m_intervalSampleBindingSet.get());
        }
        else{
            graphicsState.addBindingSet(drawState().m_emulationViewBindingSet.get());
            if(csgClipDraw){
                graphicsState.addBindingSet(csgState().m_clipBindingSet.get());
                if(writesCsgReceiverSurfaceMask)
                    graphicsState.addBindingSet(csgState().m_receiverSurfaceBindingSet.get());
                if(readsCsgIntervalSample)
                    graphicsState.addBindingSet(csgState().m_intervalSampleBindingSet.get());
            }
            else if(context.passBindingSet)
                graphicsState.addBindingSet(context.passBindingSet);
        }

        context.commandList.setGraphicsState(graphicsState);

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

