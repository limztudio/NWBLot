// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vulkan.h"

#include <vulkan/vulkan.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Forward declarations
class Device;
class Queue;
class TrackedCommandBuffer;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vulkan Context - Core Vulkan objects and capabilities


struct VulkanContext{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkAllocationCallbacks* allocationCallbacks = nullptr;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    
    // Extensions
    struct Extensions{
        bool KHR_synchronization2 = false;
        bool KHR_ray_tracing_pipeline = false;
        bool KHR_acceleration_structure = false;
        bool buffer_device_address = false;
        bool EXT_debug_utils = false;
        bool KHR_swapchain = false;
        bool KHR_dynamic_rendering = false;
    } extensions;
    
    // Device properties
    VkPhysicalDeviceProperties physicalDeviceProperties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties{};
    
    // Empty descriptor set layout for pipelines without bindings
    VkDescriptorSetLayout emptyDescriptorSetLayout = VK_NULL_HANDLE;
    
    VulkanContext() = default;
    VulkanContext(VkInstance inst, VkPhysicalDevice physDev, VkDevice dev, VkAllocationCallbacks* allocCb)
        : instance(inst)
        , physicalDevice(physDev)
        , device(dev)
        , allocationCallbacks(allocCb)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tracked Command Buffer - Command buffer with resource tracking


typedef RefCountPtr<TrackedCommandBuffer, BlankDeleter<TrackedCommandBuffer>> TrackedCommandBufferPtr;

class TrackedCommandBuffer : public RefCounter<IResource>{
public:
    VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    
    Vector<RefCountPtr<IResource>> referencedResources;
    Vector<RefCountPtr<IBuffer>> referencedStagingBuffers;
    
    u64 recordingID = 0;
    u64 submissionID = 0;
    
    TrackedCommandBuffer(const VulkanContext& context, CommandQueue::Enum queueType, u32 queueFamilyIndex);
    ~TrackedCommandBuffer() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(TrackedCommandBuffer)
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Queue - Command queue wrapper with timeline semaphore tracking


class Queue{
public:
    VkSemaphore trackingSemaphore = VK_NULL_HANDLE;
    
    Queue(const VulkanContext& context, CommandQueue::Enum queueID, VkQueue queue, u32 queueFamilyIndex);
    ~Queue();
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(Queue)
    
    [[nodiscard]] TrackedCommandBufferPtr createCommandBuffer();
    [[nodiscard]] TrackedCommandBufferPtr getOrCreateCommandBuffer();
    
    void addWaitSemaphore(VkSemaphore semaphore, u64 value);
    void addSignalSemaphore(VkSemaphore semaphore, u64 value);
    
    u64 submit(ICommandList* const* ppCmd, usize numCmd);
    u64 updateLastFinishedID();
    
    [[nodiscard]] bool pollCommandList(u64 commandListID);
    [[nodiscard]] bool waitCommandList(u64 commandListID, u64 timeout);
    
    [[nodiscard]] VkQueue getVkQueue()const{ return m_Queue; }
    [[nodiscard]] u32 getQueueFamilyIndex()const{ return m_QueueFamilyIndex; }
    [[nodiscard]] CommandQueue::Enum getQueueID()const{ return m_QueueID; }
    [[nodiscard]] u64 getLastSubmittedID()const{ return m_LastSubmittedID; }
    [[nodiscard]] u64 getLastFinishedID()const{ return m_LastFinishedID; }
    
private:
    const VulkanContext& m_Context;
    VkQueue m_Queue;
    CommandQueue::Enum m_QueueID;
    u32 m_QueueFamilyIndex;
    
    Mutex m_Mutex;
    Vector<VkSemaphore> m_WaitSemaphores;
    Vector<u64> m_WaitSemaphoreValues;
    Vector<VkSemaphore> m_SignalSemaphores;
    Vector<u64> m_SignalSemaphoreValues;
    
    u64 m_LastRecordingID = 0;
    u64 m_LastSubmittedID = 0;
    u64 m_LastFinishedID = 0;
    
