// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vulkan.h"

#include <vulkan/vulkan.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{
    extern constexpr VkAccessFlags2 GetVkAccessFlags(ResourceStates::Mask state);
    extern constexpr VkPipelineStageFlags2 GetVkPipelineStageFlags(ResourceStates::Mask state);
    extern constexpr VkImageLayout GetVkImageLayout(ResourceStates::Mask state);
    extern constexpr VkFormat ConvertFormat(Format::Enum format);
    extern constexpr VkSampleCountFlagBits GetSampleCountFlagBits(u32 sampleCount);
    extern VkDeviceAddress GetBufferDeviceAddress(IBuffer* _buffer, u64 offset);
    extern constexpr VkImageType TextureDimensionToImageType(TextureDimension::Enum dimension);
    extern constexpr VkImageViewType TextureDimensionToViewType(TextureDimension::Enum dimension);
    extern constexpr VkSampleCountFlagBits GetSampleCount(u32 sampleCount);
    extern constexpr VkImageUsageFlags PickImageUsage(const TextureDesc& desc);
    extern constexpr VkImageCreateFlags PickImageFlags(const TextureDesc& desc);
    extern constexpr VkDescriptorType ConvertDescriptorType(ResourceType::Enum type);
    extern constexpr VkShaderStageFlags ConvertShaderStages(ShaderType::Mask stages);
    extern constexpr VkComponentTypeKHR ConvertCoopVecDataType(CooperativeVectorDataType::Enum type);
    extern constexpr CooperativeVectorDataType::Enum ConvertCoopVecDataType(VkComponentTypeKHR type);
    extern constexpr VkCooperativeVectorMatrixLayoutNV ConvertCoopVecMatrixLayout(CooperativeVectorMatrixLayout::Enum layout);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Device;
class Queue;
class TrackedCommandBuffer;

class Buffer;
class Texture;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core Vulkan objects and capabilities


struct VulkanContext{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkAllocationCallbacks* allocationCallbacks = nullptr;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    
    VkPhysicalDeviceProperties physicalDeviceProperties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties{};
    
    VkDescriptorSetLayout emptyDescriptorSetLayout = VK_NULL_HANDLE;
    
    struct Extensions{
        bool KHR_synchronization2 = false;
        bool KHR_ray_tracing_pipeline = false;
        bool KHR_acceleration_structure = false;
        bool buffer_device_address = false;
        bool EXT_debug_utils = false;
        bool KHR_swapchain = false;
        bool KHR_dynamic_rendering = false;
        bool EXT_opacity_micromap = false;
        bool NV_cooperative_vector = false;
        bool NV_cluster_acceleration_structure = false;
    } extensions;
    
    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelStructProperties{};
    VkPhysicalDeviceCooperativeVectorPropertiesNV coopVecProperties{};
    VkPhysicalDeviceCooperativeVectorFeaturesNV coopVecFeatures{};
    VkPhysicalDeviceClusterAccelerationStructurePropertiesNV nvClusterAccelerationStructureProperties{};
    

    VulkanContext() = default;
    VulkanContext(VkInstance inst, VkPhysicalDevice physDev, VkDevice dev, VkAllocationCallbacks* allocCb)
        : instance(inst)
        , physicalDevice(physDev)
        , device(dev)
        , allocationCallbacks(allocCb)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command buffer with resource tracking


class TrackedCommandBuffer final : public RefCounter<IResource>, NoCopy{
public:
    TrackedCommandBuffer(const VulkanContext& context, CommandQueue::Enum queueType, u32 queueFamilyIndex);
    virtual ~TrackedCommandBuffer()override;


public:
    VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    
    Vector<RefCountPtr<IResource, BlankDeleter<IResource>>> referencedResources;
    Vector<RefCountPtr<IBuffer, BlankDeleter<IBuffer>>> referencedStagingBuffers;
    
    VkFence signalFence = VK_NULL_HANDLE;
    
    u64 recordingID = 0;
    u64 submissionID = 0;


private:
    const VulkanContext& m_context;
};
typedef RefCountPtr<TrackedCommandBuffer, BlankDeleter<TrackedCommandBuffer>> TrackedCommandBufferPtr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command queue wrapper with timeline semaphore tracking


class Queue final : NoCopy{
public:
    Queue(const VulkanContext& context, CommandQueue::Enum queueID, VkQueue queue, u32 queueFamilyIndex);
    ~Queue();


public:
    [[nodiscard]] TrackedCommandBufferPtr createCommandBuffer();
    [[nodiscard]] TrackedCommandBufferPtr getOrCreateCommandBuffer();
    
    void addWaitSemaphore(VkSemaphore semaphore, u64 value);
    void addSignalSemaphore(VkSemaphore semaphore, u64 value);
    
