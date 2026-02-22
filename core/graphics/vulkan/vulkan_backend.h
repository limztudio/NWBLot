// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vulkan.h"

#include <core/alloc/custom.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{
    extern constexpr VkAccessFlags2 GetVkAccessFlags(ResourceStates::Mask state);
    extern constexpr VkPipelineStageFlags2 GetVkPipelineStageFlags(ResourceStates::Mask state);
    extern constexpr VkImageLayout GetVkImageLayout(ResourceStates::Mask state);
    extern constexpr VkFormat ConvertFormat(Format::Enum format);
    extern constexpr VkSampleCountFlagBits GetSampleCountFlagBits(u32 sampleCount);
    extern VkDeviceAddress GetBufferDeviceAddress(IBuffer* _buffer, u64 offset = 0);
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

    Alloc::CustomArena* objectArena = nullptr;
    GraphicsAllocator* allocator = nullptr;

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
        bool EXT_debug_marker = false;
        bool KHR_swapchain = false;
        bool KHR_dynamic_rendering = false;
        bool EXT_opacity_micromap = false;
        bool NV_cooperative_vector = false;
        bool NV_cluster_acceleration_structure = false;
        bool NV_device_diagnostic_checkpoints = false;
        bool EXT_mesh_shader = false;
        bool KHR_fragment_shading_rate = false;
        bool NV_ray_tracing_invocation_reorder = false;
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
    friend class CommandList;
    friend class Queue;


public:
    TrackedCommandBuffer(const VulkanContext& context, CommandQueue::Enum, u32 queueFamilyIndex);
    virtual ~TrackedCommandBuffer()override;


private:
    VkCommandBuffer m_cmdBuf = VK_NULL_HANDLE;
    VkCommandPool m_cmdPool = VK_NULL_HANDLE;

    Vector<RefCountPtr<IResource, ArenaRefDeleter<IResource>>, Alloc::CustomAllocator<RefCountPtr<IResource, ArenaRefDeleter<IResource>>>> m_referencedResources;
    Vector<RefCountPtr<IBuffer, ArenaRefDeleter<IBuffer>>, Alloc::CustomAllocator<RefCountPtr<IBuffer, ArenaRefDeleter<IBuffer>>>> m_referencedStagingBuffers;
    Vector<VkAccelerationStructureKHR, Alloc::CustomAllocator<VkAccelerationStructureKHR>> m_referencedAccelStructHandles;

    VkFence m_signalFence = VK_NULL_HANDLE;

    u64 m_recordingID = 0;
    u64 m_submissionID = 0;

    const VulkanContext& m_context;
};
typedef RefCountPtr<TrackedCommandBuffer, ArenaRefDeleter<TrackedCommandBuffer>> TrackedCommandBufferPtr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command queue wrapper with timeline semaphore tracking


class Queue final : NoCopy{
    friend class Device;


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
    void updateLastFinishedID();

    [[nodiscard]] bool pollCommandList(u64 commandListID);
    [[nodiscard]] bool waitCommandList(u64 commandListID, u64 timeout);
    void waitForIdle();

    [[nodiscard]] VkQueue getVkQueue()const{ return m_queue; }
    [[nodiscard]] u32 getQueueFamilyIndex()const{ return m_queueFamilyIndex; }
    [[nodiscard]] CommandQueue::Enum getQueueID()const{ return m_queueID; }
    [[nodiscard]] u64 getLastSubmittedID()const{ return m_lastSubmittedID; }
    [[nodiscard]] u64 getLastFinishedID()const{ return m_lastFinishedID; }
    [[nodiscard]] VkSemaphore getTrackingSemaphore()const{ return m_trackingSemaphore; }


private:
    VkSemaphore m_trackingSemaphore = VK_NULL_HANDLE;

    const VulkanContext& m_context;
    VkQueue m_queue;
    CommandQueue::Enum m_queueID;
    u32 m_queueFamilyIndex;

