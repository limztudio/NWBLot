// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"

#include "renderer_capacity_private.h"
#include "timing_names.h"

#include <impl/assets/graphics/csg/binding_slots.h>
#include <impl/assets/graphics/csg/constants.h>
#include <impl/assets/graphics/mesh/binding_slots.h>
#include <impl/assets_material/shader_stage_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_cap_proxy_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateProxyPipeline(
    Core::Device& device,
    Core::Framebuffer* framebuffer,
    Core::ShaderHandle& meshShader,
    Core::ShaderHandle& pixelShader,
    Core::BindingLayoutHandle& capProxyBindingLayout,
    Core::BindingLayoutHandle& clipBindingLayout,
    Core::BindingLayoutHandle& capProxyOpeningMaskBindingLayout,
    Core::MeshletPipelineHandle& pipeline
){
    if(pipeline)
        return true;
    if(!framebuffer)
        return false;

    Core::MeshletPipelineDesc pipelineDesc;
    pipelineDesc
        .setMeshShader(meshShader)
        .setPixelShader(pixelShader)
        .setRenderState(ECSRenderDetail::BuildCsgCapProxyRenderState())
        .addBindingLayout(capProxyBindingLayout)
        .addBindingLayout(clipBindingLayout)
        .addBindingLayout(capProxyOpeningMaskBindingLayout)
    ;

    pipeline = device.createMeshletPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
    if(pipeline)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy meshlet pipeline"));
    return false;
}

[[nodiscard]] static bool CreateProxyComputePipeline(
    Core::Device& device,
    Core::ShaderHandle& computeShader,
    Core::BindingLayoutHandle& capProxyComputeBindingLayout,
    Core::BindingLayoutHandle& clipBindingLayout,
    Core::ComputePipelineHandle& pipeline
){
    if(pipeline)
        return true;

    Core::ComputePipelineDesc computeDesc;
    computeDesc
        .setComputeShader(computeShader)
        .addBindingLayout(capProxyComputeBindingLayout)
        .addBindingLayout(clipBindingLayout)
    ;

    pipeline = device.createComputePipeline(computeDesc);
    if(pipeline)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy compute pipeline"));
    return false;
}

[[nodiscard]] static bool CreateProxyEmulationPipeline(
    Core::Device& device,
    Core::Framebuffer* framebuffer,
    Core::InputLayoutHandle& inputLayout,
    Core::ShaderHandle& vertexShader,
    Core::ShaderHandle& pixelShader,
    Core::BindingLayoutHandle& capProxyBindingLayout,
    Core::BindingLayoutHandle& clipBindingLayout,
    Core::BindingLayoutHandle& capProxyOpeningMaskBindingLayout,
    Core::GraphicsPipelineHandle& pipeline
){
    if(pipeline)
        return true;
    if(!framebuffer)
        return false;

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setInputLayout(inputLayout)
        .setVertexShader(vertexShader)
        .setPixelShader(pixelShader)
        .setRenderState(ECSRenderDetail::BuildCsgCapProxyRenderState())
        .addBindingLayout(capProxyBindingLayout)
        .addBindingLayout(clipBindingLayout)
        .addBindingLayout(capProxyOpeningMaskBindingLayout)
    ;

    pipeline = device.createGraphicsPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
    if(pipeline)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy emulation pipeline"));
    return false;
}

template<typename ShaderSystem>
[[nodiscard]] static bool LoadProxyMeshShader(
    ShaderSystem& shaderSystem,
    Core::ShaderHandle& meshShader,
    const Name& shaderName,
    const char* debugName
){
    if(meshShader)
        return true;
    return shaderSystem.loadShader(
        meshShader,
        shaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Mesh,
        debugName
    );
}

template<typename ShaderSystem>
[[nodiscard]] static bool LoadProxyPixelShader(
    ShaderSystem& shaderSystem,
    Core::ShaderHandle& pixelShader,
    const Name& shaderName,
    const char* debugName
){
    if(pixelShader)
        return true;
    return shaderSystem.loadShader(
        pixelShader,
        shaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        debugName
    );
}

template<typename ShaderSystem>
[[nodiscard]] static bool LoadProxyComputeShader(
    ShaderSystem& shaderSystem,
    Core::ShaderHandle& computeShader,
    const Name& shaderName,
    const char* debugName
){
    if(computeShader)
        return true;
    return shaderSystem.loadShader(
        computeShader,
        shaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        debugName,
        &MaterialShaderStageNames::s_MeshComputeArchiveStageName
    );
}

