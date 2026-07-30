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
concept GraphicsApi = requires(const T& graphics){
    { graphics.getDevice() }->SameAs<GraphicsBackend::Device&>;
};

template<typename T>
concept DeviceApi = requires(
    T& device,
    const TextureDesc& textureDesc,
    Texture* texture,
    const BufferDesc& bufferDesc,
    Buffer* buffer,
    const ShaderDesc& shaderDesc,
    const void* binary,
    const SamplerDesc& samplerDesc,
    const VertexAttributeDesc* vertexAttributeDescs,
    Shader* shader,
    EventQuery* eventQuery,
    TimerQuery* timerQuery,
    TimerQueryResult& timerQueryResult,
    const FramebufferDesc& framebufferDesc,
    const GraphicsPipelineDesc& graphicsPipelineDesc,
    const FramebufferInfo& framebufferInfo,
    const ComputePipelineDesc& computePipelineDesc,
    const MeshletPipelineDesc& meshletPipelineDesc,
    const RayTracingPipelineDesc& rayTracingPipelineDesc,
    const BindingLayoutDesc& bindingLayoutDesc,
    const BindlessLayoutDesc& bindlessLayoutDesc,
    const RayTracingAccelStructDesc& accelStructDesc,
    RayTracingAccelStruct* accelStruct,
    const CommandListParameters& commandListParams,
    CommandList* const* commandLists,
    bool* outSubmitted,
    const QueueSubmissionDesc& submissionDesc,
    void* featureInfo,
    Object nativeObject
){
    { device.createTexture(textureDesc) }->SameAs<TextureHandle>;
    { device.createHandleForNativeTexture(ObjectType{}, nativeObject, textureDesc) }->SameAs<TextureHandle>;
    { device.createBuffer(bufferDesc) }->SameAs<BufferHandle>;
    { device.mapBuffer(buffer, CpuAccessMode::Write) }->SameAs<void*>;
    device.unmapBuffer(buffer);
    { device.createShader(shaderDesc, binary, usize{}) }->SameAs<ShaderHandle>;
    { device.createSampler(samplerDesc) }->SameAs<SamplerHandle>;
    { device.createInputLayout(vertexAttributeDescs, u32{}, shader) }->SameAs<InputLayoutHandle>;

    { device.createEventQuery() }->SameAs<EventQueryHandle>;
    device.setEventQuery(eventQuery, CommandQueue::Graphics);
    { device.pollEventQuery(eventQuery) }->SameAs<bool>;
    device.waitEventQuery(eventQuery);
    { device.createTimerQuery() }->SameAs<TimerQueryHandle>;
    { device.pollTimerQuery(timerQuery) }->SameAs<bool>;
    { device.getTimerQueryResult(timerQuery, timerQueryResult) }->SameAs<bool>;
    { device.getTimerQueryTime(timerQuery) }->SameAs<f32>;
    device.resetTimerQuery(timerQuery);

    { device.createFramebuffer(framebufferDesc) }->SameAs<FramebufferHandle>;
    { device.createGraphicsPipeline(graphicsPipelineDesc, framebufferInfo) }->SameAs<GraphicsPipelineHandle>;
    { device.createComputePipeline(computePipelineDesc) }->SameAs<ComputePipelineHandle>;
    { device.createMeshletPipeline(meshletPipelineDesc, framebufferInfo) }->SameAs<MeshletPipelineHandle>;
    { device.createRayTracingPipeline(rayTracingPipelineDesc) }->SameAs<RayTracingPipelineHandle>;
    { device.createBindingLayout(bindingLayoutDesc) }->SameAs<BindingLayoutHandle>;
    { device.createBindlessLayout(bindlessLayoutDesc) }->SameAs<BindingLayoutHandle>;

    { device.createAccelStruct(accelStructDesc) }->SameAs<RayTracingAccelStructHandle>;

    { device.createCommandList(commandListParams) }->SameAs<CommandListHandle>;
    { device.executeCommandLists(commandLists, usize{}, CommandQueue::Graphics) }->SameAs<u64>;
    { device.executeCommandLists(commandLists, usize{}, CommandQueue::Graphics, outSubmitted) }->SameAs<u64>;
    { device.executeCommandLists(commandLists, usize{}, CommandQueue::Graphics, submissionDesc) }->SameAs<QueueSubmissionToken>;
    { device.executeCommandLists(commandLists, usize{}, RenderLane::AsyncCompute, submissionDesc) }->SameAs<QueueSubmissionToken>;
    device.queueWaitForCommandList(CommandQueue::Graphics, CommandQueue::Graphics, u64{});
    { device.resolveRenderLane(RenderLane::AsyncCompute) }->SameAs<CommandQueue::Enum>;
    { device.isRenderLaneDedicated(RenderLane::AsyncCompute) }->SameAs<bool>;
    { device.isDeviceLost() }->SameAs<bool>;
    { device.waitForIdle() }->SameAs<bool>;
    device.runGarbageCollection();
    { device.queryFeatureSupport(Feature::Meshlets, featureInfo, usize{}) }->SameAs<bool>;
    { device.queryFormatSupport(Format::RGBA8_UNORM) }->SameAs<FormatSupport::Mask>;
    { device.getNativeQueue(ObjectType{}, CommandQueue::Graphics) }->SameAs<Object>;
    { device.isGpuCrashDiagnosticsEnabled() }->SameAs<bool>;
    { device.getGpuCrashTracker() }->SameAs<GpuCrashTracker&>;
};