    Mutex m_mutex;
    Vector<VkSemaphore, Alloc::CustomAllocator<VkSemaphore>> m_waitSemaphores;
    Vector<u64, Alloc::CustomAllocator<u64>> m_waitSemaphoreValues;
    Vector<VkSemaphore, Alloc::CustomAllocator<VkSemaphore>> m_signalSemaphores;
    Vector<u64, Alloc::CustomAllocator<u64>> m_signalSemaphoreValues;

    u64 m_lastRecordingID = 0;
    u64 m_lastSubmittedID = 0;
    u64 m_lastFinishedID = 0;

    List<TrackedCommandBufferPtr, Alloc::CustomAllocator<TrackedCommandBufferPtr>> m_commandBuffersInFlight;
    List<TrackedCommandBufferPtr, Alloc::CustomAllocator<TrackedCommandBufferPtr>> m_commandBuffersPool;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles memory allocation


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
// Device memory for placed resources


class Heap final : public RefCounter<IHeap>, NoCopy{
    friend class Device;
    friend class VulkanAllocator;
    friend class Queue;


public:
    Heap(const VulkanContext& context);
    virtual ~Heap()override;


public:
    [[nodiscard]] virtual const HeapDesc& getDescription()const override{ return m_desc; }
    virtual Object getNativeHandle(ObjectType objectType)override;


private:
    HeapDesc m_desc;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles staging buffer uploads


class UploadManager final : NoCopy{
private:
    struct BufferChunk final : public RefCounter<IResource>{
        BufferHandle buffer;
        u64 size;
        u64 allocated;
        u64 version;

        BufferChunk(BufferHandle buf, u64 sz)
            : buffer(Move(buf))
            , size(sz)
            , allocated(0)
            , version(0)
        {}
    };


public:
    UploadManager(Device& pParent, u64 defaultChunkSize, u64 memoryLimit, bool isScratchBuffer);
    ~UploadManager();


public:
    bool suballocateBuffer(u64 size, Buffer** pBuffer, u64* pOffset, void** pCpuVA, u64 currentVersion, u32 alignment = 256);
    void submitChunks(u64, u64 submittedVersion);


private:
    Device& m_device;
    u64 m_defaultChunkSize;
    u64 m_memoryLimit;
    bool m_isScratchBuffer;

    List<RefCountPtr<BufferChunk>, Alloc::CustomAllocator<RefCountPtr<BufferChunk>>> m_chunkPool;
    RefCountPtr<BufferChunk> m_currentChunk;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Buffer


class Buffer final : public RefCounter<IBuffer>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class VulkanAllocator;
    friend class UploadManager;
    friend class ShaderTable;

    friend VkDeviceAddress __hidden_vulkan::GetBufferDeviceAddress(IBuffer* _buffer, u64 offset);


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
    [[nodiscard]] virtual const BufferDesc& getDescription()const override{ return m_desc; }
    [[nodiscard]] virtual GpuVirtualAddress getGpuVirtualAddress()const override{ return m_deviceAddress; }


private:
    BufferDesc m_desc;

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    u64 m_deviceAddress = 0;
    void* m_mappedMemory = nullptr;

    Vector<u64, Alloc::CustomAllocator<u64>> m_versionTracking;
    VolatileBufferState m_volatileState;

    bool m_managed = true; // if true, owns the VkBuffer and memory

    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Texture


class Texture final : public RefCounter<ITexture>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class VulkanAllocator;
    friend class Queue;


public:
    Texture(const VulkanContext& context, VulkanAllocator& allocator);
    virtual ~Texture()override;


public:
    [[nodiscard]] virtual const TextureDesc& getDescription()const override{ return m_desc; }
    virtual Object getNativeView(ObjectType objectType, Format::Enum format, TextureSubresourceSet subresources, TextureDimension::Enum dimension, bool isReadOnlyDSV)override;

    [[nodiscard]] VkImageView getView(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format, bool isReadOnlyDSV = false);
 

private:
    u64 makeViewKey(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format, bool isReadOnlyDSV)const;


private:
    TextureDesc m_desc;

    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageCreateInfo m_imageInfo{};