static void RenderProxyShape(
    Core::Graphics& graphics,
    const MaterialPassDrawContext& context,
    const CsgFrameGpuData& csgFrameData,
    const CsgShapeTypeId shapeType,
    const CsgReceiverPass::Enum receiverPass,
    Core::BindingSet* capProxyBindingSet,
    Core::BindingSet* clipBindingSet,
    Core::BindingSet* capProxyOpeningMaskBindingSet,
    Core::MeshletPipeline* pipeline
){
    if(!pipeline)
        return;

    Core::MeshletState meshletState;
    meshletState.setPipeline(pipeline);
    meshletState.setFramebuffer(context.framebuffer);
    meshletState.setViewport(context.viewportState);
    meshletState.addBindingSet(capProxyBindingSet);
    meshletState.addBindingSet(clipBindingSet);
    meshletState.addBindingSet(capProxyOpeningMaskBindingSet);
    context.commandList.setMeshletState(meshletState);

    const ECSRenderDetail::ShaderDrivenPushConstants pushConstants = ECSRenderDetail::BuildShaderDrivenPushConstants(
        static_cast<u32>(csgFrameData.capProxyGpuItems.size()),
        static_cast<u32>(receiverPass),
        shapeType,
        context.viewportState,
        0u
    );
    context.commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

    {
        Core::GpuTimingMeasure timing(graphics.gpuTiming(), RendererGpuTimingScope::s_MeshDispatch, graphics.getDevice(), context.commandList);

        context.commandList.dispatchMesh(static_cast<u32>(csgFrameData.capProxyGpuItems.size()));
    }
}

static void RenderProxyShapeEmulated(
    Core::Graphics& graphics,
    const MaterialPassDrawContext& context,
    const CsgFrameGpuData& csgFrameData,
    const CsgShapeTypeId shapeType,
    const CsgReceiverPass::Enum receiverPass,
    Core::BindingSet* capProxyBindingSet,
    Core::BindingSet* capProxyComputeBindingSet,
    Core::BindingSet* clipBindingSet,
    Core::BindingSet* capProxyOpeningMaskBindingSet,
    Core::Buffer* emulationVertexBuffer,
    Core::ComputePipeline* computePipeline,
    Core::GraphicsPipeline* pipeline
){
    if(!computePipeline || !pipeline || !emulationVertexBuffer)
        return;

    const ECSRenderDetail::ShaderDrivenPushConstants pushConstants = ECSRenderDetail::BuildShaderDrivenPushConstants(
        static_cast<u32>(csgFrameData.capProxyGpuItems.size()),
        static_cast<u32>(receiverPass),
        shapeType,
        context.viewportState,
        0u
    );

    context.commandList.setBufferState(emulationVertexBuffer, Core::ResourceStates::UnorderedAccess);
    context.commandList.setResourceStatesForBindingSet(capProxyComputeBindingSet);
    context.commandList.setResourceStatesForBindingSet(clipBindingSet);
    context.commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(computePipeline);
    computeState.addBindingSet(capProxyComputeBindingSet);
    computeState.addBindingSet(clipBindingSet);
    context.commandList.setComputeState(computeState);
    context.commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

    {
        Core::GpuTimingMeasure timing(graphics.gpuTiming(), RendererGpuTimingScope::s_MeshDispatch, graphics.getDevice(), context.commandList);

        context.commandList.dispatch(static_cast<u32>(csgFrameData.capProxyGpuItems.size()));
    }

    context.commandList.setBufferState(emulationVertexBuffer, Core::ResourceStates::VertexBuffer);
    context.commandList.setResourceStatesForBindingSet(capProxyBindingSet);
    context.commandList.setResourceStatesForBindingSet(clipBindingSet);
    context.commandList.setResourceStatesForBindingSet(capProxyOpeningMaskBindingSet);
    context.commandList.commitBarriers();

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(pipeline);
    graphicsState.setFramebuffer(context.framebuffer);
    graphicsState.setViewport(context.viewportState);
    graphicsState.addVertexBuffer(
        Core::VertexBufferBinding()
            .setBuffer(emulationVertexBuffer)
            .setSlot(NWB_MESH_EMULATION_VERTEX_BUFFER_INDEX)
            .setOffset(0)
    );
    graphicsState.addBindingSet(capProxyBindingSet);
    graphicsState.addBindingSet(clipBindingSet);
    graphicsState.addBindingSet(capProxyOpeningMaskBindingSet);
    context.commandList.setGraphicsState(graphicsState);
    context.commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(static_cast<u32>(
        csgFrameData.capProxyGpuItems.size() * static_cast<usize>(ECSRenderDetail::s_CsgCapProxyEmulationVerticesPerProxy)
    ));
    {
        Core::GpuTimingMeasure timing(graphics.gpuTiming(), RendererGpuTimingScope::s_Raster, graphics.getDevice(), context.commandList);

        context.commandList.draw(drawArgs);
    }
}

