// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "pipeline.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Command lists and state handoffs need this queue identity without pulling in device.h (which itself includes
// command.h through the public graphics API).
namespace CommandQueue{
    enum Enum : u8{
        Graphics = 0,
        Compute,
        // Optional physical copy transport. It exists only when Vulkan exposes a distinct dedicated
        // transfer-only family; task-graph fallback continues to use Compute or Graphics otherwise.
        Transfer,

        kCount
    };
};

// A task may request a capability class, but native recording and submission need to name the exact Vulkan queue
// that owns its command pool and timeline. Physical IDs are scoped to one logical-device generation; an invalid
// ID deliberately cannot be used as an ownership or retirement key.
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

namespace GpuQueueCapability{
    enum Mask : u8{
        None = 0u,
        Transfer = 1u << 0u,
        Compute = 1u << 1u,
        Graphics = 1u << 2u,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

// `queueClass` retains broad API capability validation. `id` selects the real native transport, including when a
// device exposes more than one queue of the same class.
struct GpuPhysicalQueueInfo{
    GpuPhysicalQueueId id;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    GpuQueueCapability::Mask capabilities = GpuQueueCapability::None;
    u32 familyIndex = Limit<u32>::s_Max;
    u32 queueIndex = 0u;
    bool dedicated = false;
};

// Borrowed immutable topology view; its producer owns the storage. A compiled-graph view becomes invalid at
// reset/recompile.
struct GpuPhysicalQueueTopology{
    const GpuPhysicalQueueInfo* queues = nullptr;
    usize queueCount = 0u;
};

// Current backend-native command storage for one physical queue. Counts cover the direct worker-zero path and
// every explicit recording-worker shard. A snapshot is sampled from thread-safe counters, so fields may advance
// independently while recording/submission is concurrent. nativeHandleStorageLowerBoundBytes counts only the
// client-visible native pool and command-buffer handle objects; opaque driver allocations and wrapper/container
// capacity are deliberately excluded.
struct GpuCommandArenaStatistics{
    GpuPhysicalQueueId queue;
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


    [[nodiscard]] bool valid()const noexcept{ return queue.valid(); }
};

typedef GraphicsBackend::Handle<EventQuery> EventQueryHandle;
typedef GraphicsBackend::Handle<TimerQuery> TimerQueryHandle;

// Absolute device-timestamp values for one timer query. GPU timing uses the duration for ordinary scopes and the
// endpoints to derive cross-queue packet overlap without summing concurrent work.
struct TimerQueryResult{
    f64 beginSeconds = 0.0;
    f64 endSeconds = 0.0;

    [[nodiscard]] f64 durationSeconds()const{ return endSeconds - beginSeconds; }
};


// Captures the final tracked state of one primary command list so a later primary command list can begin
// from it. The scheduler must guarantee that the producer executes before the consumer, either by preserving
// their order in one queue submission or by using explicit queue synchronization. Resource lifetime remains
// owned by the caller: every referenced texture and buffer must stay alive until the consumer has opened.
//
// It carries both transient and permanent state. UAV-barrier policy remains local to each command list, while
// keepInitialState resources are captured after their close-time restore barriers. Before the producer is accepted,
// this handoff is the only valid cross-list source for that restored native state.
class CommandListResourceStateHandoff final : NoCopy{
    friend class GraphicsBackend::CommandList;

private:
    struct TextureState{
        Texture* texture = nullptr;
        MipLevel mipLevel = 0;
        ArraySlice arraySlice = 0;
        ResourceStates::Mask state = ResourceStates::Unknown;
        ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
        // An invalid ID means concurrently shared (or not yet claimed). Otherwise this is the exact native queue
        // that last owned the exclusive resource. A valid release destination requires a paired acquire there.
        GpuPhysicalQueueId ownerQueue;
        GpuPhysicalQueueId releaseDestinationQueue;
    };

    struct BufferState{
        Buffer* buffer = nullptr;
        ResourceStates::Mask state = ResourceStates::Unknown;
        ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
        GpuPhysicalQueueId ownerQueue;
        GpuPhysicalQueueId releaseDestinationQueue;
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
    void reset(){
        m_textureStates.clear();
        m_bufferStates.clear();
        m_permanentTextureStates.clear();
        m_permanentBufferStates.clear();
        m_deviceGeneration = 0u;
        m_valid = false;
    }
    [[nodiscard]] bool valid()const{ return m_valid; }
    [[nodiscard]] u16 deviceGeneration()const noexcept{ return m_deviceGeneration; }
    // State snapshots retain raw backend-resource pointers.  Preserve the producer Device identity so a stale
    // snapshot can be rejected before any of those pointers are inspected after device recreation.
    [[nodiscard]] bool validForDeviceGeneration(const u16 deviceGeneration)const noexcept{
        return m_valid && deviceGeneration != 0u && m_deviceGeneration == deviceGeneration;
    }

    // Builds a post-branch state snapshot from a normalized base snapshot and the final states exported by
    // independently recorded branches. Every branch must have been opened from base, and the caller must submit
    // the base producer before every branch and every branch before the eventual consumer. Only final resource
    // states are merged here: callers must still keep cross-branch read/write hazards disjoint or synchronize them
    // explicitly. Returns false if a branch is invalid or two branches leave the same resource in incompatible final
    // states. `this` must be distinct from base and every branch.
    [[nodiscard]] bool buildFanIn(
        const CommandListResourceStateHandoff& base,
        const CommandListResourceStateHandoff* const* branches,
        usize branchCount
    );

    // Builds a valid handoff containing only the selected resources from `source`. This lets a cross-queue packet
    // import exactly the resources its queue may access instead of accidentally acquiring unrelated exclusive
    // resources from a broad producer snapshot. Null resource entries are ignored.
    [[nodiscard]] bool buildResourceSubset(
        const CommandListResourceStateHandoff& source,
        Texture* const* textures,
        usize textureCount,
        Buffer* const* buffers,
        usize bufferCount
    );
    [[nodiscard]] bool buildTextureSubset(const CommandListResourceStateHandoff& source, Texture* texture);
    [[nodiscard]] bool buildTextureRangeSubset(
        const CommandListResourceStateHandoff& source,
        Texture* texture,
        TextureSubresourceSet subresources
    );
    // Copies a valid state snapshot without exposing backend tracker storage.  Packet recording uses this to retain
    // graph-owned producer seeds while legacy consumers still request their own final handoff.
    [[nodiscard]] bool copyFrom(const CommandListResourceStateHandoff& source);
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
    Color blendConstantColor{};
    u8 dynamicStencilRefValue = 0;

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
    u8 dynamicStencilRefValue = 0;

    Buffer* indirectParams = nullptr;

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
    constexpr RayTracingPipelineDesc& setAllowSpheres(bool value){ allowSpheres = value; return *this; }
    constexpr RayTracingPipelineDesc& setAllowLinearSweptSpheres(bool value){ allowLinearSweptSpheres = value; return *this; }
};

typedef GraphicsBackend::Handle<RayTracingShaderTable> RayTracingShaderTableHandle;
typedef GraphicsBackend::Handle<RayTracingPipeline> RayTracingPipelineHandle;

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