    HashMap<u64, VkImageView, Hasher<u64>, EqualTo<u64>, Alloc::CustomAllocator<Pair<const u64, VkImageView>>> m_views;

    bool m_managed = true; // if true, owns the VkImage and memory
    u64 m_tileByteSize = 0; // for sparse/tiled resources

    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Staging Texture


class StagingTexture final : public RefCounter<IStagingTexture>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    StagingTexture(const VulkanContext& context, VulkanAllocator& allocator);
    virtual ~StagingTexture()override;


public:
    [[nodiscard]] virtual const TextureDesc& getDescription()const override{ return m_desc; }


private:
    TextureDesc m_desc;

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    void* m_mappedMemory = nullptr;
    CpuAccessMode::Enum m_cpuAccess{};

    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampler


class Sampler final : public RefCounter<ISampler>, NoCopy{
    friend class Device;


public:
    Sampler(const VulkanContext& context);
    virtual ~Sampler()override;


public:
    [[nodiscard]] virtual const SamplerDesc& getDescription()const override{ return m_desc; }


private:
    SamplerDesc m_desc;
    VkSampler m_sampler = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader


class Shader final : public RefCounter<IShader>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class ShaderLibrary;


public:
    Shader(const VulkanContext& context);
    virtual ~Shader()override;


public:
    [[nodiscard]] virtual const ShaderDesc& getDescription()const override{ return m_desc; }
    virtual void getBytecode(const void** ppBytecode, usize* pSize)const override{
        *ppBytecode = m_bytecode.data();
        *pSize = m_bytecode.size();
    }


private:
    ShaderDesc m_desc;
    VkShaderModule m_shaderModule = VK_NULL_HANDLE;

    Vector<u8, Alloc::CustomAllocator<u8>> m_bytecode;

    Vector<VkSpecializationMapEntry, Alloc::CustomAllocator<VkSpecializationMapEntry>> m_specializationEntries;
    Vector<u8, Alloc::CustomAllocator<u8>> m_specializationData;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader Library


class ShaderLibrary final : public RefCounter<IShaderLibrary>, NoCopy{
    friend class Device;


public:
    ShaderLibrary(const VulkanContext& context);
    virtual ~ShaderLibrary()override;


public:
    virtual void getBytecode(const void** ppBytecode, usize* pSize)const override;
    virtual ShaderHandle getShader(const Name& entryName, ShaderType::Mask shaderType)override;


private:
    Vector<u8, Alloc::CustomAllocator<u8>> m_bytecode;
    HashMap<Name, RefCountPtr<Shader, ArenaRefDeleter<Shader>>, Hasher<Name>, EqualTo<Name>, Alloc::CustomAllocator<Pair<const Name, RefCountPtr<Shader, ArenaRefDeleter<Shader>>>>> m_shaders;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input Layout


class InputLayout final : public RefCounter<IInputLayout>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    InputLayout(const VulkanContext& context);
    virtual ~InputLayout()override = default;


public:
    [[nodiscard]] virtual const VertexAttributeDesc* getAttributeDescription(u32 index)const override{
        if(index >= m_attributes.size())
            return nullptr;
        return &m_attributes[index];
    }

    [[nodiscard]] virtual u32 getNumAttributes()const override{ return static_cast<u32>(m_attributes.size()); }


private:
    Vector<VertexAttributeDesc, Alloc::CustomAllocator<VertexAttributeDesc>> m_attributes;
    Vector<VkVertexInputBindingDescription, Alloc::CustomAllocator<VkVertexInputBindingDescription>> m_bindings;
    Vector<VkVertexInputAttributeDescription, Alloc::CustomAllocator<VkVertexInputAttributeDescription>> m_vkAttributes;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Framebuffer


class Framebuffer final : public RefCounter<IFramebuffer>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    Framebuffer(const VulkanContext& context);
    virtual ~Framebuffer()override;


public:
    [[nodiscard]] virtual const FramebufferDesc& getDescription()const override{ return m_desc; }
    [[nodiscard]] virtual const FramebufferInfoEx& getFramebufferInfo()const override{ return m_framebufferInfo; }


private:
    FramebufferDesc m_desc;
    FramebufferInfoEx m_framebufferInfo;

    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;