[[nodiscard]] static CsgCapProxyShapeResources* FindProxyShapeResources(
    Vector<CsgCapProxyShapeResources, Core::Alloc::GlobalArena>& shapeResources,
    const CsgShapeTypeId shapeType
){
    for(CsgCapProxyShapeResources& resources : shapeResources){
        if(resources.shapeType == shapeType)
            return &resources;
    }

    return nullptr;
}

[[nodiscard]] static const CsgCapProxyShapeResources* FindProxyShapeResources(
    const Vector<CsgCapProxyShapeResources, Core::Alloc::GlobalArena>& shapeResources,
    const CsgShapeTypeId shapeType
){
    for(const CsgCapProxyShapeResources& resources : shapeResources){
        if(resources.shapeType == shapeType)
            return &resources;
    }

    return nullptr;
}

[[nodiscard]] static CsgCapProxyShapeResources& EnsureProxyShapeResources(
    Vector<CsgCapProxyShapeResources, Core::Alloc::GlobalArena>& shapeResources,
    const CsgShapeTypeId shapeType
){
    if(CsgCapProxyShapeResources* resources = FindProxyShapeResources(shapeResources, shapeType))
        return *resources;

    CsgCapProxyShapeResources resources;
    resources.shapeType = shapeType;
    shapeResources.push_back(Move(resources));
    return shapeResources[shapeResources.size() - 1u];
}

