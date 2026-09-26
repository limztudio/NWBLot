// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "pipeline.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuNativePacketRecorder;
class CommandListResourceSelection;

namespace GraphicsBackend{
    class VulkanTestDispatchAccess;
};


// Queue identity for command lists and state handoffs (avoids pulling in device.h).
namespace CommandQueue{
    static constexpr u8 kCommandQueueGraphicsBase = 0;
    enum Enum : u8{
        Graphics = kCommandQueueGraphicsBase,
        Compute,
        // Optional copy transport, only when Vulkan exposes a distinct transfer-only family.
        Transfer,

        kCount
    };
};

// Native recording names the exact Vulkan queue owning the command pool and timeline. IDs are scoped to one logical-device generation; an invalid ID is never an ownership or retirement key.
struct GpuPhysicalQueueId{
    u16 index = Limit<u16>::s_Max;
    u16 deviceGeneration = 0u;

    [[nodiscard]] constexpr bool valid()const{
        return index != Limit<u16>::s_Max && deviceGeneration != 0u;
    }
};
inline constexpr bool operator==(const GpuPhysicalQueueId& lhs, const GpuPhysicalQueueId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.deviceGeneration == rhs.deviceGeneration;
}
inline constexpr bool operator!=(const GpuPhysicalQueueId& lhs, const GpuPhysicalQueueId& rhs)noexcept{
    return !(lhs == rhs);
}

// Completion edge from an accepted submission. `valid()` is false for rejected or empty work; a sync-only submission may still produce a token for dependency forwarding.
struct QueueSubmissionToken{
    u64 value = 0;
    u16 physicalQueueIndex = Limit<u16>::s_Max;
    u16 deviceGeneration = 0u;
    CommandQueue::Enum queue = CommandQueue::kCount;

    [[nodiscard]] constexpr bool valid()const{
        return static_cast<u32>(queue) < static_cast<u32>(CommandQueue::kCount) && value != 0u;
    }
    [[nodiscard]] constexpr bool hasPhysicalQueueIdentity()const{
        return physicalQueueIndex != Limit<u16>::s_Max && deviceGeneration != 0u;
    }
    [[nodiscard]] constexpr bool matchesPhysicalQueue(
        const u16 index,
        const u16 generation
    )const{
        return hasPhysicalQueueIdentity()
            && physicalQueueIndex == index
            && deviceGeneration == generation
        ;
    }
};

// Immutable queue pressure sampled at one scheduling decision. Timeline distance is a coarse availability signal
struct GpuQueueTimelineSnapshot{
    GpuPhysicalQueueId queue;
    u64 submittedValue = 0u;
    u64 completedValue = 0u;

    [[nodiscard]] constexpr bool valid()const noexcept{
        return queue.valid() && completedValue <= submittedValue;
    }
    [[nodiscard]] constexpr u64 pendingSubmissionCount()const noexcept{
        return valid() ? submittedValue - completedValue : 0u;
    }
};

namespace GpuQueueCapability{
    static constexpr u8 kGpuQueueCapabilityNoneBase = 0u;
    enum Mask : u8{
        None = kGpuQueueCapabilityNoneBase,
        Transfer = 1u << 0u,
        Compute = 1u << 1u,
        Graphics = 1u << 2u,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

// `queueClass` keeps API capability validation; `id` selects the native transport.
struct GpuPhysicalQueueInfo{
    u32 familyIndex = Limit<u32>::s_Max;
    u32 queueIndex = 0u;
    u32 timestampValidBits = 0u;
    GpuPhysicalQueueId id;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    GpuQueueCapability::Mask capabilities = GpuQueueCapability::None;
    bool dedicated = false;
};

// Borrowed immutable topology view; its producer owns the storage. A compiled-graph view becomes invalid at reset/recompile.
struct GpuPhysicalQueueTopology{
    const GpuPhysicalQueueInfo* queues = nullptr;
    usize queueCount = 0u;
};

// Current backend-native command storage for one physical queue. Snapshots sample thread-safe counters, so fields may advance during recording/submission. The storage estimate covers client-visible pool and command-buffer handles only, not opaque driver memory or wrapper capacity.
struct GpuCommandArenaStatistics{
    u64 workerArenaCount = 0u;
    u64 commandPoolEpochCount = 0u;
    u64 pendingCommandPoolEpochCount = 0u;
    u64 currentCommandBufferCount = 0u;
    u64 highWaterCommandBufferCount = 0u;
    u64 reusableCommandBufferCount = 0u;
    u64 leasedCommandBufferCount = 0u;
    u64 pendingCommandBufferCount = 0u;
    u64 growthEventCount = 0u;
    u64 resetEventCount = 0u;
    u64 nativeHandleStorageLowerBoundBytes = 0u;
    GpuPhysicalQueueId queue;


