// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "api.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsContract{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
concept BackendApi = requires(
    T& backend,
    const T& constBackend,
    GraphicsVector<AdapterInfo>& adapters,
    const BackBufferResizeCallbacks& callbacks,
    const Common::FrameParam& frameParam
){
    { constBackend.getDevice() }->SameAs<GraphicsBackend::Device*>;
    { constBackend.getRendererString() }->SameAs<const tchar*>;

    { backend.enumerateAdapters(adapters) }->SameAs<bool>;
    { backend.getCurrentBackBuffer() }->SameAs<Texture*>;
    { backend.getBackBuffer(u32{}) }->SameAs<Texture*>;
    { backend.getCurrentBackBufferIndex() }->SameAs<u32>;
    { backend.getBackBufferCount() }->SameAs<u32>;

    backend.setPlatformFrameParam(frameParam);
    { backend.createInstance() }->SameAs<bool>;
    { backend.createDevice() }->SameAs<bool>;
    { backend.createSwapChain() }->SameAs<bool>;
    backend.destroy();
    backend.resizeSwapChain();
    { backend.beginFrame(callbacks) }->SameAs<bool>;
    { backend.present() }->SameAs<bool>;
    backend.reportLiveObjects();
};

template<typename T>
concept DeviceApi = requires(
    T& device,
    const HeapDesc& heapDesc,
    const TextureDesc& textureDesc,
    Texture* texture,
    StagingTexture* stagingTexture,
    Heap* heap,
    const TextureSlice& textureSlice,
    usize* rowPitch,
    const SamplerFeedbackTextureDesc& samplerFeedbackDesc,
    const BufferDesc& bufferDesc,
    Buffer* buffer,
    u32* tileCount,
    PackedMipDesc* packedMipDesc,
    TileShape* tileShape,
    u32* subresourceTilingCount,
    SubresourceTiling* subresourceTilings,
    const TextureTilesMapping* tileMappings,
    const ShaderDesc& shaderDesc,
    const void* binary,
    const ShaderSpecialization* specializationConstants,
    const SamplerDesc& samplerDesc,
    const VertexAttributeDesc* vertexAttributeDescs,
    Shader* shader,
    EventQuery* eventQuery,
    TimerQuery* timerQuery,
    const FramebufferDesc& framebufferDesc,
    const GraphicsPipelineDesc& graphicsPipelineDesc,
    const FramebufferInfo& framebufferInfo,
    const ComputePipelineDesc& computePipelineDesc,
    const MeshletPipelineDesc& meshletPipelineDesc,
    const RayTracingPipelineDesc& rayTracingPipelineDesc,
    const BindingLayoutDesc& bindingLayoutDesc,
    const BindlessLayoutDesc& bindlessLayoutDesc,
    const BindingSetDesc& bindingSetDesc,
    const BindingLayoutHandle& bindingLayout,
    DescriptorTable* descriptorTable,
    const BindingSetItem& bindingSetItem,
    const RayTracingOpacityMicromapDesc& opacityMicromapDesc,
    const RayTracingAccelStructDesc& accelStructDesc,
    RayTracingAccelStruct* accelStruct,
    const RayTracingClusterOperationParams& clusterOperationParams,
    const CommandListParameters& commandListParams,
    CommandList* const* commandLists,
    void* featureInfo,
    Object nativeObject
){
    { device.createHeap(heapDesc) }->SameAs<HeapHandle>;
    { device.createTexture(textureDesc) }->SameAs<TextureHandle>;
    { device.getTextureMemoryRequirements(texture) }->SameAs<MemoryRequirements>;
    { device.bindTextureMemory(texture, heap, u64{}) }->SameAs<bool>;
    { device.createHandleForNativeTexture(ObjectType{}, nativeObject, textureDesc) }->SameAs<TextureHandle>;
    { device.createStagingTexture(textureDesc, CpuAccessMode::Read) }->SameAs<StagingTextureHandle>;
    { device.mapStagingTexture(stagingTexture, textureSlice, CpuAccessMode::Read, rowPitch) }->SameAs<void*>;
    device.unmapStagingTexture(stagingTexture);
    device.getTextureTiling(texture, tileCount, packedMipDesc, tileShape, subresourceTilingCount, subresourceTilings);
    device.updateTextureTileMappings(texture, tileMappings, u32{}, CommandQueue::Graphics);
    { device.createSamplerFeedbackTexture(texture, samplerFeedbackDesc) }->SameAs<SamplerFeedbackTextureHandle>;
    { device.createSamplerFeedbackForNativeTexture(ObjectType{}, nativeObject, texture) }->SameAs<SamplerFeedbackTextureHandle>;

    { device.createBuffer(bufferDesc) }->SameAs<BufferHandle>;
    { device.mapBuffer(buffer, CpuAccessMode::Write) }->SameAs<void*>;
    device.unmapBuffer(buffer);
    { device.getBufferMemoryRequirements(buffer) }->SameAs<MemoryRequirements>;
    { device.bindBufferMemory(buffer, heap, u64{}) }->SameAs<bool>;
    { device.createHandleForNativeBuffer(ObjectType{}, nativeObject, bufferDesc) }->SameAs<BufferHandle>;

    { device.createShader(shaderDesc, binary, usize{}) }->SameAs<ShaderHandle>;
    { device.createShaderSpecialization(shader, specializationConstants, u32{}) }->SameAs<ShaderHandle>;
    { device.createShaderLibrary(binary, usize{}) }->SameAs<ShaderLibraryHandle>;
    { device.createSampler(samplerDesc) }->SameAs<SamplerHandle>;
    { device.createInputLayout(vertexAttributeDescs, u32{}, shader) }->SameAs<InputLayoutHandle>;

    { device.createEventQuery() }->SameAs<EventQueryHandle>;
    device.setEventQuery(eventQuery, CommandQueue::Graphics);
    { device.pollEventQuery(eventQuery) }->SameAs<bool>;
    device.waitEventQuery(eventQuery);
    { device.createTimerQuery() }->SameAs<TimerQueryHandle>;
    { device.pollTimerQuery(timerQuery) }->SameAs<bool>;
    { device.getTimerQueryTime(timerQuery) }->SameAs<f32>;
    device.resetTimerQuery(timerQuery);

    { device.createFramebuffer(framebufferDesc) }->SameAs<FramebufferHandle>;
    { device.createGraphicsPipeline(graphicsPipelineDesc, framebufferInfo) }->SameAs<GraphicsPipelineHandle>;
    { device.createComputePipeline(computePipelineDesc) }->SameAs<ComputePipelineHandle>;
    { device.createMeshletPipeline(meshletPipelineDesc, framebufferInfo) }->SameAs<MeshletPipelineHandle>;
    { device.createRayTracingPipeline(rayTracingPipelineDesc) }->SameAs<RayTracingPipelineHandle>;
    { device.createBindingLayout(bindingLayoutDesc) }->SameAs<BindingLayoutHandle>;
    { device.createBindlessLayout(bindlessLayoutDesc) }->SameAs<BindingLayoutHandle>;
    { device.createBindingSet(bindingSetDesc, bindingLayout) }->SameAs<BindingSetHandle>;
    { device.createDescriptorTable(bindingLayout) }->SameAs<DescriptorTableHandle>;
    device.resizeDescriptorTable(descriptorTable, u32{}, bool{});
    { device.writeDescriptorTable(descriptorTable, bindingSetItem) }->SameAs<bool>;

    { device.createOpacityMicromap(opacityMicromapDesc) }->SameAs<RayTracingOpacityMicromapHandle>;
    { device.createAccelStruct(accelStructDesc) }->SameAs<RayTracingAccelStructHandle>;
    { device.getAccelStructMemoryRequirements(accelStruct) }->SameAs<MemoryRequirements>;
    { device.getClusterOperationSizeInfo(clusterOperationParams) }->SameAs<RayTracingClusterOperationSizeInfo>;
    { device.bindAccelStructMemory(accelStruct, heap, u64{}) }->SameAs<bool>;

    { device.createCommandList(commandListParams) }->SameAs<CommandListHandle>;
    { device.executeCommandLists(commandLists, usize{}, CommandQueue::Graphics) }->SameAs<u64>;
    device.queueWaitForCommandList(CommandQueue::Graphics, CommandQueue::Graphics, u64{});
    { device.waitForIdle() }->SameAs<bool>;
    device.runGarbageCollection();
    { device.queryFeatureSupport(Feature::Meshlets, featureInfo, usize{}) }->SameAs<bool>;
    { device.queryFormatSupport(Format::RGBA8_UNORM) }->SameAs<FormatSupport::Mask>;
    { device.queryCoopVecFeatures() }->SameAs<CooperativeVectorDeviceFeatures>;
    { device.getCoopVecMatrixSize(CooperativeVectorDataType::Float16, CooperativeVectorMatrixLayout::RowMajor, i32{}, i32{}) }->SameAs<usize>;
    { device.getNativeQueue(ObjectType{}, CommandQueue::Graphics) }->SameAs<Object>;
    { device.isAftermathEnabled() }->SameAs<bool>;
    { device.getAftermathCrashDumpHelper() }->SameAs<AftermathCrashDumpHelper&>;
};

