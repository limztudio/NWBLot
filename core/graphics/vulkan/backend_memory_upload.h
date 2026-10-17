// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_resource_context.h"
#include "backend_core_context.h"
#include "backend_queue.h"
#include "backend_forward.h"

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


struct BufferAllocation{
    VkBuffer buffer = VK_NULL_HANDLE;
    VulkanAllocationHandle allocation = nullptr;
    void* mappedMemory = nullptr;
    bool requiresInvalidate = false;
};

struct BufferSuballocation{
    Buffer* buffer = nullptr;
    u64 offset = 0u;
    void* cpuAddress = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    [[nodiscard]] Expected<void*, VkResult> mapBufferMemory(Buffer& buffer);
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
    [[nodiscard]] Expected<BufferAllocation, VkResult> createHostMappedBuffer(const VkBufferCreateInfo& bufferInfo);
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
    HashMap<u64, Buffer*, Alloc::GlobalArena, Hasher<u64>, EqualTo<u64>> m_bufferNativeIdentities;
    HashMap<VkImage, Texture*, Alloc::GlobalArena, Hasher<VkImage>, EqualTo<VkImage>> m_textureNativeIdentities;
    mutable Futex m_bufferNativeIdentityMutex;
    mutable Futex m_textureNativeIdentityMutex;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


    public:
        BufferChunk(CpuTaskScheduler& pool, BufferHandle buf, TrackedCommandBuffer* chunkOwner, u64 chunkNativeRecordingID, GpuPhysicalQueueId queue, u64 sz)noexcept;
        ~BufferChunk()noexcept;
    };


private:
    using BufferChunkPtr = RefCountPtr<BufferChunk>;
    using BufferChunkList = List<BufferChunkPtr, Alloc::GlobalArena>;


private:
    // The owning list keeps every chunk address stable.
    // Accepted submissions traverse only this intrusive active chain, so accumulated retired storage cannot increase the post-native-accept commit cost.
    struct QueueChunkLedger{
        GpuPhysicalQueueId queue;
        BufferChunkList chunks;
        BufferChunk* firstActiveChunk = nullptr;


    public:
        QueueChunkLedger(GraphicsArena& arena, GpuPhysicalQueueId value)
            : queue(value)
            , chunks(arena)
        {}
    };


public:
    UploadManager(Device& pParent, u64 defaultChunkSize, u64 memoryLimit, bool isScratchBuffer);
    ~UploadManager()noexcept;


public:
    void clear()noexcept;
    [[nodiscard]] Expected<BufferSuballocation> suballocateBuffer(u64 size, TrackedCommandBuffer* owner, u64 nativeRecordingID, GpuPhysicalQueueId queue, u64 completedVersion, u32 alignment = s_DefaultUploadSuballocationAlignment);
    void submitChunks(GpuPhysicalQueueId queue, u64 submittedVersion, const Queue::SubmissionCommandListIdentity* submittedCommandLists, usize submittedCommandListCount, const VulkanDetail::SubmittedCommandBufferOwnerLookup& submittedOwners)noexcept;
    void discardChunks(GpuPhysicalQueueId queue, TrackedCommandBuffer* owner, u64 nativeRecordingID, u64 reusableVersion)noexcept;
    void abandonChunks(GpuPhysicalQueueId queue, TrackedCommandBuffer* owner, u64 nativeRecordingID)noexcept;


private:
    void collectCompletedChunks();
    void trimRetiredChunksLocked(GpuPhysicalQueueId queue, u64 completedVersion)noexcept;
    [[nodiscard]] QueueChunkLedger* findQueueLedgerLocked(GpuPhysicalQueueId queue)noexcept;
    [[nodiscard]] QueueChunkLedger* findOrCreateQueueLedgerLocked(GpuPhysicalQueueId queue);
    void linkActiveChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk)noexcept;
    void retireChunkLocked(QueueChunkLedger& ledger, BufferChunk& chunk, u64 version, bool resetAllocated)noexcept;
    void retireSubmittedChunksLocked(GpuPhysicalQueueId queue, u64 version, const Queue::SubmissionCommandListIdentity* submittedCommandLists, usize submittedCommandListCount, const VulkanDetail::SubmittedCommandBufferOwnerLookup& submittedOwners)noexcept;
    void retireOwnerChunksLocked(GpuPhysicalQueueId queue, u64 version, bool resetAllocated, TrackedCommandBuffer& owner, u64 nativeRecordingID)noexcept;


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


class Buffer final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class DescriptorBufferManager;
    friend class StateTracker;
    friend class TrackedCommandBuffer;
    friend class VulkanAllocator;
    friend class UploadManager;
    friend class ShaderTable;

    friend VkDeviceAddress VulkanDetail::GetBufferDeviceAddress(Buffer* bufferResource, u64 offset)noexcept;


public:
    Buffer(const VulkanContext& context, VulkanAllocator& allocator, const BufferDesc& creationDesc, const VkBufferCreateInfo& bufferInfo, bool initialStateKnown);
    ~Buffer()noexcept;


