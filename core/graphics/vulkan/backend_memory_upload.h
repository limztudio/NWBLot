// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <core/task/cpu/scheduler.h>
#include "command_buffer_resource_references.h"
#include "heap_binding_contract.h"
#include "host_readback_sync.h"
#include "native_buffer_provenance.h"
#include "native_queue_state.h"
#include "native_texture_provenance.h"
#include "submitted_command_buffer_owner_lookup.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Device;
class VulkanTestDispatchAccess;
class Queue;
class TrackedCommandBuffer;
class StateTracker;
class GpuDescriptorHeap;
class DescriptorBufferManager;

class Buffer;
class Texture;
class AccelStruct;
class OpacityMicromap;

struct VulkanContext;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




// Handles memory allocation


class VulkanAllocator final : NoCopy{
    friend class Buffer;
    friend class Device;
    friend class Texture;


public:
    explicit VulkanAllocator(const VulkanContext& context);
    ~VulkanAllocator()noexcept;


public:
    [[nodiscard]] bool initialize();

    VkResult createBuffer(Buffer& buffer, const VkBufferCreateInfo& bufferInfo);
    void destroyBuffer(Buffer& buffer);
    VkResult mapBufferMemory(Buffer& buffer, void** outData);
    void unmapBufferMemory(Buffer& buffer);
    VkResult invalidateBufferMemory(Buffer& buffer);

    VkResult createTexture(Texture& texture, const VkImageCreateInfo& imageInfo);
    void destroyTexture(Texture& texture);

    VkResult createStagingTexture(StagingTexture& texture, const VkBufferCreateInfo& bufferInfo, CpuAccessMode::Enum cpuAccess);
    void destroyStagingTexture(StagingTexture& texture);
    VkResult invalidateStagingTextureMemory(StagingTexture& texture, u64 offset, u64 size);
    VkResult allocateHeap(Heap& heap);
    void freeHeap(Heap& heap);
    VkResult invalidateHeapMemory(Heap& heap, u64 offset, u64 size);
    VkResult bindHeapBufferMemory(Buffer& buffer, Heap& heap, u64 offset);
    VkResult bindHeapTextureMemory(Texture& texture, Heap& heap, u64 offset);
    VkResult createHostMappedBuffer(
        VkBuffer& buffer,
        VulkanAllocationHandle& allocation,
        void*& mappedMemory,
        const VkBufferCreateInfo& bufferInfo
    );
    void destroyHostMappedBuffer(VkBuffer& buffer, VulkanAllocationHandle& allocation, void*& mappedMemory)noexcept;


private:
    [[nodiscard]] bool tryRegisterBufferNativeIdentity(Buffer& buffer);
    void unregisterBufferNativeIdentity(VkBuffer nativeBuffer, Buffer& buffer)noexcept;
    [[nodiscard]] bool isBufferNativeIdentityRegistered(const Buffer& buffer)const noexcept;
    [[nodiscard]] bool tryRegisterTextureNativeIdentity(Texture& texture);
    void unregisterTextureNativeIdentity(VkImage nativeImage, Texture& texture)noexcept;
    [[nodiscard]] bool isTextureNativeIdentityRegistered(VkImage nativeImage, const Texture& texture)const noexcept;


private:
    const VulkanContext& m_context;
    VulkanAllocatorHandle m_allocator = nullptr;
    HashMap<u64, Buffer*, Hasher<u64>, EqualTo<u64>, Alloc::GlobalArena> m_bufferNativeIdentities;
    HashMap<VkImage, Texture*, Hasher<VkImage>, EqualTo<VkImage>, Alloc::GlobalArena> m_textureNativeIdentities;
    mutable Futex m_bufferNativeIdentityMutex;
    mutable Futex m_textureNativeIdentityMutex;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device memory for placed resources


class Heap final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Buffer;
    friend class Device;
    friend class Texture;
    friend class VulkanAllocator;
    friend class Queue;


private:
    struct BindingReservation{
        const void* owner = nullptr;
        VulkanDetail::HeapBindingRange range;
        VulkanDetail::HeapBindingResourceClass::Enum resourceClass =
            VulkanDetail::HeapBindingResourceClass::Buffer;
    };


public:
    Heap(const VulkanContext& context, VulkanAllocator& allocator);
    ~Heap();


public:
    [[nodiscard]] const HeapDesc& getDescription()const{ return m_desc; }
    Object getNativeHandle(ObjectType objectType);


private:
    void eraseBindingReservationLocked(const void* owner);


private:
    HeapDesc m_desc;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VulkanAllocationHandle m_allocation = nullptr;
    VkDeviceSize m_memoryOffset = 0;
    void* m_mappedMemory = nullptr;
    Vector<BindingReservation, Alloc::GlobalArena> m_bindingReservations;
    Futex m_bindingMutex;

    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
    u32 m_memoryTypeIndex = UINT32_MAX;
    bool m_requiresInvalidate = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles upload and build-scratch buffer chunks


class UploadManager final : NoCopy{
    friend class Device;


private:
    struct BufferChunk final : public RefCounter<GraphicsResource>{
        BufferHandle buffer;
        TrackedCommandBuffer* owner;
        BufferChunk* previousActiveChunk;
        BufferChunk* nextActiveChunk;
        u64 nativeRecordingID;
        u64 size;
        u64 allocated;
        u64 version;
        GpuPhysicalQueueId physicalQueue;