[[nodiscard]] static bool ProxyShapeResourcesReady(
    const Vector<CsgCapProxyShapeResources, Core::Alloc::GlobalArena>& shapeResources,
    const CsgCapProxyShapeTypeVector& neededShapeTypes,
    const bool meshSupported
){
    for(const CsgShapeTypeId shapeType : neededShapeTypes){
        const CsgCapProxyShapeResources* resources = FindProxyShapeResources(shapeResources, shapeType);
        if(!resources)
            return false;
        if(meshSupported && !resources->pipeline)
            return false;
        if(!meshSupported && (!resources->computePipeline || !resources->emulationPipeline))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgCapProxyOpeningMaskResources(Core::Texture* openingMask){
    auto* device = graphics().getDevice();
    if(!csgState().m_capProxyOpeningMaskBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CSG_BINDING_CAP_PROXY_OPENING_MASK, 1));

        csgState().m_capProxyOpeningMaskBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!csgState().m_capProxyOpeningMaskBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy opening mask binding layout"));
            return false;
        }
    }

    if(csgState().m_capProxyOpeningMaskBindingSet)
        return true;
    if(!openingMask){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG cap proxy opening mask resources require a valid opening mask target"));
        return false;
    }

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CSG_BINDING_CAP_PROXY_OPENING_MASK,
        openingMask,
        openingMask->getDescription().format,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    csgState().m_capProxyOpeningMaskBindingSet = device->createBindingSet(bindingSetDesc, csgState().m_capProxyOpeningMaskBindingLayout);
    if(!csgState().m_capProxyOpeningMaskBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy opening mask binding set"));
        return false;
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgCapProxyResources(Core::Framebuffer* framebuffer, const CsgCapProxyShapeTypeVector& shapeTypes){
    if(!framebuffer)
        return false;
    if(shapeTypes.empty())
        return true;
    if(!createCsgClipResources())
        return false;
    if(!csgState().m_capProxyOpeningMaskBindingLayout)
        return false;

    auto* device = graphics().getDevice();
    const bool meshSupported = graphics().queryFeatureSupport(Core::Feature::Meshlets);

    if(!csgState().m_capProxyBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Vertex | Core::ShaderType::Mesh | Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CSG_BINDING_CAP_PROXIES, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::ShaderDrivenPushConstants)));

        csgState().m_capProxyBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!csgState().m_capProxyBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy binding layout"));
            return false;
        }
    }

    if(!meshSupported){
        if(!m_renderer.materialSystem().createComputeEmulationResources())
            return false;
        if(!csgState().m_capProxyComputeBindingLayout){
            Core::BindingLayoutDesc bindingLayoutDesc(arena());
            bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CSG_BINDING_CAP_PROXIES, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_CSG_BINDING_CAP_PROXY_GENERATED_VERTEX, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::ShaderDrivenPushConstants)));

            csgState().m_capProxyComputeBindingLayout = device->createBindingLayout(bindingLayoutDesc);
            if(!csgState().m_capProxyComputeBindingLayout){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy compute binding layout"));
                return false;
            }
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: CSG cap proxy selected CS + VS + PS compute emulation"));
        }
    }

    auto& shaderSystem = m_renderer.shaderSystem();
    Core::Device& pipelineDevice = *device;
    for(const CsgShapeTypeId shapeType : shapeTypes){
        CsgShapeTypeInfo shapeTypeInfo;
        if(!csgShapeRegistry().findShapeType(shapeType, shapeTypeInfo)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to resolve CSG cap proxy shape type {}"), shapeType);
            return false;
        }
        if(!shapeTypeInfo.desc.capProxyShader){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG shape '{}' has cap proxy work but no proxy shader"), StringConvert(shapeTypeInfo.desc.name.c_str()));
            return false;
        }
        if(!shapeTypeInfo.desc.capProxyPixelShader){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG shape '{}' has cap proxy work but no proxy pixel shader"), StringConvert(shapeTypeInfo.desc.name.c_str()));
            return false;
        }

        CsgCapProxyShapeResources& resources = __hidden_csg_cap_proxy_resources::EnsureProxyShapeResources(
            csgState().m_capProxyShapeResources,
            shapeType
        );
        if(!__hidden_csg_cap_proxy_resources::LoadProxyPixelShader(
            shaderSystem,
            resources.pixelShader,
            shapeTypeInfo.desc.capProxyPixelShader,
            "ECSRender_CsgCapProxyShapePS"
        ))
            return false;
        if(meshSupported){
            if(!__hidden_csg_cap_proxy_resources::LoadProxyMeshShader(
                shaderSystem,
                resources.meshShader,
                shapeTypeInfo.desc.capProxyShader,
                "ECSRender_CsgCapProxyShapeMS"
            ))
                return false;
            if(!__hidden_csg_cap_proxy_resources::CreateProxyPipeline(
                pipelineDevice,
                framebuffer,
                resources.meshShader,
                resources.pixelShader,
                csgState().m_capProxyBindingLayout,
                csgState().m_clipBindingLayout,
                csgState().m_capProxyOpeningMaskBindingLayout,
                resources.pipeline
            ))
                return false;
        }else{
            if(!__hidden_csg_cap_proxy_resources::LoadProxyComputeShader(
                shaderSystem,
                resources.computeShader,
                shapeTypeInfo.desc.capProxyShader,
                "ECSRender_CsgCapProxyShapeCS"
            ))
                return false;
            if(!__hidden_csg_cap_proxy_resources::CreateProxyComputePipeline(
                pipelineDevice,
                resources.computeShader,
                csgState().m_capProxyComputeBindingLayout,
                csgState().m_clipBindingLayout,
                resources.computePipeline
            ))
                return false;
            if(!__hidden_csg_cap_proxy_resources::CreateProxyEmulationPipeline(
                pipelineDevice,
                framebuffer,
                drawState().m_emulationInputLayout,
                drawState().m_emulationVertexShader,
                resources.pixelShader,
                csgState().m_capProxyBindingLayout,
                csgState().m_clipBindingLayout,
                csgState().m_capProxyOpeningMaskBindingLayout,
                resources.emulationPipeline
            ))
                return false;
        }
    }

    return true;
}