    [[nodiscard]] bool valid()const noexcept{ return queue.valid(); }
};

// Snapshot for one recording worker on one physical queue. Direct recording is domain/index {0,0};
// shards use the GpuRecordedPacket domain/index. Counters sample independently and may advance during the query;
// the storage estimate covers handle objects only, not opaque driver memory.
struct GpuCommandArenaWorkerStatistics{
    u64 recordingWorkerDomain = 0u;
    u64 commandPoolEpochCount = 0u;
    u64 pendingCommandPoolEpochCount = 0u;
    u64 currentCommandBufferCount = 0u;
    u64 highWaterCommandBufferCount = 0u;
    u64 reusableCommandBufferCount = 0u;
    u64 leasedCommandBufferCount = 0u;
    u64 pendingCommandBufferCount = 0u;
    u64 growthEventCount = 0u;
    u64 resetEventCount = 0u;
    u64 nativeHandleStorageLowerBoundBytes = 0u;
    GpuPhysicalQueueId queue;
    u32 recordingWorkerIndex = 0u;


    [[nodiscard]] bool valid()const noexcept{ return queue.valid(); }
};

typedef GraphicsBackend::Handle<EventQuery> EventQueryHandle;
typedef GraphicsBackend::Handle<TimerQuery> TimerQueryHandle;

// One recorded begin/end cycle. Query, queue, generation, and reset authorization together stop a stale command buffer from closing or revoking another cycle after reuse.
struct TimerQueryRecordingToken{
    TimerQuery* query = nullptr;
    u64 queryIncarnation = 0u;
    u64 generation = 0u;
    u64 resetAuthorizationGeneration = 0u;
    GpuPhysicalQueueId physicalQueue;


    [[nodiscard]] constexpr bool valid()const noexcept{
        return query != nullptr && physicalQueue.valid() && queryIncarnation != 0u && generation != 0u;
    }
};

// One command-list marker. Native recording identity blocks stale closes on a reused list; markerSerial separates owners at the same nesting depth.
struct CommandMarkerRecordingToken{
    u64 recordingLeaseSerial = 0u;
    u64 nativeRecordingID = 0u;
    u64 markerSerial = 0u;


    [[nodiscard]] constexpr bool valid()const noexcept{
        return recordingLeaseSerial != 0u && nativeRecordingID != 0u && markerSerial != 0u;
    }
};

// Raw device timestamps for one timer query. Only the low timestampValidBits are exposed, so durations use modular tick arithmetic
struct TimerQueryResult{
    static constexpr u32 s_FullWidthBits = 64u;

    u64 beginTicks = 0u;
    u64 endTicks = 0u;
    f64 secondsPerTick = 0.0;
    GpuPhysicalQueueId physicalQueue;
    u32 timestampValidBits = 0u;
    bool comparableAcrossSubmissions = false;

