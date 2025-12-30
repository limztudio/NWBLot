// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <core/common/common.h>


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
    f32 transform[12];
#endif
    u32 instanceID : 24;
    u32 instanceMask : 8;
    u32 instanceContributionToHitGroupIndex : 24;
    u32 flags : 8;
    GpuVirtualAddress blasDeviceAddress;
};

static const u32 s_clasByteAlignment = 128;
static const u32 s_clasMaxTriangles = 256;
static const u32 s_clasMaxVertices = 256;
static const u32 s_maxGeometryIndex = 16777215;

struct IndirectTriangleClasArgs{
    u32               clusterId;                         // The user specified cluster Id to encode in the CLAS
    u32               clusterFlags;                      // Values of NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_CLUSTER_FLAGS to use as Cluster Flags
    u32               triangleCount : 9;                 // The number of triangles used by the CLAS (max 256)
    u32               vertexCount : 9;                   // The number of vertices used by the CLAS (max 256)
    u32               positionTruncateBitCount : 6;      // The number of bits to truncate from the position values
    u32               indexFormat : 4;                   // The index format to use for the indexBuffer, see NVAPI_3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT for possible values
    u32               opacityMicromapIndexFormat : 4;    // The index format to use for the opacityMicromapIndexBuffer, see NVAPI_3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT for possible values
    u32               baseGeometryIndexAndFlags;         // The base geometry index (lower 24 bit) and base geometry flags (NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAGS), see geometryIndexBuffer
    u16               indexBufferStride;                 // The stride of the elements of indexBuffer, in bytes
    u16               vertexBufferStride;                // The stride of the elements of vertexBuffer, in bytes
    u16               geometryIndexAndFlagsBufferStride; // The stride of the elements of geometryIndexBuffer, in bytes
    u16               opacityMicromapIndexBufferStride;  // The stride of the elements of opacityMicromapIndexBuffer, in bytes
    GpuVirtualAddress indexBuffer;                       // The index buffer to construct the CLAS
    GpuVirtualAddress vertexBuffer;                      // The vertex buffer to construct the CLAS
    GpuVirtualAddress geometryIndexAndFlagsBuffer;       // (optional) Address of an array of 32bit geometry indices and geometry flags with size equal to the triangle count.
    GpuVirtualAddress opacityMicromapArray;              // (optional) Address of a valid OMM array, if used NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_ALLOW_OMM must be set on this and all other cluster operation calls interacting with the object(s) constructed
    GpuVirtualAddress opacityMicromapIndexBuffer;        // (optional) Address of an array of indices into the OMM array
};

struct IndirectTriangleTemplateArgs{
    u32               clusterId;                         // The user specified cluster Id to encode in the cluster template
    u32               clusterFlags;                      // Values of NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_CLUSTER_FLAGS to use as Cluster Flags
    u32               triangleCount : 9;                 // The number of triangles used by the cluster template (max 256)
    u32               vertexCount : 9;                   // The number of vertices used by the cluster template (max 256)
    u32               positionTruncateBitCount : 6;      // The number of bits to truncate from the position values
    u32               indexFormat : 4;                   // The index format to use for the indexBuffer, must be one of nvrhi::rt::ClusteOperationIndexFormat
    u32               opacityMicromapIndexFormat : 4;    // The index format to use for the opacityMicromapIndexBuffer, see nvrhi::rt::ClusteOperationIndexFormat for possible values
    u32               baseGeometryIndexAndFlags;         // The base geometry index (lower 24 bit) and base geometry flags (NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAGS), see geometryIndexBuffer
    u16               indexBufferStride;                 // The stride of the elements of indexBuffer, in bytes
    u16               vertexBufferStride;                // The stride of the elements of vertexBuffer, in bytes
    u16               geometryIndexAndFlagsBufferStride; // The stride of the elements of geometryIndexBuffer, in bytes
    u16               opacityMicromapIndexBufferStride;  // The stride of the elements of opacityMicromapIndexBuffer, in bytes
    GpuVirtualAddress indexBuffer;                       // The index buffer to construct the cluster template
    GpuVirtualAddress vertexBuffer;                      // (optional) The vertex buffer to optimize the cluster template, the vertices will not be stored in the cluster template
    GpuVirtualAddress geometryIndexAndFlagsBuffer;       // (optional) Address of an array of 32bit geometry indices and geometry flags (each 32 bit value organized the same as baseGeometryIndex) with size equal to the triangle count, if non-zero the geometry indices of the CLAS triangles will be equal to the lower 24 bit of geometryIndexBuffer[triangleIndex] + baseGeometryIndex, the geometry flags for each triangle will be the bitwise OR of the flags in the upper 8 bits of baseGeometryIndex and geometryIndexBuffer[triangleIndex] otherwise all triangles will have a geometry index equal to baseGeometryIndex
    GpuVirtualAddress opacityMicromapArray;              // (optional) Address of a valid OMM array, if used NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_ALLOW_OMM must be set on this and all other cluster operation calls interacting with the object(s) constructed
    GpuVirtualAddress opacityMicromapIndexBuffer;        // (optional) Address of an array of indices into the OMM array
    GpuVirtualAddress instantiationBoundingBoxLimit;     // (optional) Pointer to 6 floats with alignment NVAPI_D3D12_RAYTRACING_CLUSTER_TEMPLATE_BOUNDS_BYTE_ALIGNMENT representing the limits of the positions of any vertices the template will ever be instantiated with
};

struct IndirectInstantiateTemplateArgs{
    u32                        clusterIdOffset;      // The offset added to the clusterId stored in the Cluster template to calculate the final clusterId that will be written to the instantiated CLAS
    u32                        geometryIndexOffset;  // The offset added to the geometry index stored for each triangle in the Cluster template to calculate the final geometry index that will be written to the triangles of the instantiated CLAS, the resulting value may not exceed maxGeometryIndexValue both of this call, and the call used to construct the original cluster template referenced
    GpuVirtualAddress          clusterTemplate;      // Address of a previously built cluster template to be instantiated
    GpuVirtualAddressAndStride vertexBuffer;         // Vertex buffer with stride to use to fetch the vertex positions used for instantiation
};

struct IndirectArgs{
    u32                       clusterCount;     // The size of the array referenced by clusterVAs
    u32                       reserved;         // Reserved, must be 0
    GpuVirtualAddress         clusterAddresses; // Address of an array of D3D12_GPU_VIRTUAL_ADDRESS holding valid addresses of CLAS previously constructed
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__cplusplus)
NWB_CORE_END
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