bool RendererCsgSystem::reserveCsgCapProxyBufferCapacity(const usize proxyCount){
    if(proxyCount == 0u)
        return true;
    if(csgState().m_capProxyBuffer && csgState().m_capProxyBufferCapacity >= proxyCount)
        return true;
    if(proxyCount > Limit<usize>::s_Max / sizeof(CsgCapProxyGpuData))
        return false;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(csgState().m_capProxyBufferCapacity, proxyCount);
    if(capacity > Limit<usize>::s_Max / sizeof(CsgCapProxyGpuData))
        return false;

    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(CsgCapProxyGpuData)))
        .setStructStride(sizeof(CsgCapProxyGpuData))
        .setDebugName(ECSRenderDetail::s_CsgCapProxyBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    Core::BufferHandle createdBuffer = graphics().createBuffer(bufferDesc);
    if(!createdBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy buffer"));
        return false;
    }

    csgState().m_capProxyBuffer = Move(createdBuffer);
    csgState().m_capProxyBufferCapacity = capacity;
    csgState().m_capProxyBindingSet.reset();
    csgState().m_capProxyComputeBindingSet.reset();
    return true;
}

bool RendererCsgSystem::reserveCsgCapProxyEmulationVertexBufferCapacity(const usize proxyCount){
    if(proxyCount == 0u)
        return true;
    if(csgState().m_capProxyEmulationVertexBuffer && csgState().m_capProxyEmulationVertexCapacity >= proxyCount)
        return true;
    if(proxyCount > Limit<usize>::s_Max / ECSRenderDetail::s_CsgCapProxyEmulationVerticesPerProxy)
        return false;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(csgState().m_capProxyEmulationVertexCapacity, proxyCount);
    if(capacity > Limit<usize>::s_Max / ECSRenderDetail::s_CsgCapProxyEmulationVerticesPerProxy)
        return false;
    const usize vertexCapacity = capacity * static_cast<usize>(ECSRenderDetail::s_CsgCapProxyEmulationVerticesPerProxy);
    if(vertexCapacity > Limit<usize>::s_Max / ECSRenderDetail::s_EmulatedVertexStride)
        return false;

    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(static_cast<u64>(vertexCapacity * ECSRenderDetail::s_EmulatedVertexStride))
        .setStructStride(ECSRenderDetail::s_EmulatedVertexStride)
        .setCanHaveUAVs(true)
        .setIsVertexBuffer(true)
        .setDebugName(ECSRenderDetail::s_CsgCapProxyEmulationVertexBufferName)
    ;

    Core::BufferHandle createdBuffer = graphics().createBuffer(bufferDesc);
    if(!createdBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy emulation vertex buffer"));
        return false;
    }

    csgState().m_capProxyEmulationVertexBuffer = Move(createdBuffer);
    csgState().m_capProxyEmulationVertexCapacity = capacity;
    csgState().m_capProxyComputeBindingSet.reset();
    return true;
}

