// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vulkan.h"

#include <vulkan/vulkan.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


namespace __hidden{


template <typename T, typename U>
T checked_cast(U u){
    static_assert(!std::is_same<T, U>::value, "Unnecessary checked_cast");
#ifdef NWB_DEBUG
    if(!u) return nullptr;
    T t = dynamic_cast<T>(u);
    NWB_ASSERT(t);
    return t;
#else
    return static_cast<T>(u);
#endif
}

// Helper function declarations (defined in vulkan_constants.cpp)
VkAccessFlags2 getVkAccessFlags(ResourceStates::Mask state);
VkPipelineStageFlags2 getVkPipelineStageFlags(ResourceStates::Mask state);
VkImageLayout getVkImageLayout(ResourceStates::Mask state);
VkFormat convertFormat(Format::Enum format);
VkSampleCountFlagBits getSampleCountFlagBits(u32 sampleCount);


} // namespace __hidden


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

class TrackedCommandBuffer final : public RefCounter<IResource>, NoCopy{
public:
    VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    
    Vector<RefCountPtr<IResource>> referencedResources;
    Vector<RefCountPtr<IBuffer>> referencedStagingBuffers;
    
    u64 recordingID = 0;
    u64 submissionID = 0;
    
    TrackedCommandBuffer(const VulkanContext& context, CommandQueue::Enum queueType, u32 queueFamilyIndex);
    ~TrackedCommandBuffer()override;
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Queue - Command queue wrapper with timeline semaphore tracking


class Queue final : NoCopy{
public:
    VkSemaphore trackingSemaphore = VK_NULL_HANDLE;
    
    Queue(const VulkanContext& context, CommandQueue::Enum queueID, VkQueue queue, u32 queueFamilyIndex);
    ~Queue();
    
    [[nodiscard]] TrackedCommandBufferPtr createCommandBuffer();
    [[nodiscard]] TrackedCommandBufferPtr getOrCreateCommandBuffer();
    
    void addWaitSemaphore(VkSemaphore semaphore, u64 value);
    void addSignalSemaphore(VkSemaphore semaphore, u64 value);
    
    u64 submit(ICommandList* const* ppCmd, usize numCmd);
    u64 updateLastFinishedID();
    
    [[nodiscard]] bool pollCommandList(u64 commandListID);
    [[nodiscard]] bool waitCommandList(u64 commandListID, u64 timeout);
    
    [[nodiscard]] VkQueue getVkQueue()const{ return m_queue; }
    [[nodiscard]] u32 getQueueFamilyIndex()const{ return m_queueFamilyIndex; }
    [[nodiscard]] CommandQueue::Enum getQueueID()const{ return m_queueID; }
    [[nodiscard]] u64 getLastSubmittedID()const{ return m_lastSubmittedID; }
    [[nodiscard]] u64 getLastFinishedID()const{ return m_lastFinishedID; }
    
private:
    const VulkanContext& m_context;
    VkQueue m_queue;
    CommandQueue::Enum m_queueID;
    u32 m_queueFamilyIndex;
    
    Mutex m_mutex;
    Vector<VkSemaphore> m_waitSemaphores;
    Vector<u64> m_waitSemaphoreValues;
    Vector<VkSemaphore> m_signalSemaphores;
    Vector<u64> m_signalSemaphoreValues;
    
    u64 m_lastRecordingID = 0;
    u64 m_lastSubmittedID = 0;
    u64 m_lastFinishedID = 0;
    
    List<TrackedCommandBufferPtr> m_commandBuffersInFlight;
    List<TrackedCommandBufferPtr> m_commandBuffersPool;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory Allocator - Handles Vulkan memory allocation


class VulkanAllocator final : NoCopy{
public:
    explicit VulkanAllocator(const VulkanContext& context);
    ~VulkanAllocator() = default;
    
    VkResult allocateBufferMemory(class Buffer* buffer, bool enableDeviceAddress = false);
    void freeBufferMemory(class Buffer* buffer);
    
    VkResult allocateTextureMemory(class Texture* texture);
    void freeTextureMemory(class Texture* texture);
    
private:
    const VulkanContext& m_context;
    
    u32 findMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties)const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Upload Manager - Handles staging buffer uploads


struct BufferChunk;

class UploadManager final : NoCopy{
public:
    UploadManager(class Device* pParent, u64 defaultChunkSize, u64 memoryLimit, bool isScratchBuffer);
    ~UploadManager();
    
