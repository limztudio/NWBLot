// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_private.h"

#include "renderer_capacity_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMaterialSystem::createMeshShaderResources(){
    if(drawState().m_meshBindingLayout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc(arena());
    bindingLayoutDesc.setVisibility(Core::ShaderType::Amplification | Core::ShaderType::Mesh | Core::ShaderType::Pixel);
    RendererMeshSystem::addMeshSourceBindingLayoutItems(bindingLayoutDesc);
    RendererMeshSystem::addMeshFrameBindingLayoutItems(bindingLayoutDesc);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::TransparentDrawPushConstants)));

    auto* device = graphics().getDevice();
    drawState().m_meshBindingLayout = device->createBindingLayout(bindingLayoutDesc);
    if(!drawState().m_meshBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh shader binding layout"));
        return false;
    }

    return true;
}

bool RendererMaterialSystem::createComputeEmulationResources(){
    if(!drawState().m_computeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        RendererMeshSystem::addMeshSourceBindingLayoutItems(bindingLayoutDesc);
        RendererMeshSystem::addMeshFrameBindingLayoutItems(bindingLayoutDesc);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(s_MeshGeneratedVertexBindingSlot, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::ShaderDrivenPushConstants)));

        auto* device = graphics().getDevice();
        drawState().m_computeBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!drawState().m_computeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation binding layout"));
            return false;
        }
    }

    if(!drawState().m_emulationVertexShader){
        if(!m_renderer.shaderSystem().loadShader(
            drawState().m_emulationVertexShader,
            ECSRenderDetail::s_MeshEmulationVertexShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Vertex,
            "ECSRender_MeshEmulationVS"
        ))
            return false;
    }

    if(!drawState().m_emulationInputLayout){
        Core::VertexAttributeDesc attributes[NWB_MESH_EMULATION_VERTEX_ATTRIBUTE_COUNT];
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_POSITION_LOCATION],
            Core::Format::RGBA32_FLOAT,
            NWB_MESH_EMULATION_VERTEX_POSITION_FLOAT_OFFSET,
            "POSITION"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_NORMAL_LOCATION],
            Core::Format::RGB32_FLOAT,
            NWB_MESH_EMULATION_VERTEX_NORMAL_FLOAT_OFFSET,
            "NORMAL"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_TANGENT_LOCATION],
            Core::Format::RGBA32_FLOAT,
            NWB_MESH_EMULATION_VERTEX_TANGENT_FLOAT_OFFSET,
            "TANGENT"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_UV0_LOCATION],
            Core::Format::RG32_FLOAT,
            NWB_MESH_EMULATION_VERTEX_UV0_FLOAT_OFFSET,
            "TEXCOORD"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_COLOR_LOCATION],
            Core::Format::RGBA32_FLOAT,
            NWB_MESH_EMULATION_VERTEX_COLOR_FLOAT_OFFSET,
            "COLOR"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_WORLD_POSITION_LOCATION],
            Core::Format::RGBA32_FLOAT,
            NWB_MESH_EMULATION_VERTEX_WORLD_POSITION_FLOAT_OFFSET,
            "POSITION1"
        );

        auto* device = graphics().getDevice();
        drawState().m_emulationInputLayout = device->createInputLayout(
            attributes,
            NWB_MESH_EMULATION_VERTEX_ATTRIBUTE_COUNT,
            drawState().m_emulationVertexShader.get()
        );
        if(!drawState().m_emulationInputLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation input layout"));
            return false;
        }
    }

    return true;
}

bool RendererMaterialSystem::createEmulationViewResources(){
    if(!drawState().m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: emulation view resources require a mesh view buffer"));
        return false;
    }

    auto* device = graphics().getDevice();
    if(!drawState().m_emulationViewBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Vertex | Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(s_MeshViewBindingSlot, 1));

        drawState().m_emulationViewBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!drawState().m_emulationViewBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation view binding layout"));
            return false;
        }
    }

    if(drawState().m_emulationViewBindingSet)
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(s_MeshViewBindingSlot, drawState().m_meshViewBuffer.get()));

    drawState().m_emulationViewBindingSet = device->createBindingSet(bindingSetDesc, drawState().m_emulationViewBindingLayout);
    if(!drawState().m_emulationViewBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation view binding set"));
        return false;
    }

    return true;
}

