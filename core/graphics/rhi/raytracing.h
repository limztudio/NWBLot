// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "framebuffer.h"

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace OpacityMicromapFormat{
    static constexpr u8 s_OpacityMicromapFormatOC1_2_StateBase = 1;
    enum Enum : u8{
        OC1_2_State = s_OpacityMicromapFormatOC1_2_StateBase,
        OC1_4_State,
    };
};

namespace RayTracingOpacityMicromapBuildFlags{
    static constexpr u8 s_RayTracingOpacityMicromapBuildFlagsNoneBase = 0;
    enum Mask : u8{
        None = s_RayTracingOpacityMicromapBuildFlagsNoneBase,

        FastTrace = 1 << 0,
        FastBuild = 1 << 1,
        AllowCompaction = 1 << 2,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

struct RayTracingOpacityMicromapUsageCount{
    u32 count = 0;
    u32 subdivisionLevel = 0;
    OpacityMicromapFormat::Enum format = OpacityMicromapFormat::OC1_2_State;
};

struct RayTracingOpacityMicromapDesc{
    Name debugName;

    RayTracingOpacityMicromapBuildFlags::Mask flags = RayTracingOpacityMicromapBuildFlags::None;
    GraphicsVector<RayTracingOpacityMicromapUsageCount> counts;

    Buffer* inputBuffer = nullptr;
    u64 inputBufferOffset = 0;

    Buffer* perOmmDescs = nullptr;
    u64 perOmmDescsOffset = 0;


public:
    explicit RayTracingOpacityMicromapDesc(GraphicsArena& arena)noexcept
        : counts(arena)
    {}


public:
    constexpr RayTracingOpacityMicromapDesc& setDebugName(const Name& value)noexcept{ debugName = value; return *this; }

    constexpr RayTracingOpacityMicromapDesc& setFlags(RayTracingOpacityMicromapBuildFlags::Mask value)noexcept{ flags = value; return *this; }

};

typedef GraphicsBackend::Handle<RayTracingOpacityMicromap> RayTracingOpacityMicromapHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AffineTransform = Float34;
inline constexpr usize s_AffineTransformFloatCount = 12u;

inline constexpr AffineTransform s_IdentityTransform = []()constexpr noexcept{
    AffineTransform value{};
    value._11 = 1.f;
    value._22 = 1.f;
    value._33 = 1.f;
    return value;
}();
static_assert(sizeof(AffineTransform) == sizeof(f32) * s_AffineTransformFloatCount, "AffineTransform GPU layout drifted");
static_assert(alignof(AffineTransform) >= alignof(Float4), "AffineTransform must stay SIMD-aligned");

namespace RayTracingGeometryFlags{
    static constexpr u8 s_RayTracingGeometryFlagsNoneBase = 0;
    enum Mask : u8{
        None = s_RayTracingGeometryFlagsNoneBase,

        Opaque = 1 << 0,
        NoDuplicateAnyHitInvocation = 1 << 1,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

namespace RayTracingGeometryType{
    static constexpr u8 s_RayTracingGeometryTypeTrianglesBase = 0;
    enum Enum : u8{
        Triangles = s_RayTracingGeometryTypeTrianglesBase,
        AABBs,
        Spheres,
        Lss,
    };
};

struct RayTracingGeometryAABB{
    f32 minX;
    f32 minY;
    f32 minZ;
    f32 maxX;
    f32 maxY;
    f32 maxZ;
};

struct RayTracingGeometryTriangles{
    Buffer* indexBuffer = nullptr;
    Buffer* vertexBuffer = nullptr;
    u64 indexOffset = 0;
    u64 vertexOffset = 0;

    RayTracingOpacityMicromap* opacityMicromap = nullptr;
    Buffer* ommIndexBuffer = nullptr;
    u64 ommIndexBufferOffset = 0;
    const RayTracingOpacityMicromapUsageCount* ommUsageCounts = nullptr;

