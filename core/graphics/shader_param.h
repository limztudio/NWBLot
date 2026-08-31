// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    u32               clusterId; \
    u32               clusterFlags; \
    u32               triangleCount : 9;                 /* The number of triangles (max 256). */ \
    u32               vertexCount : 9;                   /* The number of vertices (max 256). */ \
    u32               positionTruncateBitCount : 6; \
    u32               indexFormat : 4; \
    u32               opacityMicromapIndexFormat : 4; \
    u32               baseGeometryIndexAndFlags;         /* Low 24 bits = base geometry index; high 8 bits = flags. */ \
    u16               indexBufferStride;                 /* indexBuffer element stride in bytes. */ \
    u16               vertexBufferStride;                /* vertexBuffer element stride in bytes. */ \
    u16               geometryIndexAndFlagsBufferStride; /* geometryIndexBuffer element stride in bytes. */ \
    u16               opacityMicromapIndexBufferStride;  /* opacityMicromapIndexBuffer element stride in bytes. */ \
    GpuVirtualAddress indexBuffer; \
    GpuVirtualAddress vertexBuffer; \
    GpuVirtualAddress geometryIndexAndFlagsBuffer;        /* Optional per-triangle geometry-index/flag data. */ \
    GpuVirtualAddress opacityMicromapArray;               /* Optional valid opacity-micromap array. */ \
    GpuVirtualAddress opacityMicromapIndexBuffer;         /* Optional opacity-micromap index buffer. */

struct IndirectTriangleClasArgs{
    NWB_INDIRECT_TRIANGLE_COMMON_ARGS_FIELDS
};

struct IndirectTriangleTemplateArgs{
    NWB_INDIRECT_TRIANGLE_COMMON_ARGS_FIELDS
    GpuVirtualAddress instantiationBoundingBoxLimit;     // Optional six-float position limit for template instantiations.
};

#undef NWB_INDIRECT_TRIANGLE_COMMON_ARGS_FIELDS

struct IndirectInstantiateTemplateArgs{
    u32                        clusterIdOffset;      // Added to each template cluster ID.
    u32                        geometryIndexOffset;  // Added to each template geometry index; the result must fit s_MaxGeometryIndex.
    GpuVirtualAddress          clusterTemplate;      // GPU address of the cluster template.
    GpuVirtualAddressAndStride vertexBuffer;         // Vertex positions used for instantiation.
};

struct IndirectArgs{
    u32               clusterCount; // Number of cluster references.
#if defined(__cplusplus)
    u32               clusterReferencesStride = sizeof(GpuVirtualAddress); // Byte stride between references; must be at least 8.
#else
    u32               clusterReferencesStride; // Byte stride between references; must be at least 8.
#endif
    GpuVirtualAddress clusterAddresses; // GPU address of the first constructed CLAS reference.
};
#if defined(__cplusplus)
static_assert(IsStandardLayout_V<IndirectArgs>, "IndirectArgs must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<IndirectArgs>, "IndirectArgs must stay GPU-uploadable");
static_assert(sizeof(IndirectArgs) == 16u, "IndirectArgs GPU layout drifted");
static_assert(alignof(IndirectArgs) == alignof(GpuVirtualAddress), "IndirectArgs GPU alignment drifted");
static_assert(offsetof(IndirectArgs, clusterCount) == 0u, "IndirectArgs::clusterCount layout drifted");
static_assert(offsetof(IndirectArgs, clusterReferencesStride) == 4u, "IndirectArgs::clusterReferencesStride layout drifted");
static_assert(offsetof(IndirectArgs, clusterAddresses) == 8u, "IndirectArgs::clusterAddresses layout drifted");
static_assert(IndirectArgs{}.clusterReferencesStride >= sizeof(GpuVirtualAddress), "IndirectArgs default cluster-reference stride is invalid");
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__cplusplus)
NWB_CORE_END
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

