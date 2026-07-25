// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/renderer_private.h>
#include <impl/ecs_render/material/material_pass_csg_private.h>

#include <impl/assets/graphics/mesh/names.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMaterialSystem::createMeshShaderResources(){
    auto* device = graphics().getDevice();
    if(!drawState().m_meshBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Amplification | Core::ShaderType::Mesh | Core::ShaderType::Pixel);
        // The ordinary material mesh set is resource-only, so Backend C can use it directly in descriptor-buffer
        // pipelines. Immutable mesh streams moved to the global heap; this local set deliberately retains only the
        // per-frame instance/material/view resources. Backend A consumes the same shape through descriptor sets.
        bindingLayoutDesc.setUseDescriptorBuffer(true);
        RendererMeshSystem::addMeshFrameBindingLayoutItems(bindingLayoutDesc);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::TransparentDrawPushConstants)));

        drawState().m_meshBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!drawState().m_meshBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh shader binding layout"));
            return false;
        }
    }

    if(!drawState().m_bindlessMeshBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Amplification | Core::ShaderType::Mesh | Core::ShaderType::Pixel);
        // This pure-resource twin is consumed by AVBOIT material pipelines together with the global resource/sampler
        // heap. It remains separate from the ordinary frame set for now even though immutable mesh geometry is no
        // longer local; the next slice can retire that redundant binding-set split without conflating it with this
        // source-stream migration.
        bindingLayoutDesc.setUseDescriptorBuffer(true);
        RendererMeshSystem::addMeshFrameBindingLayoutItems(bindingLayoutDesc);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::TransparentDrawPushConstants)));

        drawState().m_bindlessMeshBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!drawState().m_bindlessMeshBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create bindless mesh shader binding layout"));
            return false;
        }
    }

    return true;
}

bool RendererMaterialSystem::createComputeEmulationResources(){
    if(!drawState().m_computeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.setUseDescriptorBuffer(true);
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
            AssetsGraphicsMesh::s_EmulationVertexShaderName,
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
            NWB_MESH_EMULATION_VERTEX_POSITION_BYTE_OFFSET,
            "POSITION"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_NORMAL_LOCATION],
            Core::Format::RGBA16_FLOAT,
            NWB_MESH_EMULATION_VERTEX_NORMAL_BYTE_OFFSET,
            "NORMAL"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_TANGENT_LOCATION],
            Core::Format::RGBA16_FLOAT,
            NWB_MESH_EMULATION_VERTEX_TANGENT_BYTE_OFFSET,
            "TANGENT"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_UV0_LOCATION],
            Core::Format::RG32_FLOAT,
            NWB_MESH_EMULATION_VERTEX_UV0_BYTE_OFFSET,
            "TEXCOORD"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_COLOR_LOCATION],
            Core::Format::RGBA16_FLOAT,
            NWB_MESH_EMULATION_VERTEX_COLOR_BYTE_OFFSET,
            "COLOR"
        );
        ECSRenderDetail::SetEmulatedVertexAttribute(
            attributes[NWB_MESH_EMULATION_VERTEX_WORLD_POSITION_LOCATION],
            Core::Format::RGBA32_FLOAT,
            NWB_MESH_EMULATION_VERTEX_WORLD_POSITION_BYTE_OFFSET,
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

bool RendererMaterialSystem::createEmulationViewBindingLayout(){
    auto* device = graphics().getDevice();
    if(!drawState().m_emulationViewBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Vertex | Core::ShaderType::Pixel);
        bindingLayoutDesc.setUseDescriptorBuffer(true);
        RendererMeshSystem::addMeshFrameBindingLayoutItems(bindingLayoutDesc);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::TransparentDrawPushConstants)));

        drawState().m_emulationViewBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!drawState().m_emulationViewBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation view binding layout"));
            return false;
        }
    }

    if(!drawState().m_bindlessEmulationViewBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(arena());
        bindingLayoutDesc.setVisibility(Core::ShaderType::Vertex | Core::ShaderType::Pixel);
        bindingLayoutDesc.setUseDescriptorBuffer(true);
        RendererMeshSystem::addMeshFrameBindingLayoutItems(bindingLayoutDesc);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ECSRenderDetail::TransparentDrawPushConstants)));

        drawState().m_bindlessEmulationViewBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!drawState().m_bindlessEmulationViewBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create bindless emulation view binding layout"));
            return false;
        }
    }

    return true;
}