    Vector<RefCountPtr<ITexture, ArenaRefDeleter<ITexture>>, Alloc::CustomAllocator<RefCountPtr<ITexture, ArenaRefDeleter<ITexture>>>> m_resources;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Graphics Pipeline


class GraphicsPipeline final : public RefCounter<IGraphicsPipeline>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    GraphicsPipeline(const VulkanContext& context);
    virtual ~GraphicsPipeline()override;


public:
    [[nodiscard]] virtual const GraphicsPipelineDesc& getDescription()const override{ return m_desc; }
    [[nodiscard]] virtual const FramebufferInfo& getFramebufferInfo()const override{ return m_framebufferInfo; }
    virtual Object getNativeHandle(ObjectType objectType)override;


private:
    GraphicsPipelineDesc m_desc;
    FramebufferInfo m_framebufferInfo;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compute Pipeline


class ComputePipeline final : public RefCounter<IComputePipeline>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    ComputePipeline(const VulkanContext& context);
    virtual ~ComputePipeline()override;


public:
    [[nodiscard]] virtual const ComputePipelineDesc& getDescription()const override{ return m_desc; }
    virtual Object getNativeHandle(ObjectType objectType)override;


private:
    ComputePipelineDesc m_desc;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Meshlet Pipeline


class MeshletPipeline final : public RefCounter<IMeshletPipeline>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    MeshletPipeline(const VulkanContext& context);
    virtual ~MeshletPipeline()override;


public:
    [[nodiscard]] virtual const MeshletPipelineDesc& getDescription()const override{ return m_desc; }
    [[nodiscard]] virtual const FramebufferInfo& getFramebufferInfo()const override{ return m_framebufferInfo; }
    virtual Object getNativeHandle(ObjectType objectType)override;


private:
    MeshletPipelineDesc m_desc;
    FramebufferInfo m_framebufferInfo;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing Pipeline


class RayTracingPipeline final : public RefCounter<IRayTracingPipeline>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class ShaderTable;


public:
    RayTracingPipeline(const VulkanContext& context);
    virtual ~RayTracingPipeline()override;


public:
    [[nodiscard]] virtual const RayTracingPipelineDesc& getDescription()const override{ return m_desc; }
    virtual RayTracingShaderTableHandle createShaderTable()override;
    virtual Object getNativeHandle(ObjectType objectType)override;


private:
    RayTracingPipelineDesc m_desc;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    Vector<u8, Alloc::CustomAllocator<u8>> m_shaderGroupHandles;

    const VulkanContext& m_context;
    Device* m_device = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader Table


class ShaderTable final : public RefCounter<IRayTracingShaderTable>, NoCopy{
    friend class CommandList;
    friend class RayTracingPipeline;


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
    virtual IRayTracingPipeline* getPipeline()override{ return m_pipeline; }
    virtual Object getNativeHandle(ObjectType objectType)override;


private:
    void allocateSBTBuffer(BufferHandle& outBuffer, u64 sbtSize);
    u32 findGroupIndex(const Name& exportName)const;


private:
    RayTracingPipeline* m_pipeline = nullptr;

    BufferHandle m_raygenBuffer;
    u64 m_raygenOffset = 0;

    BufferHandle m_missBuffer;
    u64 m_missOffset = 0;
    u32 m_missCount = 0;

    BufferHandle m_hitBuffer;
    u64 m_hitOffset = 0;
    u32 m_hitCount = 0;

    BufferHandle m_callableBuffer;
    u64 m_callableOffset = 0;
    u32 m_callableCount = 0;