    List<TrackedCommandBufferPtr> m_CommandBuffersInFlight;
    List<TrackedCommandBufferPtr> m_CommandBuffersPool;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory Allocator - Handles Vulkan memory allocation


class VulkanAllocator{
public:
    explicit VulkanAllocator(const VulkanContext& context);
    ~VulkanAllocator() = default;
    
    VkResult allocateBufferMemory(class Buffer* buffer, bool enableDeviceAddress = false);
    void freeBufferMemory(class Buffer* buffer);
    
    VkResult allocateTextureMemory(class Texture* texture);
    void freeTextureMemory(class Texture* texture);
    
private:
    const VulkanContext& m_Context;
    
    u32 findMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties)const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Upload Manager - Handles staging buffer uploads


struct BufferChunk;

class UploadManager{
public:
    UploadManager(class Device* pParent, u64 defaultChunkSize, u64 memoryLimit, bool isScratchBuffer);
    ~UploadManager();
    
    bool suballocateBuffer(u64 size, class Buffer** pBuffer, u64* pOffset, void** pCpuVA,
                          u64 currentVersion, u32 alignment = 256);
    void submitChunks(u64 currentVersion, u64 submittedVersion);
    
private:
    class Device* m_Device;
    u64 m_DefaultChunkSize;
    u64 m_MemoryLimit;
    bool m_IsScratchBuffer;
    
    List<RefCountPtr<BufferChunk>> m_ChunkPool;
    RefCountPtr<BufferChunk> m_CurrentChunk;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Buffer


class Buffer : public RefCounter<IBuffer>{
public:
    BufferDesc desc;
    
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    u64 deviceAddress = 0;
    void* mappedMemory = nullptr;
    
    // For volatile buffers - version tracking per frame
    struct VolatileBufferState{
        i32 latestVersion = 0;
        i32 minVersion = 0;
        i32 maxVersion = 0;
        bool initialized = false;
    };
    Vector<u64> versionTracking;
    VolatileBufferState volatileState;
    
    Buffer(const VulkanContext& context, VulkanAllocator& allocator);
    ~Buffer() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(Buffer)
    
    // IBuffer interface
    [[nodiscard]] const BufferDesc& getDescription()const override{ return desc; }
    [[nodiscard]] GpuVirtualAddress getGpuVirtualAddress()const override{ return deviceAddress; }
    
private:
    const VulkanContext& m_Context;
    VulkanAllocator& m_Allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Texture


class Texture : public RefCounter<ITexture>{
public:
    TextureDesc desc;
    
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageCreateInfo imageInfo{};
    
    // Cached views for different use cases
    HashMap<u64, VkImageView> views;
    
    Texture(const VulkanContext& context, VulkanAllocator& allocator);
    ~Texture() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(Texture)
    
    // ITexture interface
    [[nodiscard]] const TextureDesc& getDescription()const override{ return desc; }
    Object getNativeView(ObjectType objectType, Format::Enum format, TextureSubresourceSet subresources, TextureDimension::Enum dimension, bool isReadOnlyDSV) override;
    
    [[nodiscard]] VkImageView getView(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format, bool isReadOnlyDSV = false);
    
private:
    const VulkanContext& m_Context;
    VulkanAllocator& m_Allocator;
    
    u64 makeViewKey(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format, bool isReadOnlyDSV)const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Staging Texture


class StagingTexture : public RefCounter<IStagingTexture>{
public:
    TextureDesc desc;
    
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mappedMemory = nullptr;
    CpuAccessMode::Enum cpuAccess;
    
    StagingTexture(const VulkanContext& context, VulkanAllocator& allocator);
    ~StagingTexture() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(StagingTexture)
    
    // IStagingTexture interface
    [[nodiscard]] const TextureDesc& getDescription()const override{ return desc; }
    
private:
    const VulkanContext& m_Context;
    VulkanAllocator& m_Allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampler


class Sampler : public RefCounter<ISampler>{
public:
    SamplerDesc desc;
    VkSampler sampler = VK_NULL_HANDLE;
    
