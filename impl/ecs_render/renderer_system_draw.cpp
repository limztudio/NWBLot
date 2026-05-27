// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_system_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererSystem::renderMaterialPass(
    Core::ICommandList& commandList,
    Core::IFramebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const bool transparent,
    Core::IBindingSet* passBindingSet,
    const AvboitFrameTargets* avboitTargets
){
    if(!framebuffer)
        return;
    const bool usesAvboit = MaterialPipelinePassUsesRendererAvboit(pass);
    if(usesAvboit && (!passBindingSet || !avboitTargets || !avboitTargets->valid()))
        return;

    commandList.endRenderPass();

    Core::Alloc::ScratchArena scratchArena;
    MaterialPassDrawItemVector meshDrawItems{scratchArena};
    MaterialPassDrawItemVector computeDrawItems{scratchArena};
    InstanceGpuDataVector instanceData{scratchArena};
    MaterialTypedByteDataVector materialTypedBytes{scratchArena};

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());

    gatherMaterialPassDrawItems(
        framebuffer,
        pass,
        transparent,
        meshDrawItems,
        computeDrawItems,
        instanceData,
        materialTypedBytes
    );
    if(meshDrawItems.empty() && computeDrawItems.empty())
        return;

    const Core::FramebufferInfoEx& meshViewFramebufferInfo = framebuffer->getFramebufferInfo();
    f32 meshViewAspectRatio = 1.0f;
    if(meshViewFramebufferInfo.width != 0 && meshViewFramebufferInfo.height != 0)
        meshViewAspectRatio = static_cast<f32>(meshViewFramebufferInfo.width) / static_cast<f32>(meshViewFramebufferInfo.height);
    if(avboitTargets && avboitTargets->fullWidth > 0 && avboitTargets->fullHeight > 0)
        meshViewAspectRatio = static_cast<f32>(avboitTargets->fullWidth) / static_cast<f32>(avboitTargets->fullHeight);
    if(!updateMeshViewBuffer(commandList, meshViewAspectRatio))
        return;

    if(!uploadInstanceBuffer(commandList, instanceData))
        return;
    if(!uploadMaterialTypedBuffer(commandList, materialTypedBytes))
        return;

    if(passBindingSet){
        commandList.setResourceStatesForBindingSet(passBindingSet);
        commandList.commitBarriers();
    }

    const MaterialPassDrawContext drawContext{ commandList, framebuffer, pass, passBindingSet, avboitTargets, viewportState };
    renderMeshMaterialPassDrawItems(drawContext, meshDrawItems);
    renderComputeMaterialPassDrawItems(drawContext, computeDrawItems);
}