template<typename T>
concept CommandListApi = requires(
    T& commandList,
    Texture* texture,
    StagingTexture* stagingTexture,
    Buffer* buffer,
    SamplerFeedbackTexture* samplerFeedbackTexture,
    BindingSet* bindingSet,
    Framebuffer& framebuffer,
    RayTracingAccelStruct* accelStruct,
    RayTracingOpacityMicromap* opacityMicromap,
    TimerQuery* timerQuery,
    const TextureSlice& textureSlice,
    const TextureSubresourceSet& subresources,
    const Color& color,
    const void* data,
    const GraphicsState& graphicsState,
    const DrawArguments& drawArguments,
    const ComputeState& computeState,
    const MeshletState& meshletState,
    const RayTracingState& rayTracingState,
    const RayTracingDispatchRaysArguments& rayTracingArguments,
    const RayTracingGeometryDesc* geometries,
    const RayTracingInstanceDesc* instances,
    const RayTracingOpacityMicromapDesc& opacityMicromapDesc,
    const RayTracingClusterOperationDesc& clusterOperationDesc,
    const CooperativeVectorConvertMatrixLayoutDesc* coopVecConvertDescs,
    const AStringView markerName
){
    commandList.open();
    commandList.close();
    commandList.clearState();
    commandList.endRenderPass();

    commandList.clearTextureFloat(texture, subresources, color);
    commandList.clearDepthStencilTexture(texture, subresources, bool{}, f32{}, bool{}, u8{});
    commandList.clearTextureUInt(texture, subresources, u32{});
    commandList.copyTexture(texture, textureSlice, texture, textureSlice);
    commandList.copyTexture(stagingTexture, textureSlice, texture, textureSlice);
    commandList.copyTexture(texture, textureSlice, stagingTexture, textureSlice);
    commandList.writeTexture(texture, u32{}, u32{}, data, usize{}, usize{});
    commandList.resolveTexture(texture, subresources, texture, subresources);
    commandList.writeBuffer(buffer, data, usize{}, u64{});
    commandList.clearBufferUInt(buffer, u32{});
    commandList.copyBuffer(buffer, u64{}, buffer, u64{}, u64{});

    commandList.clearSamplerFeedbackTexture(samplerFeedbackTexture);
    commandList.decodeSamplerFeedbackTexture(buffer, samplerFeedbackTexture, Format::R8_UINT);
    commandList.setSamplerFeedbackTextureState(samplerFeedbackTexture, ResourceStates::UnorderedAccess);
    commandList.setPushConstants(data, usize{});

    commandList.setGraphicsState(graphicsState);
    commandList.draw(drawArguments);
    commandList.drawIndexed(drawArguments);
    commandList.drawIndirect(u32{}, u32{});
    commandList.drawIndexedIndirect(u32{}, u32{});
    commandList.setComputeState(computeState);
    commandList.dispatch(u32{}, u32{}, u32{});
    commandList.dispatchIndirect(u32{});
    commandList.setMeshletState(meshletState);
    commandList.dispatchMesh(u32{}, u32{}, u32{});
    commandList.setRayTracingState(rayTracingState);
    commandList.dispatchRays(rayTracingArguments);

    commandList.buildOpacityMicromap(opacityMicromap, opacityMicromapDesc);
    commandList.buildBottomLevelAccelStruct(accelStruct, geometries, usize{}, RayTracingAccelStructBuildFlags::None);
    commandList.compactBottomLevelAccelStructs();
    commandList.buildTopLevelAccelStruct(accelStruct, instances, usize{}, RayTracingAccelStructBuildFlags::None);
    commandList.executeMultiIndirectClusterOperation(clusterOperationDesc);
    commandList.buildTopLevelAccelStructFromBuffer(accelStruct, buffer, u64{}, usize{}, RayTracingAccelStructBuildFlags::None);
    commandList.convertCoopVecMatrices(coopVecConvertDescs, usize{});
    commandList.beginTimerQuery(timerQuery);
    commandList.endTimerQuery(timerQuery);
    commandList.beginMarker(markerName);
    commandList.endMarker();

    commandList.setResourceStatesForBindingSet(bindingSet);
    commandList.setResourceStatesForFramebuffer(framebuffer);
    commandList.setEnableUavBarriersForTexture(texture, bool{});
    commandList.setEnableUavBarriersForBuffer(buffer, bool{});
    commandList.beginTrackingTextureState(texture, subresources, ResourceStates::ShaderResource);
    commandList.beginTrackingBufferState(buffer, ResourceStates::ShaderResource);
    commandList.setTextureState(texture, subresources, ResourceStates::ShaderResource);
    commandList.setBufferState(buffer, ResourceStates::ShaderResource);
    commandList.setAccelStructState(accelStruct, ResourceStates::AccelStructRead);
    commandList.setPermanentTextureState(texture, ResourceStates::ShaderResource);
    commandList.setPermanentBufferState(buffer, ResourceStates::ShaderResource);
    commandList.commitBarriers();
    { commandList.getTextureSubresourceState(texture, ArraySlice{}, MipLevel{}) }->SameAs<ResourceStates::Mask>;
    { commandList.getBufferState(buffer) }->SameAs<ResourceStates::Mask>;
    commandList.getDevice();
    { commandList.getDevice() }->SameAs<Device*>;
    { commandList.getDescription() }->SameAs<const CommandListParameters&>;

};