    bool suballocateBuffer(u64 size, class Buffer** pBuffer, u64* pOffset, void** pCpuVA,
                          u64 currentVersion, u32 alignment = 256);
    void submitChunks(u64 currentVersion, u64 submittedVersion);
    
private:
    class Device* m_device;
    u64 m_defaultChunkSize;
    u64 m_memoryLimit;
    bool m_isScratchBuffer;
    
    List<RefCountPtr<BufferChunk>> m_chunkPool;
    RefCountPtr<BufferChunk> m_currentChunk;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Buffer


class Buffer final : public RefCounter<IBuffer>, NoCopy{
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
    ~Buffer()override;
    
    // IBuffer interface
    [[nodiscard]] const BufferDesc& getDescription()const override{ return desc; }
    [[nodiscard]] GpuVirtualAddress getGpuVirtualAddress()const override{ return deviceAddress; }
    
private:
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Texture


class Texture final : public RefCounter<ITexture>, NoCopy{
public:
    TextureDesc desc;
    
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageCreateInfo imageInfo{};
    
    // Cached views for different use cases
    HashMap<u64, VkImageView> views;
    
    Texture(const VulkanContext& context, VulkanAllocator& allocator);
    ~Texture()override;
    
    // ITexture interface
    [[nodiscard]] const TextureDesc& getDescription()const override{ return desc; }
    Object getNativeView(ObjectType objectType, Format::Enum format, TextureSubresourceSet subresources, TextureDimension::Enum dimension, bool isReadOnlyDSV)override;
    
    [[nodiscard]] VkImageView getView(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format, bool isReadOnlyDSV = false);
    
private:
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
    
    u64 makeViewKey(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format, bool isReadOnlyDSV)const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Staging Texture


class StagingTexture final : public RefCounter<IStagingTexture>, NoCopy{
public:
    TextureDesc desc;
    
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mappedMemory = nullptr;
    CpuAccessMode::Enum cpuAccess;
    
    StagingTexture(const VulkanContext& context, VulkanAllocator& allocator);
    ~StagingTexture()override;
    
    // IStagingTexture interface
    [[nodiscard]] const TextureDesc& getDescription()const override{ return desc; }
    
private:
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampler


class Sampler final : public RefCounter<ISampler>, NoCopy{
public:
    SamplerDesc desc;
    VkSampler sampler = VK_NULL_HANDLE;
    
    Sampler(const VulkanContext& context);
    ~Sampler()override;
    
    // ISampler interface
    [[nodiscard]] const SamplerDesc& getDescription()const override{ return desc; }
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader


class Shader final : public RefCounter<IShader>, NoCopy{
public:
    ShaderDesc desc;
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    
    Vector<u8> bytecode;
    
    Shader(const VulkanContext& context);
    ~Shader()override;
    
    // IShader interface
    [[nodiscard]] const ShaderDesc& getDescription()const override{ return desc; }
    void getBytecode(const void** ppBytecode, usize* pSize)const override{
        *ppBytecode = bytecode.data();
        *pSize = bytecode.size();
    }
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader Library


class ShaderLibrary final : public RefCounter<IShaderLibrary>, NoCopy{
public:
    Vector<u8> bytecode;
    HashMap<Name, RefCountPtr<Shader>> shaders;
    
    ShaderLibrary(const VulkanContext& context);
    ~ShaderLibrary()override;
    
    // IShaderLibrary interface
    void getBytecode(const void** ppBytecode, usize* pSize)const override;
    ShaderHandle getShader(const Name& entryName, ShaderType::Mask shaderType)override;
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input Layout


class InputLayout final : public RefCounter<IInputLayout>, NoCopy{
public:
    Vector<VertexAttributeDesc> attributes;
    Vector<VkVertexInputBindingDescription> bindings;
    Vector<VkVertexInputAttributeDescription> vkAttributes;
    
    InputLayout() = default;
    ~InputLayout()override = default;
    
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


class Framebuffer final : public RefCounter<IFramebuffer>, NoCopy{
public:
    FramebufferDesc desc;
    FramebufferInfoEx framebufferInfo;
    
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    
    Vector<RefCountPtr<ITexture>> resources;
    
    Framebuffer(const VulkanContext& context);
    ~Framebuffer()override;
    