    Sampler(const VulkanContext& context);
    ~Sampler() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(Sampler)
    
    // ISampler interface
    [[nodiscard]] const SamplerDesc& getDescription()const override{ return desc; }
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader


class Shader : public RefCounter<IShader>{
public:
    ShaderDesc desc;
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    
    Vector<u8> bytecode;
    
    Shader(const VulkanContext& context);
    ~Shader() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(Shader)
    
    // IShader interface
    [[nodiscard]] const ShaderDesc& getDescription()const override{ return desc; }
    void getBytecode(const void** ppBytecode, usize* pSize)const override{
        *ppBytecode = bytecode.data();
        *pSize = bytecode.size();
    }
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader Library


class ShaderLibrary : public RefCounter<IShaderLibrary>{
public:
    Vector<u8> bytecode;
    HashMap<Name, RefCountPtr<Shader>> shaders;
    
    ShaderLibrary(const VulkanContext& context);
    ~ShaderLibrary() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(ShaderLibrary)
    
    // IShaderLibrary interface
    void getBytecode(const void** ppBytecode, usize* pSize)const override;
    ShaderHandle getShader(const Name& entryName, ShaderType::Mask shaderType) override;
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input Layout


class InputLayout : public RefCounter<IInputLayout>{
public:
    Vector<VertexAttributeDesc> attributes;
    Vector<VkVertexInputBindingDescription> bindings;
    Vector<VkVertexInputAttributeDescription> vkAttributes;
    
    InputLayout() = default;
    ~InputLayout() override = default;
    
    // IInputLayout interface
    [[nodiscard]] const VertexAttributeDesc* getAttributeDescription(u32 index)const override{
        if(index >= attributes.size())
            return nullptr;
        return &attributes[index];
    }
    