    u64 submit(ICommandList* const* ppCmd, usize numCmd);
    void updateTextureTileMappings(ITexture* texture, const TextureTilesMapping* tileMappings, u32 numTileMappings);
    u64 updateLastFinishedID();
    
    [[nodiscard]] bool pollCommandList(u64 commandListID);
    [[nodiscard]] bool waitCommandList(u64 commandListID, u64 timeout);
    void waitForIdle();

    [[nodiscard]] VkQueue getVkQueue()const{ return m_queue; }
    [[nodiscard]] u32 getQueueFamilyIndex()const{ return m_queueFamilyIndex; }
    [[nodiscard]] CommandQueue::Enum getQueueID()const{ return m_queueID; }
    [[nodiscard]] u64 getLastSubmittedID()const{ return m_lastSubmittedID; }
    [[nodiscard]] u64 getLastFinishedID()const{ return m_lastFinishedID; }
    
    
public:
    VkSemaphore trackingSemaphore = VK_NULL_HANDLE;
    
    
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
// Handles Vulkan memory allocation


class VulkanAllocator final : NoCopy{
public:
    explicit VulkanAllocator(const VulkanContext& context);
    ~VulkanAllocator() = default;


public:
    VkResult allocateBufferMemory(Buffer* buffer, bool enableDeviceAddress = false);
    void freeBufferMemory(Buffer* buffer);
    
    VkResult allocateTextureMemory(Texture* texture);
    void freeTextureMemory(Texture* texture);


private:
    u32 findMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties)const;


private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vulkan device memory for placed resources


class Heap final : public RefCounter<IHeap>, NoCopy{
public:
    Heap(const VulkanContext& context);
    virtual ~Heap()override;
    
    
public:
    [[nodiscard]] virtual const HeapDesc& getDescription()const override{ return desc; }
    virtual Object getNativeHandle(ObjectType objectType)override;
    
    
public:
    HeapDesc desc;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles staging buffer uploads


class UploadManager final : NoCopy{
private:
    struct BufferChunk : public RefCounter<IResource>{
        RefCountPtr<Buffer, BlankDeleter<Buffer>> buffer;
        u64 size;
        u64 allocated;
        u64 version;
    
        BufferChunk(RefCountPtr<Buffer, BlankDeleter<Buffer>> buf, u64 sz)
            : buffer(buf)
            , size(sz)
            , allocated(0)
            , version(0)
        {}
    };
    
    
public:
    UploadManager(Device* pParent, u64 defaultChunkSize, u64 memoryLimit, bool isScratchBuffer);
    ~UploadManager();


public:
    bool suballocateBuffer(u64 size, Buffer** pBuffer, u64* pOffset, void** pCpuVA, u64 currentVersion, u32 alignment = 256);
    void submitChunks(u64 currentVersion, u64 submittedVersion);


private:
    Device* m_device;
    u64 m_defaultChunkSize;
    u64 m_memoryLimit;
    bool m_isScratchBuffer;
    
    List<RefCountPtr<BufferChunk, BlankDeleter<BufferChunk>>> m_chunkPool;
    RefCountPtr<BufferChunk, BlankDeleter<BufferChunk>> m_currentChunk;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Buffer


class Buffer final : public RefCounter<IBuffer>, NoCopy{
public:
    // For volatile buffers - version tracking per frame
    struct VolatileBufferState{
        i32 latestVersion = 0;
        i32 minVersion = 0;
        i32 maxVersion = 0;
        bool initialized = false;
    };
    
    
public:
    Buffer(const VulkanContext& context, VulkanAllocator& allocator);
    virtual ~Buffer()override;
    
    
public:
    [[nodiscard]] virtual const BufferDesc& getDescription()const override{ return desc; }
    [[nodiscard]] virtual GpuVirtualAddress getGpuVirtualAddress()const override{ return deviceAddress; }
    
    
public:
    BufferDesc desc;
    
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    u64 deviceAddress = 0;
    void* mappedMemory = nullptr;
    
    Vector<u64> versionTracking;
    VolatileBufferState volatileState;
    
    bool managed = true; // if true, owns the VkBuffer and memory
    
    
private:
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Texture


class Texture final : public RefCounter<ITexture>, NoCopy{
public:
    Texture(const VulkanContext& context, VulkanAllocator& allocator);
    virtual ~Texture()override;
    

public:
    [[nodiscard]] virtual const TextureDesc& getDescription()const override{ return desc; }
    virtual Object getNativeView(ObjectType objectType, Format::Enum format, TextureSubresourceSet subresources, TextureDimension::Enum dimension, bool isReadOnlyDSV)override;
    
