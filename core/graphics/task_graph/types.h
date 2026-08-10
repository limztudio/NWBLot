// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/rhi/command.h>
#include <core/graphics/rhi/raytracing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuQueueCapability{
    enum Mask : u8{
        None = 0u,
        Transfer = 1u << 0u,
        Compute = 1u << 1u,
        Graphics = 1u << 2u,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

namespace GpuQueuePreference{
    enum Enum : u8{
        Graphics,
        Compute,
        Transfer,
        Any,

        kCount,
    };
};

// Queue preferences describe desired capability classes; this ID identifies one concrete queue in a particular
// device lifetime. The current topology maps Graphics, optional dedicated Compute, and optional dedicated Transfer
// transports while leaving room for multiple queues of one class later.
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

// `queueClass` is the current physical submission transport. It is deliberately distinct from
// GpuQueuePreference: the former names a real queue, while the latter is a task declaration policy.
struct GpuPhysicalQueueInfo{
    GpuPhysicalQueueId id;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    GpuQueueCapability::Mask capabilities = GpuQueueCapability::None;
    u32 familyIndex = Limit<u32>::s_Max;
    u32 queueIndex = 0u;
    bool dedicated = false;
};

// This view is immutable for one compile. The renderer owns device discovery and supplies only queues that may
// receive work; graph code never infers or fabricates a fallback physical queue.
struct GpuTaskGraphQueueTopology{
    const GpuPhysicalQueueInfo* queues = nullptr;
    usize queueCount = 0u;
};

namespace GpuTaskCostHint{
    enum Enum : u8{
        Tiny,
        Small,
        Medium,
        Large,

        kCount,
    };
};

namespace GpuGraphResourceType{
    enum Enum : u8{
        Texture,
        Buffer,
        AccelStruct,
        HazardDomain,

        kCount,
    };
};

namespace GpuTaskResourceAccess{
    enum Enum : u8{
        Read,
        Write,
        ReadWrite,

        kCount,
    };
};

namespace GpuTaskHazardType{
    enum Enum : u8{
        Unknown,
        Explicit,
        ReadAfterWrite,
        WriteAfterRead,
        WriteAfterWrite,

        kCount,
    };
};

// Dependency-edge flags are stored in the existing frame-graph telemetry payload. They keep Phase 1 evidence
// observable without changing its wire format or the renderer's live scheduling behavior.
namespace GpuTaskGraphTelemetryEdgeFlag{
    enum Mask : u8{
        None = 0u,
        ExplicitDependency = 1u << 0u,
        InferredDependency = 1u << 1u,
        MissingLegacyScheduleDependency = 1u << 2u,
    };
};

// Queue-assignment information reuses the existing frame-graph node flags during the observational migration.
// Keeping it in the node payload avoids changing the telemetry wire schema while still making compiler decisions
// and parity failures visible.
namespace GpuTaskGraphTelemetryNodeFlag{
    enum Mask : u8{
        None = 0u,
        AssignedGraphicsQueue = 1u << 0u,
        AssignedComputeQueue = 1u << 1u,
        AssignedDedicatedQueue = 1u << 2u,
        QueueAssignmentFallback = 1u << 3u,
        LegacyQueueAssignmentMismatch = 1u << 4u,
        // Preserve every existing telemetry wire-bit; Transfer occupies the first previously unused slot.
        AssignedTransferQueue = 1u << 5u,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTaskId{
    u32 index = Limit<u32>::s_Max;
    // This is globally unique for every live graph generation, rather than only graph-local. It rejects a handle
    // accidentally carried between two concurrently live graph instances as well as one carried past reset().
    u64 generation = 0u;

    [[nodiscard]] constexpr bool valid()const{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuTaskId& lhs, const GpuTaskId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
inline constexpr bool operator!=(const GpuTaskId& lhs, const GpuTaskId& rhs)noexcept{ return !(lhs == rhs); }

struct GpuGraphResourceId{
    u32 index = Limit<u32>::s_Max;
    u64 generation = 0u;

    [[nodiscard]] constexpr bool valid()const{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuGraphResourceId& lhs, const GpuGraphResourceId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
inline constexpr bool operator!=(const GpuGraphResourceId& lhs, const GpuGraphResourceId& rhs)noexcept{ return !(lhs == rhs); }

struct GpuExternalCompletionId{
    u32 index = Limit<u32>::s_Max;
    u64 generation = 0u;

    [[nodiscard]] constexpr bool valid()const{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuExternalCompletionId& lhs, const GpuExternalCompletionId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
inline constexpr bool operator!=(const GpuExternalCompletionId& lhs, const GpuExternalCompletionId& rhs)noexcept{
    return !(lhs == rhs);
}

// A packet is the compiler-generated unit of native recording and queue submission.  Like graph handles, packet
// IDs are tied to one graph generation so a recorded or accepted packet can never be reused after reset().
struct GpuSubmissionPacketId{
    u32 index = Limit<u32>::s_Max;
    u64 generation = 0u;

    [[nodiscard]] constexpr bool valid()const{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuSubmissionPacketId& lhs, const GpuSubmissionPacketId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
inline constexpr bool operator!=(const GpuSubmissionPacketId& lhs, const GpuSubmissionPacketId& rhs)noexcept{
    return !(lhs == rhs);
}

// A compiler-derived contiguous span of native packets.  Consumers obtain ranges from GpuCompiledGraph instead of
// mirroring its topological indices in renderer scheduling code.
struct GpuSubmissionPacketRange{
    GpuSubmissionPacketId first;
    usize packetCount = 0u;

    [[nodiscard]] constexpr bool valid()const{ return first.valid() && packetCount != 0u; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Texture ranges default to every subresource. TextureSubresourceSet{} is only mip zero / array slice zero, so it
// must never be used as the graph's implicit whole-texture range.
struct GpuTaskResourceRange{
    TextureSubresourceSet textureSubresources = s_AllSubresources;
    BufferRange bufferRange = s_EntireBuffer;
};

struct GpuTaskResourceUse{
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    ResourceStates::Mask requiredState = ResourceStates::Unknown;
    GpuTaskResourceAccess::Enum access = GpuTaskResourceAccess::Read;
    // The consumer supplies an equivalent externally synchronized state source, so a matching concurrent read may
    // omit a graph-internal producer seed and its submission dependency. The compiler accepts this only for a
    // same-state Read-to-Read handoff on compatible concurrent queues.
    bool hasIndependentStateSource = false;
};

struct GpuTaskDependencyEdge{
    GpuTaskId producer;
    GpuTaskId consumer;
    // Explicit edges intentionally carry no resource. Inferred edges preserve the first resource that established
    // the dependency; resource-use telemetry retains any additional overlapping uses.
    GpuGraphResourceId resource;
    GpuTaskHazardType::Enum hazard = GpuTaskHazardType::Unknown;
};

struct GpuTaskExternalDependencyEdge{
    GpuExternalCompletionId completion;
    GpuTaskId consumer;
};


// A compiled barrier is a graph-level transition or ownership request. It deliberately names only engine resources,
// resource states, and resolved physical queues; the packet recorder lowers it through CommandList so the task graph
// stays independent of raw Vulkan barrier objects.
namespace GpuCompiledBarrierType{
    enum Enum : u8{
        TextureTransition,
        BufferTransition,
        AccelStructTransition,
        TextureUav,
        BufferUav,
        AccelStructUav,
        TextureOwnershipRelease,
        TextureOwnershipAcquire,
        BufferOwnershipRelease,
        BufferOwnershipAcquire,

        kCount,
    };
};

struct GpuCompiledBarrier{
    GpuCompiledBarrierType::Enum type = GpuCompiledBarrierType::TextureTransition;
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    ResourceStates::Mask before = ResourceStates::Unknown;
    ResourceStates::Mask after = ResourceStates::Unknown;
    GpuPhysicalQueueId sourceQueue;
    GpuPhysicalQueueId destinationQueue;
};

// A packet-state seed names the prior packet that owns the authoritative native state snapshot for one declared
// resource range.  The recorder filters that snapshot to the declared range before opening the consumer command
// list; no renderer-side fan-in is required for graph-internal edges.
struct GpuPacketStateSeed{
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    GpuSubmissionPacketId sourcePacket;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

