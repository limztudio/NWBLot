
#pragma once


#include "framebuffer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace OpacityMicromapFormat{
    enum Enum : u8{
        OC1_2_State = 1,
        OC1_4_State = 2,
    };
};

namespace RayTracingOpacityMicromapBuildFlags{
    enum Mask : u8{
        None = 0,

        FastTrace = 1 << 0,
        FastBuild = 1 << 1,
        AllowCompaction = 1 << 2,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

struct RayTracingOpacityMicromapUsageCount{
    // Number of OMMs with the specified subdivision level and format.
    u32 count = 0;
    // Micro triangle count is 4^N, where N is the subdivision level.
    u32 subdivisionLevel = 0;
    // OMM input sub format.
    OpacityMicromapFormat::Enum format = OpacityMicromapFormat::OC1_2_State;
};

struct RayTracingOpacityMicromapDesc{
    Name debugName;
    bool trackLiveness = true;

    // OMM flags. Applies to all OMMs in array.
    RayTracingOpacityMicromapBuildFlags::Mask flags = RayTracingOpacityMicromapBuildFlags::None;
    // OMM counts for each subdivision level and format combination in the inputs.
    GraphicsVector<RayTracingOpacityMicromapUsageCount> counts;

    // Base pointer for raw OMM input data.
    // Individual OMMs must be 1B aligned, though natural alignment is recommended.
    // It's also recommended to try to organize OMMs together that are expected to be used spatially close together.
    Buffer* inputBuffer = nullptr;
    u64 inputBufferOffset = 0;

    // One entry per OMM matching the VkMicromapTriangleEXT layout.
    Buffer* perOmmDescs = nullptr;
    u64 perOmmDescsOffset = 0;

    explicit RayTracingOpacityMicromapDesc(GraphicsArena& arena)
        : counts(arena)
    {}