template<typename T, typename Desc>
concept DescribedResourceApi = requires(const T& resource){
    { resource.getDescription() }->SameAs<const Desc&>;
};

template<typename T>
concept BufferApi = DescribedResourceApi<T, BufferDesc> && requires(const T& buffer){
    { buffer.getGpuVirtualAddress() }->SameAs<GpuVirtualAddress>;
};

template<typename T>
concept TextureApi = DescribedResourceApi<T, TextureDesc> && requires(
    T& texture,
    TextureSubresourceSet subresources
){
    { texture.getNativeView(ObjectType{}, Format::UNKNOWN, subresources, TextureDimension::Unknown, bool{}) }->SameAs<Object>;
};

template<typename T>
concept ShaderApi = DescribedResourceApi<T, ShaderDesc> && requires(
    const T& shader,
    const void** bytecode,
    usize* bytecodeSize
){
    shader.getBytecode(bytecode, bytecodeSize);
};

template<typename T>
concept ShaderLibraryApi = requires(
    const T& constLibrary,
    T& library,
    const void** bytecode,
    usize* bytecodeSize
){
    constLibrary.getBytecode(bytecode, bytecodeSize);
    { library.getShader(AStringView{}, ShaderType::All) }->SameAs<ShaderHandle>;
};

template<typename T>
concept FramebufferApi = DescribedResourceApi<T, FramebufferDesc> && requires(const T& framebuffer){
    { framebuffer.getFramebufferInfo() }->SameAs<const FramebufferInfoEx&>;
};