    // IFramebuffer interface
    [[nodiscard]] const FramebufferDesc& getDescription()const override{ return desc; }
    [[nodiscard]] const FramebufferInfoEx& getFramebufferInfo()const override{ return framebufferInfo; }
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Graphics Pipeline


class GraphicsPipeline final : public RefCounter<IGraphicsPipeline>, NoCopy{
public:
    GraphicsPipelineDesc desc;
    FramebufferInfo framebufferInfo;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    
    GraphicsPipeline(const VulkanContext& context);
    ~GraphicsPipeline()override;
    
    // IGraphicsPipeline interface
    [[nodiscard]] const GraphicsPipelineDesc& getDescription()const override{ return desc; }
    [[nodiscard]] const FramebufferInfo& getFramebufferInfo()const override{ return framebufferInfo; }
    Object getNativeHandle(ObjectType objectType)override;
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compute Pipeline


class ComputePipeline final : public RefCounter<IComputePipeline>, NoCopy{
public:
    ComputePipelineDesc desc;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    
    ComputePipeline(const VulkanContext& context);
    ~ComputePipeline()override;
    
    // IComputePipeline interface
    [[nodiscard]] const ComputePipelineDesc& getDescription()const override{ return desc; }
    Object getNativeHandle(ObjectType objectType)override;
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Meshlet Pipeline


class MeshletPipeline final : public RefCounter<IMeshletPipeline>, NoCopy{
public:
    MeshletPipelineDesc desc;
    FramebufferInfo framebufferInfo;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    
    MeshletPipeline(const VulkanContext& context);
    ~MeshletPipeline()override;
    
    // IMeshletPipeline interface
    [[nodiscard]] const MeshletPipelineDesc& getDescription()const override{ return desc; }
    [[nodiscard]] const FramebufferInfo& getFramebufferInfo()const override{ return framebufferInfo; }
    Object getNativeHandle(ObjectType objectType)override;
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing Pipeline


class RayTracingPipeline final : public RefCounter<IRayTracingPipeline>, NoCopy{
public:
    RayTracingPipelineDesc desc;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    Vector<u8> shaderGroupHandles;
    
    RayTracingPipeline(const VulkanContext& context);
    ~RayTracingPipeline()override;
    
    // IRayTracingPipeline interface
    [[nodiscard]] const RayTracingPipelineDesc& getDescription()const override{ return desc; }
    RayTracingShaderTableHandle createShaderTable()override;
    Object getNativeHandle(ObjectType objectType)override;
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader Table


class ShaderTable final : public RefCounter<IRayTracingShaderTable>, NoCopy{
public:
    RayTracingPipeline* pipeline = nullptr;
    
    BufferHandle raygenBuffer;
    u64 raygenOffset = 0;
    
    BufferHandle missBuffer;
    u64 missOffset = 0;
    u32 missCount = 0;
    
    BufferHandle hitBuffer;
    u64 hitOffset = 0;
    u32 hitCount = 0;
    
    BufferHandle callableBuffer;
    u64 callableOffset = 0;
    u32 callableCount = 0;
    
    ShaderTable(const VulkanContext& context);
    ~ShaderTable()override;
    
    // IRayTracingShaderTable interface
    void setRayGenerationShader(const Name& exportName, IBindingSet* bindings = nullptr)override;
    int addMissShader(const Name& exportName, IBindingSet* bindings = nullptr)override;
    int addHitGroup(const Name& exportName, IBindingSet* bindings = nullptr)override;
    int addCallableShader(const Name& exportName, IBindingSet* bindings = nullptr)override;
    void clearMissShaders()override;
    void clearHitShaders()override;
    void clearCallableShaders()override;
    IRayTracingPipeline* getPipeline()override{ return pipeline; }
    Object getNativeHandle(ObjectType objectType)override;
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Binding Layout


class BindingLayout final : public RefCounter<IBindingLayout>, NoCopy{
public:
    BindingLayoutDesc desc;
    BindlessLayoutDesc bindlessDesc;
    bool isBindless = false;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    Vector<VkDescriptorSetLayout> descriptorSetLayouts;
    
    BindingLayout(const VulkanContext& context);
    ~BindingLayout()override;
    
    // IBindingLayout interface
    [[nodiscard]] const BindingLayoutDesc* getDescription()const override{ return isBindless ? nullptr : &desc; }
    [[nodiscard]] const BindlessLayoutDesc* getBindlessDesc()const override{ return isBindless ? &bindlessDesc : nullptr; }
    Object getNativeHandle(ObjectType objectType)override{ return Object(pipelineLayout); }
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Descriptor Table


class DescriptorTable final : public RefCounter<IDescriptorTable>, NoCopy{
public:
    RefCountPtr<BindingLayout> layout;
    Vector<VkDescriptorSet> descriptorSets;
    