        BufferChunk(
            CpuTaskScheduler& pool,
            BufferHandle buf,
            TrackedCommandBuffer* chunkOwner,
            u64 chunkNativeRecordingID,
            GpuPhysicalQueueId queue,
            u64 sz
        );
        ~BufferChunk();
    };
    using BufferChunkPtr = RefCountPtr<BufferChunk>;
    using BufferChunkList = List<BufferChunkPtr, Alloc::GlobalArena>;
    // The owning list keeps every chunk address stable. Accepted submissions traverse only this intrusive active
    // chain, so accumulated retired storage cannot increase the post-native-accept commit cost.
    struct QueueChunkLedger{
        GpuPhysicalQueueId queue;
        BufferChunkList chunks;
        BufferChunk* firstActiveChunk = nullptr;

        QueueChunkLedger(GraphicsArena& arena, GpuPhysicalQueueId value)
            : queue(value)
            , chunks(arena)
        {}
    };


public:
    UploadManager(Device& pParent, u64 defaultChunkSize, u64 memoryLimit, bool isScratchBuffer);
    ~UploadManager();


public:
    void clear();
    bool suballocateBuffer(
        u64 size,
        Buffer** pBuffer,
        u64* pOffset,
        void** pCpuVA,
        TrackedCommandBuffer* owner,
        u64 nativeRecordingID,
        GpuPhysicalQueueId queue,
        u64 completedVersion,
        u32 alignment = s_DefaultUploadSuballocationAlignment
    );
    void submitChunks(
        GpuPhysicalQueueId queue,
        u64 submittedVersion,
        const Queue::SubmissionCommandListIdentity* submittedCommandLists,
        usize submittedCommandListCount,
        const VulkanDetail::SubmittedCommandBufferOwnerLookup& submittedOwners
    )noexcept;
    void discardChunks(
        GpuPhysicalQueueId queue,
        TrackedCommandBuffer* owner,
        u64 nativeRecordingID,
        u64 reusableVersion
    );
    void abandonChunks(
        GpuPhysicalQueueId queue,
        TrackedCommandBuffer* owner,
        u64 nativeRecordingID
    )noexcept;


private:
    void collectCompletedChunks();
    void trimRetiredChunksLocked(GpuPhysicalQueueId queue, u64 completedVersion);
    [[nodiscard]] QueueChunkLedger* findQueueLedgerLocked(GpuPhysicalQueueId queue)noexcept;
    [[nodiscard]] QueueChunkLedger* findOrCreateQueueLedgerLocked(GpuPhysicalQueueId queue);
    void linkActiveChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept;
    void retireChunkLocked(
        QueueChunkLedger& ledger,
        BufferChunk& chunk,
        u64 version,
        bool resetAllocated
    )noexcept;
    void retireSubmittedChunksLocked(
        GpuPhysicalQueueId queue,
        u64 version,
        const Queue::SubmissionCommandListIdentity* submittedCommandLists,
        usize submittedCommandListCount,
        const VulkanDetail::SubmittedCommandBufferOwnerLookup& submittedOwners
    )noexcept;
    void retireOwnerChunksLocked(
        GpuPhysicalQueueId queue,
        u64 version,
        bool resetAllocated,
        TrackedCommandBuffer& owner,
        u64 nativeRecordingID
    )noexcept;


private:
    Device& m_device;
    u64 m_defaultChunkSize;
    u64 m_memoryLimit;
    Futex m_mutex;
    u64 m_retiredChunkBytes = 0;

