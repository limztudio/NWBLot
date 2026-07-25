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


static void AssertCsgBindingSetsReady(
    [[maybe_unused]] const MaterialPipelineCsgBindingUse& csgBindingUse,
    [[maybe_unused]] const MaterialPassCsgBindingSets& bindingSets
){
    NWB_ASSERT(!csgBindingUse.clip || bindingSets.clip);
    NWB_ASSERT(!csgBindingUse.receiverSurface || bindingSets.receiverSurface);
    NWB_ASSERT(!csgBindingUse.intervalSample || bindingSets.intervalSample);
}

static void SetCsgBindingSetResourceStates(
    RendererCsgSystem& csgSystem,
    Core::CommandList& commandList,
    const MaterialPipelineCsgBindingUse& csgBindingUse,
    const MaterialPassCsgBindingSets& bindingSets
){
    if(!csgBindingUse.clip)
        return;

    csgSystem.setCsgClipBufferStates(commandList);
    if(csgBindingUse.receiverSurface)
        commandList.setResourceStatesForBindingSet(bindingSets.receiverSurface.get());
    if(csgBindingUse.intervalSample)
        commandList.setResourceStatesForBindingSet(bindingSets.intervalSample.get());
}

[[nodiscard]] constexpr bool UsesBindlessAvboitDepth(
    const MaterialPipelinePass::Enum pass,
    const MaterialPipelineCsgBindingUse& csgBindingUse
){
    return !csgBindingUse.clip
        && (pass == MaterialPipelinePass::AvboitOccupancy || pass == MaterialPipelinePass::AvboitExtinction)
    ;
}

[[nodiscard]] Core::BindingSet* ResolvePassBindingSet(
    const MaterialPassDrawContext& context,
    const MaterialPipelineCsgBindingUse& csgBindingUse
){
    if(!csgBindingUse.clip || !MaterialPipelinePassUsesRendererAvboit(context.pass) || !context.avboitTargets)
        return context.passBindingSet;

    switch(context.pass){
    case MaterialPipelinePass::AvboitOccupancy:
        return context.avboitTargets->csgOccupancyBindingSet.get();
    case MaterialPipelinePass::AvboitExtinction:
        return context.avboitTargets->csgExtinctionBindingSet.get();
    default:
        return context.passBindingSet;
    }
}

template<typename GraphicsState>
static void AddCsgGraphicsBindingSets(
    GraphicsState& graphicsState,
    const MaterialPipelineCsgBindingUse& csgBindingUse,
    const MaterialPassCsgBindingSets& bindingSets
){
    if(csgBindingUse.clip)
        graphicsState.addBindingSet(bindingSets.clip.get());
    if(csgBindingUse.receiverSurface)
        graphicsState.addBindingSet(bindingSets.receiverSurface.get());
    if(csgBindingUse.intervalSample)
        graphicsState.addBindingSet(bindingSets.intervalSample.get());
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
#if defined(NWB_DEBUG)
    return mesh.valid() && drawState().m_instanceBuffer && drawState().m_meshViewBuffer && drawState().m_materialTypedBuffer;
#else
    static_cast<void>(mesh);
    return true;
#endif
}

bool RendererMaterialSystem::materialPassDrawResourcesReady(const MaterialPassDrawItems& drawItems){
    const MaterialPassCsgBindingSets csgBindingSets{
        csgState().m_clipBindingSet,
        csgState().m_receiverSurfaceBindingSet,
        csgState().m_intervalSampleBindingSet
    };
    return meshMaterialPassDrawResourcesReady(drawItems.meshDrawItems, csgBindingSets)
        && computeMaterialPassDrawResourcesReady(drawItems.computeDrawItems, csgBindingSets)
    ;
}

bool RendererMaterialSystem::meshMaterialPassDrawResourcesReady(
    const MaterialPassDrawItemVector& drawItems,
    const MaterialPassCsgBindingSets& csgBindingSets
){
    for(const MaterialPassDrawItem& drawItem : drawItems){
        MeshResources* mesh = nullptr;
        MaterialPipelineResources* pipelineResources = nullptr;
        if(!findMaterialPassDrawItemResources(drawItem, mesh, pipelineResources))
            return false;
        const MaterialPipelineCsgBindingUse csgBindingUse =
            MaterialPipelineResolveCsgBindingUse(drawItem.pipelineKey, drawItem.pipelineKey.pass)
        ;
        const bool usesBindlessAvboitDepth = __hidden_material_pass_draw::UsesBindlessAvboitDepth(
            drawItem.pipelineKey.pass,
            csgBindingUse
        );
        const Core::BindingSetHandle& meshBindingSet = usesBindlessAvboitDepth
            ? mesh->bindlessMeshBindingSet
            : mesh->meshBindingSet
        ;
        if(!materialPassDrawResourcesReady(*mesh) || !pipelineResources->meshletPipeline || !meshBindingSet){
            return false;
        }

        if(!MaterialPassCsgResourcesReadyForPipelineKey(
            drawItem.pipelineKey,
            drawItem.pipelineKey.pass,
            csgBindingSets,
            true
        ))
            return false;
    }
    return true;
}

