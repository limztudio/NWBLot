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

    Core::Alloc::ScratchArena<> scratchArena;
    MaterialPassDrawItemVector meshDrawItems{Core::Alloc::ScratchAllocator<MaterialPassDrawItem>(scratchArena)};
    MaterialPassDrawItemVector computeDrawItems{Core::Alloc::ScratchAllocator<MaterialPassDrawItem>(scratchArena)};
    InstanceGpuDataVector instanceData{Core::Alloc::ScratchAllocator<InstanceGpuData>(scratchArena)};
    MaterialParameterGpuDataVector materialParameters{Core::Alloc::ScratchAllocator<MaterialParameterGpuData>(scratchArena)};

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

    gatherMaterialPassDrawItems(framebuffer, pass, transparent, meshDrawItems, computeDrawItems, instanceData, materialParameters);
    if(meshDrawItems.empty() && computeDrawItems.empty())
        return;

    if(!uploadInstanceBuffer(commandList, instanceData))
        return;
    if(!uploadMaterialParameterBuffer(commandList, materialParameters))
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
    MaterialParameterGpuDataVector& materialParameters
){
    if(!framebuffer)
        return;

    auto rendererView = m_world.view<RendererComponent>();
    auto* geometrySystem = m_world.getSystem<NWB::Impl::GeometrySystem>();
    usize rendererCapacity = rendererView.candidateCount();
    for(IRuntimeGeometryProvider* provider : m_runtimeGeometryProviders){
        if(!provider)
            continue;

        const usize providerCandidateCount = provider->runtimeGeometryCandidateCount();
        if(providerCandidateCount > Limit<usize>::s_Max - rendererCapacity){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime geometry provider candidate count overflow"));
            break;
        }
        rendererCapacity += providerCandidateCount;
    }
    meshDrawItems.reserve(rendererCapacity);
    computeDrawItems.reserve(rendererCapacity);
    instanceData.reserve(rendererCapacity);
    materialParameters.reserve(rendererCapacity);

    using MaterialParameterBlockPair = Pair<Name, __hidden_ecs_render::MaterialParameterBlock>;
    using MaterialParameterBlockMap = HashMap<
        Name,
        __hidden_ecs_render::MaterialParameterBlock,
        Hasher<Name>,
        EqualTo<Name>,
        Core::Alloc::ScratchAllocator<MaterialParameterBlockPair>
    >;
    MaterialParameterBlockMap materialParameterBlocks(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        Core::Alloc::ScratchAllocator<MaterialParameterBlockPair>(materialParameters.get_allocator())
    );
    materialParameterBlocks.reserve(rendererCapacity);

    const Core::FramebufferInfo& framebufferInfo = framebuffer->getFramebufferInfo();

    auto appendMaterialParameterBlock = [&](
        const MaterialSurfaceInfo& materialInfo,
        __hidden_ecs_render::MaterialParameterBlock& outBlock
    ) -> bool{
        const auto foundBlock = materialParameterBlocks.find(materialInfo.materialName);
        if(foundBlock != materialParameterBlocks.end()){
            outBlock = foundBlock.value();
            return true;
        }

        if(materialParameters.size() > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter offset exceeds u32 limits"));
            return false;
        }
        if(materialInfo.parameters.size() > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter count exceeds u32 limits"));
            return false;
        }
        if(materialInfo.parameters.size() > static_cast<usize>(Limit<u32>::s_Max) - materialParameters.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: gathered material parameter count exceeds u32 limits"));
            return false;
        }

        outBlock.offset = static_cast<u32>(materialParameters.size());
        outBlock.count = static_cast<u32>(materialInfo.parameters.size());
        const usize requiredParameterCapacity = materialParameters.size() + materialInfo.parameters.size();
        if(requiredParameterCapacity > materialParameters.capacity())
            materialParameters.reserve(__hidden_ecs_render::NextGrowingCapacity(
                materialParameters.capacity(),
                requiredParameterCapacity
            ));
        AppendTriviallyCopyableVector(materialParameters, materialInfo.parameters);

        materialParameterBlocks.emplace(materialInfo.materialName, outBlock);
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

            __hidden_ecs_render::MaterialParameterBlock parameterBlock;
            if(!appendMaterialParameterBlock(*materialInfo, parameterBlock))
                return Limit<u32>::s_Max;

            const u32 instanceIndex = static_cast<u32>(instanceData.size());
            instanceData.push_back(__hidden_ecs_render::BuildInstanceGpuData(
                transform,
                parameterBlock.offset,
                parameterBlock.count
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
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: GeometrySystem is not registered; static renderers cannot resolve geometry"));
            break;
        }

        Core::Assets::AssetRef<Geometry> geometryAsset;
        if(!geometrySystem->resolveGeometry(entity, geometryAsset))
            continue;

        GeometryResources* geometry = nullptr;
        if(!createGeometryResources(geometryAsset, geometry))
            continue;
        if(geometry)
            appendDrawForGeometry(entity, renderer.material, *geometry);
    }

    for(IRuntimeGeometryProvider* provider : m_runtimeGeometryProviders){
        if(!provider)
            continue;

        provider->forEachRuntimeGeometry(
            [&](const RuntimeGeometryDesc& desc){
                if(!desc.valid())
                    return;

                GeometryResources* geometry = nullptr;
                if(!createRuntimeGeometryResources(desc, geometry))
                    return;
                if(geometry)
                    appendDrawForGeometry(desc.entity, desc.material, *geometry);
            }
        );
    }
}

bool RendererSystem::createMeshShaderResources(){
    if(m_meshBindingLayout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.setVisibility(Core::ShaderType::Amplification | Core::ShaderType::Mesh | Core::ShaderType::Pixel);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(1, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(4, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(5, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_render::TransparentDrawPushConstants)));

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
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(4, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(5, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_render::ShaderDrivenPushConstants)));

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
            __hidden_ecs_render::s_MeshEmulationVertexShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Vertex,
            "ECSRender_MeshEmulationVS"
        ))
            return false;
    }

    if(!m_emulationInputLayout){
        Core::VertexAttributeDesc attributes[6];
        attributes[0]
            .setFormat(Core::Format::RGBA32_FLOAT)
            .setBufferIndex(0)
            .setOffset(0)
            .setElementStride(__hidden_ecs_render::s_EmulatedVertexStride)
            .setName("POSITION")
        ;
        attributes[1]
            .setFormat(Core::Format::RGB32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 4u)
            .setElementStride(__hidden_ecs_render::s_EmulatedVertexStride)
            .setName("NORMAL")
        ;
        attributes[2]
            .setFormat(Core::Format::RGBA32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 8u)
            .setElementStride(__hidden_ecs_render::s_EmulatedVertexStride)
            .setName("TANGENT")
        ;
        attributes[3]
            .setFormat(Core::Format::RG32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 12u)
            .setElementStride(__hidden_ecs_render::s_EmulatedVertexStride)
            .setName("TEXCOORD")
        ;
        attributes[4]
            .setFormat(Core::Format::RGBA32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 16u)
            .setElementStride(__hidden_ecs_render::s_EmulatedVertexStride)
            .setName("COLOR")
        ;
        attributes[5]
            .setFormat(Core::Format::RGBA32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 20u)
            .setElementStride(__hidden_ecs_render::s_EmulatedVertexStride)
            .setName("POSITION1")
        ;

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
        Core::BindingLayoutDesc bindingLayoutDesc;
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

    Core::BindingSetDesc bindingSetDesc;
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

    const usize capacity = __hidden_ecs_render::NextGrowingCapacity(m_instanceBufferCapacity, instanceCount);
    if(capacity > Limit<usize>::s_Max / sizeof(InstanceGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance buffer capacity overflows addressable memory"));
        return false;
    }

    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setDebugName(__hidden_ecs_render::s_InstanceBufferName)
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

bool RendererSystem::reserveMaterialParameterBufferCapacity(const usize parameterCount){
    const usize requiredCount = Max<usize>(parameterCount, 1u);
    if(requiredCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter buffer request exceeds u32 limits"));
        return false;
    }
    if(m_materialParameterBuffer && m_materialParameterBufferCapacity >= requiredCount)
        return true;

    const usize capacity = __hidden_ecs_render::NextGrowingCapacity(m_materialParameterBufferCapacity, requiredCount);
    if(capacity > Limit<usize>::s_Max / sizeof(MaterialParameterGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter buffer capacity overflows addressable memory"));
        return false;
    }

    Core::BufferDesc materialParameterBufferDesc;
    materialParameterBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(MaterialParameterGpuData)))
        .setStructStride(sizeof(MaterialParameterGpuData))
        .setDebugName(__hidden_ecs_render::s_MaterialParameterBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle materialParameterBuffer = m_graphics.createBuffer(materialParameterBufferDesc);
    if(!materialParameterBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create material parameter buffer"));
        return false;
    }

    m_materialParameterBuffer = Move(materialParameterBuffer);
    m_materialParameterBufferCapacity = capacity;
    destroyGeometryBindingSets();
    return true;
}

bool RendererSystem::updateMeshViewBuffer(Core::ICommandList& commandList, const f32 fallbackAspectRatio){
    if(!m_meshViewBuffer){
        Core::BufferDesc meshViewBufferDesc;
        meshViewBufferDesc
            .setByteSize(sizeof(__hidden_ecs_render::MeshViewGpuData))
            .setIsConstantBuffer(true)
            .setDebugName(__hidden_ecs_render::s_MeshViewBufferName)
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

    const __hidden_ecs_render::MeshViewState viewState =
        __hidden_ecs_render::ResolveMeshViewState(m_world, fallbackAspectRatio)
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

bool RendererSystem::uploadMaterialParameterBuffer(Core::ICommandList& commandList, const MaterialParameterGpuDataVector& materialParameters){
    const usize uploadCount = Max<usize>(materialParameters.size(), 1u);
    if(!reserveMaterialParameterBufferCapacity(uploadCount))
        return false;
    if(!m_materialParameterBuffer)
        return false;

    if(uploadCount > Limit<usize>::s_Max / sizeof(MaterialParameterGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter data upload size overflows"));
        return false;
    }

    MaterialParameterGpuData fallbackParameter;
    const MaterialParameterGpuData* data = materialParameters.empty() ? &fallbackParameter : materialParameters.data();
    commandList.setBufferState(m_materialParameterBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_materialParameterBuffer.get(), data, uploadCount * sizeof(MaterialParameterGpuData));
    commandList.setBufferState(m_materialParameterBuffer.get(), Core::ResourceStates::ShaderResource);
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

void RendererSystem::setMaterialPassCommonBufferStates(Core::ICommandList& commandList, const GeometryResources& geometry){
    commandList.setBufferState(geometry.shaderVertexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(geometry.shaderIndexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.setBufferState(m_materialParameterBuffer.get(), Core::ResourceStates::ShaderResource);
}

void RendererSystem::renderMeshMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, GeometryResources& geometry, MaterialPipelineResources& pipelineResources){
        if(!geometry.valid() || !pipelineResources.meshletPipeline || !m_instanceBuffer || !m_meshViewBuffer || !m_materialParameterBuffer)
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

        if(!MaterialPipelinePassUsesRendererAvboit(context.pass)){
            const __hidden_ecs_render::ShaderDrivenPushConstants pushConstants =
                __hidden_ecs_render::BuildShaderDrivenPushConstants(
                    geometry.triangleCount,
                    drawItem.instanceIndex,
                    geometry.sourceVertexLayout,
                    context.viewportState
                );
            context.commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        }
        else{
            const __hidden_ecs_render::TransparentDrawPushConstants pushConstants =
                __hidden_ecs_render::BuildTransparentDrawPushConstants(
                    geometry.triangleCount,
                    drawItem.instanceIndex,
                    geometry.sourceVertexLayout,
                    context.viewportState,
                    *context.avboitTargets,
                    drawItem.alpha
                );
            context.commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        }
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

    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, GeometryResources& geometry, MaterialPipelineResources& pipelineResources){
        if(!geometry.valid() || !pipelineResources.computePipeline || !pipelineResources.emulationPipeline || !m_instanceBuffer || !m_meshViewBuffer || !m_materialParameterBuffer)
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

        const __hidden_ecs_render::ShaderDrivenPushConstants pushConstants =
            __hidden_ecs_render::BuildShaderDrivenPushConstants(
                geometry.triangleCount,
                drawItem.instanceIndex,
                geometry.sourceVertexLayout,
                context.viewportState
            );
        context.commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
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
        if(MaterialPipelinePassUsesRendererAvboit(context.pass)){
            graphicsState.addBindingSet(nullptr);
            graphicsState.addBindingSet(context.passBindingSet);
        }
        else{
            graphicsState.addBindingSet(m_emulationViewBindingSet.get());
            if(context.passBindingSet)
                graphicsState.addBindingSet(context.passBindingSet);
        }

        context.commandList.setGraphicsState(graphicsState);

        if(MaterialPipelinePassUsesRendererAvboit(context.pass)){
            const __hidden_ecs_render::TransparentDrawPushConstants transparentPushConstants =
                __hidden_ecs_render::BuildTransparentDrawPushConstants(
                    geometry.triangleCount,
                    drawItem.instanceIndex,
                    geometry.sourceVertexLayout,
                    context.viewportState,
                    *context.avboitTargets,
                    drawItem.alpha
                );
            context.commandList.setPushConstants(&transparentPushConstants, sizeof(transparentPushConstants));
        }

        Core::DrawArguments drawArgs;
        drawArgs.setVertexCount(geometry.indexCount);
        context.commandList.draw(drawArgs);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