    GraphicsDeque<QueueChunkLedger> m_queueChunkLedgers;
    bool m_isScratchBuffer;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Buffer


class Buffer final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class DescriptorBufferManager;
    friend class StateTracker;
    friend class TrackedCommandBuffer;
    friend class VulkanAllocator;
    friend class UploadManager;
    friend class ShaderTable;

    friend VkDeviceAddress VulkanDetail::GetBufferDeviceAddress(Buffer* bufferResource, u64 offset);


private:
    struct BufferViewEntry{
        u64 byteOffset = 0;
        u64 byteSize = 0;
        VkBufferView view = VK_NULL_HANDLE;
        Format::Enum format = Format::UNKNOWN;
    };


public:
    struct VolatileBufferState{
        i32 latestVersion = 0;
        i32 minVersion = 0;
        i32 maxVersion = 0;
        bool initialized = false;
    };


public:
    Buffer(
        const VulkanContext& context,
        VulkanAllocator& allocator,
        const BufferDesc& creationDesc,
        const VkBufferCreateInfo& bufferInfo,
        bool initialStateKnown
    );
    ~Buffer();


public:
    [[nodiscard]] const BufferDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] const BufferDesc& getCreationDescription()const noexcept{ return m_creationDesc; }
    [[nodiscard]] bool descriptionMatchesCreation()const noexcept;
    [[nodiscard]] GpuVirtualAddress getGpuVirtualAddress()const{ return m_deviceAddress; }
    [[nodiscard]] u16 getDeviceGeneration()const noexcept{ return m_context.deviceGeneration; }
    // Task-graph declarations copy this production admission snapshot while retaining the Buffer itself.
    [[nodiscard]] ResourceQueueAdmissionSnapshot getQueueAdmissionSnapshot()const noexcept{
        return ResourceQueueAdmissionSnapshot{
            .queueFamilyIndices = m_bufferQueueFamilyIndices.empty() ? nullptr : m_bufferQueueFamilyIndices.data(),
            .queueFamilyIndexCount = static_cast<u32>(m_bufferQueueFamilyIndices.size()),
            .admittedQueueClasses = m_creationDesc.queueSharing,
            .usesConcurrentSharing = m_bufferInfo.sharingMode == VK_SHARING_MODE_CONCURRENT,
        };
    }
    // Resolves the state a typed task-graph import may inherit from live retained/native provenance.
    [[nodiscard]] ResourceStates::Mask resolveTaskGraphImportInitialState()const noexcept;
    virtual Object getNativeHandle(ObjectType objectType)override;


private:
    [[nodiscard]] VkBufferView getView(Format::Enum format, u64 byteOffset, u64 byteSize);
    [[nodiscard]] bool isRetainedStateKnown()const noexcept;
    void setRetainedStateKnown(bool known)noexcept;

private:
    BufferDesc m_desc;
    const BufferDesc m_creationDesc;

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VulkanAllocationHandle m_allocation = nullptr;
    const Vector<u32, Alloc::GlobalArena> m_bufferQueueFamilyIndices;
    const VkBufferCreateInfo m_bufferInfo{};
    u64 m_deviceAddress = 0;
    void* m_mappedMemory = nullptr;
    HeapHandle m_boundHeap;
    VulkanDetail::HeapBindingRange m_heapBindingRange;
    Futex m_memoryBindingMutex;
    Atomic<bool> m_retainedStateKnown = false;

