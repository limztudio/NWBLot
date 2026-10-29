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
concept BackendApi = requires(T& backend, const T& constBackend, const Common::FrameParam& frameParam, const QueueSubmissionToken& submissionToken, const QueueSubmissionPreSubmitHook& presentationClaim, SwapChainTransitionTicket& transitionTicket){
    { constBackend.getDevice() }->SameAs<GraphicsBackend::Device*>;
    { constBackend.getRendererString() }->SameAs<TStringView>;

    { backend.enumerateAdapters() }->SameAs<Expected<GraphicsVector<AdapterInfo>>>;
    { constBackend.getSelectedAdapterInfo() }->SameAs<Expected<AdapterInfo>>;
    { backend.getBackBuffer(u32{}) }->SameAs<Texture*>;
    { backend.getBackBufferCount() }->SameAs<u32>;

    backend.setPlatformFrameParam(frameParam);
    { backend.createInstance() }->SameAs<b8>;
    { backend.createDevice() }->SameAs<b8>;
    { backend.createSwapChain() }->SameAs<b8>;
    { backend.destroy() }->SameAs<b8>;
    { backend.prepareSwapChainTransition(SwapChainTransitionKind::Resize) }->SameAs<Expected<SwapChainTransitionTicket>>;
    { backend.commitSwapChainResize(Move(transitionTicket)) }->SameAs<b8>;
    { backend.commitDestroy(Move(transitionTicket)) }->SameAs<b8>;
    { backend.beginFrame() }->SameAs<BeginFrameResult>;
    { backend.abandonAcquiredFrame() }->SameAs<b8>;
    { backend.claimFramePresentationSignal() }->SameAs<QueueSubmissionPreSubmitHook>;
    { backend.confirmFramePresentationSignal(presentationClaim, submissionToken) }->SameAs<b8>;
    { backend.cancelFramePresentationSignal(presentationClaim) }->SameAs<b8>;
    { backend.present() }->SameAs<Expected<b8, PresentationFailure>>;
};