template<typename T>
concept CommandListApi = requires(
    T& commandList,
    Texture* texture,
    Buffer* buffer,
    Framebuffer& framebuffer,
    RayTracingAccelStruct* accelStruct,
    TimerQuery* timerQuery,
    const TextureSubresourceSet& subresources,
    const Rect& rect,
    const Color& color,
    const UIntColor& uintColor,
    const void* data,
    const GraphicsState& graphicsState,
    const DrawArguments& drawArguments,
    const ComputeState& computeState,
    const MeshletState& meshletState,
    const RayTracingState& rayTracingState,
    const RayTracingDispatchRaysArguments& rayTracingArguments,
    const RayTracingGeometryDesc* geometries,
    const RayTracingInstanceDesc* instances,
    const AStringView markerName,
    CommandListResourceStateHandoff& resourceStateHandoff
){
    commandList.open();
    commandList.close();
    { commandList.hasCommandBuffer() }->SameAs<bool>;
    commandList.open(&resourceStateHandoff);
    commandList.close(&resourceStateHandoff);
    resourceStateHandoff.reset();
    { resourceStateHandoff.valid() }->SameAs<bool>;
    commandList.clearState();
    commandList.endRenderPass();

    commandList.clearTextureFloat(texture, subresources, color);
    commandList.clearDepthStencilTexture(texture, subresources, bool{}, f32{}, bool{}, u8{});
    commandList.clearTextureUInt(texture, subresources, u32{});
    commandList.clearTextureUInt(texture, subresources, uintColor);
    commandList.clearTextureRectUInt(texture, subresources, rect, u32{});
    commandList.clearTextureRectUInt(texture, subresources, rect, uintColor);
    commandList.writeTexture(texture, u32{}, u32{}, data, usize{}, usize{});
    commandList.writeBuffer(buffer, data, usize{}, u64{});
    commandList.clearBufferUInt(buffer, u32{});
    commandList.copyBuffer(buffer, u64{}, buffer, u64{}, u64{});

    commandList.setPushConstants(data, usize{});

    commandList.setGraphicsState(graphicsState);
    commandList.draw(drawArguments);
    commandList.drawIndexed(drawArguments);
    commandList.setComputeState(computeState);
    commandList.dispatch(u32{}, u32{}, u32{});
    commandList.dispatchIndirect(u32{});
    commandList.setMeshletState(meshletState);
    commandList.dispatchMesh(u32{}, u32{}, u32{});
    commandList.setRayTracingState(rayTracingState);
    commandList.dispatchRays(rayTracingArguments);

    commandList.buildBottomLevelAccelStruct(accelStruct, geometries, usize{}, RayTracingAccelStructBuildFlags::None);
    commandList.buildTopLevelAccelStruct(accelStruct, instances, usize{}, RayTracingAccelStructBuildFlags::None);
    commandList.beginTimerQuery(timerQuery);
    commandList.endTimerQuery(timerQuery);
    commandList.beginMarker(markerName);
    commandList.endMarker();

    commandList.setResourceStatesForFramebuffer(framebuffer);
    commandList.setEnableUavBarriersForTexture(texture, bool{});
    commandList.setEnableUavBarriersForBuffer(buffer, bool{});
    commandList.setTextureState(texture, subresources, ResourceStates::ShaderResource);
    commandList.setBufferState(buffer, ResourceStates::ShaderResource);
    commandList.setAccelStructState(accelStruct, ResourceStates::AccelStructRead);
    commandList.releaseTextureOwnership(texture, subresources, RenderLane::AsyncCompute);
    commandList.releaseBufferOwnership(buffer, RenderLane::AsyncCompute);
    commandList.setPermanentBufferState(buffer, ResourceStates::ShaderResource);
    commandList.commitBarriers();
    { commandList.getTextureSubresourceState(texture, ArraySlice{}, MipLevel{}) }->SameAs<ResourceStates::Mask>;
    { commandList.getBufferState(buffer) }->SameAs<ResourceStates::Mask>;
    commandList.getDevice();
    { commandList.getDevice() }->SameAs<Device&>;
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
concept FramebufferApi = DescribedResourceApi<T, FramebufferDesc> && requires(const T& framebuffer){
    { framebuffer.getFramebufferInfo() }->SameAs<const FramebufferInfoEx&>;
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
    { pipeline.createShaderTable() }->SameAs<RayTracingShaderTableHandle>;
};

template<typename T>
concept RayTracingShaderTableApi = requires(
    T& shaderTable,
    AStringView exportName
){
    shaderTable.setRayGenerationShader(exportName);
    { shaderTable.addMissShader(exportName) }->SameAs<u32>;
    { shaderTable.addHitGroup(exportName) }->SameAs<u32>;
    { shaderTable.getPipeline() }->SameAs<RayTracingPipeline*>;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
