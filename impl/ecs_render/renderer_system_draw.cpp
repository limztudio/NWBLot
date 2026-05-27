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
    auto* meshSystem = m_world.getSystem<NWB::Impl::MeshSystem>();
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

#if defined(NWB_DEBUG)
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
        usize debugAlignedByteBegin = 0u;
        if(!AlignUpChecked(materialTypedBytes.size(), sizeof(u32), debugAlignedByteBegin)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed byte offset overflows alignment"));
            return false;
        }
        usize debugByteEnd = debugAlignedByteBegin;
        if(materialInfo.typedBytes.size() > Limit<usize>::s_Max - debugByteEnd){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: gathered material typed byte count overflows"));
            return false;
        }
        debugByteEnd += materialInfo.typedBytes.size();
        usize debugAlignedByteEnd = 0u;
        if(!AlignUpChecked(debugByteEnd, sizeof(u32), debugAlignedByteEnd)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed byte end overflows alignment"));
            return false;
        }
        if(debugAlignedByteBegin > static_cast<usize>(Limit<u32>::s_Max) || debugAlignedByteEnd > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: gathered material typed byte count exceeds u32 limits"));
            return false;
        }
#endif

        const usize alignedByteBegin = AlignUp(materialTypedBytes.size(), sizeof(u32));
        const usize byteEnd = alignedByteBegin + materialInfo.typedBytes.size();
        const usize alignedByteEnd = AlignUp(byteEnd, sizeof(u32));

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

    auto appendDrawForMesh = [&](
        const Core::ECS::EntityID entity,
        const Core::Assets::AssetRef<Material>& material,
        MeshResources& mesh
    ) -> bool{
#if defined(NWB_DEBUG)
        if(!mesh.valid())
            return false;
#endif

        const NWB::Impl::TransformComponent* transform = m_world.tryGetComponent<NWB::Impl::TransformComponent>(entity);

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!createMaterialSurfaceInfo(material, materialInfo))
            return false;
#if defined(NWB_DEBUG)
        if(!materialInfo || !materialInfo->valid)
            return false;
#endif
        if(materialInfo->transparent != transparent)
            return false;

        MaterialPipelineKey pipelineKey;
        pipelineKey.material = materialInfo->materialName;
        pipelineKey.framebufferInfo = framebufferInfo;
        pipelineKey.pass = pass;
        pipelineKey.twoSided = materialInfo->twoSided;

        MaterialPipelineResources* pipelineResources = nullptr;
        if(!createRendererPipeline(*materialInfo, pipelineKey, framebuffer, pipelineResources))
            return false;
#if defined(NWB_DEBUG)
        if(!pipelineResources)
            return false;
#endif

        auto appendInstance = [&]() -> u32{
#if defined(NWB_DEBUG)
            if(instanceData.size() >= static_cast<usize>(Limit<u32>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer instance count exceeds u32 limits"));
                return Limit<u32>::s_Max;
            }
#endif

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
            drawItem.meshKey = mesh.meshName;
            drawItem.pipelineKey = pipelineKey;
            drawItem.instanceIndex = instanceIndex;
            drawItems.push_back(drawItem);
            return true;
        };

        switch(pipelineResources->renderPath){
        case RenderPath::MeshShader:{
#if defined(NWB_DEBUG)
            if(!pipelineResources->meshletPipeline)
                return false;
#endif
            return appendDrawItem(meshDrawItems);
        }
        case RenderPath::ComputeEmulation:{
#if defined(NWB_DEBUG)
            if(!pipelineResources->computePipeline || !pipelineResources->emulationPipeline)
                return false;
#endif
            return appendDrawItem(computeDrawItems);
        }
        default:
            return false;
        }
    };

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        if(!meshSystem){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: MeshSystem is not registered; renderers cannot resolve mesh"));
            break;
        }

        RenderableMeshDesc resolvedMesh;
        if(!meshSystem->resolveRenderableMesh(entity, resolvedMesh))
            continue;

        MeshResources* mesh = nullptr;
        if(resolvedMesh.runtime){
            if(!createRuntimeMeshResources(resolvedMesh.runtimeMesh, mesh))
                continue;
        }
        else if(!createMeshResources(resolvedMesh.mesh, mesh))
            continue;

#if defined(NWB_DEBUG)
        if(mesh)
            appendDrawForMesh(entity, renderer.material, *mesh);
#else
        appendDrawForMesh(entity, renderer.material, *mesh);
#endif
    }
}

