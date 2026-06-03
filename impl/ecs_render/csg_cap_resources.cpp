// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"

#include "renderer_capacity_private.h"
#include "timing_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_cap_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void SetCapVertexAttributes(Core::VertexAttributeDesc (&attributes)[5]){
    attributes[0]
        .setFormat(Core::Format::RGBA32_FLOAT)
        .setBufferIndex(0u)
        .setOffset(offsetof(CsgCapVertexGpuData, positionReceiverIndex))
        .setElementStride(sizeof(CsgCapVertexGpuData))
        .setName("POSITION")
    ;
    attributes[1]
        .setFormat(Core::Format::RGBA32_FLOAT)
        .setBufferIndex(0u)
        .setOffset(offsetof(CsgCapVertexGpuData, normalCutterIndex))
        .setElementStride(sizeof(CsgCapVertexGpuData))
        .setName("NORMAL")
    ;
    attributes[2]
        .setFormat(Core::Format::RGBA32_FLOAT)
        .setBufferIndex(0u)
        .setOffset(offsetof(CsgCapVertexGpuData, tangent))
        .setElementStride(sizeof(CsgCapVertexGpuData))
        .setName("TANGENT")
    ;
    attributes[3]
        .setFormat(Core::Format::RGBA32_FLOAT)
        .setBufferIndex(0u)
        .setOffset(offsetof(CsgCapVertexGpuData, color))
        .setElementStride(sizeof(CsgCapVertexGpuData))
        .setName("COLOR")
    ;
    attributes[4]
        .setFormat(Core::Format::RGBA32_FLOAT)
        .setBufferIndex(0u)
        .setOffset(offsetof(CsgCapVertexGpuData, uv0))
        .setElementStride(sizeof(CsgCapVertexGpuData))
        .setName("TEXCOORD")
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgCapSharedResources(){
    if(!m_renderer.materialSystem().createEmulationViewResources())
        return false;
    if(!createCsgClipResources())
        return false;

    auto* device = graphics().getDevice();
    if(!csgState().m_capVertexShader){
        if(!m_renderer.shaderSystem().loadShader(
            csgState().m_capVertexShader,
            ECSRenderDetail::s_CsgCapVertexShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Vertex,
            "ECSRender_CsgCapVS"
        ))
            return false;
    }
    if(!csgState().m_capInputLayout){
        Core::VertexAttributeDesc attributes[5];
        __hidden_csg_cap_resources::SetCapVertexAttributes(attributes);

        csgState().m_capInputLayout = device->createInputLayout(attributes, 5u, csgState().m_capVertexShader.get());
        if(!csgState().m_capInputLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap input layout"));
            return false;
        }
    }

    return true;
}

bool RendererCsgSystem::createCsgOpaqueCapResources(Core::Framebuffer* framebuffer){
    if(!framebuffer)
        return false;
    if(csgState().m_capPipeline)
        return true;
    if(!createCsgCapSharedResources())
        return false;
    if(!csgState().m_capPixelShader){
        if(!m_renderer.shaderSystem().loadShader(
            csgState().m_capPixelShader,
            ECSRenderDetail::s_CsgCapPixelShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Pixel,
            "ECSRender_CsgCapPS"
        ))
            return false;
    }

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setInputLayout(csgState().m_capInputLayout)
        .setVertexShader(csgState().m_capVertexShader)
        .setPixelShader(csgState().m_capPixelShader)
        .setRenderState(ECSRenderDetail::BuildRenderStateForPass(MaterialPipelinePass::Opaque, true))
        .addBindingLayout(drawState().m_emulationViewBindingLayout)
        .addBindingLayout(csgState().m_clipBindingLayout)
    ;
    csgState().m_capPipeline = graphics().getDevice()->createGraphicsPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
    if(!csgState().m_capPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap pipeline"));
        return false;
    }

    return true;
}