    DescriptorTable(const VulkanContext& context);
    ~DescriptorTable()override;
    
    // IDescriptorTable interface
    [[nodiscard]] u32 getCapacity()const override{ return static_cast<u32>(descriptorSets.size()); }
    [[nodiscard]] u32 getFirstDescriptorIndexInHeap()const override{ return 0; }
    
    // IBindingSet interface (inherited through IDescriptorTable)
    [[nodiscard]] const BindingSetDesc* getDescription()const override{ return nullptr; }
    [[nodiscard]] IBindingLayout* getLayout()const override{ return layout; }
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Binding Set


class BindingSet final : public RefCounter<IBindingSet>, NoCopy{
public:
    BindingSetDesc desc;
    RefCountPtr<BindingLayout> layout;
    RefCountPtr<DescriptorTable> descriptorTable;
    Vector<VkDescriptorSet> descriptorSets;
    
    BindingSet(const VulkanContext& context);
    ~BindingSet()override;
    
    // IBindingSet interface
    [[nodiscard]] const BindingSetDesc* getDescription()const override{ return &desc; }
    [[nodiscard]] IBindingLayout* getLayout()const override{ return layout; }
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Acceleration Structure


class AccelStruct final : public RefCounter<IRayTracingAccelStruct>, NoCopy{
public:
    RayTracingAccelStructDesc desc;
    VkAccelerationStructureKHR accelStruct = VK_NULL_HANDLE;
    RefCountPtr<IBuffer> buffer;
    u64 deviceAddress = 0;
    bool compacted = false;
    
    AccelStruct(const VulkanContext& context);
    ~AccelStruct()override;
    
    // IRayTracingAccelStruct interface
    [[nodiscard]] const RayTracingAccelStructDesc& getDescription()const override{ return desc; }
    [[nodiscard]] bool isCompacted()const override{ return compacted; }
    [[nodiscard]] u64 getDeviceAddress()const override{ return deviceAddress; }
    Object getNativeHandle(ObjectType objectType)override;
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// State Tracker


class StateTracker final : NoCopy{
public:
    GraphicsState graphicsState;
    ComputeState computeState;
    MeshletState meshletState;
    RayTracingState rayTracingState;
    
    StateTracker();
    ~StateTracker();
    
    void reset();
    void setPermanentTextureState(ITexture* texture, ResourceStates::Mask state);
    void setPermanentBufferState(IBuffer* buffer, ResourceStates::Mask state);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command List


class CommandList final : public RefCounter<ICommandList>, NoCopy{
public:
    CommandListParameters desc;
    TrackedCommandBufferPtr currentCmdBuf;
    UniquePtr<StateTracker> stateTracker;
    bool enableAutomaticBarriers = true;
    
    // Current pipeline state for internal use
    GraphicsState currentGraphicsState;
    ComputeState currentComputeState;
    MeshletState currentMeshletState;
    RayTracingState currentRayTracingState;
    
    CommandList(class Device* device, const CommandListParameters& params);
    ~CommandList()override;
    
    // ICommandList interface - Core
    void open()override;
    void close()override;
    void clearState()override;
    
    // ICommandList interface - Resource states
    void setResourceStatesForBindingSet(IBindingSet* bindingSet)override;
    void setEnableAutomaticBarriers(bool enable)override;
    void commitBarriers()override;
    
    void setTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits)override;
    void setBufferState(IBuffer* buffer, ResourceStates::Mask stateBits)override;
    void setAccelStructState(IRayTracingAccelStruct* as, ResourceStates::Mask stateBits)override;
    
    void setPermanentTextureState(ITexture* texture, ResourceStates::Mask stateBits)override;
    void setPermanentBufferState(IBuffer* buffer, ResourceStates::Mask stateBits)override;
    
    // ICommandList interface - Clear operations
    void clearTextureFloat(ITexture* texture, TextureSubresourceSet subresources, const Color& clearColor)override;
    void clearDepthStencilTexture(ITexture* texture, TextureSubresourceSet subresources, bool clearDepth, f32 depth, bool clearStencil, u8 stencil)override;
    void clearTextureUInt(ITexture* texture, TextureSubresourceSet subresources, u32 clearColor)override;
    