    const VulkanContext& m_context;
    Device* m_device = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Binding Layout


class BindingLayout final : public RefCounter<IBindingLayout>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    BindingLayout(const VulkanContext& context);
    virtual ~BindingLayout()override;


public:
    [[nodiscard]] virtual const BindingLayoutDesc* getDescription()const override{ return m_isBindless ? nullptr : &m_desc; }
    [[nodiscard]] virtual const BindlessLayoutDesc* getBindlessDesc()const override{ return m_isBindless ? &m_bindlessDesc : nullptr; }
    virtual Object getNativeHandle(ObjectType)override{ return Object(m_pipelineLayout); }


private:
    BindingLayoutDesc m_desc;
    BindlessLayoutDesc m_bindlessDesc;
    bool m_isBindless = false;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    Vector<VkDescriptorSetLayout, Alloc::CustomAllocator<VkDescriptorSetLayout>> m_descriptorSetLayouts;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Descriptor Table


class DescriptorTable final : public RefCounter<IDescriptorTable>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    DescriptorTable(const VulkanContext& context);
    virtual ~DescriptorTable()override;


public:
    [[nodiscard]] virtual u32 getCapacity()const override{ return static_cast<u32>(m_descriptorSets.size()); }
    [[nodiscard]] virtual u32 getFirstDescriptorIndexInHeap()const override{ return 0; }

    [[nodiscard]] virtual const BindingSetDesc* getDescription()const override{ return nullptr; }
    [[nodiscard]] virtual IBindingLayout* getLayout()const override{ return m_layout.get(); }


private:
    RefCountPtr<BindingLayout, ArenaRefDeleter<BindingLayout>> m_layout;
    Vector<VkDescriptorSet, Alloc::CustomAllocator<VkDescriptorSet>> m_descriptorSets;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Binding Set


class BindingSet final : public RefCounter<IBindingSet>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    BindingSet(const VulkanContext& context);
    virtual ~BindingSet()override;


public:
    [[nodiscard]] virtual const BindingSetDesc* getDescription()const override{ return &m_desc; }
    [[nodiscard]] virtual IBindingLayout* getLayout()const override{ return m_layout.get(); }


private:
    BindingSetDesc m_desc;
    RefCountPtr<BindingLayout, ArenaRefDeleter<BindingLayout>> m_layout;
    RefCountPtr<DescriptorTable, ArenaRefDeleter<DescriptorTable>> m_descriptorTable;
    Vector<VkDescriptorSet, Alloc::CustomAllocator<VkDescriptorSet>> m_descriptorSets;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Acceleration Structure


class AccelStruct final : public RefCounter<IRayTracingAccelStruct>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    AccelStruct(const VulkanContext& context);
    virtual ~AccelStruct()override;


public:
    [[nodiscard]] virtual const RayTracingAccelStructDesc& getDescription()const override{ return m_desc; }
    [[nodiscard]] virtual bool isCompacted()const override{ return m_compacted; }
    [[nodiscard]] virtual u64 getDeviceAddress()const override{ return m_deviceAddress; }
    virtual Object getNativeHandle(ObjectType objectType)override;


private:
    RayTracingAccelStructDesc m_desc;
    VkAccelerationStructureKHR m_accelStruct = VK_NULL_HANDLE;
    RefCountPtr<IBuffer, ArenaRefDeleter<IBuffer>> m_buffer;
    u64 m_deviceAddress = 0;
    bool m_compacted = false;