void RendererSystem::gatherMaterialPassDrawItems(
    Core::IFramebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const bool transparent,
    MaterialPassDrawItemVector& meshDrawItems,
    MaterialPassDrawItemVector& computeDrawItems,
    InstanceGpuDataVector& instanceData,
    MaterialTypedByteDataVector& materialTypedBytes
){
    if(!framebuffer)
        return;

    auto rendererView = m_world.view<RendererComponent>();
    auto* geometrySystem = m_world.getSystem<NWB::Impl::GeometrySystem>();
    usize rendererCapacity = rendererView.candidateCount();
    meshDrawItems.reserve(rendererCapacity);
    computeDrawItems.reserve(rendererCapacity);
    instanceData.reserve(rendererCapacity);
    const usize materialTypedByteReserve = rendererCapacity <= Limit<usize>::s_Max / sizeof(u32)
        ? rendererCapacity * sizeof(u32)
        : rendererCapacity
    ;
    materialTypedBytes.reserve(materialTypedByteReserve);

    using MaterialTypedByteBlockMap = HashMap<
        Name,
        ECSRenderDetail::MaterialTypedByteBlock,
        Hasher<Name>,
        EqualTo<Name>,
        Core::Alloc::ScratchArena
    >;
    MaterialTypedByteBlockMap materialTypedByteBlocks(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        materialTypedBytes.get_allocator().arena()
    );
    materialTypedByteBlocks.reserve(rendererCapacity);

    const Core::FramebufferInfo& framebufferInfo = framebuffer->getFramebufferInfo();

    auto appendMaterialTypedByteBlock = [&](
        const MaterialSurfaceInfo& materialInfo,
        ECSRenderDetail::MaterialTypedByteBlock& outBlock
    ) -> bool{
        const auto foundBlock = materialTypedByteBlocks.find(materialInfo.materialName);
        if(foundBlock != materialTypedByteBlocks.end()){
            outBlock = foundBlock.value();
            return true;
        }

        if(materialInfo.typedBytes.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' is missing typed material data")
                , StringConvert(materialInfo.materialName.c_str())
            );
            return false;
        }
        if(materialTypedBytes.size() > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed byte offset exceeds u32 limits"));
            return false;
        }
        if(materialInfo.typedBytes.size() > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed byte count exceeds u32 limits"));
            return false;
        }
        usize alignedByteBegin = 0u;
        if(!AlignUpChecked(materialTypedBytes.size(), sizeof(u32), alignedByteBegin)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed byte offset overflows alignment"));
            return false;
        }
        usize byteEnd = alignedByteBegin;
        if(materialInfo.typedBytes.size() > Limit<usize>::s_Max - byteEnd){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: gathered material typed byte count overflows"));
            return false;
        }
        byteEnd += materialInfo.typedBytes.size();
        usize alignedByteEnd = 0u;
        if(!AlignUpChecked(byteEnd, sizeof(u32), alignedByteEnd)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed byte end overflows alignment"));
            return false;
        }
        if(alignedByteBegin > static_cast<usize>(Limit<u32>::s_Max) || alignedByteEnd > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: gathered material typed byte count exceeds u32 limits"));
            return false;
        }

        outBlock.byteOffset = static_cast<u32>(alignedByteBegin);
        outBlock.byteCount = static_cast<u32>(materialInfo.typedBytes.size());
        const usize requiredTypedByteCapacity = alignedByteEnd;
        if(requiredTypedByteCapacity > materialTypedBytes.capacity())
            materialTypedBytes.reserve(ECSRenderDetail::NextGrowingCapacity(
                materialTypedBytes.capacity(),
                requiredTypedByteCapacity
            ));
        materialTypedBytes.resize(alignedByteBegin, 0u);
        AppendTriviallyCopyableVector(materialTypedBytes, materialInfo.typedBytes);
        materialTypedBytes.resize(alignedByteEnd, 0u);

        materialTypedByteBlocks.emplace(materialInfo.materialName, outBlock);
        return true;
    };

    auto appendDrawForGeometry = [&](
        const Core::ECS::EntityID entity,
        const Core::Assets::AssetRef<Material>& material,
        GeometryResources& geometry
    ) -> bool{
        if(!geometry.valid())
            return false;

        const NWB::Impl::TransformComponent* transform =
            m_world.tryGetComponent<NWB::Impl::TransformComponent>(entity)
        ;

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!createMaterialSurfaceInfo(material, materialInfo))
            return false;
        if(!materialInfo || !materialInfo->valid || materialInfo->transparent != transparent)
            return false;

        MaterialPipelineKey pipelineKey;
        pipelineKey.material = materialInfo->materialName;
        pipelineKey.framebufferInfo = framebufferInfo;
        pipelineKey.pass = pass;

        MaterialPipelineResources* pipelineResources = nullptr;
        if(!createRendererPipeline(*materialInfo, pipelineKey, framebuffer, pipelineResources))
            return false;
        if(!pipelineResources)
            return false;

        auto appendInstance = [&]() -> u32{
            if(instanceData.size() >= static_cast<usize>(Limit<u32>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer instance count exceeds u32 limits"));
                return Limit<u32>::s_Max;
            }

            ECSRenderDetail::MaterialTypedByteBlock typedByteBlock;
            if(!appendMaterialTypedByteBlock(*materialInfo, typedByteBlock))
                return Limit<u32>::s_Max;

            const u32 instanceIndex = static_cast<u32>(instanceData.size());
            instanceData.push_back(ECSRenderDetail::BuildInstanceGpuData(
                transform,
                typedByteBlock.byteOffset,
                typedByteBlock.byteCount
            ));
            return instanceIndex;
        };

        auto appendDrawItem = [&](MaterialPassDrawItemVector& drawItems) -> bool{
            const u32 instanceIndex = appendInstance();
            if(instanceIndex == Limit<u32>::s_Max)
                return false;

            MaterialPassDrawItem drawItem;
            drawItem.geometryKey = geometry.geometryName;
            drawItem.pipelineKey = pipelineKey;
            drawItem.instanceIndex = instanceIndex;
            drawItems.push_back(drawItem);
            return true;
        };

        switch(pipelineResources->renderPath){
        case RenderPath::MeshShader:{
            if(!pipelineResources->meshletPipeline)
                return false;
            return appendDrawItem(meshDrawItems);
        }
        case RenderPath::ComputeEmulation:{
            if(!pipelineResources->computePipeline || !pipelineResources->emulationPipeline)
                return false;
            return appendDrawItem(computeDrawItems);
        }
        default:
            return false;
        }
    };

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        if(!geometrySystem){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: GeometrySystem is not registered; renderers cannot resolve geometry"));
            break;
        }

        RenderableGeometryDesc resolvedGeometry;
        if(!geometrySystem->resolveRenderableGeometry(entity, resolvedGeometry))
            continue;

        GeometryResources* geometry = nullptr;
        if(resolvedGeometry.runtime){
            if(!createRuntimeGeometryResources(resolvedGeometry.runtimeGeometry, geometry))
                continue;
        }
        else if(!createGeometryResources(resolvedGeometry.geometry, geometry))
            continue;

        if(geometry)
            appendDrawForGeometry(entity, renderer.material, *geometry);
    }
}

