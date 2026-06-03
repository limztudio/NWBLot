// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"

#include "csg_cap_builder.h"
#include "renderer_capacity_private.h"
#include "timing_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_caps{


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

[[nodiscard]] const u8* CsgCutterParameterBytes(const CsgFrameGpuData& csgFrameData, const CsgCutterGpuData& cutter){
    if(cutter.parameterByteSize == 0u)
        return nullptr;
    const usize byteOffset = static_cast<usize>(cutter.parameterByteOffset);
    const usize byteSize = static_cast<usize>(cutter.parameterByteSize);
    if(byteOffset > csgFrameData.parameterBytes.size() || byteSize > csgFrameData.parameterBytes.size() - byteOffset)
        return nullptr;

    return csgFrameData.parameterBytes.data() + byteOffset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::appendCsgReceiverCapGeometry(
    const MeshResources& mesh,
    const Scene::TransformComponent* transform,
    const u32 receiverIndex,
    const CsgReceiverRangeGpuData& receiverRange,
    CsgFrameGpuData& csgFrameData,
    CsgCapDrawItemVector& capDrawItems
)const{
    if(mesh.csgCapTriangles.empty() || receiverRange.cutterCount == 0u)
        return true;

    if(receiverIndex >= csgFrameData.receiverRanges.size())
        return false;
    if(static_cast<usize>(receiverRange.firstCutter) + static_cast<usize>(receiverRange.cutterCount) > csgFrameData.cutters.size())
        return false;

    Core::Alloc::ScratchArena& scratchArena = csgFrameData.capVertices.get_allocator().arena();
    for(u32 cutterOffset = 0u; cutterOffset < receiverRange.cutterCount; ++cutterOffset){
        const u32 cutterIndex = receiverRange.firstCutter + cutterOffset;
        const CsgCutterGpuData& cutter = csgFrameData.cutters[cutterIndex];
        CsgShapeTypeInfo shapeType;
        const CsgShapeTypeInfo* shapeTypePtr = nullptr;
        if(m_csgShapeRegistry.findShapeType(cutter.shapeType, shapeType))
            shapeTypePtr = &shapeType;

        const u8* parameterBytes = __hidden_csg_caps::CsgCutterParameterBytes(csgFrameData, cutter);
        if(cutter.parameterByteSize != 0u && !parameterBytes)
            return false;

        if(csgFrameData.capVertices.size() > static_cast<usize>(Limit<u32>::s_Max))
            return false;
        const u32 firstVertex = static_cast<u32>(csgFrameData.capVertices.size());
        if(!ECSRenderCsgCapBuilder::AppendCapGeometry(
            mesh.csgCapTriangles,
            transform,
            receiverIndex,
            cutter,
            shapeTypePtr,
            parameterBytes,
            static_cast<usize>(cutter.parameterByteSize),
            cutterIndex,
            csgFrameData.capVertices,
            scratchArena
        ))
            return false;

        const usize appendedVertexCount = csgFrameData.capVertices.size() - static_cast<usize>(firstVertex);
        if(appendedVertexCount == 0u)
            continue;
        if(appendedVertexCount > static_cast<usize>(Limit<u32>::s_Max))
            return false;

        capDrawItems.push_back(CsgCapDrawItem{
            firstVertex,
            static_cast<u32>(appendedVertexCount),
        });
    }

    return true;
}

bool RendererCsgSystem::createCsgCapSharedResources(){
    if(!materialSystem().createEmulationViewResources())
        return false;
    if(!createCsgClipResources())
        return false;

    auto* device = m_graphics.getDevice();
    if(!m_csgState.m_capVertexShader){
        if(!shaderSystem().loadShader(
            m_csgState.m_capVertexShader,
            ECSRenderDetail::s_CsgCapVertexShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Vertex,
            "ECSRender_CsgCapVS"
        ))
            return false;
    }
    if(!m_csgState.m_capInputLayout){
        Core::VertexAttributeDesc attributes[5];
        __hidden_csg_caps::SetCapVertexAttributes(attributes);

        m_csgState.m_capInputLayout = device->createInputLayout(attributes, 5u, m_csgState.m_capVertexShader.get());
        if(!m_csgState.m_capInputLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap input layout"));
            return false;
        }
    }

    return true;
}

bool RendererCsgSystem::createCsgOpaqueCapResources(Core::Framebuffer* framebuffer){
    if(!framebuffer)
        return false;
    if(m_csgState.m_capPipeline)
        return true;
    if(!createCsgCapSharedResources())
        return false;
    if(!m_csgState.m_capPixelShader){
        if(!shaderSystem().loadShader(
            m_csgState.m_capPixelShader,
            ECSRenderDetail::s_CsgCapPixelShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Pixel,
            "ECSRender_CsgCapPS"
        ))
            return false;
    }

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setInputLayout(m_csgState.m_capInputLayout)
        .setVertexShader(m_csgState.m_capVertexShader)
        .setPixelShader(m_csgState.m_capPixelShader)
        .setRenderState(ECSRenderDetail::BuildRenderStateForPass(MaterialPipelinePass::Opaque, true))
        .addBindingLayout(m_drawState.m_emulationViewBindingLayout)
        .addBindingLayout(m_csgState.m_clipBindingLayout)
    ;
    m_csgState.m_capPipeline = m_graphics.getDevice()->createGraphicsPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
    if(!m_csgState.m_capPipeline){
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
    if(!avboitSystem().createAvboitResources())
        return false;

    Core::ShaderHandle* pixelShader = nullptr;
    Core::GraphicsPipelineHandle* pipeline = nullptr;
    Core::BindingLayoutHandle* passBindingLayout = nullptr;
    const Name* pixelShaderName = nullptr;
    const char* pixelShaderDebugName = nullptr;
    switch(pass){
    case MaterialPipelinePass::AvboitOccupancy:
        pixelShader = &m_csgState.m_capAvboitOccupancyPixelShader;
        pipeline = &m_csgState.m_capAvboitOccupancyPipeline;
        passBindingLayout = &m_avboitState.m_occupancyBindingLayout;
        pixelShaderName = &ECSRenderDetail::s_CsgTransparentCapOccupancyPixelShaderName;
        pixelShaderDebugName = "ECSRender_CsgTransparentCapOccupancyPS";
        break;
    case MaterialPipelinePass::AvboitExtinction:
        pixelShader = &m_csgState.m_capAvboitExtinctionPixelShader;
        pipeline = &m_csgState.m_capAvboitExtinctionPipeline;
        passBindingLayout = &m_avboitState.m_extinctionBindingLayout;
        pixelShaderName = &ECSRenderDetail::s_CsgTransparentCapExtinctionPixelShaderName;
        pixelShaderDebugName = "ECSRender_CsgTransparentCapExtinctionPS";
        break;
    case MaterialPipelinePass::AvboitAccumulate:
        pixelShader = &m_csgState.m_capAvboitAccumulatePixelShader;
        pipeline = &m_csgState.m_capAvboitAccumulatePipeline;
        passBindingLayout = &m_avboitState.m_accumulateBindingLayout;
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
        if(!shaderSystem().loadShader(
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
        .setInputLayout(m_csgState.m_capInputLayout)
        .setVertexShader(m_csgState.m_capVertexShader)
        .setPixelShader(*pixelShader)
        .setRenderState(ECSRenderDetail::BuildRenderStateForPass(pass, true))
        .addBindingLayout(m_drawState.m_emulationViewBindingLayout)
        .addBindingLayout(*passBindingLayout)
        .addBindingLayout(m_csgState.m_clipBindingLayout)
    ;
    *pipeline = m_graphics.getDevice()->createGraphicsPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
    if(*pipeline)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create transparent CSG cap pipeline"));
    return false;
}

bool RendererCsgSystem::reserveCsgCapVertexBufferCapacity(const usize vertexCount){
    if(vertexCount == 0u)
        return true;
    if(m_csgState.m_capVertexBuffer && m_csgState.m_capVertexBufferCapacity >= vertexCount)
        return true;
    if(vertexCount > Limit<usize>::s_Max / sizeof(CsgCapVertexGpuData))
        return false;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(m_csgState.m_capVertexBufferCapacity, vertexCount);
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

    Core::BufferHandle createdBuffer = m_graphics.createBuffer(bufferDesc);
    if(!createdBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG cap vertex buffer"));
        return false;
    }

    m_csgState.m_capVertexBuffer = Move(createdBuffer);
    m_csgState.m_capVertexBufferCapacity = capacity;
    return true;
}

bool RendererCsgSystem::uploadCsgCapVertices(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData){
    if(!csgFrameData.hasCapWork())
        return true;
    if(!reserveCsgCapVertexBufferCapacity(csgFrameData.capVertices.size()))
        return false;

    commandList.setBufferState(m_csgState.m_capVertexBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(
        m_csgState.m_capVertexBuffer.get(),
        csgFrameData.capVertices.data(),
        csgFrameData.capVertices.size() * sizeof(CsgCapVertexGpuData)
    );
    commandList.setBufferState(m_csgState.m_capVertexBuffer.get(), Core::ResourceStates::VertexBuffer);
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
    if(!m_csgState.m_capVertexBuffer)
        return;
    if(!createCsgClipResources() || !m_csgState.m_clipBindingSet)
        return;
    if(!pipeline)
        return;
    if(MaterialPipelinePassUsesRendererAvboit(context.pass) && (!context.passBindingSet || !context.avboitTargets))
        return;

    context.commandList.setBufferState(m_csgState.m_capVertexBuffer.get(), Core::ResourceStates::VertexBuffer);
    context.commandList.setBufferState(m_drawState.m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    setCsgClipBufferStates(context.commandList);
    if(context.passBindingSet)
        context.commandList.setResourceStatesForBindingSet(context.passBindingSet);

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(pipeline);
    graphicsState.setFramebuffer(context.framebuffer);
    graphicsState.setViewport(context.viewportState);
    graphicsState.addVertexBuffer(
        Core::VertexBufferBinding()
            .setBuffer(m_csgState.m_capVertexBuffer.get())
            .setSlot(0u)
            .setOffset(0u)
    );
    graphicsState.addBindingSet(m_drawState.m_emulationViewBindingSet.get());
    if(context.passBindingSet)
        graphicsState.addBindingSet(context.passBindingSet);
    graphicsState.addBindingSet(m_csgState.m_clipBindingSet.get());
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
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_Raster, m_graphics.getDevice(), context.commandList);

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

    renderCsgCaps(context, csgFrameData, csgFrameData.opaqueCapDrawItems, m_csgState.m_capPipeline.get());
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
        pipeline = m_csgState.m_capAvboitOccupancyPipeline.get();
        break;
    case MaterialPipelinePass::AvboitExtinction:
        pipeline = m_csgState.m_capAvboitExtinctionPipeline.get();
        break;
    case MaterialPipelinePass::AvboitAccumulate:
        pipeline = m_csgState.m_capAvboitAccumulatePipeline.get();
        break;
    default:
        break;
    }

    renderCsgCaps(context, csgFrameData, csgFrameData.transparentCapDrawItems, pipeline);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

