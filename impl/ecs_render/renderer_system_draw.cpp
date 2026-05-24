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

    const Core::FramebufferInfoEx& meshViewFramebufferInfo = framebuffer->getFramebufferInfo();
    f32 meshViewAspectRatio = 1.0f;
    if(meshViewFramebufferInfo.width != 0 && meshViewFramebufferInfo.height != 0)
        meshViewAspectRatio = static_cast<f32>(meshViewFramebufferInfo.width) / static_cast<f32>(meshViewFramebufferInfo.height);
    if(avboitTargets && avboitTargets->fullWidth > 0 && avboitTargets->fullHeight > 0)
        meshViewAspectRatio = static_cast<f32>(avboitTargets->fullWidth) / static_cast<f32>(avboitTargets->fullHeight);
    if(!updateMeshViewBuffer(commandList, meshViewAspectRatio))
        return;

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
        const usize remainingTypedByteCapacity = static_cast<usize>(Limit<u32>::s_Max) - materialTypedBytes.size();
        if(materialInfo.typedBytes.size() > remainingTypedByteCapacity){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: gathered material typed byte count exceeds u32 limits"));
            return false;
        }
        if(((materialTypedBytes.size() | materialInfo.typedBytes.size()) & (sizeof(u32) - 1u)) != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed bytes must be word-aligned"));
            return false;
        }

        outBlock.byteOffset = static_cast<u32>(materialTypedBytes.size());
        outBlock.byteCount = static_cast<u32>(materialInfo.typedBytes.size());
        const usize requiredTypedByteCapacity = materialTypedBytes.size() + materialInfo.typedBytes.size();
        if(requiredTypedByteCapacity > materialTypedBytes.capacity())
            materialTypedBytes.reserve(ECSRenderDetail::NextGrowingCapacity(
                materialTypedBytes.capacity(),
                requiredTypedByteCapacity
            ));
        AppendTriviallyCopyableVector(materialTypedBytes, materialInfo.typedBytes);

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
            drawItem.alpha = materialInfo->alpha;
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

bool RendererSystem::createMeshShaderResources(){
    if(m_meshBindingLayout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
    bindingLayoutDesc.setVisibility(Core::ShaderType::Amplification | Core::ShaderType::Mesh | Core::ShaderType::Pixel);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(1, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(4, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(6, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::TransparentDrawPushConstants)));

    Core::IDevice* device = m_graphics.getDevice();
    m_meshBindingLayout = device->createBindingLayout(bindingLayoutDesc);
    if(!m_meshBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh shader binding layout"));
        return false;
    }

    return true;
}

bool RendererSystem::createComputeEmulationResources(){
    if(!m_computeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(4, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(6, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::ShaderDrivenPushConstants)));

        Core::IDevice* device = m_graphics.getDevice();
        m_computeBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_computeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation binding layout"));
            return false;
        }
    }

    if(!m_emulationVertexShader){
        if(!loadShader(
            m_emulationVertexShader,
            ECSRenderDetail::s_MeshEmulationVertexShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Vertex,
            "ECSRender_MeshEmulationVS"
        ))
            return false;
    }

    if(!m_emulationInputLayout){
        Core::VertexAttributeDesc attributes[6];
        ECSRenderDetail::SetEmulatedVertexAttribute(attributes[0], Core::Format::RGBA32_FLOAT, 0u, "POSITION");
        ECSRenderDetail::SetEmulatedVertexAttribute(attributes[1], Core::Format::RGB32_FLOAT, 4u, "NORMAL");
        ECSRenderDetail::SetEmulatedVertexAttribute(attributes[2], Core::Format::RGBA32_FLOAT, 8u, "TANGENT");
        ECSRenderDetail::SetEmulatedVertexAttribute(attributes[3], Core::Format::RG32_FLOAT, 12u, "TEXCOORD");
        ECSRenderDetail::SetEmulatedVertexAttribute(attributes[4], Core::Format::RGBA32_FLOAT, 16u, "COLOR");
        ECSRenderDetail::SetEmulatedVertexAttribute(attributes[5], Core::Format::RGBA32_FLOAT, 20u, "POSITION1");

        Core::IDevice* device = m_graphics.getDevice();
        m_emulationInputLayout = device->createInputLayout(attributes, 6, m_emulationVertexShader.get());
        if(!m_emulationInputLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation input layout"));
            return false;
        }
    }

    return true;
}

