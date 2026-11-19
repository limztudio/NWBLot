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
    u64 generation = 0u;
    u32 index = Limit<u32>::s_Max;


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr b8 operator==(const GpuTaskId& lhs, const GpuTaskId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr b8 operator!=(const GpuTaskId& lhs, const GpuTaskId& rhs)noexcept{ return !(lhs == rhs); }

struct GpuGraphResourceId{
    u64 generation = 0u;
    u32 index = Limit<u32>::s_Max;


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr b8 operator==(const GpuGraphResourceId& lhs, const GpuGraphResourceId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr b8 operator!=(const GpuGraphResourceId& lhs, const GpuGraphResourceId& rhs)noexcept{ return !(lhs == rhs); }

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
    [[nodiscard]] constexpr b8 valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr b8 operator==(const GpuGraphResourceVersionId& lhs, const GpuGraphResourceVersionId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr b8 operator!=(const GpuGraphResourceVersionId& lhs, const GpuGraphResourceVersionId& rhs)noexcept{
    return !(lhs == rhs);
}

struct GpuGraphResourceSetId{
    u64 generation = 0u;
    u32 index = Limit<u32>::s_Max;


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr b8 operator==(const GpuGraphResourceSetId& lhs, const GpuGraphResourceSetId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr b8 operator!=(const GpuGraphResourceSetId& lhs, const GpuGraphResourceSetId& rhs)noexcept{ return !(lhs == rhs); }

struct GpuUploadBlobId{
    u64 generation = 0u;
    u32 index = Limit<u32>::s_Max;


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr b8 operator==(const GpuUploadBlobId& lhs, const GpuUploadBlobId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr b8 operator!=(const GpuUploadBlobId& lhs, const GpuUploadBlobId& rhs)noexcept{
    return !(lhs == rhs);
}

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
    [[nodiscard]] constexpr b8 valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr b8 operator==(const GpuGraphPipelineId& lhs, const GpuGraphPipelineId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr b8 operator!=(const GpuGraphPipelineId& lhs, const GpuGraphPipelineId& rhs)noexcept{
    return !(lhs == rhs);
}

struct GpuExternalCompletionId{
    u64 generation = 0u;
    u32 index = Limit<u32>::s_Max;


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr b8 operator==(const GpuExternalCompletionId& lhs, const GpuExternalCompletionId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr b8 operator!=(const GpuExternalCompletionId& lhs, const GpuExternalCompletionId& rhs)noexcept{
    return !(lhs == rhs);
}

struct GpuSubmissionPacketId{
    u64 generation = 0u;
    u32 index = Limit<u32>::s_Max;


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return index != Limit<u32>::s_Max && generation != 0u; }
};
inline constexpr b8 operator==(const GpuSubmissionPacketId& lhs, const GpuSubmissionPacketId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline constexpr b8 operator!=(const GpuSubmissionPacketId& lhs, const GpuSubmissionPacketId& rhs)noexcept{
    return !(lhs == rhs);
}

struct GpuSubmissionPacketRange{
    GpuSubmissionPacketId first;
    usize packetCount = 0u;


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return first.valid() && packetCount != 0u; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTaskResourceRange{
    TextureSubresourceSet textureSubresources = s_AllSubresources;
    BufferRange bufferRange = s_EntireBuffer;
};

struct GpuTaskResourceUse{
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    ResourceStates::Mask requiredState = ResourceStates::Unknown;
    GpuTaskResourceAccess::Enum access = GpuTaskResourceAccess::Read;
    b8 hasIndependentStateSource = false;
};

struct GpuTaskResourceVersionUse{
    GpuGraphResourceVersionId version;
    GpuTaskResourceVersionRole::Enum role = GpuTaskResourceVersionRole::Count;
};

struct GpuTaskResourceSetUse{
    GpuGraphResourceSetId resourceSet;
    GpuTaskResourceRange range;
    ResourceStates::Mask requiredState = ResourceStates::Unknown;
    GpuTaskResourceAccess::Enum access = GpuTaskResourceAccess::Read;
    b8 hasIndependentStateSource = false;
};

struct GpuTaskDependencyEdge{
    GpuTaskId producer;
    GpuTaskId consumer;
    GpuGraphResourceId resource;
    GpuGraphResourceVersionId resourceVersion;
    GpuTaskHazardType::Enum hazard = GpuTaskHazardType::Unknown;
};

struct GpuTaskExternalDependencyEdge{
    GpuExternalCompletionId completion;
    GpuTaskId consumer;
};

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
        TextureStateExport,
        BufferStateExport,
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
    b8 isGraphInitialState = false;
    b8 isInitialOwnerHandoff = false;
    b8 forceMemoryDependency = false;
};
static constexpr usize s_GpuCompiledBarrierByteSize = 72u;
static_assert(sizeof(GpuCompiledBarrier) == s_GpuCompiledBarrierByteSize, "GpuCompiledBarrier should keep its compact runtime layout");

struct GpuPacketStateSeed{
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    GpuSubmissionPacketId sourcePacket;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