    [[nodiscard]] VkImageView getView(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format, bool isReadOnlyDSV = false);
    
    
private:
    u64 makeViewKey(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format, bool isReadOnlyDSV)const;
    
    
public:
    TextureDesc desc;
    
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageCreateInfo imageInfo{};
    
    HashMap<u64, VkImageView> views;
    
    bool managed = true; // if true, owns the VkImage and memory
    u64 tileByteSize = 0; // for sparse/tiled resources
    
    
private:
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Staging Texture


class StagingTexture final : public RefCounter<IStagingTexture>, NoCopy{
public:
    StagingTexture(const VulkanContext& context, VulkanAllocator& allocator);
    virtual ~StagingTexture()override;
    
    
public:
    [[nodiscard]] virtual const TextureDesc& getDescription()const override{ return desc; }


public:
    TextureDesc desc;
    
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mappedMemory = nullptr;
    CpuAccessMode::Enum cpuAccess;

    
private:
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampler


class Sampler final : public RefCounter<ISampler>, NoCopy{
public:
    Sampler(const VulkanContext& context);
    virtual ~Sampler()override;


public:
    [[nodiscard]] virtual const SamplerDesc& getDescription()const override{ return desc; }


public:
    SamplerDesc desc;
    VkSampler sampler = VK_NULL_HANDLE;

    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader


class Shader final : public RefCounter<IShader>, NoCopy{
public:
    Shader(const VulkanContext& context);
    virtual ~Shader()override;
    
    
public:
    [[nodiscard]] virtual const ShaderDesc& getDescription()const override{ return desc; }
    virtual void getBytecode(const void** ppBytecode, usize* pSize)const override{
        *ppBytecode = bytecode.data();
        *pSize = bytecode.size();
    }


public:
    ShaderDesc desc;
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    
    Vector<u8> bytecode;
    
    // Specialization constants for pipeline creation
    Vector<VkSpecializationMapEntry> specializationEntries;
    Vector<u8> specializationData;


private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader Library


class ShaderLibrary final : public RefCounter<IShaderLibrary>, NoCopy{
public:
    ShaderLibrary(const VulkanContext& context);
    virtual ~ShaderLibrary()override;


public:
    virtual void getBytecode(const void** ppBytecode, usize* pSize)const override;
    virtual ShaderHandle getShader(const Name& entryName, ShaderType::Mask shaderType)override;


public:
    Vector<u8> bytecode;
    HashMap<Name, RefCountPtr<Shader, BlankDeleter<Shader>>> shaders;


private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input Layout


class InputLayout final : public RefCounter<IInputLayout>, NoCopy{
public:
    InputLayout() = default;
    virtual ~InputLayout()override = default;


public:
    [[nodiscard]] virtual const VertexAttributeDesc* getAttributeDescription(u32 index)const override{
        if(index >= attributes.size())
            return nullptr;
        return &attributes[index];
    }