void RendererSystem::setMaterialPassCommonBufferStates(
    Core::ICommandList& commandList,
    const GeometryResources& geometry
){
    commandList.setBufferState(geometry.positionBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(geometry.normalBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(geometry.tangentBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(geometry.uv0Buffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(geometry.colorBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(geometry.vertexRefBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(geometry.meshletDescBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(geometry.meshletBoundsBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(geometry.meshletVertexRefBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(geometry.meshletPrimitiveIndexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.setBufferState(m_materialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
}

bool RendererSystem::materialPassDrawResourcesReady(const GeometryResources& geometry)const{
    return
        geometry.valid()
        && m_instanceBuffer
        && m_meshViewBuffer
        && m_materialTypedBuffer
    ;
}

void RendererSystem::setMaterialPassDrawPushConstants(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItem& drawItem,
    const GeometryResources& geometry
){
    if(MaterialPipelinePassUsesRendererAvboit(context.pass)){
        ECSRenderDetail::SetTransparentDrawPushConstants(
            context.commandList,
            geometry.meshletCount,
            drawItem.instanceIndex,
            context.viewportState,
            *context.avboitTargets
        );
        return;
    }

    ECSRenderDetail::SetShaderDrivenPushConstants(
        context.commandList,
        geometry.meshletCount,
        drawItem.instanceIndex,
        context.viewportState
    );
}

void RendererSystem::renderMeshMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, GeometryResources& geometry, MaterialPipelineResources& pipelineResources){
        if(
            !materialPassDrawResourcesReady(geometry)
            || !pipelineResources.meshletPipeline
        )
            return;
        if(!createMeshBindingSet(geometry))
            return;

        setMaterialPassCommonBufferStates(context.commandList, geometry);

        Core::MeshletState meshletState;
        meshletState.setPipeline(pipelineResources.meshletPipeline.get());
        meshletState.setFramebuffer(context.framebuffer);
        meshletState.setViewport(context.viewportState);
        meshletState.addBindingSet(geometry.meshBindingSet.get());
        if(context.passBindingSet)
            meshletState.addBindingSet(context.passBindingSet);

        context.commandList.setMeshletState(meshletState);

        setMaterialPassDrawPushConstants(context, drawItem, geometry);
        context.commandList.dispatchMesh(geometry.meshletCount);
    });
}

void RendererSystem::renderComputeMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    if(drawItems.empty())
        return;
    if(!m_meshViewBuffer || !createEmulationViewResources() || !m_emulationViewBindingSet)
        return;

    const bool usesAvboit = MaterialPipelinePassUsesRendererAvboit(context.pass);

    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, GeometryResources& geometry, MaterialPipelineResources& pipelineResources){
        if(
            !materialPassDrawResourcesReady(geometry)
            || !pipelineResources.computePipeline
            || !pipelineResources.emulationPipeline
        )
            return;
        if(!createComputeBindingSet(geometry))
            return;
        if(!geometry.computeBindingSet || !geometry.emulationVertexBuffer)
            return;

        setMaterialPassCommonBufferStates(context.commandList, geometry);
        context.commandList.setBufferState(geometry.emulationVertexBuffer.get(), Core::ResourceStates::UnorderedAccess);

        Core::ComputeState computeState;
        computeState.setPipeline(pipelineResources.computePipeline.get());
        computeState.addBindingSet(geometry.computeBindingSet.get());

        context.commandList.setComputeState(computeState);

        ECSRenderDetail::SetShaderDrivenPushConstants(
            context.commandList,
            geometry.meshletCount,
            drawItem.instanceIndex,
            context.viewportState
        );
        context.commandList.dispatch(geometry.meshletCount);

        context.commandList.setBufferState(geometry.emulationVertexBuffer.get(), Core::ResourceStates::VertexBuffer);

        Core::GraphicsState graphicsState;
        graphicsState.setPipeline(pipelineResources.emulationPipeline.get());
        graphicsState.setFramebuffer(context.framebuffer);
        graphicsState.setViewport(context.viewportState);
        graphicsState.addVertexBuffer(
            Core::VertexBufferBinding()
                .setBuffer(geometry.emulationVertexBuffer.get())
                .setSlot(0)
                .setOffset(0)
        );
        if(usesAvboit){
            graphicsState.addBindingSet(nullptr);
            graphicsState.addBindingSet(context.passBindingSet);
        }
        else{
            graphicsState.addBindingSet(m_emulationViewBindingSet.get());
            if(context.passBindingSet)
                graphicsState.addBindingSet(context.passBindingSet);
        }

        context.commandList.setGraphicsState(graphicsState);

        if(usesAvboit)
            setMaterialPassDrawPushConstants(context, drawItem, geometry);

        Core::DrawArguments drawArgs;
        drawArgs.setVertexCount(geometry.meshletPrimitiveIndexCount);
        context.commandList.draw(drawArgs);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