bool RendererMaterialSystem::createEmulationViewResources(){
    auto* device = graphics().getDevice();
    if(!createEmulationViewBindingLayout())
        return false;

    if(drawState().m_emulationViewBindingSet && drawState().m_bindlessEmulationViewBindingSet)
        return true;
    if(!drawState().m_instanceBuffer || !drawState().m_meshViewBuffer || !drawState().m_materialTypedBuffer)
        return true;

    if(!drawState().m_emulationViewBindingSet){
        Core::BindingSetDesc bindingSetDesc(arena());
        m_renderer.meshSystem().addMeshFrameBindingItems(bindingSetDesc);

        drawState().m_emulationViewBindingSet = device->createBindingSet(bindingSetDesc, drawState().m_emulationViewBindingLayout);
        if(!drawState().m_emulationViewBindingSet){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation view binding set"));
            return false;
        }
    }

    if(!drawState().m_bindlessEmulationViewBindingSet){
        Core::BindingSetDesc bindingSetDesc(arena());
        m_renderer.meshSystem().addMeshFrameBindingItems(bindingSetDesc);

        drawState().m_bindlessEmulationViewBindingSet = device->createBindingSet(
            bindingSetDesc,
            drawState().m_bindlessEmulationViewBindingLayout
        );
        if(!drawState().m_bindlessEmulationViewBindingSet){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create bindless emulation view binding set"));
            return false;
        }
    }

    return true;
}

bool RendererMaterialSystem::prepareMaterialPassResourceBindings(const MaterialPassDrawItems& drawItems){
    return prepareMeshMaterialPassResourceBindings(drawItems.meshDrawItems)
        && prepareComputeMaterialPassResourceBindings(drawItems.computeDrawItems)
    ;
}

bool RendererMaterialSystem::prepareMeshMaterialPassResourceBindings(const MaterialPassDrawItemVector& drawItems){
    bool ready = true;
    const MaterialPassCsgBindingSets csgBindingSets{
        csgState().m_clipBindingSet,
        csgState().m_receiverSurfaceBindingSet,
        csgState().m_intervalSampleBindingSet
    };
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
        if(!ready)
            return;
        if(!pipelineResources.meshletPipeline){
            ready = false;
            return;
        }
        if(!m_renderer.meshSystem().createMeshGeometryHeapHandles(mesh)){
            ready = false;
            return;
        }
        const bool usesBindlessAvboitResources =
            MaterialPipelinePassUsesRendererAvboit(drawItem.pipelineKey.pass)
        ;
        if(usesBindlessAvboitResources){
            if(!mesh.bindlessMeshBindingSet && !m_renderer.meshSystem().createBindlessMeshBindingSet(mesh)){
                ready = false;
                return;
            }
        }
        else if(!mesh.meshBindingSet && !m_renderer.meshSystem().createMeshBindingSet(mesh)){
            ready = false;
            return;
        }

        if(!MaterialPassCsgResourcesReadyForPipelineKey(
            drawItem.pipelineKey,
            drawItem.pipelineKey.pass,
            csgBindingSets,
            false
        ))
            ready = false;
    });
    return ready;
}