    [[nodiscard]] virtual u32 getNumAttributes()const override{
        return static_cast<u32>(attributes.size());
    }


public:
    Vector<VertexAttributeDesc> attributes;
    Vector<VkVertexInputBindingDescription> bindings;
    Vector<VkVertexInputAttributeDescription> vkAttributes;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Framebuffer


class Framebuffer final : public RefCounter<IFramebuffer>, NoCopy{
public:
    Framebuffer(const VulkanContext& context);
    virtual ~Framebuffer()override;
    
    
public:
    [[nodiscard]] virtual const FramebufferDesc& getDescription()const override{ return desc; }
    [[nodiscard]] virtual const FramebufferInfoEx& getFramebufferInfo()const override{ return framebufferInfo; }
    
    
public:
    FramebufferDesc desc;
    FramebufferInfoEx framebufferInfo;
    
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    
    Vector<RefCountPtr<ITexture, BlankDeleter<ITexture>>> resources;

    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Graphics Pipeline


class GraphicsPipeline final : public RefCounter<IGraphicsPipeline>, NoCopy{
public:
    GraphicsPipeline(const VulkanContext& context);
    virtual ~GraphicsPipeline()override;
    
    
public:
    [[nodiscard]] virtual const GraphicsPipelineDesc& getDescription()const override{ return desc; }
    [[nodiscard]] virtual const FramebufferInfo& getFramebufferInfo()const override{ return framebufferInfo; }
    virtual Object getNativeHandle(ObjectType objectType)override;
    
    
public:
    GraphicsPipelineDesc desc;
    FramebufferInfo framebufferInfo;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compute Pipeline


class ComputePipeline final : public RefCounter<IComputePipeline>, NoCopy{
public:
    ComputePipeline(const VulkanContext& context);
    virtual ~ComputePipeline()override;
    
    
public:
    [[nodiscard]] virtual const ComputePipelineDesc& getDescription()const override{ return desc; }
    virtual Object getNativeHandle(ObjectType objectType)override;
    
    
public:
    ComputePipelineDesc desc;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Meshlet Pipeline


class MeshletPipeline final : public RefCounter<IMeshletPipeline>, NoCopy{
public:
    MeshletPipeline(const VulkanContext& context);
    virtual ~MeshletPipeline()override;
    

public:
    [[nodiscard]] virtual const MeshletPipelineDesc& getDescription()const override{ return desc; }
    [[nodiscard]] virtual const FramebufferInfo& getFramebufferInfo()const override{ return framebufferInfo; }
    virtual Object getNativeHandle(ObjectType objectType)override;
    
    
public:
    MeshletPipelineDesc desc;
    FramebufferInfo framebufferInfo;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing Pipeline


class RayTracingPipeline final : public RefCounter<IRayTracingPipeline>, NoCopy{
    friend Device;
    

public:
    RayTracingPipeline(const VulkanContext& context);
    virtual ~RayTracingPipeline()override;
    
    
public:
    [[nodiscard]] virtual const RayTracingPipelineDesc& getDescription()const override{ return desc; }
    virtual RayTracingShaderTableHandle createShaderTable()override;
    virtual Object getNativeHandle(ObjectType objectType)override;
    
    
public:
    RayTracingPipelineDesc desc;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    Vector<u8> shaderGroupHandles;
    

private:
    const VulkanContext& m_context;
    Device* m_device = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader Table


class ShaderTable final : public RefCounter<IRayTracingShaderTable>, NoCopy{
public:
    ShaderTable(const VulkanContext& context, Device* device);
    virtual ~ShaderTable()override;
    
    
public:
    virtual void setRayGenerationShader(const Name& exportName, IBindingSet* bindings = nullptr)override;
    virtual u32 addMissShader(const Name& exportName, IBindingSet* bindings = nullptr)override;
    virtual u32 addHitGroup(const Name& exportName, IBindingSet* bindings = nullptr)override;
    virtual u32 addCallableShader(const Name& exportName, IBindingSet* bindings = nullptr)override;
    virtual void clearMissShaders()override;
    virtual void clearHitShaders()override;
    virtual void clearCallableShaders()override;
    virtual IRayTracingPipeline* getPipeline()override{ return pipeline; }
    virtual Object getNativeHandle(ObjectType objectType)override;
    
    
private:
    void allocateSBTBuffer(BufferHandle& outBuffer, u64 sbtSize);
    u32 findGroupIndex(const Name& exportName)const;
    
    
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

    
private:
    const VulkanContext& m_context;
    Device* m_device = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Binding Layout


class BindingLayout final : public RefCounter<IBindingLayout>, NoCopy{
public:
    BindingLayout(const VulkanContext& context);
    virtual ~BindingLayout()override;

    
public:
    [[nodiscard]] virtual const BindingLayoutDesc* getDescription()const override{ return isBindless ? nullptr : &desc; }
    [[nodiscard]] virtual const BindlessLayoutDesc* getBindlessDesc()const override{ return isBindless ? &bindlessDesc : nullptr; }
    virtual Object getNativeHandle(ObjectType objectType)override{ return Object(pipelineLayout); }

    
public:
    BindingLayoutDesc desc;
    BindlessLayoutDesc bindlessDesc;
    bool isBindless = false;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    Vector<VkDescriptorSetLayout> descriptorSetLayouts;

    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Descriptor Table


class DescriptorTable final : public RefCounter<IDescriptorTable>, NoCopy{
public:
    DescriptorTable(const VulkanContext& context);
    virtual ~DescriptorTable()override;
    
    
public:
    [[nodiscard]] virtual u32 getCapacity()const override{ return static_cast<u32>(descriptorSets.size()); }
    [[nodiscard]] virtual u32 getFirstDescriptorIndexInHeap()const override{ return 0; }
    