template<typename T>
concept RayTracingOpacityMicromapApi = DescribedResourceApi<T, RayTracingOpacityMicromapDesc> && requires(const T& micromap){
    { micromap.isCompacted() }->SameAs<bool>;
    { micromap.getDeviceAddress() }->SameAs<u64>;
};

template<typename T>
concept RayTracingAccelStructApi = DescribedResourceApi<T, RayTracingAccelStructDesc> && requires(const T& accelStruct){
    { accelStruct.isCompacted() }->SameAs<bool>;
    { accelStruct.getDeviceAddress() }->SameAs<u64>;
};

template<typename T>
concept InputLayoutApi = requires(const T& inputLayout){
    { inputLayout.getAttributeDescription(u32{}) }->SameAs<const VertexAttributeDesc*>;
    { inputLayout.getNumAttributes() }->SameAs<u32>;
};

template<typename T>
concept BindingLayoutApi = requires(const T& layout){
    { layout.getDescription() }->SameAs<const BindingLayoutDesc*>;
    { layout.getBindlessDesc() }->SameAs<const BindlessLayoutDesc*>;
};

template<typename T>
concept BindingSetApi = requires(const T& bindingSet){
    { bindingSet.getDescription() }->SameAs<const BindingSetDesc*>;
    { bindingSet.getLayout() }->SameAs<BindingLayout*>;
};