    u32 indexCount = 0;
    u32 vertexCount = 0;
    u32 vertexStride = 0;
    u32 numOmmUsageCounts = 0;
    Format::Enum indexFormat = Format::UNKNOWN;
    Format::Enum vertexFormat = Format::UNKNOWN;
    Format::Enum ommIndexFormat = Format::UNKNOWN;


public:
    constexpr RayTracingGeometryTriangles& setIndexBuffer(Buffer* value)noexcept{ indexBuffer = value; return *this; }
    constexpr RayTracingGeometryTriangles& setIndexBuffer(Buffer& value)noexcept{ indexBuffer = &value; return *this; }

    constexpr RayTracingGeometryTriangles& setVertexBuffer(Buffer* value)noexcept{ vertexBuffer = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexBuffer(Buffer& value)noexcept{ vertexBuffer = &value; return *this; }

    constexpr RayTracingGeometryTriangles& setIndexFormat(Format::Enum value)noexcept{ indexFormat = value; return *this; }

    constexpr RayTracingGeometryTriangles& setVertexFormat(Format::Enum value)noexcept{ vertexFormat = value; return *this; }

    constexpr RayTracingGeometryTriangles& setIndexCount(u32 value)noexcept{ indexCount = value; return *this; }

    constexpr RayTracingGeometryTriangles& setVertexCount(u32 value)noexcept{ vertexCount = value; return *this; }

    constexpr RayTracingGeometryTriangles& setVertexStride(u32 value)noexcept{ vertexStride = value; return *this; }

};

struct RayTracingGeometryAABBs{
    Buffer* buffer = nullptr;
    Buffer* reserved = nullptr;
    u64 offset = 0;
    u32 count = 0;
    u32 stride = 0;


public:
    constexpr RayTracingGeometryAABBs& setBuffer(Buffer* value)noexcept{ buffer = value; return *this; }
    constexpr RayTracingGeometryAABBs& setBuffer(Buffer& value)noexcept{ buffer = &value; return *this; }

    constexpr RayTracingGeometryAABBs& setOffset(u64 value)noexcept{ offset = value; return *this; }

    constexpr RayTracingGeometryAABBs& setCount(u32 value)noexcept{ count = value; return *this; }

};

struct RayTracingGeometrySpheres{
    Buffer* indexBuffer = nullptr;
    Buffer* vertexBuffer = nullptr;
    u64 indexOffset = 0;
    u64 vertexPositionOffset = 0;
    u64 vertexRadiusOffset = 0;

    u32 indexCount = 0;
    u32 vertexCount = 0;
    u32 indexStride = 0;
    u32 vertexPositionStride = 0;
    u32 vertexRadiusStride = 0;
    Format::Enum indexFormat = Format::UNKNOWN;
    Format::Enum vertexPositionFormat = Format::UNKNOWN;
    Format::Enum vertexRadiusFormat = Format::UNKNOWN;


public:
    constexpr RayTracingGeometrySpheres& setIndexBuffer(Buffer* value)noexcept{ indexBuffer = value; return *this; }
    constexpr RayTracingGeometrySpheres& setIndexBuffer(Buffer& value)noexcept{ indexBuffer = &value; return *this; }