void RendererSystem::setMaterialPassCommonBufferStates(
    Core::ICommandList& commandList,
    const MeshResources& mesh
){
    forEachMeshSourceBuffer(mesh, [&](const u32, const Core::BufferHandle& buffer, const bool){
        commandList.setBufferState(buffer.get(), Core::ResourceStates::ShaderResource);
    });
    commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.setBufferState(m_materialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
}

bool RendererSystem::materialPassDrawResourcesReady(const MeshResources& mesh)const{
#if defined(NWB_DEBUG)
    return mesh.valid() && m_instanceBuffer && m_meshViewBuffer && m_materialTypedBuffer;
#else
    static_cast<void>(mesh);
    return true;
#endif
}

u32 RendererSystem::meshDispatchFlags(
    const MeshResources& mesh,
    const MaterialPipelinePass::Enum pass,
    const bool twoSided
)const{
    u32 flags = 0u;
    if(!mesh.runtimeMesh)
        flags |= ECSRenderDetail::s_MeshDispatchFlagMeshletFrustumCull;
    if(!mesh.runtimeMesh && pass == MaterialPipelinePass::Opaque && !twoSided)
        flags |= ECSRenderDetail::s_MeshDispatchFlagMeshletConeCull;
    return flags;
}

void RendererSystem::setMaterialPassDrawPushConstants(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItem& drawItem,
    const MeshResources& mesh
){
    const u32 dispatchFlags = meshDispatchFlags(mesh, context.pass, drawItem.pipelineKey.twoSided);
    if(MaterialPipelinePassUsesRendererAvboit(context.pass)){
        ECSRenderDetail::SetTransparentDrawPushConstants(
            context.commandList,
            mesh.meshletCount,
            drawItem.instanceIndex,
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
        context.viewportState,
        dispatchFlags
    );
}

void RendererSystem::renderMeshMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
#if defined(NWB_DEBUG)
        if(!materialPassDrawResourcesReady(mesh) || !pipelineResources.meshletPipeline)
            return;
#endif
        if(!createMeshBindingSet(mesh))
            return;

        setMaterialPassCommonBufferStates(context.commandList, mesh);

        Core::MeshletState meshletState;
        meshletState.setPipeline(pipelineResources.meshletPipeline.get());
        meshletState.setFramebuffer(context.framebuffer);
        meshletState.setViewport(context.viewportState);
        meshletState.addBindingSet(mesh.meshBindingSet.get());
        if(context.passBindingSet)
            meshletState.addBindingSet(context.passBindingSet);

        context.commandList.setMeshletState(meshletState);

        setMaterialPassDrawPushConstants(context, drawItem, mesh);
        context.commandList.dispatchMesh(mesh.meshletCount);
    });
}

void RendererSystem::renderComputeMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    if(drawItems.empty())
        return;
    if(!createEmulationViewResources())
        return;
#if defined(NWB_DEBUG)
    if(!m_meshViewBuffer || !m_emulationViewBindingSet)
        return;
#endif

    const bool usesAvboit = MaterialPipelinePassUsesRendererAvboit(context.pass);

    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
#if defined(NWB_DEBUG)
        if(!materialPassDrawResourcesReady(mesh) || !pipelineResources.computePipeline || !pipelineResources.emulationPipeline)
            return;
#endif
        if(!createComputeBindingSet(mesh))
            return;
#if defined(NWB_DEBUG)
        if(!mesh.computeBindingSet || !mesh.emulationVertexBuffer)
            return;
#endif

        setMaterialPassCommonBufferStates(context.commandList, mesh);
        context.commandList.setBufferState(mesh.emulationVertexBuffer.get(), Core::ResourceStates::UnorderedAccess);

        Core::ComputeState computeState;
        computeState.setPipeline(pipelineResources.computePipeline.get());
        computeState.addBindingSet(mesh.computeBindingSet.get());

        context.commandList.setComputeState(computeState);

        ECSRenderDetail::SetShaderDrivenPushConstants(
            context.commandList,
            mesh.meshletCount,
            drawItem.instanceIndex,
            context.viewportState,
            meshDispatchFlags(mesh, context.pass, drawItem.pipelineKey.twoSided)
        );
        context.commandList.dispatch(mesh.meshletCount);

        context.commandList.setBufferState(mesh.emulationVertexBuffer.get(), Core::ResourceStates::VertexBuffer);

        Core::GraphicsState graphicsState;
        graphicsState.setPipeline(pipelineResources.emulationPipeline.get());
        graphicsState.setFramebuffer(context.framebuffer);
        graphicsState.setViewport(context.viewportState);
        graphicsState.addVertexBuffer(
            Core::VertexBufferBinding()
                .setBuffer(mesh.emulationVertexBuffer.get())
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
            setMaterialPassDrawPushConstants(context, drawItem, mesh);

        Core::DrawArguments drawArgs;
        drawArgs.setVertexCount(mesh.meshletPrimitiveIndexCount);
        context.commandList.draw(drawArgs);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

