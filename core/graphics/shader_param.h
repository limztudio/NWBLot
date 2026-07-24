// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#ifndef NWB_CORE_GRAPHICS_SHADER_PARAM_H
#define NWB_CORE_GRAPHICS_SHADER_PARAM_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/global.h>

#if defined(__cplusplus)
#include <cstddef>
#else
#include <core/common/module.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(__cplusplus)
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__cplusplus)
NWB_CORE_BEGIN
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef u64 GpuVirtualAddress;
struct GpuVirtualAddressAndStride{
    GpuVirtualAddress startAddress;
    u64 strideInBytes;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct IndirectInstanceDesc{
#if !defined(__cplusplus)
    float4 transform[3];
#else
    Float4 transform[3] = {};
#endif
    u32 instanceID : 24;
    u32 instanceMask : 8;
    u32 instanceContributionToHitGroupIndex : 24;
    u32 flags : 8;
    GpuVirtualAddress blasDeviceAddress;
};
#if defined(__cplusplus)
static_assert(IsStandardLayout_V<IndirectInstanceDesc>, "IndirectInstanceDesc must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<IndirectInstanceDesc>, "IndirectInstanceDesc must stay GPU-uploadable");
static_assert(sizeof(IndirectInstanceDesc) == 64u, "IndirectInstanceDesc GPU layout drifted");
static_assert(alignof(IndirectInstanceDesc) >= alignof(Float4), "IndirectInstanceDesc must stay SIMD-aligned");
static_assert((offsetof(IndirectInstanceDesc, transform) % alignof(Float4)) == 0, "IndirectInstanceDesc::transform must stay SIMD-aligned");
#endif

inline constexpr u32 s_ClasByteAlignment = 128;
inline constexpr u32 s_ClasMaxTriangles = 256;
inline constexpr u32 s_ClasMaxVertices = 256;
inline constexpr u32 s_MaxGeometryIndex = 16777215;

// CLAS construction and template construction share this ABI prefix exactly. Keep it macro-defined rather than using
// a C++ base type: this header is also consumed as a shader ABI and both structs must remain plain contiguous records.
#define NWB_INDIRECT_TRIANGLE_COMMON_ARGS_FIELDS \
    u32               clusterId;                         /* The user specified cluster Id. */ \
    u32               clusterFlags;                      /* Cluster operation flags. */ \
    u32               triangleCount : 9;                 /* The number of triangles (max 256). */ \
    u32               vertexCount : 9;                   /* The number of vertices (max 256). */ \
    u32               positionTruncateBitCount : 6;      /* The number of bits to truncate from position values. */ \
    u32               indexFormat : 4;                   /* The index-buffer element format. */ \
    u32               opacityMicromapIndexFormat : 4;    /* The opacity-micromap index format. */ \
    u32               baseGeometryIndexAndFlags;         /* Low 24 bits = base geometry index; high 8 bits = flags. */ \
    u16               indexBufferStride;                 /* indexBuffer element stride in bytes. */ \
    u16               vertexBufferStride;                /* vertexBuffer element stride in bytes. */ \
    u16               geometryIndexAndFlagsBufferStride; /* geometryIndexBuffer element stride in bytes. */ \
    u16               opacityMicromapIndexBufferStride;  /* opacityMicromapIndexBuffer element stride in bytes. */ \
    GpuVirtualAddress indexBuffer;                        /* CLAS index buffer. */ \
    GpuVirtualAddress vertexBuffer;                       /* CLAS vertex buffer. */ \
    GpuVirtualAddress geometryIndexAndFlagsBuffer;        /* Optional per-triangle geometry-index/flag data. */ \
    GpuVirtualAddress opacityMicromapArray;               /* Optional valid opacity-micromap array. */ \
    GpuVirtualAddress opacityMicromapIndexBuffer;         /* Optional opacity-micromap index buffer. */

struct IndirectTriangleClasArgs{
    NWB_INDIRECT_TRIANGLE_COMMON_ARGS_FIELDS
};

struct IndirectTriangleTemplateArgs{
    NWB_INDIRECT_TRIANGLE_COMMON_ARGS_FIELDS
    GpuVirtualAddress instantiationBoundingBoxLimit;     // (optional) Pointer to 6 floats representing the limits of the positions of any vertices the template will ever be instantiated with
};

#undef NWB_INDIRECT_TRIANGLE_COMMON_ARGS_FIELDS

struct IndirectInstantiateTemplateArgs{
    u32                        clusterIdOffset;      // The offset added to the clusterId stored in the Cluster template to calculate the final clusterId that will be written to the instantiated CLAS
    u32                        geometryIndexOffset;  // The offset added to the geometry index stored for each triangle in the Cluster template to calculate the final geometry index that will be written to the triangles of the instantiated CLAS, the resulting value may not exceed maxGeometryIndexValue both of this call, and the call used to construct the original cluster template referenced
    GpuVirtualAddress          clusterTemplate;      // Address of a previously built cluster template to be instantiated
    GpuVirtualAddressAndStride vertexBuffer;         // Vertex buffer with stride to use to fetch the vertex positions used for instantiation
};

struct IndirectArgs{
    u32                       clusterCount;     // The size of the array referenced by clusterVAs
    u32                       reserved;         // Reserved, must be 0
    GpuVirtualAddress         clusterAddresses; // Address of an array holding valid GPU addresses of previously constructed CLAS objects
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__cplusplus)
NWB_CORE_END
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