    constexpr RayTracingGeometrySpheres& setVertexBuffer(Buffer* value)noexcept{ vertexBuffer = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexBuffer(Buffer& value)noexcept{ vertexBuffer = &value; return *this; }

    constexpr RayTracingGeometrySpheres& setIndexFormat(Format::Enum value)noexcept{ indexFormat = value; return *this; }

    constexpr RayTracingGeometrySpheres& setIndexCount(u32 value)noexcept{ indexCount = value; return *this; }

    constexpr RayTracingGeometrySpheres& setVertexCount(u32 value)noexcept{ vertexCount = value; return *this; }

};

namespace RayTracingGeometryLssPrimitiveFormat{
    static constexpr u8 s_RayTracingGeometryLssPrimitiveFormatListBase = 0;
    enum Enum : u8{
        List = s_RayTracingGeometryLssPrimitiveFormatListBase,
        SuccessiveImplicit,
    };
};

namespace RayTracingGeometryLssEndcapMode{
    static constexpr u8 s_RayTracingGeometryLssEndcapModeNoneBase = 0;
    enum Enum : u8{
        None = s_RayTracingGeometryLssEndcapModeNoneBase,
        Chained,
    };
};

struct RayTracingGeometryLss{
    Buffer* indexBuffer = nullptr;
    Buffer* vertexBuffer = nullptr;
    u64 indexOffset = 0;
    u64 vertexPositionOffset = 0;
    u64 vertexRadiusOffset = 0;

    u32 indexCount = 0;
    u32 primitiveCount = 0;
    u32 vertexCount = 0;
    u32 indexStride = 0;
    u32 vertexPositionStride = 0;
    u32 vertexRadiusStride = 0;
    Format::Enum indexFormat = Format::UNKNOWN;
    Format::Enum vertexPositionFormat = Format::UNKNOWN;
    Format::Enum vertexRadiusFormat = Format::UNKNOWN;
    RayTracingGeometryLssPrimitiveFormat::Enum primitiveFormat = RayTracingGeometryLssPrimitiveFormat::List;
    RayTracingGeometryLssEndcapMode::Enum endcapMode = RayTracingGeometryLssEndcapMode::None;


public:
    constexpr RayTracingGeometryLss& setIndexBuffer(Buffer* value)noexcept{ indexBuffer = value; return *this; }
    constexpr RayTracingGeometryLss& setIndexBuffer(Buffer& value)noexcept{ indexBuffer = &value; return *this; }

    constexpr RayTracingGeometryLss& setVertexBuffer(Buffer* value)noexcept{ vertexBuffer = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexBuffer(Buffer& value)noexcept{ vertexBuffer = &value; return *this; }

    constexpr RayTracingGeometryLss& setIndexFormat(Format::Enum value)noexcept{ indexFormat = value; return *this; }

    constexpr RayTracingGeometryLss& setIndexCount(u32 value)noexcept{ indexCount = value; return *this; }

    constexpr RayTracingGeometryLss& setVertexCount(u32 value)noexcept{ vertexCount = value; return *this; }

};

struct RayTracingGeometryDesc{
    AffineTransform transform{};

    union GeomTypeUnion{
        RayTracingGeometryTriangles triangles;
        RayTracingGeometryAABBs aabbs;
        RayTracingGeometrySpheres spheres;
        RayTracingGeometryLss lss;
    } geometryData;

    RayTracingGeometryFlags::Mask flags = RayTracingGeometryFlags::None;
    RayTracingGeometryType::Enum geometryType = RayTracingGeometryType::Triangles;
    b8 useTransform = false;


public:
    RayTracingGeometryDesc()noexcept
        : geometryData{}
    {}


public:
    RayTracingGeometryDesc& setTransform(const AffineTransform& value)noexcept{ FOUNDATION_MEMCPY(&transform, sizeof(transform), &value, sizeof(AffineTransform)); useTransform = true; return *this; }

    constexpr RayTracingGeometryDesc& setFlags(RayTracingGeometryFlags::Mask value)noexcept{ flags = value; return *this; }

    constexpr RayTracingGeometryDesc& setTriangles(const RayTracingGeometryTriangles& value)noexcept{ geometryData.triangles = value; geometryType = RayTracingGeometryType::Triangles; return *this; }

};

namespace RayTracingInstanceFlags{
    static constexpr auto s_RayTracingInstanceFlagsNoneBase = 0;
    enum Mask : u32{
        None = s_RayTracingInstanceFlagsNoneBase,