    Vector<u64, Alloc::GlobalArena> m_versionTracking;
    Vector<BufferViewEntry, Alloc::GlobalArena> m_bufferViews;
    VolatileBufferState m_volatileState;
    Futex m_bufferViewsMutex;

    bool m_persistentlyMapped = false;
    bool m_requiresInvalidate = false;
    bool m_managed = true; // if true, owns the VkBuffer or VMA allocation
    const bool m_creationInitialStateKnown;
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


inline UploadManager::BufferChunk::BufferChunk(
    CpuTaskScheduler& pool,
    BufferHandle buf,
    TrackedCommandBuffer* chunkOwner,
    u64 chunkNativeRecordingID,
    GpuPhysicalQueueId queue,
    u64 sz
)
    : RefCounter<GraphicsResource>(pool)
    , buffer(Move(buf))
    , owner(chunkOwner)
    , previousActiveChunk(nullptr)
    , nextActiveChunk(nullptr)
    , nativeRecordingID(chunkNativeRecordingID)
    , size(sz)
    , allocated(0)
    , version(0)
    , physicalQueue(queue)
{}
inline UploadManager::BufferChunk::~BufferChunk() = default;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Texture


struct TextureViewKey{
    TextureSubresourceSet subresources;
    TextureDimension::Enum dimension = TextureDimension::Unknown;
    Format::Enum format = Format::UNKNOWN;
};

inline bool operator==(const TextureViewKey& lhs, const TextureViewKey& rhs)noexcept{
    return lhs.subresources == rhs.subresources && lhs.dimension == rhs.dimension && lhs.format == rhs.format;
}

struct TextureViewKeyHasher{
    usize operator()(const TextureViewKey& value)const noexcept{
        usize seed = 0;
        ::HashCombine(seed, value.subresources);
        ::HashCombine(seed, static_cast<u32>(value.dimension));
        ::HashCombine(seed, static_cast<u32>(value.format));
        return seed;
    }
};


class Texture final : public RefCounter<GraphicsResource>, NoCopy{
    friend bool VulkanDetail::BuildTextureImageViewCreateInfo(
        Texture& texture,
        const TextureSubresourceSet& resolvedSubresources,
        TextureDimension::Enum dimension,
        Format::Enum format,
        const tchar* operationName,
        bool assertFailure,
        VkImageViewCreateInfo& outViewInfo
    );
    friend bool VulkanDetail::BuildImageViewCreateInfo(
        Texture& texture,
        const DescriptorWriteItem& item,
        VkImageViewCreateInfo& outViewInfo
    );

    friend class BackendContext;
    friend class Device;
    friend class CommandList;
    friend class DescriptorBufferManager;
    friend class StateTracker;
    friend class VulkanAllocator;
    friend class Queue;
    friend class TrackedCommandBuffer;


public:
    Texture(
        const VulkanContext& context,
        VulkanAllocator& allocator,
        const TextureDesc& creationDesc,
        const VkImageCreateInfo& imageInfo,
        bool initialStateKnown
    );
    ~Texture();


public:
    [[nodiscard]] const TextureDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] const TextureDesc& getCreationDescription()const noexcept{ return m_creationDesc; }
    [[nodiscard]] bool descriptionMatchesCreation()const noexcept;
    [[nodiscard]] u16 getDeviceGeneration()const noexcept{ return m_context.deviceGeneration; }
    // Task-graph declarations copy this production admission snapshot while retaining the Texture itself.
    [[nodiscard]] ResourceQueueAdmissionSnapshot getQueueAdmissionSnapshot()const noexcept{
        return ResourceQueueAdmissionSnapshot{
            .queueFamilyIndices = m_imageQueueFamilyIndices.empty() ? nullptr : m_imageQueueFamilyIndices.data(),
            .queueFamilyIndexCount = static_cast<u32>(m_imageQueueFamilyIndices.size()),
            .admittedQueueClasses = m_creationDesc.queueSharing,
            .usesConcurrentSharing = m_imageInfo.sharingMode == VK_SHARING_MODE_CONCURRENT,
        };
    }
    // Resolves the state a typed task-graph import may inherit from live retained/native provenance.
    [[nodiscard]] ResourceStates::Mask resolveTaskGraphImportInitialState()const;
    Object getNativeHandle(ObjectType objectType);
    Object getNativeView(
        ObjectType objectType,
        Format::Enum format,
        TextureSubresourceSet subresources,
        TextureDimension::Enum dimension,
        bool
    );