    [[nodiscard]] virtual const BindingSetDesc* getDescription()const override{ return nullptr; }
    [[nodiscard]] virtual IBindingLayout* getLayout()const override{ return layout.get(); }
    
    
public:
    RefCountPtr<BindingLayout, BlankDeleter<BindingLayout>> layout;
    Vector<VkDescriptorSet> descriptorSets;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Binding Set


class BindingSet final : public RefCounter<IBindingSet>, NoCopy{
public:
    BindingSet(const VulkanContext& context);
    virtual ~BindingSet()override;
    
    
public:
    [[nodiscard]] virtual const BindingSetDesc* getDescription()const override{ return &desc; }
    [[nodiscard]] virtual IBindingLayout* getLayout()const override{ return layout.get(); }
    
    
public:
    BindingSetDesc desc;
    RefCountPtr<BindingLayout, BlankDeleter<BindingLayout>> layout;
    RefCountPtr<DescriptorTable, BlankDeleter<DescriptorTable>> descriptorTable;
    Vector<VkDescriptorSet> descriptorSets;

    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Acceleration Structure


class AccelStruct final : public RefCounter<IRayTracingAccelStruct>, NoCopy{
public:
    AccelStruct(const VulkanContext& context);
    virtual ~AccelStruct()override;
    
    
public:
    [[nodiscard]] virtual const RayTracingAccelStructDesc& getDescription()const override{ return desc; }
    [[nodiscard]] virtual bool isCompacted()const override{ return compacted; }
    [[nodiscard]] virtual u64 getDeviceAddress()const override{ return deviceAddress; }
    virtual Object getNativeHandle(ObjectType objectType)override;
    
    
public:
    RayTracingAccelStructDesc desc;
    VkAccelerationStructureKHR accelStruct = VK_NULL_HANDLE;
    RefCountPtr<IBuffer, BlankDeleter<IBuffer>> buffer;
    u64 deviceAddress = 0;
    bool compacted = false;

    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Opacity Micromap


class OpacityMicromap final : public RefCounter<IRayTracingOpacityMicromap>, NoCopy{
public:
    OpacityMicromap(const VulkanContext& context);
    virtual ~OpacityMicromap()override;
    
    
public:
    [[nodiscard]] virtual const RayTracingOpacityMicromapDesc& getDescription()const override{ return desc; }
    [[nodiscard]] virtual bool isCompacted()const override{ return compacted; }
    [[nodiscard]] virtual u64 getDeviceAddress()const override{ return deviceAddress; }
    
    
public:
    RayTracingOpacityMicromapDesc desc;
    RefCountPtr<IBuffer, BlankDeleter<IBuffer>> dataBuffer;
    VkMicromapEXT micromap = VK_NULL_HANDLE;
    u64 deviceAddress = 0;
    bool compacted = false;
    
    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// State Tracker


class StateTracker final : NoCopy{
public:
    StateTracker();
    ~StateTracker();
    
    
public:
    void reset();
    void setPermanentTextureState(ITexture* texture, ResourceStates::Mask state);
    void setPermanentBufferState(IBuffer* buffer, ResourceStates::Mask state);
    
    [[nodiscard]] bool isPermanentTexture(ITexture* texture)const;
    [[nodiscard]] bool isPermanentBuffer(IBuffer* buffer)const;
    [[nodiscard]] ResourceStates::Mask getTextureState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel)const;
    [[nodiscard]] ResourceStates::Mask getBufferState(IBuffer* buffer)const;
    
    void beginTrackingTexture(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Mask state);
    void beginTrackingBuffer(IBuffer* buffer, ResourceStates::Mask state);
    
    void setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers);
    void setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers);
    
    
public:
    GraphicsState graphicsState;
    ComputeState computeState;
    MeshletState meshletState;
    RayTracingState rayTracingState;
    
    
private:
    HashMap<ITexture*, ResourceStates::Mask> m_permanentTextureStates;
    HashMap<IBuffer*, ResourceStates::Mask> m_permanentBufferStates;
    HashMap<ITexture*, ResourceStates::Mask> m_textureStates;
    HashMap<IBuffer*, ResourceStates::Mask> m_bufferStates;
    HashMap<ITexture*, bool> m_textureUavBarriers;
    HashMap<IBuffer*, bool> m_bufferUavBarriers;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command List


class CommandList final : public RefCounter<ICommandList>, NoCopy{
public:
    CommandList(Device* device, const CommandListParameters& params);
    virtual ~CommandList()override;
    
    
public:
    virtual void open()override;
    virtual void close()override;
    virtual void clearState()override;
    
    virtual void setResourceStatesForBindingSet(IBindingSet* bindingSet)override;
    virtual void setEnableAutomaticBarriers(bool enable)override;
    virtual void commitBarriers()override;
    
    virtual void setTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits)override;
    virtual void setBufferState(IBuffer* buffer, ResourceStates::Mask stateBits)override;
    virtual void setAccelStructState(IRayTracingAccelStruct* as, ResourceStates::Mask stateBits)override;
    
    virtual void setPermanentTextureState(ITexture* texture, ResourceStates::Mask stateBits)override;
    virtual void setPermanentBufferState(IBuffer* buffer, ResourceStates::Mask stateBits)override;
    
    virtual void clearTextureFloat(ITexture* texture, TextureSubresourceSet subresources, const Color& clearColor)override;
    virtual void clearDepthStencilTexture(ITexture* texture, TextureSubresourceSet subresources, bool clearDepth, f32 depth, bool clearStencil, u8 stencil)override;
    virtual void clearTextureUInt(ITexture* texture, TextureSubresourceSet subresources, u32 clearColor)override;
    
