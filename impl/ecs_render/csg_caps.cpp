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


static void SetPlaneCapVertexAttributes(Core::VertexAttributeDesc (&attributes)[5]){
    attributes[0]
        .setFormat(Core::Format::RGBA32_FLOAT)
        .setBufferIndex(0u)
        .setOffset(offsetof(CsgPlaneCapVertexGpuData, positionReceiverIndex))
        .setElementStride(sizeof(CsgPlaneCapVertexGpuData))
        .setName("POSITION")
    ;
    attributes[1]
        .setFormat(Core::Format::RGBA32_FLOAT)
        .setBufferIndex(0u)
        .setOffset(offsetof(CsgPlaneCapVertexGpuData, normalCutterIndex))
        .setElementStride(sizeof(CsgPlaneCapVertexGpuData))
        .setName("NORMAL")
    ;
    attributes[2]
        .setFormat(Core::Format::RGBA32_FLOAT)
        .setBufferIndex(0u)
        .setOffset(offsetof(CsgPlaneCapVertexGpuData, tangent))
        .setElementStride(sizeof(CsgPlaneCapVertexGpuData))
        .setName("TANGENT")
    ;
    attributes[3]
        .setFormat(Core::Format::RGBA32_FLOAT)
        .setBufferIndex(0u)
        .setOffset(offsetof(CsgPlaneCapVertexGpuData, color))
        .setElementStride(sizeof(CsgPlaneCapVertexGpuData))
        .setName("COLOR")
    ;
    attributes[4]
        .setFormat(Core::Format::RGBA32_FLOAT)
        .setBufferIndex(0u)
        .setOffset(offsetof(CsgPlaneCapVertexGpuData, uv0))
        .setElementStride(sizeof(CsgPlaneCapVertexGpuData))
        .setName("TEXCOORD")
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::appendCsgReceiverPlaneCapGeometry(
    const MeshResources& mesh,
    const Scene::TransformComponent* transform,
    const u32 receiverIndex,
    const CsgReceiverRangeGpuData& receiverRange,
    CsgFrameGpuData& csgFrameData,
    CsgPlaneCapDrawItemVector& capDrawItems
)const{
    if(mesh.csgPlaneCapTriangles.empty() || receiverRange.cutterCount == 0u)
        return true;

    if(receiverIndex >= csgFrameData.receiverRanges.size())
        return false;
    if(static_cast<usize>(receiverRange.firstCutter) + static_cast<usize>(receiverRange.cutterCount) > csgFrameData.cutters.size())
        return false;

    Core::Alloc::ScratchArena& scratchArena = csgFrameData.planeCapVertices.get_allocator().arena();
    for(u32 cutterOffset = 0u; cutterOffset < receiverRange.cutterCount; ++cutterOffset){
        const u32 cutterIndex = receiverRange.firstCutter + cutterOffset;
        const CsgCutterGpuData& cutter = csgFrameData.cutters[cutterIndex];
        if(csgFrameData.planeCapVertices.size() > static_cast<usize>(Limit<u32>::s_Max))
            return false;
        const u32 firstVertex = static_cast<u32>(csgFrameData.planeCapVertices.size());
        if(!ECSRenderCsgCapBuilder::AppendCapGeometry(
            mesh.csgPlaneCapTriangles,
            transform,
            receiverIndex,
            cutter,
            cutterIndex,
            csgFrameData.planeCapVertices,
            scratchArena
        ))
            return false;

        const usize appendedVertexCount = csgFrameData.planeCapVertices.size() - static_cast<usize>(firstVertex);
        if(appendedVertexCount == 0u)
            continue;
        if(appendedVertexCount > static_cast<usize>(Limit<u32>::s_Max))
            return false;

        capDrawItems.push_back(CsgPlaneCapDrawItem{
            firstVertex,
            static_cast<u32>(appendedVertexCount),
        });
    }

    return true;
}

bool RendererSystem::createCsgPlaneCapSharedResources(){
    if(!createEmulationViewResources())
        return false;
    if(!createCsgClipResources())
        return false;

    auto* device = m_graphics.getDevice();
    if(!m_csgState.m_planeCapVertexShader){
        if(!loadShader(
            m_csgState.m_planeCapVertexShader,
            ECSRenderDetail::s_CsgPlaneCapVertexShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Vertex,
            "ECSRender_CsgPlaneCapVS"
        ))
            return false;
    }
    if(!m_csgState.m_planeCapInputLayout){
        Core::VertexAttributeDesc attributes[5];
        __hidden_csg_caps::SetPlaneCapVertexAttributes(attributes);

        m_csgState.m_planeCapInputLayout = device->createInputLayout(attributes, 5u, m_csgState.m_planeCapVertexShader.get());
        if(!m_csgState.m_planeCapInputLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG plane cap input layout"));
            return false;
        }
    }

    return true;
}

bool RendererSystem::createCsgPlaneCapResources(Core::Framebuffer* framebuffer){
    if(!framebuffer)
        return false;
    if(m_csgState.m_planeCapPipeline)
        return true;
    if(!createCsgPlaneCapSharedResources())
        return false;
    if(!m_csgState.m_planeCapPixelShader){
        if(!loadShader(
            m_csgState.m_planeCapPixelShader,
            ECSRenderDetail::s_CsgPlaneCapPixelShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Pixel,
            "ECSRender_CsgPlaneCapPS"
        ))
            return false;
    }

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setInputLayout(m_csgState.m_planeCapInputLayout)
        .setVertexShader(m_csgState.m_planeCapVertexShader)
        .setPixelShader(m_csgState.m_planeCapPixelShader)
        .setRenderState(ECSRenderDetail::BuildRenderStateForPass(MaterialPipelinePass::Opaque, true))
        .addBindingLayout(m_drawState.m_emulationViewBindingLayout)
        .addBindingLayout(m_csgState.m_clipBindingLayout)
    ;
    m_csgState.m_planeCapPipeline = m_graphics.getDevice()->createGraphicsPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
    if(!m_csgState.m_planeCapPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG plane cap pipeline"));
        return false;
    }

    return true;
}

bool RendererSystem::createCsgTransparentPlaneCapResources(Core::Framebuffer* framebuffer, const MaterialPipelinePass::Enum pass){
    if(!framebuffer)
        return false;
    if(!createCsgPlaneCapSharedResources())
        return false;
    if(!createAvboitResources())
        return false;

    Core::ShaderHandle* pixelShader = nullptr;
    Core::GraphicsPipelineHandle* pipeline = nullptr;
    Core::BindingLayoutHandle* passBindingLayout = nullptr;
    const Name* pixelShaderName = nullptr;
    const char* pixelShaderDebugName = nullptr;
    switch(pass){
    case MaterialPipelinePass::AvboitOccupancy:
        pixelShader = &m_csgState.m_planeCapAvboitOccupancyPixelShader;
        pipeline = &m_csgState.m_planeCapAvboitOccupancyPipeline;
        passBindingLayout = &m_avboitState.m_occupancyBindingLayout;
        pixelShaderName = &ECSRenderDetail::s_CsgTransparentCapOccupancyPixelShaderName;
        pixelShaderDebugName = "ECSRender_CsgTransparentCapOccupancyPS";
        break;
    case MaterialPipelinePass::AvboitExtinction:
        pixelShader = &m_csgState.m_planeCapAvboitExtinctionPixelShader;
        pipeline = &m_csgState.m_planeCapAvboitExtinctionPipeline;
        passBindingLayout = &m_avboitState.m_extinctionBindingLayout;
        pixelShaderName = &ECSRenderDetail::s_CsgTransparentCapExtinctionPixelShaderName;
        pixelShaderDebugName = "ECSRender_CsgTransparentCapExtinctionPS";
        break;
    case MaterialPipelinePass::AvboitAccumulate:
        pixelShader = &m_csgState.m_planeCapAvboitAccumulatePixelShader;
        pipeline = &m_csgState.m_planeCapAvboitAccumulatePipeline;
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
        if(!loadShader(
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
        .setInputLayout(m_csgState.m_planeCapInputLayout)
        .setVertexShader(m_csgState.m_planeCapVertexShader)
        .setPixelShader(*pixelShader)
        .setRenderState(ECSRenderDetail::BuildRenderStateForPass(pass, true))
        .addBindingLayout(m_drawState.m_emulationViewBindingLayout)
        .addBindingLayout(*passBindingLayout)
        .addBindingLayout(m_csgState.m_clipBindingLayout)
    ;
    *pipeline = m_graphics.getDevice()->createGraphicsPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
    if(*pipeline)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create transparent CSG plane cap pipeline"));
    return false;
}

bool RendererSystem::reserveCsgPlaneCapVertexBufferCapacity(const usize vertexCount){
    if(vertexCount == 0u)
        return true;
    if(m_csgState.m_planeCapVertexBuffer && m_csgState.m_planeCapVertexBufferCapacity >= vertexCount)
        return true;
    if(vertexCount > Limit<usize>::s_Max / sizeof(CsgPlaneCapVertexGpuData))
        return false;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(m_csgState.m_planeCapVertexBufferCapacity, vertexCount);
    if(capacity > Limit<usize>::s_Max / sizeof(CsgPlaneCapVertexGpuData))
        return false;

    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(CsgPlaneCapVertexGpuData)))
        .setStructStride(sizeof(CsgPlaneCapVertexGpuData))
        .setIsVertexBuffer(true)
        .setDebugName(ECSRenderDetail::s_CsgPlaneCapVertexBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    Core::BufferHandle createdBuffer = m_graphics.createBuffer(bufferDesc);
    if(!createdBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG plane cap vertex buffer"));
        return false;
    }

    m_csgState.m_planeCapVertexBuffer = Move(createdBuffer);
    m_csgState.m_planeCapVertexBufferCapacity = capacity;
    return true;
}

bool RendererSystem::uploadCsgPlaneCapVertices(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData){
    if(!csgFrameData.hasPlaneCapWork())
        return true;
    if(!reserveCsgPlaneCapVertexBufferCapacity(csgFrameData.planeCapVertices.size()))
        return false;

    commandList.setBufferState(m_csgState.m_planeCapVertexBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(
        m_csgState.m_planeCapVertexBuffer.get(),
        csgFrameData.planeCapVertices.data(),
        csgFrameData.planeCapVertices.size() * sizeof(CsgPlaneCapVertexGpuData)
    );
    commandList.setBufferState(m_csgState.m_planeCapVertexBuffer.get(), Core::ResourceStates::VertexBuffer);
    commandList.commitBarriers();
    return true;
}

void RendererSystem::renderCsgPlaneCaps(
    const MaterialPassDrawContext& context,
    const CsgFrameGpuData& csgFrameData,
    const CsgPlaneCapDrawItemVector& capDrawItems,
    Core::GraphicsPipeline* pipeline
){
    if(!csgFrameData.hasPlaneCapWork() || capDrawItems.empty())
        return;
    if(!m_csgState.m_planeCapVertexBuffer)
        return;
    if(!createCsgClipResources() || !m_csgState.m_clipBindingSet)
        return;
    if(!pipeline)
        return;
    if(MaterialPipelinePassUsesRendererAvboit(context.pass) && (!context.passBindingSet || !context.avboitTargets))
        return;

    context.commandList.setBufferState(m_csgState.m_planeCapVertexBuffer.get(), Core::ResourceStates::VertexBuffer);
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
            .setBuffer(m_csgState.m_planeCapVertexBuffer.get())
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

    for(const CsgPlaneCapDrawItem& drawItem : capDrawItems){
        Core::DrawArguments drawArgs;
        drawArgs.setVertexCount(drawItem.vertexCount);
        drawArgs.setStartVertexLocation(drawItem.firstVertex);
        {
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_Raster, m_graphics.getDevice(), context.commandList);

            context.commandList.draw(drawArgs);
        }
    }
}

void RendererSystem::renderCsgOpaquePlaneCaps(
    const MaterialPassDrawContext& context,
    const CsgFrameGpuData& csgFrameData
){
    if(!csgFrameData.hasOpaquePlaneCapWork())
        return;
    if(!createCsgPlaneCapResources(context.framebuffer))
        return;

    renderCsgPlaneCaps(context, csgFrameData, csgFrameData.opaquePlaneCapDrawItems, m_csgState.m_planeCapPipeline.get());
}

void RendererSystem::renderCsgTransparentPlaneCaps(
    const MaterialPassDrawContext& context,
    const CsgFrameGpuData& csgFrameData
){
    if(!csgFrameData.hasTransparentPlaneCapWork())
        return;
    if(!createCsgTransparentPlaneCapResources(context.framebuffer, context.pass))
        return;

    Core::GraphicsPipeline* pipeline = nullptr;
    switch(context.pass){
    case MaterialPipelinePass::AvboitOccupancy:
        pipeline = m_csgState.m_planeCapAvboitOccupancyPipeline.get();
        break;
    case MaterialPipelinePass::AvboitExtinction:
        pipeline = m_csgState.m_planeCapAvboitExtinctionPipeline.get();
        break;
    case MaterialPipelinePass::AvboitAccumulate:
        pipeline = m_csgState.m_planeCapAvboitAccumulatePipeline.get();
        break;
    default:
        break;
    }

    renderCsgPlaneCaps(context, csgFrameData, csgFrameData.transparentPlaneCapDrawItems, pipeline);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