    [[nodiscard]] VkImageView getView(
        const TextureSubresourceSet& subresources,
        TextureDimension::Enum dimension,
        Format::Enum format
    );


private:
    [[nodiscard]] bool canRevokeUnmanagedNativeImage(VkImage expectedNativeImage);
    [[nodiscard]] bool prepareRevokeUnmanagedNativeImage(VkImage expectedNativeImage);
    void commitRevokeUnmanagedNativeImage(VkImage expectedNativeImage)noexcept;
    void releasePreparedRevokeUnmanagedNativeImageIdentity(VkImage expectedNativeImage)noexcept;
    [[nodiscard]] bool isRetainedSubresourceStateKnown(ArraySlice arraySlice, MipLevel mipLevel);
    void setRetainedSubresourceStateKnown(ArraySlice arraySlice, MipLevel mipLevel, bool known)noexcept;


private:
    TextureDesc m_desc;
    const TextureDesc m_creationDesc;
    VulkanDetail::TextureFormatBlockLayout m_formatLayout;
    VkImageAspectFlags m_aspectMask = 0;

    VkImage m_image = VK_NULL_HANDLE;
    VkImage m_preparedRevokedNativeImage = VK_NULL_HANDLE;
    VulkanAllocationHandle m_allocation = nullptr;
    const Vector<u32, Alloc::GlobalArena> m_imageQueueFamilyIndices;
    const VkImageCreateInfo m_imageInfo{};
    HeapHandle m_boundHeap;
    VulkanDetail::HeapBindingRange m_heapBindingRange;
    Futex m_memoryBindingMutex;

    HashMap<TextureViewKey, VkImageView, TextureViewKeyHasher, EqualTo<TextureViewKey>, Alloc::GlobalArena> m_views;
    Futex m_viewsMutex;
    Vector<u8, Alloc::GlobalArena> m_retainedSubresourceStates;
    mutable Futex m_retainedSubresourceStatesMutex;

    const bool m_creationInitialStateKnown;
    bool m_managed = true; // if true, owns the VkImage or VMA allocation
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Staging Texture


class StagingTexture final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class VulkanAllocator;


public:
    StagingTexture(const VulkanContext& context, VulkanAllocator& allocator);
    ~StagingTexture();


public:
    [[nodiscard]] const TextureDesc& getDescription()const{ return m_desc; }


private:
    TextureDesc m_desc;
    TextureDesc m_creationDesc;
    VulkanDetail::TextureFormatBlockLayout m_formatLayout;
    VkImageAspectFlags m_aspectMask = 0;
    u64 m_arrayByteSize = 0;
    u64 m_totalByteSize = 0;
    ResourceQueueSharing::Mask m_creationQueueSharing = ResourceQueueSharing::Exclusive;
    VkSharingMode m_creationSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VulkanDetail::StagingTextureMipLayoutVector m_mipLayouts;
    VulkanDetail::StagingTextureQueueFamilyVector m_admittedQueueFamilies;

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VulkanAllocationHandle m_allocation = nullptr;
    Futex m_mappingMutex;
    void* m_mappedMemory = nullptr;