template<typename T>
concept DeviceApi = requires(
    T& device,
    const T& constDevice,
    const HeapDesc& heapDesc,
    const TextureDesc& textureDesc,
    Texture& texture,
    StagingTexture& stagingTexture,
    Heap& heap,
    const TextureSlice& textureSlice,
    const BufferDesc& bufferDesc,
    Buffer& buffer,
    const ShaderDesc& shaderDesc,
    const void* binary,
    const ShaderSpecialization* specializationConstants,
    const SamplerDesc& samplerDesc,
    const VertexAttributeDesc* vertexAttributeDescs,
    Shader* shader,
    EventQuery& eventQuery,
    TimerQuery& timerQuery,
    const FramebufferDesc& framebufferDesc,
    const GraphicsPipelineDesc& graphicsPipelineDesc,
    const FramebufferInfo& framebufferInfo,
    const ComputePipelineDesc& computePipelineDesc,
    const MeshletPipelineDesc& meshletPipelineDesc,
    const RayTracingPipelineDesc& rayTracingPipelineDesc,
    const BindingLayoutDesc& bindingLayoutDesc,
    const BindlessLayoutDesc& bindlessLayoutDesc,
    const RayTracingOpacityMicromapDesc& opacityMicromapDesc,
    const RayTracingAccelStructDesc& accelStructDesc,
    RayTracingAccelStruct* accelStruct,
    const RayTracingClusterOperationParams& clusterOperationParams,
    const CommandListParameters& commandListParams,
    CommandList* const* commandLists,
    const QueueSubmissionDesc& submissionDesc,
    Object nativeObject
){
    { device.createHeap(heapDesc) }->SameAs<Expected<HeapHandle>>;
    { device.createTexture(textureDesc) }->SameAs<Expected<TextureHandle>>;
    { device.getTextureMemoryRequirements(texture) }->SameAs<Expected<MemoryRequirements>>;
    { device.bindTextureMemory(texture, heap, u64{}) }->SameAs<b8>;
    { device.createHandleForNativeTexture(ObjectType{}, nativeObject, textureDesc, {}) }->SameAs<Expected<TextureHandle>>;
    requires (!requires{ device.createHandleForNativeTexture(ObjectType{}, nativeObject, textureDesc); });
    { device.createStagingTexture(textureDesc, CpuAccessMode::Read) }->SameAs<Expected<StagingTextureHandle>>;
    { device.mapStagingTexture(stagingTexture, textureSlice, CpuAccessMode::Read) }->SameAs<Expected<StagingTextureMapping>>;
    device.unmapStagingTexture(stagingTexture);

    { device.createBuffer(bufferDesc) }->SameAs<Expected<BufferHandle>>;
    { device.mapBuffer(buffer, CpuAccessMode::Write) }->SameAs<Expected<void*>>;
    device.unmapBuffer(buffer);
    { device.getBufferMemoryRequirements(buffer) }->SameAs<Expected<MemoryRequirements>>;
    { device.bindBufferMemory(buffer, heap, u64{}) }->SameAs<b8>;
    { device.createHandleForNativeBuffer(ObjectType{}, nativeObject, bufferDesc, {}) }->SameAs<Expected<BufferHandle>>;
    requires (!requires{ device.createHandleForNativeBuffer(ObjectType{}, nativeObject, bufferDesc, u32{}); });

    { device.createShader(shaderDesc, binary, usize{}) }->SameAs<Expected<ShaderHandle>>;
    { device.createShaderSpecialization(*shader, specializationConstants, u32{}) }->SameAs<Expected<ShaderHandle>>;
    { device.createShaderLibrary(binary, usize{}) }->SameAs<Expected<ShaderLibraryHandle>>;
    { device.createSampler(samplerDesc) }->SameAs<Expected<SamplerHandle>>;
    { device.createInputLayout(vertexAttributeDescs, u32{}, shader) }->SameAs<Expected<InputLayoutHandle>>;

    { device.createEventQuery() }->SameAs<Expected<EventQueryHandle>>;
    { device.setEventQuery(eventQuery, CommandQueue::Graphics) }->SameAs<b8>;
    { device.pollEventQuery(eventQuery) }->SameAs<Expected<b8>>;
    { device.waitEventQuery(eventQuery) }->SameAs<b8>;
    { device.createTimerQuery() }->SameAs<Expected<TimerQueryHandle>>;
    { device.pollTimerQuery(timerQuery) }->SameAs<Expected<b8>>;
    { device.getTimerQueryResult(timerQuery) }->SameAs<Expected<TimerQueryResult>>;
    { device.getTimerQueryTime(timerQuery) }->SameAs<Expected<f32>>;
    { device.resetTimerQuery(timerQuery) }->SameAs<b8>;

    { device.createFramebuffer(framebufferDesc) }->SameAs<Expected<FramebufferHandle>>;
    { device.createGraphicsPipeline(graphicsPipelineDesc, framebufferInfo) }->SameAs<Expected<GraphicsPipelineHandle>>;
    { device.createComputePipeline(computePipelineDesc) }->SameAs<Expected<ComputePipelineHandle>>;
    { device.createMeshletPipeline(meshletPipelineDesc, framebufferInfo) }->SameAs<Expected<MeshletPipelineHandle>>;
    { device.createRayTracingPipeline(rayTracingPipelineDesc) }->SameAs<Expected<RayTracingPipelineHandle>>;
    { device.createBindingLayout(bindingLayoutDesc) }->SameAs<Expected<BindingLayoutHandle>>;
    { device.createBindlessLayout(bindlessLayoutDesc) }->SameAs<Expected<BindingLayoutHandle>>;

    { device.createOpacityMicromap(opacityMicromapDesc) }->SameAs<Expected<RayTracingOpacityMicromapHandle>>;
    { device.createAccelStruct(accelStructDesc) }->SameAs<Expected<RayTracingAccelStructHandle>>;
    { device.getAccelStructMemoryRequirements(*accelStruct) }->SameAs<Expected<MemoryRequirements>>;
    { device.getClusterOperationSizeInfo(clusterOperationParams) }->SameAs<Expected<RayTracingClusterOperationSizeInfo>>;
    { device.bindAccelStructMemory(*accelStruct, heap, u64{}) }->SameAs<b8>;

    { device.createCommandList(commandListParams) }->SameAs<Expected<CommandListHandle>>;
    { device.executeCommandLists(commandLists, usize{}, CommandQueue::Graphics, submissionDesc) }->SameAs<Expected<QueueSubmissionReceipt>>;
    { device.executeCommandLists(commandLists, usize{}, GpuPhysicalQueueId{}, submissionDesc) }->SameAs<Expected<QueueSubmissionReceipt>>;
    device.queueWaitForCommandList(CommandQueue::Graphics, CommandQueue::Graphics, u64{});
    { device.getDeviceGeneration() }->SameAs<u16>;
    { device.getPhysicalQueueIndex(CommandQueue::Graphics) }->SameAs<u16>;
    { device.getPrimaryPhysicalQueue(CommandQueue::Graphics) }->SameAs<GpuPhysicalQueueId>;
    { device.getPhysicalQueueTopology() }->SameAs<GpuPhysicalQueueTopology>;
    { device.getPhysicalQueueInfo(GpuPhysicalQueueId{}) }->SameAs<const GpuPhysicalQueueInfo*>;
    { device.getQueueTimelineSnapshot(GpuPhysicalQueueId{}) }->SameAs<Expected<GpuQueueTimelineSnapshot>>;
    { device.getCommandArenaStatistics(GpuPhysicalQueueId{}) }->SameAs<GpuCommandArenaStatistics>;
    { device.getCommandArenaWorkerStatistics(GpuPhysicalQueueId{}, u64{}, u32{}) }->SameAs<GpuCommandArenaWorkerStatistics>;
    { device.matchesPhysicalQueueIdentity(CommandQueue::Graphics, u16{}, u16{}) }->SameAs<b8>;
    { device.matchesPhysicalQueueIdentity(GpuPhysicalQueueId{}) }->SameAs<b8>;
    { device.validateSubmissionWaitToken(QueueSubmissionToken{}) }->SameAs<b8>;
    { device.isDeviceLost() }->SameAs<b8>;
    { device.requiresRecreation() }->SameAs<b8>;
    { device.waitForIdle() }->SameAs<b8>;
    device.runGarbageCollection();
    { constDevice.queryFeatureSupport(Feature::Meshlets) }noexcept->SameAs<b8>;
    { constDevice.getWaveLaneCounts() }noexcept->SameAs<WaveLaneCountRange>;
    { device.queryFormatSupport(Format::RGBA8_UNORM) }->SameAs<FormatSupport::Mask>;
    { device.queryCoopVecFeatures() }->SameAs<Expected<CooperativeVectorDeviceFeatures>>;
    { device.getCoopVecMatrixSize(CooperativeVectorDataType::Float16, CooperativeVectorMatrixLayout::RowMajor, i32{}, i32{}) }->SameAs<Expected<usize>>;
    { device.getNativeQueue(ObjectType{}, CommandQueue::Graphics) }->SameAs<Object>;
    { device.getNativeQueue(ObjectType{}, GpuPhysicalQueueId{}) }->SameAs<Object>;
    { device.isGpuCrashDiagnosticsEnabled() }->SameAs<b8>;
    { device.getGpuCrashTracker() }->SameAs<GpuCrashTracker&>;
};

