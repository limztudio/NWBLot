// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/rhi/command.h>
#include <core/graphics/rhi/raytracing.h>

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskCostHint{
    enum Enum : u8{
        Tiny,
        Small,
        Medium,
        Large,

        Count,
    };
};

namespace GpuGraphResourceType{
    enum Enum : u8{
        Texture,
        Buffer,
        AccelStruct,
        HazardDomain,

        Count,
    };
};

namespace GpuTaskResourceAccess{
    enum Enum : u8{
        Read,
        Write,
        ReadWrite,

        Count,
    };
};

namespace GpuGraphResourceVersionOrigin{
    enum Enum : u8{
        TaskProduced,
        ImportedRoot,

        Count,
    };
};

namespace GpuTaskResourceVersionRole{
    enum Enum : u8{
        Produce,
        Consume,

        Count,
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

        Count,
    };
};

// Edge flags ride in the existing telemetry payload; wire format and scheduling unchanged.
namespace GpuTaskGraphTelemetryEdgeFlag{
    static constexpr u8 s_GpuTaskGraphTelemetryEdgeFlagNoneBase = 0u;
    enum Mask : u8{
        None = s_GpuTaskGraphTelemetryEdgeFlagNoneBase,
        ExplicitDependency = 1u << 0u,
        InferredDependency = 1u << 1u,
        VersionDependency = 1u << 2u,
        VersionLifetime = 1u << 3u,
    };
};