    virtual void copyTexture(ITexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice)override;
    virtual void copyTexture(IStagingTexture* dest, const TextureSlice& destSlice, ITexture* src, const TextureSlice& srcSlice)override;
    virtual void copyTexture(ITexture* dest, const TextureSlice& destSlice, IStagingTexture* src, const TextureSlice& srcSlice)override;
    virtual void writeBuffer(IBuffer* buffer, const void* data, usize dataSize, u64 destOffsetBytes = 0)override;
    virtual void clearBufferUInt(IBuffer* buffer, u32 clearValue)override;
    virtual void copyBuffer(IBuffer* dest, u64 destOffsetBytes, IBuffer* src, u64 srcOffsetBytes, u64 dataSizeBytes)override;
    virtual void writeTexture(ITexture* dest, u32 arraySlice, u32 mipLevel, const void* data, usize rowPitch, usize depthPitch = 0)override;
    virtual void resolveTexture(ITexture* dest, const TextureSubresourceSet& dstSubresources, ITexture* src, const TextureSubresourceSet& srcSubresources)override;
    
    virtual void clearSamplerFeedbackTexture(ISamplerFeedbackTexture* texture)override;
    virtual void decodeSamplerFeedbackTexture(IBuffer* buffer, ISamplerFeedbackTexture* texture, Format::Enum format)override;
    virtual void setSamplerFeedbackTextureState(ISamplerFeedbackTexture* texture, ResourceStates::Mask stateBits)override;
    
    virtual void setPushConstants(const void* data, usize byteSize)override;
    
    virtual void setGraphicsState(const GraphicsState& state)override;
    virtual void draw(const DrawArguments& args)override;
    virtual void drawIndexed(const DrawArguments& args)override;
    virtual void drawIndirect(u32 offsetBytes, u32 drawCount = 1)override;
    virtual void drawIndexedIndirect(u32 offsetBytes, u32 drawCount = 1)override;
    
    virtual void setComputeState(const ComputeState& state)override;
    virtual void dispatch(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1)override;
    virtual void dispatchIndirect(u32 offsetBytes)override;
    
    virtual void setMeshletState(const MeshletState& state)override;
    virtual void dispatchMesh(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1)override;
    
    virtual void setRayTracingState(const RayTracingState& state)override;
    virtual void dispatchRays(const RayTracingDispatchRaysArguments& args)override;
    virtual void buildBottomLevelAccelStruct(IRayTracingAccelStruct* as, const RayTracingGeometryDesc* pGeometries, usize numGeometries, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None)override;
    virtual void compactBottomLevelAccelStructs()override;
    virtual void buildTopLevelAccelStruct(IRayTracingAccelStruct* as, const RayTracingInstanceDesc* pInstances, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None)override;
    virtual void buildOpacityMicromap(IRayTracingOpacityMicromap* omm, const RayTracingOpacityMicromapDesc& desc)override;
    virtual void buildTopLevelAccelStructFromBuffer(IRayTracingAccelStruct* as, IBuffer* instanceBuffer, u64 instanceBufferOffset, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None)override;
    virtual void executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& desc)override;
    virtual void convertCoopVecMatrices(CooperativeVectorConvertMatrixLayoutDesc const* convertDescs, usize numDescs)override;
    
    virtual void beginTimerQuery(ITimerQuery* query)override;
    virtual void endTimerQuery(ITimerQuery* query)override;
    virtual void beginMarker(const Name& name)override;
    virtual void endMarker()override;
    
    virtual void setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers)override;
    virtual void setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers)override;
    virtual void beginTrackingTextureState(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits)override;
    virtual void beginTrackingBufferState(IBuffer* buffer, ResourceStates::Mask stateBits)override;
    virtual ResourceStates::Mask getTextureSubresourceState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel)override;
    virtual ResourceStates::Mask getBufferState(IBuffer* buffer)override;
    
    virtual IDevice* getDevice()override;
    virtual const CommandListParameters& getDescription()override;
    
public:
    void copyTextureToBuffer(IBuffer* dest, u64 destOffsetBytes, u32 destRowPitch, ITexture* src, const TextureSlice& srcSlice);
    void setEventQuery(IEventQuery* query, CommandQueue::Enum waitQueue);
    void resetEventQuery(IEventQuery* query);
    void waitEventQuery(IEventQuery* query);
    [[nodiscard]] TrackedCommandBufferPtr getCurrentCmdBuf()const{ return currentCmdBuf; }
    
    
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
    
    // Deferred barrier batching
    Vector<VkImageMemoryBarrier2> m_pendingImageBarriers;
    Vector<VkBufferMemoryBarrier2> m_pendingBufferBarriers;
    