bool RendererMaterialSystem::computeMaterialPassDrawResourcesReady(
    const MaterialPassDrawItemVector& drawItems,
    const MaterialPassCsgBindingSets& csgBindingSets
){
    if(drawItems.empty())
        return true;
    if(!drawState().m_emulationViewBindingSet || !drawState().m_bindlessEmulationViewBindingSet)
        return false;

    for(const MaterialPassDrawItem& drawItem : drawItems){
        MeshResources* mesh = nullptr;
        MaterialPipelineResources* pipelineResources = nullptr;
        if(!findMaterialPassDrawItemResources(drawItem, mesh, pipelineResources))
            return false;
        if(
            !materialPassDrawResourcesReady(*mesh)
            || !pipelineResources->computePipeline
            || !pipelineResources->emulationPipeline
            || !mesh->computeBindingSet
            || !mesh->emulationVertexBuffer
        ){
            return false;
        }

        if(!MaterialPassCsgResourcesReadyForPipelineKey(
            drawItem.pipelineKey,
            drawItem.pipelineKey.pass,
            csgBindingSets,
            true
        ))
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
    const MaterialPassCsgBindingSets csgBindingSets{
        csgState().m_clipBindingSet,
        csgState().m_receiverSurfaceBindingSet,
        csgState().m_intervalSampleBindingSet
    };
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
        NWB_ASSERT(materialPassDrawResourcesReady(mesh));
        NWB_ASSERT(pipelineResources.meshletPipeline);
        const MaterialPipelineCsgBindingUse csgBindingUse =
            MaterialPipelineResolveCsgBindingUse(drawItem.pipelineKey, context.pass);
        const bool usesAvboit = MaterialPipelinePassUsesRendererAvboit(context.pass);
        const bool usesBindlessAvboitDepth = __hidden_material_pass_draw::UsesBindlessAvboitDepth(context.pass, csgBindingUse);
        Core::BindingSet* const passBindingSet = __hidden_material_pass_draw::ResolvePassBindingSet(context, csgBindingUse);
        Core::BindingSet* const meshBindingSet = usesBindlessAvboitDepth
            ? mesh.bindlessMeshBindingSet.get()
            : mesh.meshBindingSet.get()
        ;
        NWB_ASSERT(meshBindingSet);
        __hidden_material_pass_draw::AssertCsgBindingSetsReady(csgBindingUse, csgBindingSets);

        setMaterialPassCommonBufferStates(context.commandList, mesh);
        __hidden_material_pass_draw::SetCsgBindingSetResourceStates(
            m_renderer.csgSystem(),
            context.commandList,
            csgBindingUse,
            csgBindingSets
        );

        Core::MeshletState meshletState;
        meshletState.setPipeline(pipelineResources.meshletPipeline.get());
        meshletState.setFramebuffer(context.framebuffer);
        meshletState.setViewport(context.viewportState);
        meshletState.addBindingSet(meshBindingSet);
        if(passBindingSet)
            meshletState.addBindingSet(passBindingSet);
        else if(csgBindingUse.clip && usesAvboit)
            meshletState.addBindingSet(nullptr);
        __hidden_material_pass_draw::AddCsgGraphicsBindingSets(meshletState, csgBindingUse, csgBindingSets);

        context.commandList.setMeshletState(meshletState);
        if(usesBindlessAvboitDepth)
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
    NWB_ASSERT(drawState().m_emulationViewBindingSet);

    const bool usesAvboit = MaterialPipelinePassUsesRendererAvboit(context.pass);
    const MaterialPassCsgBindingSets csgBindingSets{
        csgState().m_clipBindingSet,
        csgState().m_receiverSurfaceBindingSet,
        csgState().m_intervalSampleBindingSet
    };
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
        NWB_ASSERT(materialPassDrawResourcesReady(mesh));
        NWB_ASSERT(pipelineResources.computePipeline);
        NWB_ASSERT(pipelineResources.emulationPipeline);
        const MaterialPipelineCsgBindingUse csgBindingUse =
            MaterialPipelineResolveCsgBindingUse(drawItem.pipelineKey, context.pass);
        const bool usesBindlessAvboitDepth = __hidden_material_pass_draw::UsesBindlessAvboitDepth(context.pass, csgBindingUse);
        Core::BindingSet* const passBindingSet = __hidden_material_pass_draw::ResolvePassBindingSet(context, csgBindingUse);
        NWB_ASSERT(mesh.computeBindingSet);
        NWB_ASSERT(mesh.emulationVertexBuffer);
        __hidden_material_pass_draw::AssertCsgBindingSetsReady(csgBindingUse, csgBindingSets);

        setMaterialPassCommonBufferStates(context.commandList, mesh);
        __hidden_material_pass_draw::SetCsgBindingSetResourceStates(
            m_renderer.csgSystem(),
            context.commandList,
            csgBindingUse,
            csgBindingSets
        );
        context.commandList.setBufferState(mesh.emulationVertexBuffer.get(), Core::ResourceStates::UnorderedAccess);

        Core::ComputeState computeState;
        computeState.setPipeline(pipelineResources.computePipeline.get());
        computeState.addBindingSet(mesh.computeBindingSet.get());
        if(csgBindingUse.clip && usesAvboit)
            computeState.addBindingSet(nullptr);
        if(csgBindingUse.clip)
            computeState.addBindingSet(csgBindingSets.clip.get());

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
            Core::BindingSet* const emulationViewBindingSet = pipelineResources.emulationGraphicsUsesMeshFrameSet
                ? (usesBindlessAvboitDepth ? drawState().m_bindlessEmulationViewBindingSet.get() : drawState().m_emulationViewBindingSet.get())
                : nullptr
            ;
            graphicsState.addBindingSet(emulationViewBindingSet);
            graphicsState.addBindingSet(passBindingSet);
            __hidden_material_pass_draw::AddCsgGraphicsBindingSets(graphicsState, csgBindingUse, csgBindingSets);
        }
        else{
            graphicsState.addBindingSet(drawState().m_emulationViewBindingSet.get());
            if(csgBindingUse.clip)
                __hidden_material_pass_draw::AddCsgGraphicsBindingSets(graphicsState, csgBindingUse, csgBindingSets);
            else if(context.passBindingSet)
                graphicsState.addBindingSet(context.passBindingSet);
        }

        context.commandList.setGraphicsState(graphicsState);
        if(usesBindlessAvboitDepth)
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