    constexpr RayTracingOpacityMicromapDesc& setDebugName(const Name& value){ debugName = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setTrackLiveness(bool value){ trackLiveness = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setFlags(RayTracingOpacityMicromapBuildFlags::Mask value){ flags = value; return *this; }
    RayTracingOpacityMicromapDesc& setCounts(const GraphicsVector<RayTracingOpacityMicromapUsageCount>& value){ counts = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setInputBuffer(Buffer* value){ inputBuffer = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setInputBufferOffset(u64 value){ inputBufferOffset = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setPerOmmDescs(Buffer* value){ perOmmDescs = value; return *this; }
    constexpr RayTracingOpacityMicromapDesc& setPerOmmDescsOffset(u64 value){ perOmmDescsOffset = value; return *this; }
};

typedef GraphicsBackend::Handle<RayTracingOpacityMicromap> RayTracingOpacityMicromapHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing AccelStruct


using AffineTransform = Float34;

inline constexpr AffineTransform s_identityTransform = []()constexpr noexcept{
    AffineTransform value{};
    value._11 = 1.f;
    value._22 = 1.f;
    value._33 = 1.f;
    return value;
}();
static_assert(sizeof(AffineTransform) == sizeof(f32) * 12u, "AffineTransform GPU layout drifted");
static_assert(alignof(AffineTransform) >= alignof(Float4), "AffineTransform must stay SIMD-aligned");

namespace RayTracingGeometryFlags{
    enum Mask : u8{
        None = 0,

        Opaque = 1 << 0,
        NoDuplicateAnyHitInvocation = 1 << 1,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

namespace RayTracingGeometryType{
    enum Enum : u8{
        Triangles = 0,
        AABBs = 1,
        Spheres = 2,
        Lss = 3,
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
    Buffer* indexBuffer = nullptr;   // make sure the first 2 fields in all Geometry
    Buffer* vertexBuffer = nullptr;  // structs are Buffer* for easier debugging
    u64 indexOffset = 0;
    u64 vertexOffset = 0;

    RayTracingOpacityMicromap* opacityMicromap = nullptr;
    Buffer* ommIndexBuffer = nullptr;
    u64 ommIndexBufferOffset = 0;
    const RayTracingOpacityMicromapUsageCount* pOmmUsageCounts = nullptr;

    u32 indexCount = 0;
    u32 vertexCount = 0;
    u32 vertexStride = 0;
    u32 numOmmUsageCounts = 0;
    Format::Enum indexFormat = Format::UNKNOWN;
    Format::Enum vertexFormat = Format::UNKNOWN;
    Format::Enum ommIndexFormat = Format::UNKNOWN;

    constexpr RayTracingGeometryTriangles& setIndexBuffer(Buffer* value){ indexBuffer = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexBuffer(Buffer* value){ vertexBuffer = value; return *this; }
    constexpr RayTracingGeometryTriangles& setIndexFormat(Format::Enum value){ indexFormat = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexFormat(Format::Enum value){ vertexFormat = value; return *this; }
    constexpr RayTracingGeometryTriangles& setIndexOffset(u64 value){ indexOffset = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexOffset(u64 value){ vertexOffset = value; return *this; }
    constexpr RayTracingGeometryTriangles& setIndexCount(u32 value){ indexCount = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexCount(u32 value){ vertexCount = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexStride(u32 value){ vertexStride = value; return *this; }
    constexpr RayTracingGeometryTriangles& setOpacityMicromap(RayTracingOpacityMicromap* value){ opacityMicromap = value; return *this; }
    constexpr RayTracingGeometryTriangles& setOmmIndexBuffer(Buffer* value){ ommIndexBuffer = value; return *this; }
    constexpr RayTracingGeometryTriangles& setOmmIndexBufferOffset(u64 value){ ommIndexBufferOffset = value; return *this; }
    constexpr RayTracingGeometryTriangles& setOmmIndexFormat(Format::Enum value){ ommIndexFormat = value; return *this; }
    constexpr RayTracingGeometryTriangles& setPOmmUsageCounts(const RayTracingOpacityMicromapUsageCount* value){ pOmmUsageCounts = value; return *this; }
    constexpr RayTracingGeometryTriangles& setNumOmmUsageCounts(u32 value){ numOmmUsageCounts = value; return *this; }
};

struct RayTracingGeometryAABBs{
    Buffer* buffer = nullptr;
    Buffer* reserved = nullptr;
    u64 offset = 0;
    u32 count = 0;
    u32 stride = 0;

    constexpr RayTracingGeometryAABBs& setBuffer(Buffer* value){ buffer = value; return *this; }
    constexpr RayTracingGeometryAABBs& setOffset(u64 value){ offset = value; return *this; }
    constexpr RayTracingGeometryAABBs& setCount(u32 value){ count = value; return *this; }
    constexpr RayTracingGeometryAABBs& setStride(u32 value){ stride = value; return *this; }
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

    constexpr RayTracingGeometrySpheres& setIndexBuffer(Buffer* value){ indexBuffer = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexBuffer(Buffer* value){ vertexBuffer = value; return *this; }
    constexpr RayTracingGeometrySpheres& setIndexFormat(Format::Enum value){ indexFormat = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexPositionFormat(Format::Enum value){ vertexPositionFormat = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexRadiusFormat(Format::Enum value){ vertexRadiusFormat = value; return *this; }
    constexpr RayTracingGeometrySpheres& setIndexOffset(u64 value){ indexOffset = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexPositionOffset(u64 value){ vertexPositionOffset = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexRadiusOffset(u64 value){ vertexRadiusOffset = value; return *this; }
    constexpr RayTracingGeometrySpheres& setIndexCount(u32 value){ indexCount = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexCount(u32 value){ vertexCount = value; return *this; }
    constexpr RayTracingGeometrySpheres& setIndexStride(u32 value){ indexStride = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexPositionStride(u32 value){ vertexPositionStride = value; return *this; }
    constexpr RayTracingGeometrySpheres& setVertexRadiusStride(u32 value){ vertexRadiusStride = value; return *this; }
};

namespace RayTracingGeometryLssPrimitiveFormat{
    enum Enum : u8{
        List = 0,
        SuccessiveImplicit = 1,
    };
};

namespace RayTracingGeometryLssEndcapMode{
    enum Enum : u8{
        None = 0,
        Chained = 1,
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

    constexpr RayTracingGeometryLss& setIndexBuffer(Buffer* value){ indexBuffer = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexBuffer(Buffer* value){ vertexBuffer = value; return *this; }
    constexpr RayTracingGeometryLss& setIndexFormat(Format::Enum value){ indexFormat = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexPositionFormat(Format::Enum value){ vertexPositionFormat = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexRadiusFormat(Format::Enum value){ vertexRadiusFormat = value; return *this; }
    constexpr RayTracingGeometryLss& setIndexOffset(u64 value){ indexOffset = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexPositionOffset(u64 value){ vertexPositionOffset = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexRadiusOffset(u64 value){ vertexRadiusOffset = value; return *this; }
    constexpr RayTracingGeometryLss& setIndexCount(u32 value){ indexCount = value; return *this; }
    constexpr RayTracingGeometryLss& setPrimitiveCount(u32 value){ primitiveCount = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexCount(u32 value){ vertexCount = value; return *this; }
    constexpr RayTracingGeometryLss& setIndexStride(u32 value){ indexStride = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexPositionStride(u32 value){ vertexPositionStride = value; return *this; }
    constexpr RayTracingGeometryLss& setVertexRadiusStride(u32 value){ vertexRadiusStride = value; return *this; }
    constexpr RayTracingGeometryLss& setPrimitiveFormat(RayTracingGeometryLssPrimitiveFormat::Enum value){ primitiveFormat = value; return *this; }
    constexpr RayTracingGeometryLss& setEndcapMode(RayTracingGeometryLssEndcapMode::Enum value){ endcapMode = value; return *this; }
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
    bool useTransform = false;

    RayTracingGeometryDesc()
        : geometryData{}
    {}

    RayTracingGeometryDesc& setTransform(const AffineTransform& value){ NWB_MEMCPY(&transform, sizeof(transform), &value, sizeof(AffineTransform)); useTransform = true; return *this; }
    constexpr RayTracingGeometryDesc& setFlags(RayTracingGeometryFlags::Mask value){ flags = value; return *this; }
    constexpr RayTracingGeometryDesc& setTriangles(const RayTracingGeometryTriangles& value){ geometryData.triangles = value; geometryType = RayTracingGeometryType::Triangles; return *this; }
    constexpr RayTracingGeometryDesc& setAABBs(const RayTracingGeometryAABBs& value){ geometryData.aabbs = value; geometryType = RayTracingGeometryType::AABBs; return *this; }
    constexpr RayTracingGeometryDesc& setSpheres(const RayTracingGeometrySpheres& value){ geometryData.spheres = value; geometryType = RayTracingGeometryType::Spheres; return *this; }
    constexpr RayTracingGeometryDesc& setLss(const RayTracingGeometryLss& value){ geometryData.lss = value; geometryType = RayTracingGeometryType::Lss; return *this; }
};

namespace RayTracingInstanceFlags{
    enum Mask : u32{
        None = 0,

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
    AffineTransform transform{};
    u32 instanceID : 24;
    u32 instanceMask : 8;
    u32 instanceContributionToHitGroupIndex : 24;
    RayTracingInstanceFlags::Mask flags : 8;
    union{
        RayTracingAccelStruct* bottomLevelAS; // for buildTopLevelAccelStruct
        u64 blasDeviceAddress; // for buildTopLevelAccelStructFromBuffer - use RayTracingAccelStruct::getDeviceAddress()
    };

    RayTracingInstanceDesc()
        : instanceID(0)
        , instanceMask(0)
        , instanceContributionToHitGroupIndex(0)
        , flags(RayTracingInstanceFlags::None)
        , bottomLevelAS(nullptr)
    {
        setTransform(s_identityTransform);
    }

    constexpr RayTracingInstanceDesc& setInstanceID(u32 value){ instanceID = value; return *this; }
    constexpr RayTracingInstanceDesc& setInstanceContributionToHitGroupIndex(u32 value){ instanceContributionToHitGroupIndex = value; return *this; }
    constexpr RayTracingInstanceDesc& setInstanceMask(u32 value){ instanceMask = value; return *this; }
    RayTracingInstanceDesc& setTransform(const AffineTransform& value){ NWB_MEMCPY(&transform, sizeof(transform), &value, sizeof(AffineTransform)); return *this; }
    constexpr RayTracingInstanceDesc& setFlags(RayTracingInstanceFlags::Mask value){ flags = value; return *this; }
    constexpr RayTracingInstanceDesc& setBLAS(RayTracingAccelStruct* value){ bottomLevelAS = value; return *this; }
};
static_assert(sizeof(RayTracingInstanceDesc) == 64, "sizeof(InstanceDesc) is supposed to be 64 bytes");
static_assert(sizeof(IndirectInstanceDesc) == sizeof(RayTracingInstanceDesc));

namespace RayTracingAccelStructBuildFlags{
    enum Mask : u8{
        None = 0,

        AllowUpdate = 1 << 0,
        AllowCompaction = 1 << 1,
        PreferFastTrace = 1 << 2,
        PreferFastBuild = 1 << 3,
        MinimizeMemory = 0x10,
        PerformUpdate = 0x20,

        // Allows a TLAS to include an instance that points at a null BLAS or has a zero instance mask.
        // Only affects local validation; it does not translate to backend AS build flags.
        AllowEmptyInstances = 0x80,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

struct RayTracingAccelStructDesc{
    usize topLevelMaxInstances = 0; // only applies when isTopLevel = true
    GraphicsVector<RayTracingGeometryDesc> bottomLevelGeometries; // only applies when isTopLevel = false
    Name debugName;
    RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None;
    bool trackLiveness = true;
    bool isTopLevel = false;
    bool isVirtual = false;

    explicit RayTracingAccelStructDesc(GraphicsArena& arena)
        : bottomLevelGeometries(arena)
    {}

    constexpr RayTracingAccelStructDesc& setTopLevelMaxInstances(usize value){ topLevelMaxInstances = value; isTopLevel = true; return *this; }
    RayTracingAccelStructDesc& addBottomLevelGeometry(const RayTracingGeometryDesc& value){ bottomLevelGeometries.push_back(value); isTopLevel = false; return *this; }
    constexpr RayTracingAccelStructDesc& setBuildFlags(RayTracingAccelStructBuildFlags::Mask value){ buildFlags = value; return *this; }
    constexpr RayTracingAccelStructDesc& setDebugName(const Name& value){ debugName = value; return *this; }
    constexpr RayTracingAccelStructDesc& setTrackLiveness(bool value){ trackLiveness = value; return *this; }
    constexpr RayTracingAccelStructDesc& setIsTopLevel(bool value){ isTopLevel = value; return *this; }
    constexpr RayTracingAccelStructDesc& setIsVirtual(bool value){ isVirtual = value; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing AccelStruct


typedef GraphicsBackend::Handle<RayTracingAccelStruct> RayTracingAccelStructHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing Clusters


namespace RayTracingClusterOperationType{
    enum Enum : u8{
        Move,                       // Moves CLAS, CLAS Templates, or Cluster BLAS
        ClasBuild,                  // Builds CLAS from clusters of triangles
        ClasBuildTemplates,         // Builds CLAS templates from triangles
        ClasInstantiateTemplates,   // Instantiates CLAS templates
        BlasBuild,                  // Builds Cluster BLAS from CLAS
    };
};

namespace RayTracingClusterOperationMoveType{
    enum Enum : u8{
        BottomLevel,                // Moved objects are Clustered BLAS
        ClusterLevel,               // Moved objects are CLAS
        Template,                   // Moved objects are Cluster Templates
    };
};

namespace RayTracingClusterOperationMode{
    enum Enum : u8{
        ImplicitDestinations,       // Provide total buffer space, driver places results within, returns VAs and actual sizes
        ExplicitDestinations,       // Provide individual target VAs, driver places them there, returns actual sizes
        GetSizes,                   // Get minimum size per element
    };
};

namespace RayTracingClusterOperationFlags{
    enum Mask : u8{
        None = 0,

        FastTrace = 1 << 0,
        FastBuild = 1 << 1,
        NoOverlap = 1 << 2,
        AllowOMM = 1 << 3,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

namespace RayTracingClusterOperationIndexFormat{
    enum Enum : u8{
        IndexFormat8bit = 1,
        IndexFormat16bit = 2,
        IndexFormat32bit = 4,
    };
};

struct RayTracingClusterOperationSizeInfo{
    u64 resultMaxSizeInBytes = 0;
    u64 scratchSizeInBytes = 0;
};

struct RayTracingClusterOperationMoveParams{
    RayTracingClusterOperationMoveType::Enum type = RayTracingClusterOperationMoveType::BottomLevel;
    u32 maxBytes = 0;
};

struct RayTracingClusterOperationClasBuildParams{
    // Vertex format accepted by the backend cluster acceleration structure implementation.
    Format::Enum vertexFormat = Format::RGB32_FLOAT;

    // Index of the last geometry in a single CLAS
    u32 maxGeometryIndex = 0;

    // Maximum number of unique geometries in a single CLAS
    u32 maxUniqueGeometryCount = 1;

    // Maximum number of triangles in a single CLAS
    u32 maxTriangleCount = 0;

    // Maximum number of vertices in a single CLAS
    u32 maxVertexCount = 0;

    // Maximum number of triangles summed over all CLAS (in the current cluster operation)
    u32 maxTotalTriangleCount = 0;

    // Maximum number of vertices summed over all CLAS (in the current cluster operation)
    u32 maxTotalVertexCount = 0;

    // Minimum number of bits to be truncated in vertex positions across all CLAS (in the current cluster operation)
    u32 minPositionTruncateBitCount = 0;
};

struct RayTracingClusterOperationBlasBuildParams{
    // Maximum number of CLAS references in a single BLAS
    u32 maxClasPerBlasCount = 0;

    // Maximum number of CLAS references summed over all BLAS (in the current cluster operation)
    u32 maxTotalClasCount = 0;
};

struct RayTracingClusterOperationParams{
    // Maximum number of acceleration structures (or templates) to build/instantiate/move
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

    u64 scratchSizeInBytes = 0;                            // Size of scratch resource returned by getClusterOperationSizeInfo() scratchSizeInBytes

    // Input Resources
    Buffer* inIndirectArgCountBuffer = nullptr;            // Buffer containing the number of AS to build, instantiate, or move
    u64 inIndirectArgCountOffsetInBytes = 0;               // Offset (in bytes) to where the count is in the inIndirectArgCountBuffer
    Buffer* inIndirectArgsBuffer = nullptr;                // Buffer of descriptor array of format IndirectTriangleClasArgs, IndirectTriangleTemplateArgs, IndirectInstantiateTemplateArgs
    u64 inIndirectArgsOffsetInBytes = 0;                   // Offset (in bytes) to where the descriptor array starts inIndirectArgsBuffer

    // In/Out Resources
    Buffer* inOutAddressesBuffer = nullptr;                // Array of addresseses of CLAS, CLAS Templates, or BLAS
    u64 inOutAddressesOffsetInBytes = 0;                   // Offset (in bytes) to where the addresses array starts in inOutAddressesBuffer

    // Output Resources
    Buffer* outSizesBuffer = nullptr;                      // Sizes (in bytes) of CLAS, CLAS Templates, or BLAS
    u64 outSizesOffsetInBytes = 0;                         // Offset (in bytes) to where the output sizes array starts in outSizesBuffer
    Buffer* outAccelerationStructuresBuffer = nullptr;     // Destination buffer for CLAS, CLAS Template, or BLAS data. Size must be calculated with getOperationSizeInfo or with the outSizesBuffer result of OperationMode::GetSizes
    u64 outAccelerationStructuresOffsetInBytes = 0;        // Offset (in bytes) to where the output acceleration structures starts in outAccelerationStructuresBuffer
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