template<typename T>
concept CommandListApi = requires(
    T& commandList,
    Texture& texture,
    StagingTexture& stagingTexture,
    Buffer& buffer,
    Framebuffer& framebuffer,
    RayTracingAccelStruct* accelStruct,
    RayTracingOpacityMicromap* opacityMicromap,
    TimerQuery& timerQuery,
    TimerQueryRecordingToken& timerQueryRecordingToken,
    const TextureSlice& textureSlice,
    const TextureSubresourceSet& subresources,
    const Rect& rect,
    const Box& box,
    const Color& color,
    const RenderPassParameters& renderPassParameters,
    const UIntColor& uintColor,
    const IntColor& intColor,
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
    const AStringView markerName,
    CommandListResourceStateHandoff& resourceStateHandoff
){
    commandList.open();
    commandList.close();
    { commandList.hasCommandBuffer() }->SameAs<b8>;
    commandList.open(&resourceStateHandoff);
    commandList.close(&resourceStateHandoff);
    resourceStateHandoff.reset();
    { resourceStateHandoff.valid() }->SameAs<b8>;
    commandList.clearState();
    commandList.beginRenderPass(framebuffer, renderPassParameters);
    commandList.endRenderPass();

    commandList.clearTextureFloat(texture, subresources, color);
    commandList.clearTextureRectFloat(texture, subresources, rect, color);
    commandList.clearTextureBoxFloat(texture, subresources, box, color);
    commandList.clearDepthStencilTexture(texture, subresources, b8{}, f32{}, b8{}, u8{});
    commandList.clearDepthStencilTextureRect(texture, subresources, rect, b8{}, f32{}, b8{}, u8{});
    commandList.clearDepthStencilTextureBox(texture, subresources, box, b8{}, f32{}, b8{}, u8{});
    commandList.clearTextureUInt(texture, subresources, u32{});
    commandList.clearTextureUInt(texture, subresources, uintColor);
    commandList.clearTextureRectUInt(texture, subresources, rect, u32{});
    commandList.clearTextureRectUInt(texture, subresources, rect, uintColor);
    commandList.clearTextureBoxUInt(texture, subresources, box, u32{});
    commandList.clearTextureBoxUInt(texture, subresources, box, uintColor);
    commandList.clearTextureInt(texture, subresources, i32{});
    commandList.clearTextureInt(texture, subresources, intColor);
    commandList.clearTextureRectInt(texture, subresources, rect, i32{});
    commandList.clearTextureRectInt(texture, subresources, rect, intColor);
    commandList.clearTextureBoxInt(texture, subresources, box, i32{});
    commandList.clearTextureBoxInt(texture, subresources, box, intColor);
    commandList.copyTexture(texture, textureSlice, texture, textureSlice);
    commandList.copyTexture(stagingTexture, textureSlice, texture, textureSlice);
    commandList.copyTexture(texture, textureSlice, stagingTexture, textureSlice);
    { commandList.tryWriteTexture(texture, u32{}, u32{}, data, usize{}, usize{}) }->SameAs<b8>;
    commandList.writeTexture(texture, u32{}, u32{}, data, usize{}, usize{});
    commandList.resolveTexture(texture, subresources, texture, subresources);
    { commandList.tryWriteBuffer(buffer, data, usize{}, u64{}) }->SameAs<b8>;
    commandList.writeBuffer(buffer, data, usize{}, u64{});
    commandList.clearBufferUInt(buffer, u32{});
    commandList.copyBuffer(buffer, u64{}, buffer, u64{}, u64{});

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
    commandList.buildTopLevelAccelStruct(accelStruct, instances, usize{}, RayTracingAccelStructBuildFlags::None);
    commandList.executeMultiIndirectClusterOperation(clusterOperationDesc);
    commandList.buildTopLevelAccelStructFromBuffer(accelStruct, &buffer, u64{}, usize{}, RayTracingAccelStructBuildFlags::None);
    commandList.convertCoopVecMatrices(coopVecConvertDescs, usize{});
    { commandList.resetTimerQuery(timerQuery) }->SameAs<b8>;
    { commandList.beginTimerQuery(timerQuery) }->SameAs<Expected<TimerQueryRecordingToken>>;
    { commandList.endTimerQuery(timerQuery, timerQueryRecordingToken) }->SameAs<b8>;
    { commandList.endTimerQueryFromExistingClaim(timerQuery, timerQueryRecordingToken) } noexcept -> SameAs<b8>;
    commandList.beginMarker(markerName);
    commandList.endMarker();
    commandList.abandonMarker();

    commandList.setResourceStatesForFramebuffer(framebuffer);
    commandList.setEnableUavBarriersForTexture(&texture, b8{});
    commandList.setEnableUavBarriersForBuffer(&buffer, b8{});
    commandList.beginTrackingTextureState(&texture, subresources, ResourceStates::ShaderResource);
    commandList.beginTrackingBufferState(&buffer, ResourceStates::ShaderResource);
    commandList.setTextureState(&texture, subresources, ResourceStates::ShaderResource);
    commandList.setBufferState(&buffer, ResourceStates::ShaderResource);
    commandList.setAccelStructState(accelStruct, ResourceStates::AccelStructRead);
    commandList.releaseTextureOwnership(&texture, subresources, CommandQueue::Compute);
    commandList.releaseBufferOwnership(&buffer, CommandQueue::Compute);
    commandList.releaseTextureOwnership(&texture, subresources, GpuPhysicalQueueId{});
    commandList.releaseBufferOwnership(&buffer, GpuPhysicalQueueId{});
    commandList.releaseTextureOwnership(&texture, subresources, CommandQueue::Transfer);
    commandList.releaseBufferOwnership(&buffer, CommandQueue::Transfer);
    commandList.setPermanentTextureState(&texture, ResourceStates::ShaderResource);
    commandList.setPermanentBufferState(&buffer, ResourceStates::ShaderResource);
    commandList.commitBarriers();
    { commandList.getTextureSubresourceState(&texture, ArraySlice{}, MipLevel{}) }->SameAs<ResourceStates::Mask>;
    { commandList.getBufferState(&buffer) }->SameAs<ResourceStates::Mask>;
    { commandList.getPermanentTextureState(&texture) }->SameAs<ResourceStates::Mask>;
    { commandList.getPermanentBufferState(&buffer) }->SameAs<ResourceStates::Mask>;
    commandList.getDevice();
    { commandList.getDevice() }->SameAs<Device&>;
    { commandList.getDescription() }->SameAs<const CommandListParameters&>;
    { commandList.getResolvedDescription() }->SameAs<CommandListParameters>;

};

