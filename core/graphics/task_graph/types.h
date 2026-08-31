// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/rhi/command.h>
#include <core/graphics/rhi/raytracing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuQueuePreference{
    enum Enum : u8{
        Graphics,
        Compute,
        Transfer,
        Any,

        kCount,
    };
};

// The graph consumes the Device's immutable physical registry. Keep the existing name as a source-compatible
// graph-facing alias while making the queue metadata itself part of the RHI command contract.
using GpuTaskGraphQueueTopology = GpuPhysicalQueueTopology;

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

namespace GpuGraphResourceVersionOrigin{
    enum Enum : u8{
        TaskProduced,
        ImportedRoot,

        kCount,
    };
};

namespace GpuTaskResourceVersionRole{
    enum Enum : u8{
        Produce,
        Consume,

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
        VersionDependency,
        VersionLifetime,

        kCount,
    };
};

// Dependency-edge flags are stored in the existing frame-graph telemetry payload without changing its wire format
// or the renderer's live scheduling behavior.
namespace GpuTaskGraphTelemetryEdgeFlag{
    enum Mask : u8{
        None = 0u,
        ExplicitDependency = 1u << 0u,
        InferredDependency = 1u << 1u,
        VersionDependency = 1u << 2u,
        VersionLifetime = 1u << 3u,
    };
};

// Queue-assignment information reuses the existing frame-graph node flags. Keeping it in the node payload avoids
// changing the telemetry wire schema while still making compiler decisions visible.
namespace GpuTaskGraphTelemetryNodeFlag{
    enum Mask : u8{
        None = 0u,
        AssignedGraphicsQueue = 1u << 0u,
        AssignedComputeQueue = 1u << 1u,
        AssignedDedicatedQueue = 1u << 2u,
        QueueAssignmentFallback = 1u << 3u,
        QueueAssignmentCompilerOverride = 1u << 4u,
        AssignedTransferQueue = 1u << 5u,
        QueueAssignmentSameClassRouting = 1u << 6u,
        QueueAssignmentTimingRouting = 1u << 7u,
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

struct GpuGraphResourceVersionId{
    u32 index = Limit<u32>::s_Max;
    u64 generation = 0u;