    [[nodiscard]] bool valid()const{
        return timestampValidBits > 0u && timestampValidBits <= s_FullWidthBits && secondsPerTick > 0.0;
    }
    [[nodiscard]] u64 timestampMask()const{
        if(!valid())
            return 0u;
        return timestampValidBits == s_FullWidthBits ? Limit<u64>::s_Max : (static_cast<u64>(1u) << timestampValidBits) - 1u;
    }
    [[nodiscard]] u64 maskedBeginTicks()const{ return beginTicks & timestampMask(); }
    [[nodiscard]] u64 durationTicks()const{
        const u64 mask = timestampMask();
        if(mask == 0u)
            return 0u;

        const u64 duration = (endTicks & mask) - (beginTicks & mask);
        return timestampValidBits == s_FullWidthBits ? duration : duration & mask;
    }
    [[nodiscard]] f64 durationSeconds()const{ return static_cast<f64>(durationTicks()) * secondsPerTick; }
    [[nodiscard]] bool hasComparableRange()const{
        return
            valid()
            && comparableAcrossSubmissions
            && timestampValidBits == s_FullWidthBits
            && physicalQueue.valid()
            && beginTicks <= endTicks
        ;
    }
};


// Final tracked state of one primary list for the next to begin from (scheduler orders producer < consumer). Caller-owned lifetimes: refs stay alive until the consumer opens.

// It carries both transient and permanent state. UAV-barrier policy remains local to each command list, while keepInitialState resources are captured after their close-time restore barriers. Before the producer is accepted, this handoff is the only valid cross-list source for that restored native state.
class CommandListResourceStateHandoff final : NoCopy{
    friend class GraphicsBackend::CommandList;
    friend class GpuNativePacketRecorder;
    friend class GraphicsBackend::VulkanTestDispatchAccess;

private:
    struct TextureState{
        Texture* texture = nullptr;
        MipLevel mipLevel = 0;
        ArraySlice arraySlice = 0;
        ResourceStates::Mask state = ResourceStates::Unknown;
        ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
        // An invalid ID means concurrently shared (or not yet claimed). Otherwise this is the exact native queue that last owned the exclusive resource. A valid release destination requires a paired acquire there.
        GpuPhysicalQueueId ownerQueue;
        GpuPhysicalQueueId releaseDestinationQueue;
    };

    struct BufferState{
        Buffer* buffer = nullptr;
        ResourceStates::Mask state = ResourceStates::Unknown;
        ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
        GpuPhysicalQueueId ownerQueue;
        GpuPhysicalQueueId releaseDestinationQueue;
        BufferRange range = s_EntireBuffer;
    };

    struct PermanentTextureState{
        Texture* texture = nullptr;
        ResourceStates::Mask state = ResourceStates::Unknown;
        ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
        GpuPhysicalQueueId ownerQueue;
        GpuPhysicalQueueId releaseDestinationQueue;
    };


public:
    explicit CommandListResourceStateHandoff(GraphicsArena& arena)
        : m_textureStates(arena)
        , m_bufferStates(arena)
        , m_permanentTextureStates(arena)
        , m_permanentBufferStates(arena)
    {}


public:
    void reset()noexcept{
        m_textureStates.clear();
        m_bufferStates.clear();
        m_permanentTextureStates.clear();
        m_permanentBufferStates.clear();
        m_deviceGeneration = 0u;
        m_valid = false;
    }
    [[nodiscard]] bool valid()const{ return m_valid; }
    [[nodiscard]] u16 deviceGeneration()const noexcept{ return m_deviceGeneration; }
    // State snapshots retain raw backend-resource pointers.  Preserve the producer Device identity so a stale snapshot can be rejected before any of those pointers are inspected after device recreation.
    [[nodiscard]] bool validForDeviceGeneration(const u16 deviceGeneration)const noexcept{
        return m_valid && deviceGeneration != 0u && m_deviceGeneration == deviceGeneration;
    }

    // Fan-in merge of branch-final states over a normalized base (branches opened from base; base < branches < consumer).
    // Final states only: callers keep cross-branch hazards disjoint. False on invalid/incompatible; `this` != inputs.
    [[nodiscard]] bool buildFanIn(
        const CommandListResourceStateHandoff& base,
        const CommandListResourceStateHandoff* const* branches,
        usize branchCount,
        Alloc::ScratchArena& scratchArena
    );

