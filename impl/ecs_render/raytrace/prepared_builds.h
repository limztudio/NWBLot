// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Material surface hooks in the shadow, caustic, and GI trace paths select texture assets through typed bindless slots.
// Keep preflight handles beside trace geometry; declaration never inspects mutable caches while recording.
using PreparedShadowTraceMaterialSampledTextureVector = Vector< Core::TextureHandle, Core::Alloc::GlobalArena >;


// A BLAS build derives geometry from retained buffers. Keep the resolved op, not a mesh pointer: meshes may prune before record.
struct PreparedMeshBlasBuild{
    Name meshName = s_NameNone;
    Core::BufferHandle positionBuffer;
    Core::BufferHandle triangleIndexBuffer;
    Core::RayTracingAccelStructHandle blas;
    Core::BufferHandle blasBackingBuffer;
    u64 runtimeMeshVersion = 0u;
    u64 geometryContentRevision = 0u;
    u64 acceptedGeometryContentRevision = 0u;
    usize positionByteSize = 0u;
    u32 vertexStride = 0u;
    u32 vertexCount = 0u;
    u32 indexCount = 0u;
    u32 refitsBeforeBuild = 0u;
    u32 refitsAfterBuild = 0u;
    bool runtimeMesh = false;
    bool buildPending = false;
    bool firstBuild = false;
    bool backingFresh = false;
    bool performRefit = false;
};

using PreparedMeshBlasBuildVector = Vector< PreparedMeshBlasBuild, Core::Alloc::GlobalArena >;


// The software builder shares global scratch; one frozen op retains mesh inputs/outputs plus the scratch generation.
// Never retain MeshResources pointers: runtime geometry can be replaced or pruned between preflight and Shadow Preparation recording.
struct PreparedMeshSwBvhBuild{
    Name meshName = s_NameNone;
    Core::BufferHandle positionBuffer;
    Core::BufferHandle triangleIndexBuffer;
    Core::BufferHandle nodeBuffer;
    Core::BufferHandle parentBuffer;
    Core::BufferHandle sortKeysBuffer;
    Core::BufferHandle sortPayloadBuffer;
    Core::BufferHandle visitCounterBuffer;
    Core::GpuDescriptorHandle positionHeapHandle;
    Core::GpuDescriptorHandle triangleIndexHeapHandle;
    Core::GpuDescriptorHandle nodeHeapHandle;
    Core::GpuDescriptorHandle parentHeapHandle;
    Core::GpuDescriptorHandle sortKeysHeapHandle;
    Core::GpuDescriptorHandle sortPayloadHeapHandle;
    Core::GpuDescriptorHandle visitCounterHeapHandle;
    Float3Int aabbMin;
    Float3Int aabbMax;
    u64 runtimeMeshVersion = 0u;
    u64 geometryContentRevision = 0u;
    u64 acceptedGeometryContentRevision = 0u;
    usize positionByteSize = 0u;
    usize indexByteSize = 0u;
    usize nodeByteSize = 0u;
    usize parentByteSize = 0u;
    usize sortKeysByteSize = 0u;
    usize sortPayloadByteSize = 0u;
    usize visitCounterByteSize = 0u;
    u32 primitiveCount = 0u;
    u32 refitsBeforeBuild = 0u;
    u32 refitsAfterBuild = 0u;
    bool runtimeMesh = false;
    bool buildPending = false;
    bool firstBuild = false;
    bool performRefit = false;
};

using PreparedMeshSwBvhBuildVector = Vector< PreparedMeshSwBvhBuild, Core::Alloc::GlobalArena >;


// Scene traversal consumes one descriptor entry per distinct mesh. Own the handles so recording sees preparation's set.
struct PreparedSceneSwBvhMesh{
    Name meshName = s_NameNone;
    Core::BufferHandle nodeBuffer;
    Core::BufferHandle positionBuffer;
    Core::BufferHandle triangleIndexBuffer;
    Core::BufferHandle attributeBuffer;
    Core::GpuDescriptorHandle nodeHeapHandle;
    Core::GpuDescriptorHandle positionHeapHandle;
    Core::GpuDescriptorHandle triangleIndexHeapHandle;
    Core::GpuDescriptorHandle attributeHeapHandle;
    u64 runtimeMeshVersion = 0u;
    u64 geometryContentRevision = 0u;
    usize nodeByteSize = 0u;
    usize positionByteSize = 0u;
    usize triangleIndexByteSize = 0u;
    usize attributeByteSize = 0u;
    u32 primitiveCount = 0u;
    bool runtimeMesh = false;
};

using PreparedSceneSwBvhMeshVector = Vector< PreparedSceneSwBvhMesh, Core::Alloc::GlobalArena >;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