    u32 m_bufferOffsetAlignment = 0;
    CpuAccessMode::Enum m_cpuAccess{};
    bool m_requiresInvalidate = false;
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampler


class Sampler final : public RefCounter<GraphicsResource>, NoCopy{
    friend class CommandList;
    friend class Device;
    friend class DescriptorBufferManager;
    friend class GpuDescriptorHeap;


public:
    Sampler(const VulkanContext& context);
    ~Sampler();


public:
    [[nodiscard]] const SamplerDesc& getDescription()const{ return m_desc; }


private:
    SamplerDesc m_desc;
    VkSampler m_sampler = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader


class Shader final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class ShaderLibrary;


public:
    Shader(const VulkanContext& context);
    ~Shader();


public:
    [[nodiscard]] const ShaderDesc& getDescription()const{ return m_desc; }
    void getBytecode(const void** ppBytecode, usize* pSize)const{
        *ppBytecode = m_spirvWords.data();
        *pSize = m_spirvWords.size() * sizeof(u32);
    }


private:
    [[nodiscard]] VkSpecializationInfo makeSpecializationInfo()const;


private:
    ShaderDesc m_desc;
    VkShaderModule m_shaderModule = VK_NULL_HANDLE;

    Vector<u32, Alloc::GlobalArena> m_spirvWords;
    GraphicsString m_entryPointName;

    Vector<VkSpecializationMapEntry, Alloc::GlobalArena> m_specializationEntries;
    Vector<u32, Alloc::GlobalArena> m_specializationData;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shader Library


struct ShaderLibraryKey{
    explicit ShaderLibraryKey(GraphicsArena& arena)
        : entryName(arena)
    {}
    ShaderLibraryKey(GraphicsArena& arena, const AStringView inEntryName, const ShaderType::Mask inShaderType)
        : entryName(inEntryName, arena)
        , shaderType(inShaderType)
    {}

    GraphicsString entryName;
    ShaderType::Mask shaderType = ShaderType::None;
};

inline bool operator==(const ShaderLibraryKey& lhs, const ShaderLibraryKey& rhs)noexcept{
    return lhs.entryName == rhs.entryName && lhs.shaderType == rhs.shaderType;
}

struct ShaderLibraryKeyHasher{
    usize operator()(const ShaderLibraryKey& value)const noexcept{
        usize seed = Hasher<GraphicsString>{}(value.entryName);
        ::HashCombine(seed, static_cast<u32>(value.shaderType));
        return seed;
    }
};


class ShaderLibrary final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;


public:
    ShaderLibrary(const VulkanContext& context);
    ~ShaderLibrary();


public:
    void getBytecode(const void** ppBytecode, usize* pSize)const;
    ShaderHandle getShader(AStringView entryName, ShaderType::Mask shaderType);


private:
    Vector<u32, Alloc::GlobalArena> m_spirvWords;
    HashMap<ShaderLibraryKey, Handle<Shader>, ShaderLibraryKeyHasher, EqualTo<ShaderLibraryKey>, GraphicsArena> m_shaders;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input Layout


class InputLayout final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    InputLayout(const VulkanContext& context);
    ~InputLayout() = default;


public:
    [[nodiscard]] const VertexAttributeDesc* getAttributeDescription(u32 index)const{
        if(index >= m_attributes.size())
            return nullptr;
        return &m_attributes[index];
    }

    [[nodiscard]] u32 getNumAttributes()const{ return static_cast<u32>(m_attributes.size()); }


private:
    Vector<VertexAttributeDesc, Alloc::GlobalArena> m_attributes;
    Vector<VkVertexInputBindingDescription, Alloc::GlobalArena> m_bindings;
    Vector<VkVertexInputAttributeDescription, Alloc::GlobalArena> m_vkAttributes;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Framebuffer


class Framebuffer final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    Framebuffer(const VulkanContext& context);
    ~Framebuffer();


public:
    [[nodiscard]] const FramebufferDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] const FramebufferInfoEx& getFramebufferInfo()const{ return m_framebufferInfo; }


private:
    FramebufferDesc m_desc;
    FramebufferInfoEx m_framebufferInfo;

    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;

    Vector<TextureHandle, Alloc::GlobalArena> m_resources;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