bool RendererSystem::createEmulationViewResources(){
    if(!m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: emulation view resources require a mesh view buffer"));
        return false;
    }

    Core::IDevice* device = m_graphics.getDevice();
    if(!m_emulationViewBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(4, 1));

        m_emulationViewBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_emulationViewBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation view binding layout"));
            return false;
        }
    }

    if(m_emulationViewBindingSet)
        return true;

    Core::BindingSetDesc bindingSetDesc(m_arena);
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(4, m_meshViewBuffer.get()));

    m_emulationViewBindingSet = device->createBindingSet(bindingSetDesc, m_emulationViewBindingLayout);
    if(!m_emulationViewBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation view binding set"));
        return false;
    }

    return true;
}

bool RendererSystem::reserveInstanceBufferCapacity(const usize instanceCount){
    if(instanceCount == 0)
        return true;
    if(instanceCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance buffer request exceeds u32 instance-index limits"));
        return false;
    }
    if(m_instanceBuffer && m_instanceBufferCapacity >= instanceCount)
        return true;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(m_instanceBufferCapacity, instanceCount);
    if(capacity > Limit<usize>::s_Max / sizeof(InstanceGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance buffer capacity overflows addressable memory"));
        return false;
    }

    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setDebugName(ECSRenderDetail::s_InstanceBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle instanceBuffer = m_graphics.createBuffer(instanceBufferDesc);
    if(!instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create instance data buffer"));
        return false;
    }

    m_instanceBuffer = Move(instanceBuffer);
    m_instanceBufferCapacity = capacity;
    destroyGeometryBindingSets();
    return true;
}

bool RendererSystem::reserveMaterialTypedBufferCapacity(const usize byteCount){
    usize requiredByteCount = Max<usize>(byteCount, sizeof(u32));
    if(!AlignUpChecked(requiredByteCount, sizeof(u32), requiredByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed buffer request overflows alignment"));
        return false;
    }
    if(requiredByteCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed buffer request exceeds u32 byte-offset limits"));
        return false;
    }
    if(m_materialTypedBuffer && m_materialTypedBufferCapacity >= requiredByteCount)
        return true;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(m_materialTypedBufferCapacity, requiredByteCount);
    Core::BufferDesc materialTypedBufferDesc;
    materialTypedBufferDesc
        .setByteSize(static_cast<u64>(capacity))
        .setStructStride(sizeof(u32))
        .setDebugName(ECSRenderDetail::s_MaterialTypedBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle materialTypedBuffer = m_graphics.createBuffer(materialTypedBufferDesc);
    if(!materialTypedBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create material typed buffer"));
        return false;
    }

    m_materialTypedBuffer = Move(materialTypedBuffer);
    m_materialTypedBufferCapacity = capacity;
    destroyGeometryBindingSets();
    return true;
}

bool RendererSystem::updateMeshViewBuffer(Core::ICommandList& commandList, const f32 fallbackAspectRatio){
    if(!m_meshViewBuffer){
        Core::BufferDesc meshViewBufferDesc;
        meshViewBufferDesc
            .setByteSize(sizeof(ECSRenderDetail::MeshViewGpuData))
            .setIsConstantBuffer(true)
            .setDebugName(ECSRenderDetail::s_MeshViewBufferName)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        Core::BufferHandle meshViewBuffer = m_graphics.createBuffer(meshViewBufferDesc);
        if(!meshViewBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh view buffer"));
            return false;
        }

        m_meshViewBuffer = Move(meshViewBuffer);
        destroyGeometryBindingSets();
    }

    const ECSRenderDetail::MeshViewState viewState =
        ECSRenderDetail::ResolveMeshViewState(m_world, fallbackAspectRatio)
    ;

    commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_meshViewBuffer.get(), &viewState, sizeof(viewState));
    commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    return true;
}

bool RendererSystem::uploadInstanceBuffer(Core::ICommandList& commandList, const InstanceGpuDataVector& instanceData){
    if(instanceData.empty())
        return true;
    if(!reserveInstanceBufferCapacity(instanceData.size()))
        return false;
    if(!m_instanceBuffer)
        return false;

    if(instanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance data upload size overflows"));
        return false;
    }

    commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_instanceBuffer.get(), instanceData.data(), instanceData.size() * sizeof(InstanceGpuData));
    commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

bool RendererSystem::uploadMaterialTypedBuffer(
    Core::ICommandList& commandList,
    const MaterialTypedByteDataVector& materialTypedBytes
){
    if(materialTypedBytes.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed data upload is empty"));
        return false;
    }
    if(!materialTypedBytes.empty() && (materialTypedBytes.size() & (sizeof(u32) - 1u)) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed data upload is not word-aligned"));
        return false;
    }

    usize uploadBytes = materialTypedBytes.size();
    if(!AlignUpChecked(uploadBytes, sizeof(u32), uploadBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed data upload size overflows alignment"));
        return false;
    }
    if(!reserveMaterialTypedBufferCapacity(uploadBytes))
        return false;
    if(!m_materialTypedBuffer)
        return false;

    commandList.setBufferState(m_materialTypedBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_materialTypedBuffer.get(), materialTypedBytes.data(), uploadBytes);
    commandList.setBufferState(m_materialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

bool RendererSystem::findMaterialPassDrawItemResources(
    const MaterialPassDrawItem& drawItem,
    GeometryResources*& outGeometry,
    MaterialPipelineResources*& outPipelineResources
){
    outGeometry = nullptr;
    outPipelineResources = nullptr;

    const auto foundGeometry = m_geometryMeshes.find(drawItem.geometryKey);
    if(foundGeometry == m_geometryMeshes.end())
        return false;

    const auto foundPipeline = m_materialPipelines.find(drawItem.pipelineKey);
    if(foundPipeline == m_materialPipelines.end())
        return false;

    outGeometry = &foundGeometry.value();
    outPipelineResources = &foundPipeline.value();
    return true;
}

void RendererSystem::setMaterialPassCommonBufferStates(
    Core::ICommandList& commandList,
    const GeometryResources& geometry
){
    commandList.setBufferState(geometry.shaderVertexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(geometry.shaderIndexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.setBufferState(m_materialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
}

void RendererSystem::setMaterialPassDrawPushConstants(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItem& drawItem,
    const GeometryResources& geometry
){
    if(MaterialPipelinePassUsesRendererAvboit(context.pass)){
        ECSRenderDetail::SetTransparentDrawPushConstants(
            context.commandList,
            geometry.triangleCount,
            drawItem.instanceIndex,
            geometry.sourceVertexLayout,
            context.viewportState,
            *context.avboitTargets,
            drawItem.alpha
        );
        return;
    }

    ECSRenderDetail::SetShaderDrivenPushConstants(
        context.commandList,
        geometry.triangleCount,
        drawItem.instanceIndex,
        geometry.sourceVertexLayout,
        context.viewportState
    );
}

void RendererSystem::renderMeshMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, GeometryResources& geometry, MaterialPipelineResources& pipelineResources){
        if(
            !geometry.valid()
            || !pipelineResources.meshletPipeline
            || !m_instanceBuffer
            || !m_meshViewBuffer
            || !m_materialTypedBuffer
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
        context.commandList.dispatchMesh(geometry.dispatchGroupCount);
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
            !geometry.valid()
            || !pipelineResources.computePipeline
            || !pipelineResources.emulationPipeline
            || !m_instanceBuffer
            || !m_meshViewBuffer
            || !m_materialTypedBuffer
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
            geometry.triangleCount,
            drawItem.instanceIndex,
            geometry.sourceVertexLayout,
            context.viewportState
        );
        context.commandList.dispatch(geometry.dispatchGroupCount);

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
        drawArgs.setVertexCount(geometry.indexCount);
        context.commandList.draw(drawArgs);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