    // ICommandList interface - Copy operations
    void copyTexture(ITexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice)override;
    void copyTexture(IStagingTexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice)override;
    void copyTexture(ITexture* dest, const TextureSlice& destSlice, IStagingTexture* src, const TextureSlice& srcSlice)override;
    void writeBuffer(IBuffer* buffer, const void* data, usize dataSize, u64 destOffsetBytes = 0)override;
    void clearBufferUInt(IBuffer* buffer, u32 clearValue)override;
    void copyBuffer(IBuffer* dest, u64 destOffsetBytes, IBuffer* src, u64 srcOffsetBytes, u64 dataSizeBytes)override;
    void writeTexture(ITexture* dest, u32 arraySlice, u32 mipLevel, const void* data, usize rowPitch, usize depthPitch = 0)override;
    void resolveTexture(ITexture* dest, const TextureSubresourceSet& dstSubresources, ITexture* src, const TextureSubresourceSet& srcSubresources)override;
    
    // ICommandList interface - Sampler Feedback (stubs)
    void clearSamplerFeedbackTexture(ISamplerFeedbackTexture* texture)override;
    void decodeSamplerFeedbackTexture(IBuffer* buffer, ISamplerFeedbackTexture* texture, Format::Enum format)override;
    void setSamplerFeedbackTextureState(ISamplerFeedbackTexture* texture, ResourceStates::Mask stateBits)override;
    
    // ICommandList interface - Push Constants
    void setPushConstants(const void* data, usize byteSize)override;
    
    // ICommandList interface - Graphics
    void setGraphicsState(const GraphicsState& state)override;
    void draw(const DrawArguments& args)override;
    void drawIndexed(const DrawArguments& args)override;
    void drawIndirect(u32 offsetBytes, u32 drawCount = 1)override;
    void drawIndexedIndirect(u32 offsetBytes, u32 drawCount = 1)override;
    
    // ICommandList interface - Compute
    void setComputeState(const ComputeState& state)override;
    void dispatch(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1)override;
    void dispatchIndirect(u32 offsetBytes)override;
    
    // ICommandList interface - Meshlet
    void setMeshletState(const MeshletState& state)override;
    void dispatchMesh(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1)override;
    
    // ICommandList interface - Ray Tracing
    void setRayTracingState(const RayTracingState& state)override;
    void dispatchRays(const RayTracingDispatchRaysArguments& args)override;
    void buildBottomLevelAccelStruct(IRayTracingAccelStruct* as, const RayTracingGeometryDesc* pGeometries, usize numGeometries, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None)override;
    void compactBottomLevelAccelStructs()override;
    void buildTopLevelAccelStruct(IRayTracingAccelStruct* as, const RayTracingInstanceDesc* pInstances, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None)override;
    void buildOpacityMicromap(IRayTracingOpacityMicromap* omm, const RayTracingOpacityMicromapDesc& desc)override;
    void buildTopLevelAccelStructFromBuffer(IRayTracingAccelStruct* as, IBuffer* instanceBuffer, u64 instanceBufferOffset, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None)override;
    void executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& desc)override;
    void convertCoopVecMatrices(CooperativeVectorConvertMatrixLayoutDesc const* convertDescs, usize numDescs)override;
    
    // ICommandList interface - Queries and markers
    void beginTimerQuery(ITimerQuery* query)override;
    void endTimerQuery(ITimerQuery* query)override;
    void beginMarker(const Name& name)override;
    void endMarker()override;
    
    // ICommandList interface - UAV barriers and tracking
    void setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers)override;
    void setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers)override;
    void beginTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits)override;
    void beginTrackingBufferState(IBuffer* buffer, ResourceStates::Mask stateBits)override;
    ResourceStates::Mask getTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel)override;
    ResourceStates::Mask getBufferState(IBuffer* buffer)override;
    
    // ICommandList interface - Accessors
    IDevice* getDevice()override;
    const CommandListParameters& getDesc()override;
    
    // Internal helpers (not interface)
    void copyTextureToBuffer(IBuffer* dest, u64 destOffsetBytes, u32 destRowPitch, ITexture* src, const TextureSlice& srcSlice);
    void setEventQuery(IEventQuery* query, CommandQueue::Enum waitQueue);
    void resetEventQuery(IEventQuery* query);
    void waitEventQuery(IEventQuery* query);
    [[nodiscard]] TrackedCommandBufferPtr getCurrentCmdBuf()const{ return currentCmdBuf; }
    
private:
    class Device* m_device;
    const VulkanContext* m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Event Query


class EventQuery final : public RefCounter<IEventQuery>, NoCopy{
public:
    VkFence fence = VK_NULL_HANDLE;
    bool started = false;
    