    // Pending BLAS compaction requests
    Vector<RefCountPtr<AccelStruct, BlankDeleter<AccelStruct>>> m_pendingCompactions;


private:
    void beginRenderPass(IFramebuffer* framebuffer, const RenderPassParameters& params);
    void endRenderPass();


private:
    Device* m_device;
    const VulkanContext* m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Event Query


class EventQuery final : public RefCounter<IEventQuery>, NoCopy{
public:
    EventQuery(const VulkanContext& context);
    virtual ~EventQuery()override;
    
    
public:
    VkFence fence = VK_NULL_HANDLE;
    bool started = false;

    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timer Query


class TimerQuery final : public RefCounter<ITimerQuery>, NoCopy{
public:
    TimerQuery(const VulkanContext& context);
    virtual ~TimerQuery()override;
    
    
public:
    VkQueryPool queryPool = VK_NULL_HANDLE;
    bool started = false;
    bool resolved = false;

    
private:
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device Implementation


class Device final : public RefCounter<IDevice>, NoCopy{
public:
    explicit Device(const DeviceDesc& desc);
    virtual ~Device()override;
    

public:
    [[nodiscard]] virtual HeapHandle createHeap(const HeapDesc& d)override;
    [[nodiscard]] virtual TextureHandle createTexture(const TextureDesc& d)override;
    [[nodiscard]] virtual MemoryRequirements getTextureMemoryRequirements(ITexture* texture)override;
    virtual bool bindTextureMemory(ITexture* texture, IHeap* heap, u64 offset)override;
    [[nodiscard]] virtual TextureHandle createHandleForNativeTexture(ObjectType objectType, Object texture, const TextureDesc& desc)override;
    [[nodiscard]] virtual StagingTextureHandle createStagingTexture(const TextureDesc& d, CpuAccessMode::Enum cpuAccess)override;
    virtual void* mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode::Enum cpuAccess, usize* outRowPitch)override;
    virtual void unmapStagingTexture(IStagingTexture* tex)override;
    virtual void getTextureTiling(ITexture* texture, u32* numTiles, PackedMipDesc* desc, TileShape* tileShape, u32* subresourceTilingsNum, SubresourceTiling* subresourceTilings)override;
    virtual void updateTextureTileMappings(ITexture* texture, const TextureTilesMapping* tileMappings, u32 numTileMappings, CommandQueue::Enum executionQueue = CommandQueue::Graphics)override;
    [[nodiscard]] virtual SamplerFeedbackTextureHandle createSamplerFeedbackTexture(ITexture* pairedTexture, const SamplerFeedbackTextureDesc& desc)override;
    [[nodiscard]] virtual SamplerFeedbackTextureHandle createSamplerFeedbackForNativeTexture(ObjectType objectType, Object texture, ITexture* pairedTexture)override;
    [[nodiscard]] virtual BufferHandle createBuffer(const BufferDesc& d)override;
    virtual void* mapBuffer(IBuffer* buffer, CpuAccessMode::Enum cpuAccess)override;
    virtual void unmapBuffer(IBuffer* buffer)override;
    [[nodiscard]] virtual MemoryRequirements getBufferMemoryRequirements(IBuffer* buffer)override;
    virtual bool bindBufferMemory(IBuffer* buffer, IHeap* heap, u64 offset)override;
    [[nodiscard]] virtual BufferHandle createHandleForNativeBuffer(ObjectType objectType, Object buffer, const BufferDesc& desc)override;
    [[nodiscard]] virtual ShaderHandle createShader(const ShaderDesc& d, const void* binary, usize binarySize)override;
    [[nodiscard]] virtual ShaderHandle createShaderSpecialization(IShader* baseShader, const ShaderSpecialization* constants, u32 numConstants)override;
    [[nodiscard]] virtual ShaderLibraryHandle createShaderLibrary(const void* binary, usize binarySize)override;
    [[nodiscard]] virtual SamplerHandle createSampler(const SamplerDesc& d)override;
    [[nodiscard]] virtual InputLayoutHandle createInputLayout(const VertexAttributeDesc* d, u32 attributeCount, IShader* vertexShader)override;
    [[nodiscard]] virtual EventQueryHandle createEventQuery()override;
    virtual void setEventQuery(IEventQuery* query, CommandQueue::Enum queue)override;
    virtual bool pollEventQuery(IEventQuery* query)override;
    virtual void waitEventQuery(IEventQuery* query)override;
    virtual void resetEventQuery(IEventQuery* query)override;
    [[nodiscard]] virtual TimerQueryHandle createTimerQuery()override;
    virtual bool pollTimerQuery(ITimerQuery* query)override;
    virtual f32 getTimerQueryTime(ITimerQuery* query)override;
    virtual void resetTimerQuery(ITimerQuery* query)override;
    [[nodiscard]] virtual GraphicsAPI::Enum getGraphicsAPI()override{ return GraphicsAPI::VULKAN; }
    [[nodiscard]] virtual FramebufferHandle createFramebuffer(const FramebufferDesc& desc)override;
    [[nodiscard]] virtual GraphicsPipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc, FramebufferInfo const& fbinfo)override;
    [[nodiscard]] virtual ComputePipelineHandle createComputePipeline(const ComputePipelineDesc& desc)override;
    [[nodiscard]] virtual MeshletPipelineHandle createMeshletPipeline(const MeshletPipelineDesc& desc, FramebufferInfo const& fbinfo)override;
    [[nodiscard]] virtual RayTracingPipelineHandle createRayTracingPipeline(const RayTracingPipelineDesc& desc)override;
    [[nodiscard]] virtual BindingLayoutHandle createBindingLayout(const BindingLayoutDesc& desc)override;
    [[nodiscard]] virtual BindingLayoutHandle createBindlessLayout(const BindlessLayoutDesc& desc)override;
    [[nodiscard]] virtual BindingSetHandle createBindingSet(const BindingSetDesc& desc, IBindingLayout* layout)override;
    [[nodiscard]] virtual DescriptorTableHandle createDescriptorTable(IBindingLayout* layout)override;
    virtual void resizeDescriptorTable(IDescriptorTable* descriptorTable, u32 newSize, bool keepContents = true)override;
    virtual bool writeDescriptorTable(IDescriptorTable* descriptorTable, const BindingSetItem& item)override;
    [[nodiscard]] virtual RayTracingOpacityMicromapHandle createOpacityMicromap(const RayTracingOpacityMicromapDesc& desc)override;
    [[nodiscard]] virtual RayTracingAccelStructHandle createAccelStruct(const RayTracingAccelStructDesc& desc)override;
    [[nodiscard]] virtual MemoryRequirements getAccelStructMemoryRequirements(IRayTracingAccelStruct* as)override;
    [[nodiscard]] virtual RayTracingClusterOperationSizeInfo getClusterOperationSizeInfo(const RayTracingClusterOperationParams& params)override;
    virtual bool bindAccelStructMemory(IRayTracingAccelStruct* as, IHeap* heap, u64 offset)override;
    [[nodiscard]] virtual CommandListHandle createCommandList(const CommandListParameters& params = CommandListParameters())override;
    virtual u64 executeCommandLists(ICommandList* const* pCommandLists, usize numCommandLists, CommandQueue::Enum executionQueue = CommandQueue::Graphics)override;
    virtual void queueWaitForCommandList(CommandQueue::Enum waitQueue, CommandQueue::Enum executionQueue, u64 instance)override;
    virtual bool waitForIdle()override;
    virtual void runGarbageCollection()override;
    virtual bool queryFeatureSupport(Feature::Enum feature, void* pInfo = nullptr, usize infoSize = 0)override;
    [[nodiscard]] virtual FormatSupport::Mask queryFormatSupport(Format::Enum format)override;
    [[nodiscard]] virtual CooperativeVectorDeviceFeatures queryCoopVecFeatures()override;
    virtual usize getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, int rows, int columns)override;
    [[nodiscard]] virtual Object getNativeQueue(ObjectType objectType, CommandQueue::Enum queue)override;
    virtual bool isAftermathEnabled()override{ return false; }
    [[nodiscard]] virtual AftermathCrashDumpHelper& getAftermathCrashDumpHelper()override;
    
    [[nodiscard]] virtual VkSemaphore getQueueSemaphore(CommandQueue::Enum queue)override;
    virtual void queueWaitForSemaphore(CommandQueue::Enum waitQueue, VkSemaphore semaphore, u64 value)override;
    virtual void queueSignalSemaphore(CommandQueue::Enum executionQueue, VkSemaphore semaphore, u64 value)override;
    [[nodiscard]] virtual u64 queueGetCompletedInstance(CommandQueue::Enum queue)override;
    
public:
    [[nodiscard]] const VulkanContext& getContext()const{ return m_context; }
    [[nodiscard]] Queue* getQueue(CommandQueue::Enum queueType)const;
    [[nodiscard]] VulkanAllocator& getAllocator(){ return m_allocator; }
    [[nodiscard]] UploadManager* getUploadManager(){ return m_uploadManager.get(); }
    [[nodiscard]] UploadManager* getScratchManager(){ return m_scratchManager.get(); }
    

private:
    VulkanContext m_context;
    VulkanAllocator m_allocator;
    UniquePtr<Queue> m_queues[static_cast<u32>(CommandQueue::kCount)];
    
    UniquePtr<UploadManager> m_uploadManager;
    UniquePtr<UploadManager> m_scratchManager;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

