// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"

#include "csg_cap_builder.h"
#include "renderer_capacity_private.h"
#include "timing_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::appendCsgReceiverPlaneCapGeometry(
    const MeshResources& mesh,
    const Scene::TransformComponent* transform,
    const u32 receiverIndex,
    const CsgReceiverRangeGpuData& receiverRange,
    CsgFrameGpuData& csgFrameData
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

        csgFrameData.opaquePlaneCapDrawItems.push_back(CsgPlaneCapDrawItem{
            firstVertex,
            static_cast<u32>(appendedVertexCount),
        });
    }

    return true;
}

bool RendererSystem::createCsgPlaneCapResources(Core::Framebuffer* framebuffer){
    if(!framebuffer)
        return false;
    if(m_csgState.m_planeCapPipeline)
        return true;
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
    if(!m_csgState.m_planeCapInputLayout){
        Core::VertexAttributeDesc attributes[5];
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

        m_csgState.m_planeCapInputLayout = device->createInputLayout(attributes, 5u, m_csgState.m_planeCapVertexShader.get());
        if(!m_csgState.m_planeCapInputLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG plane cap input layout"));
            return false;
        }
    }

    Core::RenderState renderState = ECSRenderDetail::BuildMeshRenderState();
    renderState.rasterState.setCullNone();

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setInputLayout(m_csgState.m_planeCapInputLayout)
        .setVertexShader(m_csgState.m_planeCapVertexShader)
        .setPixelShader(m_csgState.m_planeCapPixelShader)
        .setRenderState(renderState)
        .addBindingLayout(m_drawState.m_emulationViewBindingLayout)
        .addBindingLayout(m_csgState.m_clipBindingLayout)
    ;
    m_csgState.m_planeCapPipeline = device->createGraphicsPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
    if(!m_csgState.m_planeCapPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG plane cap pipeline"));
        return false;
    }

    return true;
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
    if(!csgFrameData.hasOpaquePlaneCapWork())
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

void RendererSystem::renderCsgOpaquePlaneCaps(
    const MaterialPassDrawContext& context,
    const CsgFrameGpuData& csgFrameData
){
    if(!csgFrameData.hasOpaquePlaneCapWork())
        return;
    if(!m_csgState.m_planeCapVertexBuffer)
        return;
    if(!createCsgPlaneCapResources(context.framebuffer))
        return;
    if(!createCsgClipResources() || !m_csgState.m_clipBindingSet)
        return;

    context.commandList.setBufferState(m_csgState.m_planeCapVertexBuffer.get(), Core::ResourceStates::VertexBuffer);
    context.commandList.setBufferState(m_drawState.m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    setCsgClipBufferStates(context.commandList);

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(m_csgState.m_planeCapPipeline.get());
    graphicsState.setFramebuffer(context.framebuffer);
    graphicsState.setViewport(context.viewportState);
    graphicsState.addVertexBuffer(
        Core::VertexBufferBinding()
            .setBuffer(m_csgState.m_planeCapVertexBuffer.get())
            .setSlot(0u)
            .setOffset(0u)
    );
    graphicsState.addBindingSet(m_drawState.m_emulationViewBindingSet.get());
    graphicsState.addBindingSet(m_csgState.m_clipBindingSet.get());
    context.commandList.setGraphicsState(graphicsState);

    for(const CsgPlaneCapDrawItem& drawItem : csgFrameData.opaquePlaneCapDrawItems){
        Core::DrawArguments drawArgs;
        drawArgs.setVertexCount(drawItem.vertexCount);
        drawArgs.setStartVertexLocation(drawItem.firstVertex);
        {
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_Raster, m_graphics.getDevice(), context.commandList);

            context.commandList.draw(drawArgs);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