    [[nodiscard]] constexpr bool valid()const{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuGraphResourceVersionId& lhs, const GpuGraphResourceVersionId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
inline constexpr bool operator!=(const GpuGraphResourceVersionId& lhs, const GpuGraphResourceVersionId& rhs)noexcept{
    return !(lhs == rhs);
}

// A resource set is a graph-owned immutable collection of imported resources. Task declarations may apply one
// uniform access contract to every member; the graph freezes that declaration into ordinary per-resource uses before
// compiler analysis, so sets never become an opaque runtime synchronization domain.
struct GpuGraphResourceSetId{
    u32 index = Limit<u32>::s_Max;
    u64 generation = 0u;

    [[nodiscard]] constexpr bool valid()const{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuGraphResourceSetId& lhs, const GpuGraphResourceSetId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
inline constexpr bool operator!=(const GpuGraphResourceSetId& lhs, const GpuGraphResourceSetId& rhs)noexcept{ return !(lhs == rhs); }

// Upload bytes belong to one graph generation just like task/resource handles. The blob is CPU-side ownership
// only: native recording copies it through the existing per-command-buffer staging allocator, after which normal
// upload-chunk retirement remains responsible for GPU lifetime.
struct GpuUploadBlobId{
    u32 index = Limit<u32>::s_Max;
    u64 generation = 0u;

    [[nodiscard]] constexpr bool valid()const{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuUploadBlobId& lhs, const GpuUploadBlobId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
inline constexpr bool operator!=(const GpuUploadBlobId& lhs, const GpuUploadBlobId& rhs)noexcept{
    return !(lhs == rhs);
}

// Pipelines use the same graph-generation contract as resources and tasks.  They deliberately identify a
// graph-owned retained handle rather than exposing a backend pointer to packet capture or future IR records.
namespace GpuGraphPipelineType{
    enum Enum : u8{
        Graphics,
        Compute,
        Meshlet,
        RayTracing,

        kCount,
    };
};

struct GpuGraphPipelineId{
    u32 index = Limit<u32>::s_Max;
    u64 generation = 0u;

    [[nodiscard]] constexpr bool valid()const{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuGraphPipelineId& lhs, const GpuGraphPipelineId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
inline constexpr bool operator!=(const GpuGraphPipelineId& lhs, const GpuGraphPipelineId& rhs)noexcept{
    return !(lhs == rhs);
}

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

// A packet is the compiler-generated unit of native recording and queue submission.  Its generation identifies one
// immutable compiled plan rather than the declared graph, so recompiling one unchanged graph still invalidates
// packet-local recording, submission, timing, hook, and capture handles.
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

// Resource versions provide semantic producer/consumer ordering without changing the physical state/access contract
// above. Every version declaration remains distinct even when multiple declarations name the same physical range.
struct GpuTaskResourceVersionUse{
    GpuGraphResourceVersionId version;
    GpuTaskResourceVersionRole::Enum role = GpuTaskResourceVersionRole::kCount;
};

// The range/state/access contract applies to every member of `resourceSet`. Sets deliberately do not retain an
// independent pseudo-resource: GpuTaskGraph expands them in declaration order into concrete GpuTaskResourceUse
// records, preserving the normal exact-resource hazard and barrier machinery.
struct GpuTaskResourceSetUse{
    GpuGraphResourceSetId resourceSet;
    GpuTaskResourceRange range;
    ResourceStates::Mask requiredState = ResourceStates::Unknown;
    GpuTaskResourceAccess::Enum access = GpuTaskResourceAccess::Read;
    bool hasIndependentStateSource = false;
};

struct GpuTaskDependencyEdge{
    GpuTaskId producer;
    GpuTaskId consumer;
    // Explicit edges intentionally carry no resource. Inferred edges preserve the first resource that established
    // the dependency; resource-use telemetry retains any additional overlapping uses.
    GpuGraphResourceId resource;
    // Version-derived dependencies retain the semantic version that established the edge. Other types leave it empty.
    GpuGraphResourceVersionId resourceVersion;
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
        // A terminal imported-resource requirement. Lowering performs a real transition when needed, then always
        // retains the requested state in the packet snapshot for the external consumer.
        TextureStateExport,
        BufferStateExport,
        // Acceleration structures lower through their typed backing allocations, but remain graph resources in
        // their own right.  These markers let the graph retain/export that backing state and pair queue-family
        // ownership without forcing every caller to import the backing Buffer as a second resource.
        AccelStructOwnershipRelease,
        AccelStructOwnershipAcquire,
        AccelStructStateExport,

        kCount,
    };
};

struct GpuCompiledBarrier{
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    ResourceStates::Mask before = ResourceStates::Unknown;
    ResourceStates::Mask after = ResourceStates::Unknown;
    GpuPhysicalQueueId sourceQueue;
    GpuPhysicalQueueId destinationQueue;
    GpuCompiledBarrierType::Enum type = GpuCompiledBarrierType::TextureTransition;
    // A first-use marker. A known graph declaration initializes an otherwise unknown native tracker from `before`.
    // An Unknown Read/ReadWrite first use instead requires an explicit native source at recording time; an imported
    // packet state handoff remains authoritative when CommandList::open already supplied one.
    bool isGraphInitialState = false;
    // Only the first use of an imported external ownership handoff consumes the descriptor-owned state source.
    // Later graph-internal ownership acquires use their producer packet snapshot instead.
    bool isInitialOwnerHandoff = false;
    // A same-state write hazard still requires native execution and memory dependencies even when the ordinary
    // mutable UAV-barrier policy is disabled. Initial-state, ownership, and export records never set this flag.
    bool forceMemoryDependency = false;
};
static_assert(sizeof(GpuCompiledBarrier) == 72u, "GpuCompiledBarrier should keep its compact runtime layout");

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