template<typename T>
concept DescriptorTableApi = requires(const T& descriptorTable){
    { descriptorTable.getCapacity() }->SameAs<u32>;
    { descriptorTable.getFirstDescriptorIndexInHeap() }->SameAs<u32>;
};

template<typename T>
concept GraphicsPipelineApi = DescribedResourceApi<T, GraphicsPipelineDesc> && requires(const T& pipeline){
    { pipeline.getFramebufferInfo() }->SameAs<const FramebufferInfo&>;
};

template<typename T>
concept ComputePipelineApi = DescribedResourceApi<T, ComputePipelineDesc>;

template<typename T>
concept MeshletPipelineApi = DescribedResourceApi<T, MeshletPipelineDesc> && requires(const T& pipeline){
    { pipeline.getFramebufferInfo() }->SameAs<const FramebufferInfo&>;
};

template<typename T>
concept RayTracingPipelineApi = DescribedResourceApi<T, RayTracingPipelineDesc> && requires(T& pipeline){
    { pipeline.createShaderTable() }->SameAs<RayTracingShaderTableHandle>;
};

template<typename T>
concept RayTracingShaderTableApi = requires(
    T& shaderTable,
    AStringView exportName,
    BindingSet* bindingSet
){
    shaderTable.setRayGenerationShader(exportName, bindingSet);
    { shaderTable.addMissShader(exportName, bindingSet) }->SameAs<u32>;
    { shaderTable.addHitGroup(exportName, bindingSet) }->SameAs<u32>;
    { shaderTable.addCallableShader(exportName, bindingSet) }->SameAs<u32>;
    shaderTable.clearMissShaders();
    shaderTable.clearHitShaders();
    shaderTable.clearCallableShaders();
    { shaderTable.getPipeline() }->SameAs<RayTracingPipeline*>;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