public:
    [[nodiscard]] const BufferDesc& getDescription()const noexcept{ return m_desc; }
    [[nodiscard]] const BufferDesc& getCreationDescription()const noexcept{ return m_creationDesc; }
    [[nodiscard]] bool descriptionMatchesCreation()const noexcept;
    [[nodiscard]] GpuVirtualAddress getGpuVirtualAddress()const noexcept{ return m_deviceAddress; }
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
    virtual Object getNativeHandle(ObjectType objectType)noexcept override;


private:
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

    const bool m_creationInitialStateKnown;
    bool m_persistentlyMapped = false;
    bool m_requiresInvalidate = false;
    bool m_managed = true; // if true, owns the VkBuffer or VMA allocation
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


inline UploadManager::BufferChunk::BufferChunk(CpuTaskScheduler& pool, BufferHandle buf, TrackedCommandBuffer* chunkOwner, u64 chunkNativeRecordingID, GpuPhysicalQueueId queue, u64 sz)noexcept
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
inline UploadManager::BufferChunk::~BufferChunk()noexcept = default;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    friend Expected<VkImageViewCreateInfo> VulkanDetail::BuildTextureImageViewCreateInfo(Texture& texture, const TextureSubresourceSet& resolvedSubresources, TextureDimension::Enum dimension, Format::Enum format, TStringView operationName, bool assertFailure);

    friend class BackendContext;
    friend class Device;
    friend class CommandList;
    friend class DescriptorBufferManager;
    friend class StateTracker;
    friend class VulkanAllocator;
    friend class Queue;
    friend class TrackedCommandBuffer;


public:
    Texture(const VulkanContext& context, VulkanAllocator& allocator, const TextureDesc& creationDesc, const VkImageCreateInfo& imageInfo, bool initialStateKnown);
    ~Texture()noexcept;


public:
    [[nodiscard]] const TextureDesc& getDescription()const noexcept{ return m_desc; }
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
    [[nodiscard]] ResourceStates::Mask resolveTaskGraphImportInitialState()const noexcept;
    Object getNativeHandle(ObjectType objectType)noexcept;
    [[nodiscard]] Expected<Object> getNativeView(ObjectType objectType, Format::Enum format, TextureSubresourceSet subresources, TextureDimension::Enum dimension, bool);

    [[nodiscard]] Expected<VkImageView> getView(const TextureSubresourceSet& subresources, TextureDimension::Enum dimension, Format::Enum format);


private:
    [[nodiscard]] bool canRevokeUnmanagedNativeImage(VkImage expectedNativeImage)noexcept;
    [[nodiscard]] bool prepareRevokeUnmanagedNativeImage(VkImage expectedNativeImage)noexcept;
    void commitRevokeUnmanagedNativeImage(VkImage expectedNativeImage)noexcept;
    void releasePreparedRevokeUnmanagedNativeImageIdentity(VkImage expectedNativeImage)noexcept;
    [[nodiscard]] bool isRetainedSubresourceStateKnown(ArraySlice arraySlice, MipLevel mipLevel)noexcept;
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

    HashMap<TextureViewKey, VkImageView, Alloc::GlobalArena, TextureViewKeyHasher, EqualTo<TextureViewKey>> m_views;
    Futex m_viewsMutex;
    Vector<u8, Alloc::GlobalArena> m_retainedSubresourceStates;
    mutable Futex m_retainedSubresourceStatesMutex;

    const bool m_creationInitialStateKnown;
    bool m_managed = true; // if true, owns the VkImage or VMA allocation
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class StagingTexture final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class VulkanAllocator;


public:
    StagingTexture(const VulkanContext& context, VulkanAllocator& allocator)noexcept;
    ~StagingTexture()noexcept;


public:
    [[nodiscard]] const TextureDesc& getDescription()const noexcept{ return m_desc; }


private:
    TextureDesc m_desc;
    TextureDesc m_creationDesc;
    VulkanDetail::TextureFormatBlockLayout m_formatLayout;
    VkImageAspectFlags m_aspectMask = 0;
    u64 m_arrayByteSize = 0;
    u64 m_totalByteSize = 0;
    VkSharingMode m_creationSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VulkanDetail::StagingTextureMipLayoutVector m_mipLayouts;
    VulkanDetail::StagingTextureQueueFamilyVector m_admittedQueueFamilies;

    VkBuffer m_buffer = VK_NULL_HANDLE;
    VulkanAllocationHandle m_allocation = nullptr;
    Futex m_mappingMutex;
    void* m_mappedMemory = nullptr;

    u32 m_bufferOffsetAlignment = 0;
    ResourceQueueSharing::Mask m_creationQueueSharing = ResourceQueueSharing::Exclusive;
    CpuAccessMode::Enum m_cpuAccess{};
    bool m_requiresInvalidate = false;
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