        TriangleCullDisable = 1 << 0,
        TriangleFrontCounterclockwise = 1 << 1,
        ForceOpaque = 1 << 2,
        ForceNonOpaque = 1 << 3,
        ForceOMM2State = 1 << 4,
        DisableOMMs = 1 << 5,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

struct RayTracingInstanceDesc{
    static constexpr usize s_ByteSize = 64u;

    AffineTransform transform{};
    u32 instanceID : 24;
    u32 instanceMask : 8;
    u32 instanceContributionToHitGroupIndex : 24;
    RayTracingInstanceFlags::Mask flags : 8;
    union{
        RayTracingAccelStruct* bottomLevelAS;
        u64 blasDeviceAddress;
    };


public:
    RayTracingInstanceDesc()noexcept
        : instanceID(0)
        , instanceMask(0)
        , instanceContributionToHitGroupIndex(0)
        , flags(RayTracingInstanceFlags::None)
        , bottomLevelAS(nullptr)
    {
        setTransform(s_IdentityTransform);
    }


public:
    constexpr RayTracingInstanceDesc& setInstanceID(u32 value)noexcept{ instanceID = value; return *this; }

    constexpr RayTracingInstanceDesc& setInstanceMask(u32 value)noexcept{ instanceMask = value; return *this; }

    RayTracingInstanceDesc& setTransform(const AffineTransform& value)noexcept{ FOUNDATION_MEMCPY(&transform, sizeof(transform), &value, sizeof(AffineTransform)); return *this; }

    constexpr RayTracingInstanceDesc& setFlags(RayTracingInstanceFlags::Mask value)noexcept{ flags = value; return *this; }

    [[nodiscard]] usize bottomLevelIdentity()const noexcept{ return reinterpret_cast<usize>(bottomLevelAS); }

    constexpr RayTracingInstanceDesc& setBottomLevelAS(RayTracingAccelStruct* value)noexcept{ bottomLevelAS = value; return *this; }
    constexpr RayTracingInstanceDesc& setBottomLevelAS(RayTracingAccelStruct& value)noexcept{ bottomLevelAS = &value; return *this; }
};
static_assert(sizeof(RayTracingInstanceDesc) == RayTracingInstanceDesc::s_ByteSize, "sizeof(InstanceDesc) is supposed to be 64 bytes");
static_assert(sizeof(IndirectInstanceDesc) == sizeof(RayTracingInstanceDesc));

namespace RayTracingAccelStructBuildFlags{
    static constexpr u8 s_RayTracingAccelStructBuildFlagsNoneBase = 0;
    enum Mask : u8{
        None = s_RayTracingAccelStructBuildFlagsNoneBase,

        AllowUpdate = 1 << 0,
        PreferFastTrace = 1 << 2,
        PreferFastBuild = 1 << 3,
        MinimizeMemory = 0x10,
        PerformUpdate = 0x20,

        AllowEmptyInstances = 0x80,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

struct RayTracingAccelStructDesc{
    usize topLevelMaxInstances = 0;
    GraphicsVector<RayTracingGeometryDesc> bottomLevelGeometries;
    Name debugName;
    RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None;
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
    b8 isTopLevel = false;
    b8 isVirtual = false;


public:
    explicit RayTracingAccelStructDesc(GraphicsArena& arena)noexcept
        : bottomLevelGeometries(arena)
    {}


public:
    constexpr RayTracingAccelStructDesc& setTopLevelMaxInstances(usize value)noexcept{ topLevelMaxInstances = value; isTopLevel = true; return *this; }

    RayTracingAccelStructDesc& addBottomLevelGeometry(const RayTracingGeometryDesc& value){ bottomLevelGeometries.push_back(value); isTopLevel = false; return *this; }

    constexpr RayTracingAccelStructDesc& setBuildFlags(RayTracingAccelStructBuildFlags::Mask value)noexcept{ buildFlags = value; return *this; }

    constexpr RayTracingAccelStructDesc& setDebugName(const Name& value)noexcept{ debugName = value; return *this; }