    VkQueryPool m_compactionQueryPool = VK_NULL_HANDLE;
    u32 m_compactionQueryIndex = 0;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Opacity Micromap


class OpacityMicromap final : public RefCounter<IRayTracingOpacityMicromap>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    OpacityMicromap(const VulkanContext& context);
    virtual ~OpacityMicromap()override;


public:
    [[nodiscard]] virtual const RayTracingOpacityMicromapDesc& getDescription()const override{ return m_desc; }
    [[nodiscard]] virtual bool isCompacted()const override{ return m_compacted; }
    [[nodiscard]] virtual u64 getDeviceAddress()const override{ return m_deviceAddress; }


private:
    RayTracingOpacityMicromapDesc m_desc;
    RefCountPtr<IBuffer, ArenaRefDeleter<IBuffer>> m_dataBuffer;
    VkMicromapEXT m_micromap = VK_NULL_HANDLE;
    u64 m_deviceAddress = 0;
    bool m_compacted = false;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// State Tracker


class StateTracker final : NoCopy{
    friend class CommandList;


public:
    StateTracker(const VulkanContext& context);
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


private:
    GraphicsState m_graphicsState;
    ComputeState m_computeState;
    MeshletState m_meshletState;
    RayTracingState m_rayTracingState;

private:
    HashMap<ITexture*, ResourceStates::Mask, Hasher<ITexture*>, EqualTo<ITexture*>, Alloc::CustomAllocator<Pair<const ITexture*, ResourceStates::Mask>>> m_permanentTextureStates;
    HashMap<IBuffer*, ResourceStates::Mask, Hasher<IBuffer*>, EqualTo<IBuffer*>, Alloc::CustomAllocator<Pair<const IBuffer*, ResourceStates::Mask>>> m_permanentBufferStates;
    HashMap<ITexture*, ResourceStates::Mask, Hasher<ITexture*>, EqualTo<ITexture*>, Alloc::CustomAllocator<Pair<const ITexture*, ResourceStates::Mask>>> m_textureStates;
    HashMap<IBuffer*, ResourceStates::Mask, Hasher<IBuffer*>, EqualTo<IBuffer*>, Alloc::CustomAllocator<Pair<const IBuffer*, ResourceStates::Mask>>> m_bufferStates;
    HashMap<ITexture*, bool, Hasher<ITexture*>, EqualTo<ITexture*>, Alloc::CustomAllocator<Pair<const ITexture*, bool>>> m_textureUavBarriers;
    HashMap<IBuffer*, bool, Hasher<IBuffer*>, EqualTo<IBuffer*>, Alloc::CustomAllocator<Pair<const IBuffer*, bool>>> m_bufferUavBarriers;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command List


struct RenderPassParameters{
    bool clearColorTargets = false;
    Color colorClearValues[8]{};
    u8 colorClearMask = 0xff;
    bool clearDepthTarget = false;
    f32 depthClearValue = 1.0f;
    bool clearStencilTarget = false;
    u8 stencilClearValue = 0;


    [[nodiscard]] bool clearColorTarget(u32 index)const{ return (colorClearMask & (1u << index)) != 0; }
};

class CommandList final : public RefCounter<ICommandList>, NoCopy{
    friend class Queue;


public:
    CommandList(Device& device, const CommandListParameters& params);
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
    virtual void writeTexture(ITexture* dest, u32 arraySlice, u32 mipLevel, const void* data, usize rowPitch, usize /*depthPitch*/ = 0)override;
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
    [[nodiscard]] TrackedCommandBufferPtr getCurrentCmdBuf()const{ return m_currentCmdBuf; }


private:
    void beginRenderPass(IFramebuffer* framebuffer, const RenderPassParameters& params);
    void endRenderPass();


private:
    CommandListParameters m_desc;
    TrackedCommandBufferPtr m_currentCmdBuf;
    CustomUniquePtr<StateTracker> m_stateTracker;
    bool m_enableAutomaticBarriers = true;

    GraphicsState m_currentGraphicsState;
    ComputeState m_currentComputeState;
    MeshletState m_currentMeshletState;
    RayTracingState m_currentRayTracingState;

    Device& m_device;
    const VulkanContext& m_context;
    AftermathMarkerTracker m_aftermathMarkerTracker;

    Vector<VkImageMemoryBarrier2, Alloc::CustomAllocator<VkImageMemoryBarrier2>> m_pendingImageBarriers;
    Vector<VkBufferMemoryBarrier2, Alloc::CustomAllocator<VkBufferMemoryBarrier2>> m_pendingBufferBarriers;