bool RendererMaterialSystem::prepareComputeMaterialPassResourceBindings(const MaterialPassDrawItemVector& drawItems){
    if(drawItems.empty())
        return true;
    if(
        !createEmulationViewResources()
        || !drawState().m_emulationViewBindingSet
        || !drawState().m_bindlessEmulationViewBindingSet
    )
        return false;

    bool ready = true;
    const MaterialPassCsgBindingSets csgBindingSets{
        csgState().m_clipBindingSet,
        csgState().m_receiverSurfaceBindingSet,
        csgState().m_intervalSampleBindingSet
    };
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, MeshResources& mesh, MaterialPipelineResources& pipelineResources){
        if(!ready)
            return;
        if(!pipelineResources.computePipeline || !pipelineResources.emulationPipeline){
            ready = false;
            return;
        }
        if(!m_renderer.meshSystem().createMeshGeometryHeapHandles(mesh)){
            ready = false;
            return;
        }
        if(!mesh.computeBindingSet && !m_renderer.meshSystem().createComputeBindingSet(mesh)){
            ready = false;
            return;
        }

        if(!MaterialPassCsgResourcesReadyForPipelineKey(
            drawItem.pipelineKey,
            drawItem.pipelineKey.pass,
            csgBindingSets,
            false
        ))
            ready = false;
    });
    return ready;
}

bool RendererMaterialSystem::reserveInstanceBufferCapacity(const usize instanceCount){
    if(instanceCount == 0)
        return true;
    NWB_ASSERT(instanceCount <= static_cast<usize>(Limit<u32>::s_Max));
    if(drawState().m_instanceBuffer && drawState().m_instanceBufferCapacity >= instanceCount)
        return true;

    const usize capacity = ::NextGrowingCapacity(drawState().m_instanceBufferCapacity, instanceCount);
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
    m_renderer.csgSystem().destroyCsgIntervalCapFillMaterialBindingSet();
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

    const usize capacity = ::NextGrowingCapacity(drawState().m_materialTypedBufferCapacity, requiredByteCount);
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
    m_renderer.csgSystem().destroyCsgIntervalCapFillMaterialBindingSet();
    return true;
}

bool RendererMaterialSystem::prepareMaterialPassDrawBuffers(
    const InstanceGpuDataVector& instanceData,
    const MaterialTypedByteDataVector& materialTypedBytes
){
    usize uploadBytes = 0u;
    if(!ECSRenderDetail::ResolveMaterialTypedUploadByteCount(materialTypedBytes, uploadBytes))
        return false;

    return reserveInstanceBufferCapacity(instanceData.size()) && reserveMaterialTypedBufferCapacity(uploadBytes);
}

bool RendererMaterialSystem::materialPassDrawBuffersReady(
    const InstanceGpuDataVector& instanceData,
    const MaterialTypedByteDataVector& materialTypedBytes
)const{
    usize uploadBytes = 0u;
    if(!ECSRenderDetail::ResolveMaterialTypedUploadByteCount(materialTypedBytes, uploadBytes))
        return false;

    const usize requiredMaterialTypedBytes = Max<usize>(uploadBytes, sizeof(u32));
    NWB_ASSERT((requiredMaterialTypedBytes & (sizeof(u32) - 1u)) == 0u);

    return
        (instanceData.empty() || (drawState().m_instanceBuffer && drawState().m_instanceBufferCapacity >= instanceData.size()))
        && drawState().m_materialTypedBuffer
        && drawState().m_materialTypedBufferCapacity >= requiredMaterialTypedBytes
    ;
}

bool RendererMaterialSystem::uploadInstanceBuffer(Core::CommandList& commandList, const InstanceGpuDataVector& instanceData){
    if(instanceData.empty())
        return true;
    NWB_ASSERT(drawState().m_instanceBuffer);
    NWB_ASSERT(drawState().m_instanceBufferCapacity >= instanceData.size());

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
    NWB_ASSERT(drawState().m_materialTypedBuffer);
    NWB_ASSERT(drawState().m_materialTypedBufferCapacity >= uploadBytes);

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
    ECSRenderDetail::AssertMaterialTypedUploadRanges(materialTypedRanges, materialTypedBytes);
#endif

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_MaterialUpload, graphics().getDevice(), commandList);

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