bool RendererCsgSystem::uploadCsgCapProxies(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData){
    if(!csgFrameData.hasCapProxyWork())
        return true;
    if(csgFrameData.capProxyGpuItems.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;
    if(!reserveCsgCapProxyBufferCapacity(csgFrameData.capProxyGpuItems.size()))
        return false;
    if(!csgState().m_capProxyBindingLayout || !drawState().m_meshViewBuffer)
        return false;
    const bool computeEmulationActive = csgState().m_capProxyComputeBindingLayout != nullptr;
    if(computeEmulationActive && csgFrameData.capProxyGpuItems.size() > static_cast<usize>(Limit<u32>::s_Max / ECSRenderDetail::s_CsgCapProxyEmulationVerticesPerProxy))
        return false;
    if(computeEmulationActive && !reserveCsgCapProxyEmulationVertexBufferCapacity(csgFrameData.capProxyGpuItems.size()))
        return false;
    if(!csgState().m_capProxyBindingSet){
        Core::BindingSetDesc bindingSetDesc(arena());
        bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CSG_BINDING_CAP_PROXIES, csgState().m_capProxyBuffer.get()));
        bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, drawState().m_meshViewBuffer.get()));
        csgState().m_capProxyBindingSet = graphics().getDevice()->createBindingSet(bindingSetDesc, csgState().m_capProxyBindingLayout);
        if(!csgState().m_capProxyBindingSet){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy binding set"));
            return false;
        }
    }
    if(computeEmulationActive && !csgState().m_capProxyComputeBindingSet){
        Core::BindingSetDesc bindingSetDesc(arena());
        bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CSG_BINDING_CAP_PROXIES, csgState().m_capProxyBuffer.get()));
        bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_MESH_BINDING_VIEW, drawState().m_meshViewBuffer.get()));
        bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(
            NWB_CSG_BINDING_CAP_PROXY_GENERATED_VERTEX,
            csgState().m_capProxyEmulationVertexBuffer.get()
        ));
        csgState().m_capProxyComputeBindingSet = graphics().getDevice()->createBindingSet(bindingSetDesc, csgState().m_capProxyComputeBindingLayout);
        if(!csgState().m_capProxyComputeBindingSet){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap proxy compute binding set"));
            return false;
        }
    }

    commandList.setBufferState(csgState().m_capProxyBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(
        csgState().m_capProxyBuffer.get(),
        csgFrameData.capProxyGpuItems.data(),
        csgFrameData.capProxyGpuItems.size() * sizeof(CsgCapProxyGpuData)
    );
    commandList.setBufferState(csgState().m_capProxyBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

void RendererCsgSystem::renderCsgOpaqueCapProxies(
    const MaterialPassDrawContext& context,
    const CsgFrameGpuData& csgFrameData,
    Core::Texture* openingMaskTarget
){
    if(!csgFrameData.hasCapProxyWork())
        return;
    if(!csgState().m_capProxyBuffer)
        return;
    if(!openingMaskTarget)
        return;
    const bool meshSupported = graphics().queryFeatureSupport(Core::Feature::Meshlets);
    const bool proxyResourcesReady =
        csgState().m_capProxyBindingLayout
        && csgState().m_clipBindingLayout
        && csgState().m_capProxyOpeningMaskBindingLayout
        && __hidden_csg_cap_proxy_resources::ProxyShapeResourcesReady(csgState().m_capProxyShapeResources, csgFrameData.capProxyShapeTypes, meshSupported)
    ;
    if(!proxyResourcesReady)
        return;
    if(!csgState().m_clipBindingSet)
        return;
    if(!csgState().m_capProxyOpeningMaskBindingSet)
        return;
    if(!csgState().m_capProxyBindingSet)
        return;
    if(!meshSupported && (!csgState().m_capProxyComputeBindingSet || !csgState().m_capProxyEmulationVertexBuffer))
        return;

    context.commandList.setBufferState(drawState().m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    context.commandList.setBufferState(csgState().m_capProxyBuffer.get(), Core::ResourceStates::ShaderResource);
    context.commandList.setTextureState(openingMaskTarget, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    setCsgClipBufferStates(context.commandList);
    context.commandList.setResourceStatesForBindingSet(csgState().m_capProxyBindingSet.get());
    context.commandList.setResourceStatesForBindingSet(csgState().m_clipBindingSet.get());
    context.commandList.setResourceStatesForBindingSet(csgState().m_capProxyOpeningMaskBindingSet.get());
    context.commandList.commitBarriers();

    for(const CsgShapeTypeId shapeType : csgFrameData.capProxyShapeTypes){
        const CsgCapProxyShapeResources* shapeResources = __hidden_csg_cap_proxy_resources::FindProxyShapeResources(
            csgState().m_capProxyShapeResources,
            shapeType
        );
        if(!shapeResources)
            continue;

        if(meshSupported){
            __hidden_csg_cap_proxy_resources::RenderProxyShape(
                graphics(),
                context,
                csgFrameData,
                shapeType,
                CsgReceiverPass::Opaque,
                csgState().m_capProxyBindingSet.get(),
                csgState().m_clipBindingSet.get(),
                csgState().m_capProxyOpeningMaskBindingSet.get(),
                shapeResources->pipeline.get()
            );
        }else{
            __hidden_csg_cap_proxy_resources::RenderProxyShapeEmulated(
                graphics(),
                context,
                csgFrameData,
                shapeType,
                CsgReceiverPass::Opaque,
                csgState().m_capProxyBindingSet.get(),
                csgState().m_capProxyComputeBindingSet.get(),
                csgState().m_clipBindingSet.get(),
                csgState().m_capProxyOpeningMaskBindingSet.get(),
                csgState().m_capProxyEmulationVertexBuffer.get(),
                shapeResources->computePipeline.get(),
                shapeResources->emulationPipeline.get()
            );
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