bool RendererCsgSystem::createCsgTransparentCapResources(Core::Framebuffer* framebuffer, const MaterialPipelinePass::Enum pass){
    if(!framebuffer)
        return false;
    if(!createCsgCapSharedResources())
        return false;
    if(!m_renderer.avboitSystem().createAvboitResources())
        return false;

    Core::ShaderHandle* pixelShader = nullptr;
    Core::GraphicsPipelineHandle* pipeline = nullptr;
    Core::BindingLayoutHandle* passBindingLayout = nullptr;
    const Name* pixelShaderName = nullptr;
    const char* pixelShaderDebugName = nullptr;
    switch(pass){
    case MaterialPipelinePass::AvboitOccupancy:
        pixelShader = &csgState().m_capAvboitOccupancyPixelShader;
        pipeline = &csgState().m_capAvboitOccupancyPipeline;
        passBindingLayout = &avboitState().m_occupancyBindingLayout;
        pixelShaderName = &ECSRenderDetail::s_CsgTransparentCapOccupancyPixelShaderName;
        pixelShaderDebugName = "ECSRender_CsgTransparentCapOccupancyPS";
        break;
    case MaterialPipelinePass::AvboitExtinction:
        pixelShader = &csgState().m_capAvboitExtinctionPixelShader;
        pipeline = &csgState().m_capAvboitExtinctionPipeline;
        passBindingLayout = &avboitState().m_extinctionBindingLayout;
        pixelShaderName = &ECSRenderDetail::s_CsgTransparentCapExtinctionPixelShaderName;
        pixelShaderDebugName = "ECSRender_CsgTransparentCapExtinctionPS";
        break;
    case MaterialPipelinePass::AvboitAccumulate:
        pixelShader = &csgState().m_capAvboitAccumulatePixelShader;
        pipeline = &csgState().m_capAvboitAccumulatePipeline;
        passBindingLayout = &avboitState().m_accumulateBindingLayout;
        pixelShaderName = &ECSRenderDetail::s_CsgTransparentCapAccumulatePixelShaderName;
        pixelShaderDebugName = "ECSRender_CsgTransparentCapAccumulatePS";
        break;
    default:
        return false;
    }
    NWB_ASSERT(pixelShader);
    NWB_ASSERT(pipeline);
    NWB_ASSERT(passBindingLayout);
    NWB_ASSERT(pixelShaderName);

    if(*pipeline)
        return true;
    if(!*pixelShader){
        if(!m_renderer.shaderSystem().loadShader(
            *pixelShader,
            *pixelShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Pixel,
            pixelShaderDebugName
        ))
            return false;
    }

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setInputLayout(csgState().m_capInputLayout)
        .setVertexShader(csgState().m_capVertexShader)
        .setPixelShader(*pixelShader)
        .setRenderState(ECSRenderDetail::BuildRenderStateForPass(pass, true))
        .addBindingLayout(drawState().m_emulationViewBindingLayout)
        .addBindingLayout(*passBindingLayout)
        .addBindingLayout(csgState().m_clipBindingLayout)
    ;
    *pipeline = graphics().getDevice()->createGraphicsPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
    if(*pipeline)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create transparent CSG cap pipeline"));
    return false;
}

bool RendererCsgSystem::reserveCsgCapVertexBufferCapacity(const usize vertexCount){
    if(vertexCount == 0u)
        return true;
    if(csgState().m_capVertexBuffer && csgState().m_capVertexBufferCapacity >= vertexCount)
        return true;
    if(vertexCount > Limit<usize>::s_Max / sizeof(CsgCapVertexGpuData))
        return false;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(csgState().m_capVertexBufferCapacity, vertexCount);
    if(capacity > Limit<usize>::s_Max / sizeof(CsgCapVertexGpuData))
        return false;

    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(CsgCapVertexGpuData)))
        .setStructStride(sizeof(CsgCapVertexGpuData))
        .setIsVertexBuffer(true)
        .setDebugName(ECSRenderDetail::s_CsgCapVertexBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    Core::BufferHandle createdBuffer = graphics().createBuffer(bufferDesc);
    if(!createdBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap vertex buffer"));
        return false;
    }

    csgState().m_capVertexBuffer = Move(createdBuffer);
    csgState().m_capVertexBufferCapacity = capacity;
    return true;
}

