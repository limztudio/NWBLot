// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