    Vector<RefCountPtr<AccelStruct, ArenaRefDeleter<AccelStruct>>, Alloc::CustomAllocator<RefCountPtr<AccelStruct, ArenaRefDeleter<AccelStruct>>>> m_pendingCompactions;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Event Query


class EventQuery final : public RefCounter<IEventQuery>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    EventQuery(const VulkanContext& context);
    virtual ~EventQuery()override;


private:
    VkFence m_fence = VK_NULL_HANDLE;
    bool m_started = false;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timer Query


class TimerQuery final : public RefCounter<ITimerQuery>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    TimerQuery(const VulkanContext& context);
    virtual ~TimerQuery()override;


private:
    VkQueryPool m_queryPool = VK_NULL_HANDLE;
    bool m_started = false;
    bool m_resolved = false;

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
    virtual void* mapStagingTexture(IStagingTexture* tex, const TextureSlice& slice, CpuAccessMode::Enum, usize* outRowPitch)override;
    virtual void unmapStagingTexture(IStagingTexture* tex)override;
    virtual void getTextureTiling(ITexture* texture, u32* numTiles, PackedMipDesc* desc, TileShape* tileShape, u32* subresourceTilingsNum, SubresourceTiling* subresourceTilings)override;
    virtual void updateTextureTileMappings(ITexture* texture, const TextureTilesMapping* tileMappings, u32 numTileMappings, CommandQueue::Enum executionQueue = CommandQueue::Graphics)override;
    [[nodiscard]] virtual SamplerFeedbackTextureHandle createSamplerFeedbackTexture(ITexture* pairedTexture, const SamplerFeedbackTextureDesc& desc)override;
    [[nodiscard]] virtual SamplerFeedbackTextureHandle createSamplerFeedbackForNativeTexture(ObjectType objectType, Object texture, ITexture* pairedTexture)override;
    [[nodiscard]] virtual BufferHandle createBuffer(const BufferDesc& d)override;
    virtual void* mapBuffer(IBuffer* buffer, CpuAccessMode::Enum)override;
    virtual void unmapBuffer(IBuffer* buffer)override;
    [[nodiscard]] virtual MemoryRequirements getBufferMemoryRequirements(IBuffer* buffer)override;
    virtual bool bindBufferMemory(IBuffer* buffer, IHeap* heap, u64 offset)override;
    [[nodiscard]] virtual BufferHandle createHandleForNativeBuffer(ObjectType objectType, Object buffer, const BufferDesc& desc)override;
    [[nodiscard]] virtual ShaderHandle createShader(const ShaderDesc& d, const void* binary, usize binarySize)override;
    [[nodiscard]] virtual ShaderHandle createShaderSpecialization(IShader* baseShader, const ShaderSpecialization* constants, u32 numConstants)override;
    [[nodiscard]] virtual ShaderLibraryHandle createShaderLibrary(const void* binary, usize binarySize)override;
    [[nodiscard]] virtual SamplerHandle createSampler(const SamplerDesc& d)override;
    [[nodiscard]] virtual InputLayoutHandle createInputLayout(const VertexAttributeDesc* d, u32 attributeCount, IShader*)override;
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
    virtual bool queryFeatureSupport(Feature::Enum feature, void* = nullptr, usize = 0)override;
    [[nodiscard]] virtual FormatSupport::Mask queryFormatSupport(Format::Enum format)override;
    [[nodiscard]] virtual CooperativeVectorDeviceFeatures queryCoopVecFeatures()override;
    virtual usize getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, int rows, int columns)override;
    [[nodiscard]] virtual Object getNativeQueue(ObjectType objectType, CommandQueue::Enum queue)override;
    virtual bool isAftermathEnabled()override{ return m_aftermathEnabled; }
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
    // Aftermath must be first due to reverse destruction order
    // Queues will destroy CommandLists which will unregister from m_aftermathCrashDumpHelper in their destructors
    bool m_aftermathEnabled = false;
    AftermathCrashDumpHelper m_aftermathCrashDumpHelper;

    VkAllocationCallbacks m_allocationCallbacksStorage{};

    VulkanContext m_context;
    VulkanAllocator m_allocator;
    CustomUniquePtr<Queue> m_queues[static_cast<u32>(CommandQueue::kCount)];

    CustomUniquePtr<UploadManager> m_uploadManager;
    CustomUniquePtr<UploadManager> m_scratchManager;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

