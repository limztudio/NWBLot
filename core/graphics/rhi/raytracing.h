// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "framebuffer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Ray Tracing AccelStruct


using AffineTransform = Float34;
inline constexpr usize s_AffineTransformFloatCount = 12u;

inline constexpr AffineTransform s_identityTransform = []()constexpr noexcept{
    AffineTransform value{};
    value._11 = 1.f;
    value._22 = 1.f;
    value._33 = 1.f;
    return value;
}();
static_assert(sizeof(AffineTransform) == sizeof(f32) * s_AffineTransformFloatCount, "AffineTransform GPU layout drifted");
static_assert(alignof(AffineTransform) >= alignof(Float4), "AffineTransform must stay SIMD-aligned");

struct RayTracingGeometryTriangles{
    Buffer* indexBuffer = nullptr;   // make sure the first 2 fields in all Geometry
    Buffer* vertexBuffer = nullptr;  // structs are Buffer* for easier debugging
    u64 indexOffset = 0;
    u64 vertexOffset = 0;

    u32 indexCount = 0;
    u32 vertexCount = 0;
    u32 vertexStride = 0;
    Format::Enum indexFormat = Format::UNKNOWN;
    Format::Enum vertexFormat = Format::UNKNOWN;

    constexpr RayTracingGeometryTriangles& setIndexBuffer(Buffer* value){ indexBuffer = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexBuffer(Buffer* value){ vertexBuffer = value; return *this; }
    constexpr RayTracingGeometryTriangles& setIndexFormat(Format::Enum value){ indexFormat = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexFormat(Format::Enum value){ vertexFormat = value; return *this; }
    constexpr RayTracingGeometryTriangles& setIndexOffset(u64 value){ indexOffset = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexOffset(u64 value){ vertexOffset = value; return *this; }
    constexpr RayTracingGeometryTriangles& setIndexCount(u32 value){ indexCount = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexCount(u32 value){ vertexCount = value; return *this; }
    constexpr RayTracingGeometryTriangles& setVertexStride(u32 value){ vertexStride = value; return *this; }
};

struct RayTracingGeometryDesc{
    RayTracingGeometryTriangles triangles;

    constexpr RayTracingGeometryDesc& setTriangles(const RayTracingGeometryTriangles& value){ triangles = value; return *this; }
};

namespace RayTracingInstanceFlags{
    enum Mask : u32{
        None = 0,

        ForceOpaque = 1 << 2,
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
    RayTracingAccelStruct* bottomLevelAS = nullptr;

    RayTracingInstanceDesc()
        : instanceID(0)
        , instanceMask(0)
        , instanceContributionToHitGroupIndex(0)
        , flags(RayTracingInstanceFlags::None)
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
static_assert(sizeof(RayTracingInstanceDesc) == RayTracingInstanceDesc::s_ByteSize, "sizeof(InstanceDesc) is supposed to be 64 bytes");
static_assert(sizeof(IndirectInstanceDesc) == sizeof(RayTracingInstanceDesc));

namespace RayTracingAccelStructBuildFlags{
    enum Mask : u8{
        None = 0,

        AllowUpdate = 1 << 0,
        PreferFastTrace = 1 << 2,
        PerformUpdate = 0x20,
    };

    NWB_DEFINE_GRAPHICS_MASK_OPERATORS(Mask)
};

struct RayTracingAccelStructDesc{
    usize topLevelMaxInstances = 0; // only applies when isTopLevel = true
    GraphicsVector<RayTracingGeometryDesc> bottomLevelGeometries; // only applies when isTopLevel = false
    Name debugName;
    RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None;
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
    bool isTopLevel = false;

    explicit RayTracingAccelStructDesc(GraphicsArena& arena)
        : bottomLevelGeometries(arena)
    {}

    constexpr RayTracingAccelStructDesc& setTopLevelMaxInstances(usize value){ topLevelMaxInstances = value; isTopLevel = true; return *this; }
    RayTracingAccelStructDesc& addBottomLevelGeometry(const RayTracingGeometryDesc& value){ bottomLevelGeometries.push_back(value); isTopLevel = false; return *this; }
    constexpr RayTracingAccelStructDesc& setBuildFlags(RayTracingAccelStructBuildFlags::Mask value){ buildFlags = value; return *this; }
    constexpr RayTracingAccelStructDesc& setDebugName(const Name& value){ debugName = value; return *this; }
    constexpr RayTracingAccelStructDesc& setQueueSharing(ResourceQueueSharing::Mask value){ queueSharing = value; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing AccelStruct


typedef GraphicsBackend::Handle<RayTracingAccelStruct> RayTracingAccelStructHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