template<typename T, typename Desc>
concept DescribedResourceApi = requires(const T& resource){
    { resource.getDescription() }->SameAs<const Desc&>;
};

template<typename T>
concept BufferApi = DescribedResourceApi<T, BufferDesc> && requires(const T& buffer){
    { buffer.getCreationDescription() }->SameAs<const BufferDesc&>;
    { buffer.descriptionMatchesCreation() }->SameAs<b8>;
    { buffer.getGpuVirtualAddress() }->SameAs<GpuVirtualAddress>;
};

template<typename T>
concept TextureApi = DescribedResourceApi<T, TextureDesc> && requires(T& texture, TextureSubresourceSet subresources){
    { texture.getCreationDescription() }->SameAs<const TextureDesc&>;
    { texture.descriptionMatchesCreation() }->SameAs<b8>;
    { texture.getNativeView(ObjectType{}, Format::UNKNOWN, subresources, TextureDimension::Unknown, b8{}) }->SameAs<Expected<Object>>;
};

template<typename T>
concept ShaderApi = DescribedResourceApi<T, ShaderDesc> && requires(const T& shader, const void*& bytecode, usize& bytecodeSize){
    shader.getBytecode(bytecode, bytecodeSize);
};

template<typename T>
concept ShaderLibraryApi = requires(const T& constLibrary, T& library, const void*& bytecode, usize& bytecodeSize){
    constLibrary.getBytecode(bytecode, bytecodeSize);
    { library.getShader(AStringView{}, ShaderType::All) }->SameAs<Expected<ShaderHandle>>;
};