    [[nodiscard]] u32 getNumAttributes()const override{
        return static_cast<u32>(attributes.size());
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Framebuffer


class Framebuffer : public RefCounter<IFramebuffer>{
public:
    FramebufferDesc desc;
    FramebufferInfoEx framebufferInfo;
    
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    
    Vector<RefCountPtr<ITexture>> resources;
    
    Framebuffer(const VulkanContext& context);
    ~Framebuffer() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(Framebuffer)
    
    // IFramebuffer interface
    [[nodiscard]] const FramebufferDesc& getDescription()const override{ return desc; }
    [[nodiscard]] const FramebufferInfoEx& getFramebufferInfo()const override{ return framebufferInfo; }
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Graphics Pipeline


class GraphicsPipeline : public RefCounter<IGraphicsPipeline>{
public:
    GraphicsPipelineDesc desc;
    VkPipeline pipeline = VK_NULL_HANDLE;
    
    GraphicsPipeline(const VulkanContext& context);
    ~GraphicsPipeline() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(GraphicsPipeline)
    
    // IGraphicsPipeline interface
    [[nodiscard]] const GraphicsPipelineDesc& getDescription()const override{ return desc; }
    Object getNativeObject(ObjectType objectType) override;
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compute Pipeline


class ComputePipeline : public RefCounter<IComputePipeline>{
public:
    ComputePipelineDesc desc;
    VkPipeline pipeline = VK_NULL_HANDLE;
    
    ComputePipeline(const VulkanContext& context);
    ~ComputePipeline() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(ComputePipeline)
    
    // IComputePipeline interface
    [[nodiscard]] const ComputePipelineDesc& getDescription()const override{ return desc; }
    Object getNativeObject(ObjectType objectType) override;
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Meshlet Pipeline


class MeshletPipeline : public RefCounter<IMeshletPipeline>{
public:
    MeshletPipelineDesc desc;
    VkPipeline pipeline = VK_NULL_HANDLE;
    
    MeshletPipeline(const VulkanContext& context);
    ~MeshletPipeline() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(MeshletPipeline)
    
    // IMeshletPipeline interface
    [[nodiscard]] const MeshletPipelineDesc& getDescription()const override{ return desc; }
    Object getNativeObject(ObjectType objectType) override;
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing Pipeline


class RayTracingPipeline : public RefCounter<IRayTracingPipeline>{
public:
    RayTracingPipelineDesc desc;
    VkPipeline pipeline = VK_NULL_HANDLE;
    
    RayTracingPipeline(const VulkanContext& context);
    ~RayTracingPipeline() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(RayTracingPipeline)
    
    // IRayTracingPipeline interface
    [[nodiscard]] const RayTracingPipelineDesc& getDescription()const override{ return desc; }
    Object getNativeObject(ObjectType objectType) override;
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Binding Layout


class BindingLayout : public RefCounter<IBindingLayout>{
public:
    BindingLayoutDesc desc;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    Vector<VkDescriptorSetLayout> descriptorSetLayouts;
    
    BindingLayout(const VulkanContext& context);
    ~BindingLayout() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(BindingLayout)
    
    // IBindingLayout interface
    [[nodiscard]] const BindingLayoutDesc* getDescription()const override{ return &desc; }
    Object getNativeObject(ObjectType objectType) override{ return Object(pipelineLayout); }
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Descriptor Table


class DescriptorTable : public RefCounter<IDescriptorTable>{
public:
    RefCountPtr<BindingLayout> layout;
    Vector<VkDescriptorSet> descriptorSets;
    
    DescriptorTable(const VulkanContext& context);
    ~DescriptorTable() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(DescriptorTable)
    
    // IDescriptorTable interface
    [[nodiscard]] u32 getCapacity()const override{ return static_cast<u32>(descriptorSets.size()); }
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Binding Set


class BindingSet : public RefCounter<IBindingSet>{
public:
    BindingSetDesc desc;
    RefCountPtr<DescriptorTable> descriptorTable;
    
    BindingSet(const VulkanContext& context);
    ~BindingSet() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(BindingSet)
    
    // IBindingSet interface
    [[nodiscard]] const BindingSetDesc* getDescription()const override{ return &desc; }
    [[nodiscard]] IDescriptorTable* getDescriptorTable()const override{ return descriptorTable; }
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Acceleration Structure


class AccelStruct : public RefCounter<IRayTracingAccelStruct>{
public:
    RayTracingAccelStructDesc desc;
    VkAccelerationStructureKHR accelStruct = VK_NULL_HANDLE;
    RefCountPtr<IBuffer> buffer;
    
    AccelStruct(const VulkanContext& context);
    ~AccelStruct() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(AccelStruct)
    
    // IRayTracingAccelStruct interface
    [[nodiscard]] const RayTracingAccelStructDesc& getDescription()const override{ return desc; }
    Object getNativeObject(ObjectType objectType) override;
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// State Tracker


class StateTracker{
public:
    GraphicsState graphicsState;
    ComputeState computeState;
    MeshletState meshletState;
    RayTracingState rayTracingState;
    
    StateTracker();
    ~StateTracker();
    
    void reset();
    void setPermanentTextureState(ITexture* texture, ResourceStates state);
    void setPermanentBufferState(IBuffer* buffer, ResourceStates state);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command List


class CommandList : public RefCounter<ICommandList>{
public:
    CommandListDesc desc;
    TrackedCommandBufferPtr currentCmdBuf;
    UniquePtr<StateTracker> stateTracker;
    bool enableAutomaticBarriers = true;
    
    CommandList(class Device* device, const CommandListParameters& params);
    ~CommandList() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(CommandList)
    
    // ICommandList interface - Core
    void open() override;
    void close() override;
    void clearState() override;
    
    // ICommandList interface - Resource states
    void setResourceStatesForBindingSet(IBindingSet* bindingSet) override;
    void setEnableAutomaticBarriers(bool enable) override;
    void commitBarriers() override;
    
    void setTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates stateBits) override;
    void setBufferState(IBuffer* buffer, ResourceStates stateBits) override;
    void setAccelStructState(IRayTracingAccelStruct* as, ResourceStates stateBits) override;
    
    void setPermanentTextureState(ITexture* texture, ResourceStates stateBits) override;
    void setPermanentBufferState(IBuffer* buffer, ResourceStates stateBits) override;
    
    // ICommandList interface - Clear operations
    void clearTextureFloat(ITexture* texture, TextureSubresourceSet subresources, const Color& clearColor) override;
    void clearDepthStencilTexture(ITexture* texture, TextureSubresourceSet subresources, bool clearDepth, f32 depth, bool clearStencil, u8 stencil) override;
    void clearTextureUInt(ITexture* texture, TextureSubresourceSet subresources, u32 clearColor) override;
    
    // ICommandList interface - Copy operations
    void copyTexture(ITexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice) override;
    void copyTextureToBuffer(IBuffer* dest, u64 destOffsetBytes, u32 destRowPitch, ITexture* src, const TextureSlice& srcSlice) override;
    void writeBuffer(IBuffer* buffer, const void* data, usize dataSize, u64 destOffsetBytes = 0) override;
    void clearBufferUInt(IBuffer* buffer, const u32& clearValue) override;
    void copyBuffer(IBuffer* dest, u64 destOffsetBytes, IBuffer* src, u64 srcOffsetBytes, u64 dataSizeBytes) override;
    void writeTexture(ITexture* dest, u32 arraySlice, u32 mipLevel, const void* data, usize rowPitch, usize depthPitch = 0) override;
    void resolveTexture(ITexture* dest, const TextureSubresourceSet& dstSubresources, ITexture* src, const TextureSubresourceSet& srcSubresources) override;
    
    // ICommandList interface - Graphics
    void beginRenderPass(IFramebuffer* framebuffer, const RenderPassParameters& params) override;
    void endRenderPass() override;
    [[nodiscard]] GraphicsState& getGraphicsState() override;
    void setGraphicsState(const GraphicsState& state) override;
    void draw(const DrawArguments& args) override;
    void drawIndexed(const DrawArguments& args) override;
    void drawIndirect(const DrawIndirectArguments& args) override;
    
    // ICommandList interface - Compute
    [[nodiscard]] ComputeState& getComputeState() override;
    void setComputeState(const ComputeState& state) override;
    void dispatch(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1) override;
    void dispatchIndirect(IBuffer* buffer, u64 offsetBytes = 0) override;
    
    // ICommandList interface - Meshlet
    [[nodiscard]] MeshletState& getMeshletState() override;
    void setMeshletState(const MeshletState& state) override;
    void dispatchMesh(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1) override;
    
    // ICommandList interface - Ray Tracing
    [[nodiscard]] RayTracingState& getRayTracingState() override;
    void setRayTracingState(const RayTracingState& state) override;
    void dispatchRays(const RayTracingDispatchArguments& args) override;
    void buildBottomLevelAccelStruct(IRayTracingAccelStruct* as, const RayTracingGeometryDesc* pGeometries, usize numGeometries, RayTracingAccelStructBuildFlags buildFlags) override;
    void compactBottomLevelAccelStruct(IRayTracingAccelStruct* src, IRayTracingAccelStruct* dest) override;
    void buildTopLevelAccelStruct(IRayTracingAccelStruct* as, const RayTracingInstanceDesc* pInstances, usize numInstances, RayTracingAccelStructBuildFlags buildFlags) override;
    void buildOpacityMicromap(IRayTracingOpacityMicromap* omm, const RayTracingOpacityMicromapDesc& desc) override;
    
    // ICommandList interface - Queries and markers
    void beginTimerQuery(ITimerQuery* query) override;
    void endTimerQuery(ITimerQuery* query) override;
    void beginMarker(const char* name) override;
    void endMarker() override;
    void setEventQuery(IEventQuery* query, CommandQueue::Enum waitQueue) override;
    void resetEventQuery(IEventQuery* query) override;
    void waitEventQuery(IEventQuery* query) override;
    
    // Internal
    [[nodiscard]] TrackedCommandBufferPtr getCurrentCmdBuf()const{ return currentCmdBuf; }
    
private:
    class Device* m_Device;
    const VulkanContext* m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Event Query


class EventQuery : public RefCounter<IEventQuery>{
public:
    VkFence fence = VK_NULL_HANDLE;
    bool started = false;
    
    EventQuery(const VulkanContext& context);
    ~EventQuery() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(EventQuery)
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timer Query


class TimerQuery : public RefCounter<ITimerQuery>{
public:
    VkQueryPool queryPool = VK_NULL_HANDLE;
    bool started = false;
    bool resolved = false;
    
    TimerQuery(const VulkanContext& context);
    ~TimerQuery() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(TimerQuery)
    
private:
    const VulkanContext& m_Context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device Implementation


class Device : public RefCounter<IDevice>{
public:
    explicit Device(const DeviceDesc& desc);
    ~Device() override;
    
    NWB_NOT_COPYABLE_NOT_MOVABLE(Device)
    
    // Core::IDevice interface
    [[nodiscard]] HeapHandle createHeap(const HeapDesc& d) override;
    [[nodiscard]] TextureHandle createTexture(const TextureDesc& d) override;
    [[nodiscard]] MemoryRequirements getTextureMemoryRequirements(ITexture* texture) override;
    bool bindTextureMemory(ITexture* texture, IHeap* heap, u64 offset) override;
    [[nodiscard]] TextureHandle createHandleForNativeTexture(ObjectType objectType, Object texture, const TextureDesc& desc) override;
    [[nodiscard]] StagingTextureHandle createStagingTexture(const TextureDesc& d, CpuAccessMode::Enum cpuAccess) override;
    void* mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode::Enum cpuAccess, usize* outRowPitch) override;
    void unmapStagingTexture(IStagingTexture* tex) override;
    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& d) override;
    void* mapBuffer(IBuffer* buffer, CpuAccessMode::Enum cpuAccess) override;
    void unmapBuffer(IBuffer* buffer) override;
    [[nodiscard]] MemoryRequirements getBufferMemoryRequirements(IBuffer* buffer) override;
    bool bindBufferMemory(IBuffer* buffer, IHeap* heap, u64 offset) override;
    [[nodiscard]] BufferHandle createHandleForNativeBuffer(ObjectType objectType, Object buffer, const BufferDesc& desc) override;
    [[nodiscard]] ShaderHandle createShader(const ShaderDesc& d, const void* binary, usize binarySize) override;
    [[nodiscard]] ShaderHandle createShaderSpecialization(IShader* baseShader, const ShaderSpecialization* constants, u32 numConstants) override;
    [[nodiscard]] ShaderLibraryHandle createShaderLibrary(const void* binary, usize binarySize) override;
    [[nodiscard]] SamplerHandle createSampler(const SamplerDesc& d) override;
    [[nodiscard]] InputLayoutHandle createInputLayout(const VertexAttributeDesc* d, u32 attributeCount, IShader* vertexShader) override;
    [[nodiscard]] EventQueryHandle createEventQuery() override;
    void setEventQuery(IEventQuery* query, CommandQueue::Enum queue) override;
    bool pollEventQuery(IEventQuery* query) override;
    void waitEventQuery(IEventQuery* query) override;
    void resetEventQuery(IEventQuery* query) override;
    [[nodiscard]] TimerQueryHandle createTimerQuery() override;
    bool pollTimerQuery(ITimerQuery* query) override;
    f32 getTimerQueryTime(ITimerQuery* query) override;
    void resetTimerQuery(ITimerQuery* query) override;
    [[nodiscard]] GraphicsAPI::Enum getGraphicsAPI() override{ return GraphicsAPI::VULKAN; }
    [[nodiscard]] FramebufferHandle createFramebuffer(const FramebufferDesc& desc) override;
    [[nodiscard]] GraphicsPipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc, FramebufferInfo const& fbinfo) override;
    [[nodiscard]] ComputePipelineHandle createComputePipeline(const ComputePipelineDesc& desc) override;
    [[nodiscard]] MeshletPipelineHandle createMeshletPipeline(const MeshletPipelineDesc& desc, FramebufferInfo const& fbinfo) override;
    [[nodiscard]] RayTracingPipelineHandle createRayTracingPipeline(const RayTracingPipelineDesc& desc) override;
    [[nodiscard]] BindingLayoutHandle createBindingLayout(const BindingLayoutDesc& desc) override;
    [[nodiscard]] BindingLayoutHandle createBindlessLayout(const BindlessLayoutDesc& desc) override;
    [[nodiscard]] BindingSetHandle createBindingSet(const BindingSetDesc& desc, IBindingLayout* layout) override;
    [[nodiscard]] DescriptorTableHandle createDescriptorTable(IBindingLayout* layout) override;
    void resizeDescriptorTable(IDescriptorTable* descriptorTable, u32 newSize, bool keepContents = true) override;
    bool writeDescriptorTable(IDescriptorTable* descriptorTable, const BindingSetItem& item) override;
    [[nodiscard]] RayTracingOpacityMicromapHandle createOpacityMicromap(const RayTracingOpacityMicromapDesc& desc) override;
    [[nodiscard]] RayTracingAccelStructHandle createAccelStruct(const RayTracingAccelStructDesc& desc) override;
    [[nodiscard]] MemoryRequirements getAccelStructMemoryRequirements(IRayTracingAccelStruct* as) override;
    [[nodiscard]] RayTracingClusterOperationSizeInfo getClusterOperationSizeInfo(const RayTracingClusterOperationParams& params) override;
    bool bindAccelStructMemory(IRayTracingAccelStruct* as, IHeap* heap, u64 offset) override;
    [[nodiscard]] CommandListHandle createCommandList(const CommandListParameters& params = CommandListParameters()) override;
    u64 executeCommandLists(ICommandList* const* pCommandLists, usize numCommandLists, CommandQueue::Enum executionQueue = CommandQueue::Graphics) override;
    void queueWaitForCommandList(CommandQueue::Enum waitQueue, CommandQueue::Enum executionQueue, u64 instance) override;
    bool waitForIdle() override;
    void runGarbageCollection() override;
    bool queryFeatureSupport(Feature::Enum feature, void* pInfo = nullptr, usize infoSize = 0) override;
    [[nodiscard]] FormatSupport::Mask queryFormatSupport(Format::Enum format) override;
    [[nodiscard]] CooperativeVectorDeviceFeatures queryCoopVecFeatures() override;
    usize getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns) override;
    [[nodiscard]] Object getNativeQueue(ObjectType objectType, CommandQueue::Enum queue) override;
    bool isAftermathEnabled() override{ return false; }
    [[nodiscard]] AftermathCrashDumpHelper& getAftermathCrashDumpHelper() override;
    
    // Vulkan::IDevice interface
    [[nodiscard]] VkSemaphore getQueueSemaphore(CommandQueue::Enum queue) override;
    void queueWaitForSemaphore(CommandQueue::Enum waitQueue, VkSemaphore semaphore, u64 value) override;
    void queueSignalSemaphore(CommandQueue::Enum executionQueue, VkSemaphore semaphore, u64 value) override;
    [[nodiscard]] u64 queueGetCompletedInstance(CommandQueue::Enum queue) override;
    
    // Internal methods
    [[nodiscard]] const VulkanContext& getContext()const{ return m_Context; }
    [[nodiscard]] Queue* getQueue(CommandQueue::Enum queueType)const;
    [[nodiscard]] VulkanAllocator& getAllocator(){ return m_Allocator; }
    
private:
    VulkanContext m_Context;
    VulkanAllocator m_Allocator;
    UniquePtr<Queue> m_Queues[static_cast<u32>(CommandQueue::kCount)];
    
    // Upload managers for different purposes
    UniquePtr<UploadManager> m_UploadManager;
    UniquePtr<UploadManager> m_ScratchManager;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