    // Subset handoff of selected resources (cross-queue imports take only what their queue may access). Nulls ignored,
    // `source` may alias `this`; failure-atomic.
    [[nodiscard]] bool buildResourceSubset(
        const CommandListResourceStateHandoff& source,
        Texture* const* textures,
        usize textureCount,
        Buffer* const* buffers,
        usize bufferCount,
        Alloc::ScratchArena& scratchArena
    );
    // Reuses one completed selection across multiple filtered snapshots without retaining its scratch storage.
    [[nodiscard]] bool buildResourceSubset(const CommandListResourceStateHandoff& source, const CommandListResourceSelection& selection);
    [[nodiscard]] bool buildTextureSubset(const CommandListResourceStateHandoff& source, Texture* texture, Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool buildTextureRangeSubset(
        const CommandListResourceStateHandoff& source,
        Texture* texture,
        TextureSubresourceSet subresources
    );
    // Clips ordinary buffer states. Pending ownership releases must be selected in full because the acquire must use their original byte ranges
    [[nodiscard]] bool buildBufferRangeSubset(const CommandListResourceStateHandoff& source, Buffer* buffer, BufferRange range);
    // Imported exclusive-owner texture handoffs must provide one concrete state for every selected subresource. Verify that exact coverage before a graph lowers its paired acquire, including the source owner and (when distinct) the release destination captured by the producer's native state tracker.
    [[nodiscard]] bool coversTextureRangeWithOwnership(
        Texture* texture,
        TextureSubresourceSet subresources,
        GpuPhysicalQueueId expectedOwnerQueue,
        GpuPhysicalQueueId expectedReleaseDestinationQueue
    )const;
    // Buffer ownership must cover every byte in the requested range; unrelated intervals may have different owners.
    [[nodiscard]] bool coversBufferWithOwnership(
        Buffer* buffer,
        GpuPhysicalQueueId expectedOwnerQueue,
        GpuPhysicalQueueId expectedReleaseDestinationQueue,
        BufferRange range = s_EntireBuffer
    )const;
    // Copies a valid state snapshot without exposing backend tracker storage.
    // Packet recording uses this to retain graph-owned producer seeds while legacy consumers still request their own final handoff.
    [[nodiscard]] bool copyFrom(const CommandListResourceStateHandoff& source);
    // Compares immutable snapshot contents rather than the address of a producer-owned snapshot.
    // This is suitable for declaration deduplication across producer-storage retirement and same-address allocator reuse.
    [[nodiscard]] bool equivalentTo(const CommandListResourceStateHandoff& snapshot)const noexcept;
    // Exchanges complete snapshot storage without allocating. Both snapshots must be backed by the same arena so each vector remains paired with the allocator that owns its storage after the exchange.
    [[nodiscard]] bool exchangeSnapshot(CommandListResourceStateHandoff& snapshot)noexcept;
    [[nodiscard]] bool empty()const noexcept;


private:
    GraphicsVector<TextureState> m_textureStates;
    GraphicsVector<BufferState> m_bufferStates;
    GraphicsVector<PermanentTextureState> m_permanentTextureStates;
    GraphicsVector<BufferState> m_permanentBufferStates;
    u16 m_deviceGeneration = 0u;
    bool m_valid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RenderPassLoadAction{
    enum Enum : u8{
        Load,
        Clear,
        Discard,
        Count,
    };
};

namespace RenderPassStoreAction{
    enum Enum : u8{
        Store,
        Discard,
        Count,
    };
};

struct RenderPassAttachmentActions{
    RenderPassLoadAction::Enum loadAction = RenderPassLoadAction::Load;
    RenderPassStoreAction::Enum storeAction = RenderPassStoreAction::Store;
};

struct RenderPassParameters{
    Color colorClearValues[s_MaxRenderTargets]{};
    RenderPassAttachmentActions colorAttachmentActions[s_MaxRenderTargets]{};
    f32 depthClearValue = s_DepthClearValue;
    RenderPassAttachmentActions depthAttachmentActions;
    RenderPassAttachmentActions stencilAttachmentActions;
    u8 stencilClearValue = 0;
};

struct VertexBufferBinding{
    Buffer* buffer = nullptr;
    u64 offset = 0;
    u32 slot = 0;

    constexpr VertexBufferBinding& setBuffer(Buffer* value){ buffer = value; return *this; }
    constexpr VertexBufferBinding& setSlot(u32 value){ slot = value; return *this; }
    constexpr VertexBufferBinding& setOffset(u64 value){ offset = value; return *this; }
};
inline bool operator==(const VertexBufferBinding& lhs, const VertexBufferBinding& rhs)noexcept{
    return lhs.buffer == rhs.buffer && lhs.offset == rhs.offset && lhs.slot == rhs.slot;
}
inline bool operator!=(const VertexBufferBinding& lhs, const VertexBufferBinding& rhs)noexcept{ return !(lhs == rhs); }

struct IndexBufferBinding{
    Buffer* buffer = nullptr;
    u32 offset = 0;
    Format::Enum format = Format::UNKNOWN;

    constexpr IndexBufferBinding& setBuffer(Buffer* value){ buffer = value; return *this; }
    constexpr IndexBufferBinding& setFormat(Format::Enum value){ format = value; return *this; }
    constexpr IndexBufferBinding& setOffset(u32 value){ offset = value; return *this; }
};
inline bool operator==(const IndexBufferBinding& lhs, const IndexBufferBinding& rhs)noexcept{
    return lhs.buffer == rhs.buffer && lhs.offset == rhs.offset && lhs.format == rhs.format;
}
inline bool operator!=(const IndexBufferBinding& lhs, const IndexBufferBinding& rhs)noexcept{ return !(lhs == rhs); }

struct GraphicsState{
    GraphicsPipeline* pipeline = nullptr;
    Framebuffer* framebuffer = nullptr;
    ViewportState viewport;
    VariableRateShadingState shadingRateState;
    u8 dynamicStencilRefValue = 0;
    Color blendConstantColor{};
    FixedVector<VertexBufferBinding, s_MaxVertexAttributes> vertexBuffers;
    IndexBufferBinding indexBuffer;

    Buffer* indirectParams = nullptr;

    constexpr GraphicsState& setPipeline(GraphicsPipeline* value){ pipeline = value; return *this; }
    constexpr GraphicsState& setFramebuffer(Framebuffer* value){ framebuffer = value; return *this; }
    constexpr GraphicsState& setViewport(const ViewportState& value){ viewport = value; return *this; }
    constexpr GraphicsState& setShadingRateState(const VariableRateShadingState& value){ shadingRateState = value; return *this; }
    constexpr GraphicsState& setBlendColor(const Color& value){ blendConstantColor = value; return *this; }
    constexpr GraphicsState& setDynamicStencilRefValue(u8 value){ dynamicStencilRefValue = value; return *this; }
    GraphicsState& addVertexBuffer(const VertexBufferBinding& value){ vertexBuffers.push_back(value); return *this; }
    constexpr GraphicsState& setIndexBuffer(const IndexBufferBinding& value){ indexBuffer = value; return *this; }
    constexpr GraphicsState& setIndirectParams(Buffer* value){ indirectParams = value; return *this; }
};

struct DrawArguments{
    u32 vertexCount = 0;
    u32 instanceCount = 1;
    u32 startIndexLocation = 0;
    u32 startVertexLocation = 0;
    u32 startInstanceLocation = 0;

    constexpr DrawArguments& setVertexCount(u32 value){ vertexCount = value; return *this; }
    constexpr DrawArguments& setInstanceCount(u32 value){ instanceCount = value; return *this; }
    constexpr DrawArguments& setStartIndexLocation(u32 value){ startIndexLocation = value; return *this; }
    constexpr DrawArguments& setStartVertexLocation(u32 value){ startVertexLocation = value; return *this; }
    constexpr DrawArguments& setStartInstanceLocation(u32 value){ startInstanceLocation = value; return *this; }
};

struct DrawIndirectArguments{
    u32 vertexCount = 0;
    u32 instanceCount = 1;
    u32 startVertexLocation = 0;
    u32 startInstanceLocation = 0;

    constexpr DrawIndirectArguments& setVertexCount(u32 value){ vertexCount = value; return *this; }
    constexpr DrawIndirectArguments& setInstanceCount(u32 value){ instanceCount = value; return *this; }
    constexpr DrawIndirectArguments& setStartVertexLocation(u32 value){ startVertexLocation = value; return *this; }
    constexpr DrawIndirectArguments& setStartInstanceLocation(u32 value){ startInstanceLocation = value; return *this; }
};

struct DrawIndexedIndirectArguments{
    u32 indexCount = 0;
    u32 instanceCount = 1;
    u32 startIndexLocation = 0;
    i32  baseVertexLocation = 0;
    u32 startInstanceLocation = 0;

    constexpr DrawIndexedIndirectArguments& setIndexCount(u32 value){ indexCount = value; return *this; }
    constexpr DrawIndexedIndirectArguments& setInstanceCount(u32 value){ instanceCount = value; return *this; }
    constexpr DrawIndexedIndirectArguments& setStartIndexLocation(u32 value){ startIndexLocation = value; return *this; }
    constexpr DrawIndexedIndirectArguments& setBaseVertexLocation(i32 value){ baseVertexLocation = value; return *this; }
    constexpr DrawIndexedIndirectArguments& setStartInstanceLocation(u32 value){ startInstanceLocation = value; return *this; }
};

struct ComputeState{
    ComputePipeline* pipeline = nullptr;

    Buffer* indirectParams = nullptr;

    constexpr ComputeState& setPipeline(ComputePipeline* value){ pipeline = value; return *this; }
    constexpr ComputeState& setIndirectParams(Buffer* value){ indirectParams = value; return *this; }
};

struct DispatchIndirectArguments{
    u32 groupsX = 1;
    u32 groupsY = 1;
    u32 groupsZ = 1;

    constexpr DispatchIndirectArguments& setGroupsX(u32 value){ groupsX = value; return *this; }
    constexpr DispatchIndirectArguments& setGroupsY(u32 value){ groupsY = value; return *this; }
    constexpr DispatchIndirectArguments& setGroupsZ(u32 value){ groupsZ = value; return *this; }
    constexpr DispatchIndirectArguments& setGroups2D(u32 x, u32 y){ groupsX = x; groupsY = y; return *this; }
    constexpr DispatchIndirectArguments& setGroups3D(u32 x, u32 y, u32 z){ groupsX = x; groupsY = y; groupsZ = z; return *this; }
};

struct MeshletState{
    MeshletPipeline* pipeline = nullptr;
    Framebuffer* framebuffer = nullptr;
    ViewportState viewport;
    Color blendConstantColor{};
    Buffer* indirectParams = nullptr;
    u8 dynamicStencilRefValue = 0;

    constexpr MeshletState& setPipeline(MeshletPipeline* value){ pipeline = value; return *this; }
    constexpr MeshletState& setFramebuffer(Framebuffer* value){ framebuffer = value; return *this; }
    constexpr MeshletState& setViewport(const ViewportState& value){ viewport = value; return *this; }
    constexpr MeshletState& setBlendColor(const Color& value){ blendConstantColor = value; return *this; }
    constexpr MeshletState& setIndirectParams(Buffer* value){ indirectParams = value; return *this; }
    constexpr MeshletState& setDynamicStencilRefValue(u8 value){ dynamicStencilRefValue = value; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Ray Tracing


struct RayTracingPipelineShaderDesc{
    ShaderHandle shader;
    BindingLayoutHandle bindingLayout;
    GraphicsString exportName;

    explicit RayTracingPipelineShaderDesc(GraphicsArena& arena);
    ~RayTracingPipelineShaderDesc();

    RayTracingPipelineShaderDesc& setShader(const ShaderHandle& value);
    RayTracingPipelineShaderDesc& setBindingLayout(const BindingLayoutHandle& value);
    RayTracingPipelineShaderDesc& setExportName(AStringView value){ exportName.assign(value); return *this; }
};

struct RayTracingPipelineHitGroupDesc{
    ShaderHandle closestHitShader;
    ShaderHandle anyHitShader;
    ShaderHandle intersectionShader;
    BindingLayoutHandle bindingLayout;
    GraphicsString exportName;
    bool isProceduralPrimitive = false;

    explicit RayTracingPipelineHitGroupDesc(GraphicsArena& arena);
    ~RayTracingPipelineHitGroupDesc();

    RayTracingPipelineHitGroupDesc& setClosestHitShader(const ShaderHandle& value);
    RayTracingPipelineHitGroupDesc& setAnyHitShader(const ShaderHandle& value);
    RayTracingPipelineHitGroupDesc& setIntersectionShader(const ShaderHandle& value);
    RayTracingPipelineHitGroupDesc& setBindingLayout(const BindingLayoutHandle& value);
    RayTracingPipelineHitGroupDesc& setExportName(AStringView value){ exportName.assign(value); return *this; }
    constexpr RayTracingPipelineHitGroupDesc& setIsProceduralPrimitive(bool value){ isProceduralPrimitive = value; return *this; }
};

struct RayTracingPipelineDesc{
    GraphicsVector<RayTracingPipelineShaderDesc> shaders;
    GraphicsVector<RayTracingPipelineHitGroupDesc> hitGroups;
    BindingLayoutVector globalBindingLayouts;
    u32 maxPayloadSize = 0;
    u32 maxAttributeSize = sizeof(f32) * 2; // typical case: float2 uv;
    u32 maxRecursionDepth = 1;
    i32 hlslExtensionsUAV = -1;
    bool allowOpacityMicromaps = false;
    bool allowClusterAccelerationStructures = false;
    bool allowSpheres = false;
    bool allowLinearSweptSpheres = false;

    explicit RayTracingPipelineDesc(GraphicsArena& arena)
        : shaders(arena)
        , hitGroups(arena)
    {}
    ~RayTracingPipelineDesc();

    RayTracingPipelineDesc& addShader(const RayTracingPipelineShaderDesc& value);
    RayTracingPipelineDesc& addHitGroup(const RayTracingPipelineHitGroupDesc& value);
    RayTracingPipelineDesc& addBindingLayout(const BindingLayoutHandle& value);
    constexpr RayTracingPipelineDesc& setMaxPayloadSize(u32 value){ maxPayloadSize = value; return *this; }
    constexpr RayTracingPipelineDesc& setMaxAttributeSize(u32 value){ maxAttributeSize = value; return *this; }
    constexpr RayTracingPipelineDesc& setMaxRecursionDepth(u32 value){ maxRecursionDepth = value; return *this; }
    constexpr RayTracingPipelineDesc& setHlslExtensionsUAV(i32 value){ hlslExtensionsUAV = value; return *this; }
    constexpr RayTracingPipelineDesc& setAllowOpacityMicromaps(bool value){ allowOpacityMicromaps = value; return *this; }
    constexpr RayTracingPipelineDesc& setAllowClusterAccelerationStructures(bool value){ allowClusterAccelerationStructures = value; return *this; }
    constexpr RayTracingPipelineDesc& setAllowSpheres(bool value){ allowSpheres = value; return *this; }
    constexpr RayTracingPipelineDesc& setAllowLinearSweptSpheres(bool value){ allowLinearSweptSpheres = value; return *this; }
};

typedef GraphicsBackend::Handle<RayTracingShaderTable> RayTracingShaderTableHandle;
typedef GraphicsBackend::Handle<RayTracingPipeline> RayTracingPipelineHandle;

inline constexpr u32 s_InvalidRayTracingShaderTableRecordIndex = Limit<u32>::s_Max;

struct RayTracingState{
    RayTracingShaderTable* shaderTable = nullptr;

    constexpr RayTracingState& setShaderTable(RayTracingShaderTable* value){ shaderTable = value; return *this; }
};

struct RayTracingDispatchRaysArguments{
    u32 width = 1;
    u32 height = 1;
    u32 depth = 1;

    constexpr RayTracingDispatchRaysArguments& setWidth(u32 value){ width = value; return *this; }
    constexpr RayTracingDispatchRaysArguments& setHeight(u32 value){ height = value; return *this; }
    constexpr RayTracingDispatchRaysArguments& setDepth(u32 value){ depth = value; return *this; }
    constexpr RayTracingDispatchRaysArguments& setDimensions(u32 w, u32 h = 1, u32 d = 1){ width = w; height = h; depth = d; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