bool RendererCsgSystem::uploadCsgCapVertices(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData){
    if(!csgFrameData.hasCapWork())
        return true;
    if(!reserveCsgCapVertexBufferCapacity(csgFrameData.capVertices.size()))
        return false;

    commandList.setBufferState(csgState().m_capVertexBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(
        csgState().m_capVertexBuffer.get(),
        csgFrameData.capVertices.data(),
        csgFrameData.capVertices.size() * sizeof(CsgCapVertexGpuData)
    );
    commandList.setBufferState(csgState().m_capVertexBuffer.get(), Core::ResourceStates::VertexBuffer);
    commandList.commitBarriers();
    return true;
}

void RendererCsgSystem::renderCsgCaps(
    const MaterialPassDrawContext& context,
    const CsgFrameGpuData& csgFrameData,
    const CsgCapDrawItemVector& capDrawItems,
    Core::GraphicsPipeline* pipeline
){
    if(!csgFrameData.hasCapWork() || capDrawItems.empty())
        return;
    if(!csgState().m_capVertexBuffer)
        return;
    if(!createCsgClipResources() || !csgState().m_clipBindingSet)
        return;
    if(!pipeline)
        return;
    if(MaterialPipelinePassUsesRendererAvboit(context.pass) && (!context.passBindingSet || !context.avboitTargets))
        return;

    context.commandList.setBufferState(csgState().m_capVertexBuffer.get(), Core::ResourceStates::VertexBuffer);
    context.commandList.setBufferState(drawState().m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    setCsgClipBufferStates(context.commandList);
    if(context.passBindingSet)
        context.commandList.setResourceStatesForBindingSet(context.passBindingSet);

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(pipeline);
    graphicsState.setFramebuffer(context.framebuffer);
    graphicsState.setViewport(context.viewportState);
    graphicsState.addVertexBuffer(
        Core::VertexBufferBinding()
            .setBuffer(csgState().m_capVertexBuffer.get())
            .setSlot(0u)
            .setOffset(0u)
    );
    graphicsState.addBindingSet(drawState().m_emulationViewBindingSet.get());
    if(context.passBindingSet)
        graphicsState.addBindingSet(context.passBindingSet);
    graphicsState.addBindingSet(csgState().m_clipBindingSet.get());
    context.commandList.setGraphicsState(graphicsState);

    if(MaterialPipelinePassUsesRendererAvboit(context.pass)){
        ECSRenderDetail::SetTransparentDrawPushConstants(
            context.commandList,
            0u,
            0u,
            0u,
            context.viewportState,
            *context.avboitTargets,
            0u
        );
    }

    for(const CsgCapDrawItem& drawItem : capDrawItems){
        Core::DrawArguments drawArgs;
        drawArgs.setVertexCount(drawItem.vertexCount);
        drawArgs.setStartVertexLocation(drawItem.firstVertex);
        {
            Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_Raster, graphics().getDevice(), context.commandList);

            context.commandList.draw(drawArgs);
        }
    }
}

void RendererCsgSystem::renderCsgOpaqueCaps(
    const MaterialPassDrawContext& context,
    const CsgFrameGpuData& csgFrameData
){
    if(!csgFrameData.hasOpaqueCapWork())
        return;
    if(!createCsgOpaqueCapResources(context.framebuffer))
        return;

    renderCsgCaps(context, csgFrameData, csgFrameData.opaqueCapDrawItems, csgState().m_capPipeline.get());
}

void RendererCsgSystem::renderCsgTransparentCaps(
    const MaterialPassDrawContext& context,
    const CsgFrameGpuData& csgFrameData
){
    if(!csgFrameData.hasTransparentCapWork())
        return;
    if(!createCsgTransparentCapResources(context.framebuffer, context.pass))
        return;

    Core::GraphicsPipeline* pipeline = nullptr;
    switch(context.pass){
    case MaterialPipelinePass::AvboitOccupancy:
        pipeline = csgState().m_capAvboitOccupancyPipeline.get();
        break;
    case MaterialPipelinePass::AvboitExtinction:
        pipeline = csgState().m_capAvboitExtinctionPipeline.get();
        break;
    case MaterialPipelinePass::AvboitAccumulate:
        pipeline = csgState().m_capAvboitAccumulatePipeline.get();
        break;
    default:
        break;
    }

    renderCsgCaps(context, csgFrameData, csgFrameData.transparentCapDrawItems, pipeline);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
