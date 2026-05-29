// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::createMeshShaderResources(){
    if(m_meshBindingLayout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
    bindingLayoutDesc.setVisibility(Core::ShaderType::Amplification | Core::ShaderType::Mesh | Core::ShaderType::Pixel);
    addMeshSourceBindingLayoutItems(bindingLayoutDesc);
    addMeshFrameBindingLayoutItems(bindingLayoutDesc);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::TransparentDrawPushConstants)));

    auto* device = m_graphics.getDevice();
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
        addMeshSourceBindingLayoutItems(bindingLayoutDesc);
        addMeshFrameBindingLayoutItems(bindingLayoutDesc);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(s_MeshGeneratedVertexBindingSlot, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::ShaderDrivenPushConstants)));

        auto* device = m_graphics.getDevice();
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

        auto* device = m_graphics.getDevice();
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

    auto* device = m_graphics.getDevice();
    if(!m_emulationViewBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(11, 1));

        m_emulationViewBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_emulationViewBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation view binding layout"));
            return false;
        }
    }

    if(m_emulationViewBindingSet)
        return true;

    Core::BindingSetDesc bindingSetDesc(m_arena);
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(11, m_meshViewBuffer.get()));

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
#if defined(NWB_DEBUG)
    if(instanceCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance buffer request exceeds u32 instance-index limits"));
        return false;
    }
#endif
    if(m_instanceBuffer && m_instanceBufferCapacity >= instanceCount)
        return true;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(m_instanceBufferCapacity, instanceCount);
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
    Core::BufferHandle instanceBuffer = m_graphics.createBuffer(instanceBufferDesc);
    if(!instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create instance data buffer"));
        return false;
    }

    m_instanceBuffer = Move(instanceBuffer);
    m_instanceBufferCapacity = capacity;
    destroyMeshBindingSets();
    return true;
}

bool RendererSystem::reserveMaterialTypedBufferCapacity(const usize byteCount){
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
    destroyMeshBindingSets();
    return true;
}

bool RendererSystem::updateMeshViewBuffer(Core::CommandList& commandList, const f32 fallbackAspectRatio){
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
        destroyMeshBindingSets();
    }

    const ECSRenderDetail::MeshViewGpuData viewState = ECSRenderDetail::ResolveMeshViewState(m_world, fallbackAspectRatio);

    commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_meshViewBuffer.get(), &viewState, sizeof(viewState));
    commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    return true;
}

bool RendererSystem::uploadInstanceBuffer(Core::CommandList& commandList, const InstanceGpuDataVector& instanceData){
    if(instanceData.empty())
        return true;
    if(!reserveInstanceBufferCapacity(instanceData.size()))
        return false;
#if defined(NWB_DEBUG)
    if(!m_instanceBuffer)
        return false;
#endif

#if defined(NWB_DEBUG)
    if(instanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance data upload size overflows"));
        return false;
    }
#endif

    commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_instanceBuffer.get(), instanceData.data(), instanceData.size() * sizeof(InstanceGpuData));
    commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

bool RendererSystem::uploadMaterialTypedBuffer(
    Core::CommandList& commandList,
    const MaterialTypedByteDataVector& materialTypedBytes
){
#if defined(NWB_DEBUG)
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
#else
    usize uploadBytes = AlignUp(materialTypedBytes.size(), sizeof(u32));
#endif
    if(!reserveMaterialTypedBufferCapacity(uploadBytes))
        return false;
#if defined(NWB_DEBUG)
    if(!m_materialTypedBuffer)
        return false;
#endif

    commandList.setBufferState(m_materialTypedBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_materialTypedBuffer.get(), materialTypedBytes.data(), uploadBytes);
    commandList.setBufferState(m_materialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

bool RendererSystem::findMaterialPassDrawItemResources(
    const MaterialPassDrawItem& drawItem,
    MeshResources*& outMesh,
    MaterialPipelineResources*& outPipelineResources
){
    outMesh = nullptr;
    outPipelineResources = nullptr;

    const auto foundMesh = m_meshMeshes.find(drawItem.meshKey);
    if(foundMesh == m_meshMeshes.end())
        return false;

    const auto foundPipeline = m_materialPipelines.find(drawItem.pipelineKey);
    if(foundPipeline == m_materialPipelines.end())
        return false;

    outMesh = &foundMesh.value();
    outPipelineResources = &foundPipeline.value();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