// Queue-assignment summary flags supplement the detailed frame-graph assignment payload.
namespace GpuTaskGraphTelemetryNodeFlag{
    static constexpr u8 s_GpuTaskGraphTelemetryNodeFlagNoneBase = 0u;
    enum Mask : u8{
        None = s_GpuTaskGraphTelemetryNodeFlagNoneBase,
        AssignedGraphicsQueue = 1u << 0u,
        AssignedComputeQueue = 1u << 1u,
        AssignedDedicatedQueue = 1u << 2u,
        AssignedTransferQueue = 1u << 5u,
        QueueAssignmentRerouted = 1u << 6u,
        QueueAssignmentTimingRouting = 1u << 7u,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTaskId{
    // Globally unique per live graph generation, so handles cannot leak across live graphs or past reset().
    u64 generation = 0u;
    u32 index = Limit<u32>::s_Max;


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuTaskId& lhs, const GpuTaskId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr bool operator!=(const GpuTaskId& lhs, const GpuTaskId& rhs)noexcept{ return !(lhs == rhs); }

struct GpuGraphResourceId{
    u64 generation = 0u;
    u32 index = Limit<u32>::s_Max;


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuGraphResourceId& lhs, const GpuGraphResourceId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr bool operator!=(const GpuGraphResourceId& lhs, const GpuGraphResourceId& rhs)noexcept{ return !(lhs == rhs); }

struct GpuGraphResourceIdHasher{
    [[nodiscard]] usize operator()(const GpuGraphResourceId& resource)const noexcept{
        usize hash = Hasher<u64>{}(resource.generation);
        HashCombine(hash, resource.index);
        return hash;
    }
};

struct GpuGraphResourceVersionId{
    u64 generation = 0u;
    u32 index = Limit<u32>::s_Max;


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuGraphResourceVersionId& lhs, const GpuGraphResourceVersionId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr bool operator!=(const GpuGraphResourceVersionId& lhs, const GpuGraphResourceVersionId& rhs)noexcept{
    return !(lhs == rhs);
}

// A resource set is a graph-owned immutable collection. One uniform contract on members freezes into per-resource uses; sets never sync.
struct GpuGraphResourceSetId{
    u64 generation = 0u;
    u32 index = Limit<u32>::s_Max;


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuGraphResourceSetId& lhs, const GpuGraphResourceSetId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr bool operator!=(const GpuGraphResourceSetId& lhs, const GpuGraphResourceSetId& rhs)noexcept{ return !(lhs == rhs); }

// Upload bytes belong to one graph generation. Recording copies the blob via the staging allocator; chunk retirement owns GPU lifetime.
struct GpuUploadBlobId{
    u64 generation = 0u;
    u32 index = Limit<u32>::s_Max;


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuUploadBlobId& lhs, const GpuUploadBlobId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr bool operator!=(const GpuUploadBlobId& lhs, const GpuUploadBlobId& rhs)noexcept{
    return !(lhs == rhs);
}

// Pipelines use the same graph-generation contract as resources and tasks.
// They deliberately identify a graph-owned retained handle rather than exposing a backend pointer to packet capture or future IR records.
namespace GpuGraphPipelineType{
    enum Enum : u8{
        Graphics,
        Compute,
        Meshlet,
        RayTracing,

        Count,
    };
};

struct GpuGraphPipelineId{
    u64 generation = 0u;
    u32 index = Limit<u32>::s_Max;


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuGraphPipelineId& lhs, const GpuGraphPipelineId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr bool operator!=(const GpuGraphPipelineId& lhs, const GpuGraphPipelineId& rhs)noexcept{
    return !(lhs == rhs);
}

struct GpuExternalCompletionId{
    u64 generation = 0u;
    u32 index = Limit<u32>::s_Max;


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuExternalCompletionId& lhs, const GpuExternalCompletionId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr bool operator!=(const GpuExternalCompletionId& lhs, const GpuExternalCompletionId& rhs)noexcept{
    return !(lhs == rhs);
}

// A packet is the compiler unit of recording and submission. Its generation names one immutable plan; recompile invalidates packet-local handles.
struct GpuSubmissionPacketId{
    u64 generation = 0u;
    u32 index = Limit<u32>::s_Max;


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr bool operator==(const GpuSubmissionPacketId& lhs, const GpuSubmissionPacketId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr bool operator!=(const GpuSubmissionPacketId& lhs, const GpuSubmissionPacketId& rhs)noexcept{
    return !(lhs == rhs);
}

// A compiler-derived contiguous span of native packets.
// Consumers obtain ranges from GpuCompiledGraph instead of mirroring its topological indices in renderer scheduling code.
struct GpuSubmissionPacketRange{
    GpuSubmissionPacketId first;
    usize packetCount = 0u;


public:
    [[nodiscard]] constexpr bool valid()const noexcept{ return first.valid() && packetCount != 0u; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Texture ranges default to every subresource.
// TextureSubresourceSet{} is only mip zero / array slice zero, so it must never be used as the graph's implicit whole-texture range.
struct GpuTaskResourceRange{
    TextureSubresourceSet textureSubresources = s_AllSubresources;
    BufferRange bufferRange = s_EntireBuffer;
};

struct GpuTaskResourceUse{
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    ResourceStates::Mask requiredState = ResourceStates::Unknown;
    GpuTaskResourceAccess::Enum access = GpuTaskResourceAccess::Read;
    // The consumer brings an equivalent externally synced state source, so a matching concurrent read may skip the producer seed.
    // Accepted only for same-state Read-to-Read handoffs on compatible concurrent queues.
    bool hasIndependentStateSource = false;
};

// Resource versions provide semantic producer/consumer ordering without changing the physical state/access contract above.
// Every version declaration remains distinct even when multiple declarations name the same physical range.
struct GpuTaskResourceVersionUse{
    GpuGraphResourceVersionId version;
    GpuTaskResourceVersionRole::Enum role = GpuTaskResourceVersionRole::Count;
};

// The range/state/access contract covers every member of `resourceSet`. Sets expand in declaration order; no pseudo-resource.
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
    // Explicit edges intentionally carry no resource.
    // Inferred edges preserve the first resource that established the dependency; resource-use telemetry retains any additional overlapping uses.
    GpuGraphResourceId resource;
    // Version-derived dependencies retain the semantic version that established the edge. Other types leave it empty.
    GpuGraphResourceVersionId resourceVersion;
    GpuTaskHazardType::Enum hazard = GpuTaskHazardType::Unknown;
};

struct GpuTaskExternalDependencyEdge{
    GpuExternalCompletionId completion;
    GpuTaskId consumer;
};


// A compiled barrier names only engine resources, states, and resolved queues; the recorder lowers it via CommandList.
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
        // Lowering transitions when needed, then keeps the requested state in the packet snapshot for the external consumer.
        TextureStateExport,
        BufferStateExport,
        // Accel structs lower via typed backing allocs but stay graph resources.
        // These markers retain/export backing state and pair queue ownership without importing the Buffer separately.
        AccelStructOwnershipRelease,
        AccelStructOwnershipAcquire,
        AccelStructStateExport,

        Count,
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
    // A first-use marker. A known declaration inits an unknown tracker from `before`.
    // Unknown Read/ReadWrite first use needs a native source at record time; an imported handoff wins when open() gave one.
    bool isGraphInitialState = false;
    // Only the first use of an imported external ownership handoff consumes the descriptor-owned state source.
    // Later graph-internal ownership acquires use their producer packet snapshot instead.
    bool isInitialOwnerHandoff = false;
    // A same-state write hazard still requires native execution and memory dependencies even when the ordinary mutable UAV-barrier policy is disabled.
    // Initial-state, ownership, and export records never set this flag.
    bool forceMemoryDependency = false;
};
static constexpr usize s_GpuCompiledBarrierByteSize = 72u;
static_assert(sizeof(GpuCompiledBarrier) == s_GpuCompiledBarrierByteSize, "GpuCompiledBarrier should keep its compact runtime layout");

// A packet-state seed names the prior packet owning the native snapshot for one range. The recorder filters it to the declared range.
struct GpuPacketStateSeed{
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    GpuSubmissionPacketId sourcePacket;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