bool RendererMaterialSystem::reserveInstanceBufferCapacity(const usize instanceCount){
    if(instanceCount == 0)
        return true;
#if defined(NWB_DEBUG)
    if(instanceCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance buffer request exceeds u32 instance-index limits"));
        return false;
    }
#endif
    if(drawState().m_instanceBuffer && drawState().m_instanceBufferCapacity >= instanceCount)
        return true;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(drawState().m_instanceBufferCapacity, instanceCount);
#if defined(NWB_DEBUG)
    if(capacity > Limit<usize>::s_Max / sizeof(InstanceGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance buffer capacity overflows addressable memory"));
        return false;
    }
#endif

    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setDebugName(ECSRenderDetail::s_InstanceBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle instanceBuffer = graphics().createBuffer(instanceBufferDesc);
    if(!instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create instance data buffer"));
        return false;
    }

    drawState().m_instanceBuffer = Move(instanceBuffer);
    drawState().m_instanceBufferCapacity = capacity;
    m_renderer.meshSystem().destroyMeshBindingSets();
    return true;
}

bool RendererMaterialSystem::reserveMaterialTypedBufferCapacity(const usize byteCount){
    usize requiredByteCount = Max<usize>(byteCount, sizeof(u32));
#if defined(NWB_DEBUG)
    if(!AlignUpChecked(requiredByteCount, sizeof(u32), requiredByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed buffer request overflows alignment"));
        return false;
    }
    if(requiredByteCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed buffer request exceeds u32 byte-offset limits"));
        return false;
    }
#else
    requiredByteCount = AlignUp(requiredByteCount, sizeof(u32));
#endif
    if(drawState().m_materialTypedBuffer && drawState().m_materialTypedBufferCapacity >= requiredByteCount)
        return true;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(drawState().m_materialTypedBufferCapacity, requiredByteCount);
    Core::BufferDesc materialTypedBufferDesc;
    materialTypedBufferDesc
        .setByteSize(static_cast<u64>(capacity))
        .setStructStride(sizeof(u32))
        .setDebugName(ECSRenderDetail::s_MaterialTypedBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle materialTypedBuffer = graphics().createBuffer(materialTypedBufferDesc);
    if(!materialTypedBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create material typed buffer"));
        return false;
    }

    drawState().m_materialTypedBuffer = Move(materialTypedBuffer);
    drawState().m_materialTypedBufferCapacity = capacity;
    m_renderer.meshSystem().destroyMeshBindingSets();
    return true;
}

bool RendererMaterialSystem::updateMeshViewBuffer(Core::CommandList& commandList, const f32 fallbackAspectRatio){
    if(!drawState().m_meshViewBuffer){
        Core::BufferDesc meshViewBufferDesc;
        meshViewBufferDesc
            .setByteSize(sizeof(ECSRenderDetail::MeshViewGpuData))
            .setIsConstantBuffer(true)
            .setDebugName(ECSRenderDetail::s_MeshViewBufferName)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        Core::BufferHandle meshViewBuffer = graphics().createBuffer(meshViewBufferDesc);
        if(!meshViewBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh view buffer"));
            return false;
        }

        drawState().m_meshViewBuffer = Move(meshViewBuffer);
        m_renderer.meshSystem().destroyMeshBindingSets();
    }

    const ECSRenderDetail::MeshViewGpuData viewState = ECSRenderDetail::ResolveMeshViewState(world(), fallbackAspectRatio);
    if(
        drawState().m_meshViewGpuDataValid
        && NWB_MEMCMP(drawState().m_meshViewGpuData, &viewState, sizeof(viewState)) == 0
    )
        return true;

    commandList.setBufferState(drawState().m_meshViewBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(drawState().m_meshViewBuffer.get(), &viewState, sizeof(viewState));
    commandList.setBufferState(drawState().m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    NWB_MEMCPY(drawState().m_meshViewGpuData, sizeof(drawState().m_meshViewGpuData), &viewState, sizeof(viewState));
    drawState().m_meshViewGpuDataValid = true;
    return true;
}

bool RendererMaterialSystem::uploadInstanceBuffer(Core::CommandList& commandList, const InstanceGpuDataVector& instanceData){
    if(instanceData.empty())
        return true;
    if(!reserveInstanceBufferCapacity(instanceData.size()))
        return false;
    NWB_ASSERT(drawState().m_instanceBuffer);

#if defined(NWB_DEBUG)
    if(instanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance data upload size overflows"));
        return false;
    }
#endif

    commandList.setBufferState(drawState().m_instanceBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(drawState().m_instanceBuffer.get(), instanceData.data(), instanceData.size() * sizeof(InstanceGpuData));
    commandList.setBufferState(drawState().m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

bool RendererMaterialSystem::uploadMaterialTypedBuffer(
    Core::CommandList& commandList,
    const MaterialTypedByteDataVector& materialTypedBytes
){
    usize uploadBytes = 0u;
    if(!ECSRenderDetail::ResolveMaterialTypedUploadByteCount(materialTypedBytes, uploadBytes))
        return false;
    if(!reserveMaterialTypedBufferCapacity(uploadBytes))
        return false;
    NWB_ASSERT(drawState().m_materialTypedBuffer);

    commandList.setBufferState(drawState().m_materialTypedBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(drawState().m_materialTypedBuffer.get(), materialTypedBytes.data(), uploadBytes);
    commandList.setBufferState(drawState().m_materialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

bool RendererMaterialSystem::uploadMaterialPassDrawBuffers(
    Core::CommandList& commandList,
    const InstanceGpuDataVector& instanceData,
#if defined(NWB_DEBUG)
    const ECSRenderDetail::MaterialTypedInstanceRangeVector& materialTypedRanges,
#endif
    const MaterialTypedByteDataVector& materialTypedBytes
){
#if defined(NWB_DEBUG)
    NWB_ASSERT(instanceData.size() == materialTypedRanges.size());
    if(!ECSRenderDetail::ValidateMaterialTypedUploadRanges(materialTypedRanges, materialTypedBytes))
        return false;
#endif

    return uploadInstanceBuffer(commandList, instanceData) && uploadMaterialTypedBuffer(commandList, materialTypedBytes);
}

bool RendererMaterialSystem::findMaterialPassDrawItemResources(
    const MaterialPassDrawItem& drawItem,
    MeshResources*& outMesh,
    MaterialPipelineResources*& outPipelineResources
){
    outMesh = nullptr;
    outPipelineResources = nullptr;

    const auto foundMesh = meshState().m_meshes.find(drawItem.meshKey);
    if(foundMesh == meshState().m_meshes.end())
        return false;

    const auto foundPipeline = materialState().m_pipelines.find(drawItem.pipelineKey);
    if(foundPipeline == materialState().m_pipelines.end())
        return false;

    outMesh = &foundMesh.value();
    outPipelineResources = &foundPipeline.value();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