template<typename T>
concept FramebufferApi = DescribedResourceApi<T, FramebufferDesc> && requires(const T& framebuffer){
    { framebuffer.getFramebufferInfo() }->SameAs<const FramebufferInfoEx&>;
};

template<typename T>
concept RayTracingOpacityMicromapApi = DescribedResourceApi<T, RayTracingOpacityMicromapDesc> && requires(const T& micromap){
    { micromap.getDeviceAddress() }->SameAs<u64>;
};

template<typename T>
concept RayTracingAccelStructApi = DescribedResourceApi<T, RayTracingAccelStructDesc> && requires(const T& accelStruct){
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
    { pipeline.createShaderTable() }->SameAs<Expected<RayTracingShaderTableHandle>>;
};

template<typename T>
concept RayTracingShaderTableApi = requires(T& shaderTable, AStringView exportName){
    { shaderTable.setRayGenerationShader(exportName) }->SameAs<b8>;
    { shaderTable.addMissShader(exportName) }->SameAs<Expected<u32>>;
    { shaderTable.addHitGroup(exportName) }->SameAs<Expected<u32>>;
    { shaderTable.addCallableShader(exportName) }->SameAs<Expected<u32>>;
    shaderTable.clearMissShaders();
    shaderTable.clearHitShaders();
    shaderTable.clearCallableShaders();
    { shaderTable.getPipeline() }->SameAs<RayTracingPipeline&>;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