    constexpr RayTracingAccelStructDesc& setQueueSharing(ResourceQueueSharing::Mask value)noexcept{ queueSharing = value; return *this; }

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef GraphicsBackend::Handle<RayTracingAccelStruct> RayTracingAccelStructHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingClusterOperationType{
    enum Enum : u8{
        Move,
        ClasBuild,
        ClasBuildTemplates,
        ClasInstantiateTemplates,
        BlasBuild,
    };
};

namespace RayTracingClusterOperationMoveType{
    enum Enum : u8{
        BottomLevel,
        ClusterLevel,
        Template,
    };
};

namespace RayTracingClusterOperationMode{
    enum Enum : u8{
        ImplicitDestinations,
        ExplicitDestinations,
        GetSizes,
    };

    [[nodiscard]] constexpr b8 IsDestinationTopologyValid(const Enum mode, const b8 hasAddresses, const b8 hasSizes, const b8 hasImplicitDestination)noexcept{
        switch(mode){
        case ImplicitDestinations: return hasImplicitDestination;
        case ExplicitDestinations: return hasAddresses && hasSizes && !hasImplicitDestination;
        case GetSizes:             return !hasAddresses && hasSizes && !hasImplicitDestination;
        default:                   return false;
        }
    }
};

namespace RayTracingClusterOperationFlags{
    static constexpr u8 s_RayTracingClusterOperationFlagsNoneBase = 0;
    enum Mask : u8{
        None = s_RayTracingClusterOperationFlagsNoneBase,

        FastTrace = 1 << 0,
        FastBuild = 1 << 1,
        NoOverlap = 1 << 2,
        AllowOMM = 1 << 3,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

struct RayTracingClusterOperationSizeInfo{
    u64 resultMaxSizeInBytes = 0;
    u64 scratchSizeInBytes = 0;
};

struct RayTracingClusterOperationMoveParams{
    u32 maxBytes = 0;
    RayTracingClusterOperationMoveType::Enum type = RayTracingClusterOperationMoveType::BottomLevel;
};

struct RayTracingClusterOperationClasBuildParams{
    u32 maxGeometryIndex = 0;

    u32 maxUniqueGeometryCount = 1;

    u32 maxTriangleCount = 0;

    u32 maxVertexCount = 0;

    u32 maxTotalTriangleCount = 0;

    u32 maxTotalVertexCount = 0;

    u32 minPositionTruncateBitCount = 0;

    Format::Enum vertexFormat = Format::RGB32_FLOAT;
};

struct RayTracingClusterOperationBlasBuildParams{
    u32 maxClasPerBlasCount = 0;

    u32 maxTotalClasCount = 0;
};

struct RayTracingClusterOperationParams{
    u32 maxArgCount = 0;

    RayTracingClusterOperationType::Enum type = RayTracingClusterOperationType::Move;
    RayTracingClusterOperationMode::Enum mode = RayTracingClusterOperationMode::ImplicitDestinations;
    RayTracingClusterOperationFlags::Mask flags = RayTracingClusterOperationFlags::None;

    RayTracingClusterOperationMoveParams move;
    RayTracingClusterOperationClasBuildParams clas;
    RayTracingClusterOperationBlasBuildParams blas;
};

struct RayTracingClusterOperationDesc{
    RayTracingClusterOperationParams params;

    u64 scratchSizeInBytes = 0;

    Buffer* inIndirectArgCountBuffer = nullptr;
    u64 inIndirectArgCountOffsetInBytes = 0;
    Buffer* inIndirectArgsBuffer = nullptr;
    u64 inIndirectArgsOffsetInBytes = 0;

    Buffer* inOutAddressesBuffer = nullptr;
    u64 inOutAddressesOffsetInBytes = 0;

    Buffer* outSizesBuffer = nullptr;
    u64 outSizesOffsetInBytes = 0;
    Buffer* outAccelerationStructuresBuffer = nullptr;
    u64 outAccelerationStructuresOffsetInBytes = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