    EventQuery(const VulkanContext& context);
    ~EventQuery()override;
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timer Query


class TimerQuery final : public RefCounter<ITimerQuery>, NoCopy{
public:
    VkQueryPool queryPool = VK_NULL_HANDLE;
    bool started = false;
    bool resolved = false;
    
    TimerQuery(const VulkanContext& context);
    ~TimerQuery()override;
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device Implementation


class Device final : public RefCounter<IDevice>, NoCopy{
public:
    explicit Device(const DeviceDesc& desc);
    ~Device()override;
    
    // Core::IDevice interface
    [[nodiscard]] HeapHandle createHeap(const HeapDesc& d)override;
    [[nodiscard]] TextureHandle createTexture(const TextureDesc& d)override;
    [[nodiscard]] MemoryRequirements getTextureMemoryRequirements(ITexture* texture)override;
    bool bindTextureMemory(ITexture* texture, IHeap* heap, u64 offset)override;
    [[nodiscard]] TextureHandle createHandleForNativeTexture(ObjectType objectType, Object texture, const TextureDesc& desc)override;
    [[nodiscard]] StagingTextureHandle createStagingTexture(const TextureDesc& d, CpuAccessMode::Enum cpuAccess)override;
    void* mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode::Enum cpuAccess, usize* outRowPitch)override;
    void unmapStagingTexture(IStagingTexture* tex)override;
    void getTextureTiling(ITexture* texture, u32* numTiles, PackedMipDesc* desc, TileShape* tileShape, u32* subresourceTilingsNum, SubresourceTiling* subresourceTilings)override;
    void updateTextureTileMappings(ITexture* texture, const TextureTilesMapping* tileMappings, u32 numTileMappings, CommandQueue::Enum executionQueue = CommandQueue::Graphics)override;
    [[nodiscard]] SamplerFeedbackTextureHandle createSamplerFeedbackTexture(ITexture* pairedTexture, const SamplerFeedbackTextureDesc& desc)override;
    [[nodiscard]] SamplerFeedbackTextureHandle createSamplerFeedbackForNativeTexture(ObjectType objectType, Object texture, ITexture* pairedTexture)override;
    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& d)override;
    void* mapBuffer(IBuffer* buffer, CpuAccessMode::Enum cpuAccess)override;
    void unmapBuffer(IBuffer* buffer)override;
    [[nodiscard]] MemoryRequirements getBufferMemoryRequirements(IBuffer* buffer)override;
    bool bindBufferMemory(IBuffer* buffer, IHeap* heap, u64 offset)override;
    [[nodiscard]] BufferHandle createHandleForNativeBuffer(ObjectType objectType, Object buffer, const BufferDesc& desc)override;
    [[nodiscard]] ShaderHandle createShader(const ShaderDesc& d, const void* binary, usize binarySize)override;
    [[nodiscard]] ShaderHandle createShaderSpecialization(IShader* baseShader, const ShaderSpecialization* constants, u32 numConstants)override;
    [[nodiscard]] ShaderLibraryHandle createShaderLibrary(const void* binary, usize binarySize)override;
    [[nodiscard]] SamplerHandle createSampler(const SamplerDesc& d)override;
    [[nodiscard]] InputLayoutHandle createInputLayout(const VertexAttributeDesc* d, u32 attributeCount, IShader* vertexShader)override;
    [[nodiscard]] EventQueryHandle createEventQuery()override;
    void setEventQuery(IEventQuery* query, CommandQueue::Enum queue)override;
    bool pollEventQuery(IEventQuery* query)override;
    void waitEventQuery(IEventQuery* query)override;
    void resetEventQuery(IEventQuery* query)override;
    [[nodiscard]] TimerQueryHandle createTimerQuery()override;
    bool pollTimerQuery(ITimerQuery* query)override;
    f32 getTimerQueryTime(ITimerQuery* query)override;
    void resetTimerQuery(ITimerQuery* query)override;
    [[nodiscard]] GraphicsAPI::Enum getGraphicsAPI()override{ return GraphicsAPI::VULKAN; }
    [[nodiscard]] FramebufferHandle createFramebuffer(const FramebufferDesc& desc)override;
    [[nodiscard]] GraphicsPipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc, FramebufferInfo const& fbinfo)override;
    [[nodiscard]] ComputePipelineHandle createComputePipeline(const ComputePipelineDesc& desc)override;
    [[nodiscard]] MeshletPipelineHandle createMeshletPipeline(const MeshletPipelineDesc& desc, FramebufferInfo const& fbinfo)override;
    [[nodiscard]] RayTracingPipelineHandle createRayTracingPipeline(const RayTracingPipelineDesc& desc)override;
    [[nodiscard]] BindingLayoutHandle createBindingLayout(const BindingLayoutDesc& desc)override;
    [[nodiscard]] BindingLayoutHandle createBindlessLayout(const BindlessLayoutDesc& desc)override;
    [[nodiscard]] BindingSetHandle createBindingSet(const BindingSetDesc& desc, IBindingLayout* layout)override;
    [[nodiscard]] DescriptorTableHandle createDescriptorTable(IBindingLayout* layout)override;
    void resizeDescriptorTable(IDescriptorTable* descriptorTable, u32 newSize, bool keepContents = true)override;
    bool writeDescriptorTable(IDescriptorTable* descriptorTable, const BindingSetItem& item)override;
    [[nodiscard]] RayTracingOpacityMicromapHandle createOpacityMicromap(const RayTracingOpacityMicromapDesc& desc)override;
    [[nodiscard]] RayTracingAccelStructHandle createAccelStruct(const RayTracingAccelStructDesc& desc)override;
    [[nodiscard]] MemoryRequirements getAccelStructMemoryRequirements(IRayTracingAccelStruct* as)override;
    [[nodiscard]] RayTracingClusterOperationSizeInfo getClusterOperationSizeInfo(const RayTracingClusterOperationParams& params)override;
    bool bindAccelStructMemory(IRayTracingAccelStruct* as, IHeap* heap, u64 offset)override;
    [[nodiscard]] CommandListHandle createCommandList(const CommandListParameters& params = CommandListParameters())override;
    u64 executeCommandLists(ICommandList* const* pCommandLists, usize numCommandLists, CommandQueue::Enum executionQueue = CommandQueue::Graphics)override;
    void queueWaitForCommandList(CommandQueue::Enum waitQueue, CommandQueue::Enum executionQueue, u64 instance)override;
    bool waitForIdle()override;
    void runGarbageCollection()override;
    bool queryFeatureSupport(Feature::Enum feature, void* pInfo = nullptr, usize infoSize = 0)override;
    [[nodiscard]] FormatSupport::Mask queryFormatSupport(Format::Enum format)override;
    [[nodiscard]] CooperativeVectorDeviceFeatures queryCoopVecFeatures()override;
    usize getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, int rows, int columns)override;
    [[nodiscard]] Object getNativeQueue(ObjectType objectType, CommandQueue::Enum queue)override;
    bool isAftermathEnabled()override{ return false; }
    [[nodiscard]] AftermathCrashDumpHelper& getAftermathCrashDumpHelper()override;
    
    // Vulkan::IDevice interface
    [[nodiscard]] VkSemaphore getQueueSemaphore(CommandQueue::Enum queue)override;
    void queueWaitForSemaphore(CommandQueue::Enum waitQueue, VkSemaphore semaphore, u64 value)override;
    void queueSignalSemaphore(CommandQueue::Enum executionQueue, VkSemaphore semaphore, u64 value)override;
    [[nodiscard]] u64 queueGetCompletedInstance(CommandQueue::Enum queue)override;
    
    // Internal methods
    [[nodiscard]] const VulkanContext& getContext()const{ return m_context; }
    [[nodiscard]] Queue* getQueue(CommandQueue::Enum queueType)const;
    [[nodiscard]] VulkanAllocator& getAllocator(){ return m_allocator; }
    [[nodiscard]] UploadManager* getUploadManager(){ return m_uploadManager.get(); }
    [[nodiscard]] UploadManager* getScratchManager(){ return m_scratchManager.get(); }
    
private:
    VulkanContext m_context;
    VulkanAllocator m_allocator;
    UniquePtr<Queue> m_queues[static_cast<u32>(CommandQueue::kCount)];
    
    // Upload managers for different purposes
    UniquePtr<UploadManager> m_uploadManager;
    UniquePtr<UploadManager> m_scratchManager;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
