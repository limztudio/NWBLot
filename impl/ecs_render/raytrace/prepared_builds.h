// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Material surface hooks in the shadow, caustic, and GI trace paths select texture assets through typed bindless
// slots. Keep the exact preflight-resolved handles beside the trace geometry so graph declaration never has to
// inspect mutable material caches while recording.
using PreparedShadowTraceMaterialSampledTextureVector = Vector<
    Core::TextureHandle,
    Core::Alloc::GlobalArena
>;


// A BLAS build derives its geometry directly from retained buffers. Keep the resolved operation rather than a
// MeshResources pointer: runtime meshes can be replaced or pruned between preflight and native recording.
struct PreparedMeshBlasBuild{
    Name meshName = NAME_NONE;
    Core::BufferHandle positionBuffer;
    Core::BufferHandle triangleIndexBuffer;
    Core::RayTracingAccelStructHandle blas;
    Core::BufferHandle blasBackingBuffer;
    u64 runtimeMeshVersion = 0u;
    usize positionByteSize = 0u;
    u32 vertexStride = 0u;
    u32 vertexCount = 0u;
    u32 indexCount = 0u;
    u32 refitsBeforeBuild = 0u;
    u32 refitsAfterBuild = 0u;
    bool runtimeMesh = false;
    bool firstBuild = false;
    bool backingFresh = false;
    bool performRefit = false;
};

using PreparedMeshBlasBuildVector = Vector<
    PreparedMeshBlasBuild,
    Core::Alloc::GlobalArena
>;


// The software mesh builder shares global sort/payload/counter storage, so one frozen operation retains both its
// mesh-local inputs/outputs and the exact shared scratch generation. Never retain MeshResources pointers: runtime
// geometry can be replaced or pruned between preflight and Shadow Preparation recording.
struct PreparedMeshSwBvhBuild{
    Name meshName = NAME_NONE;
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

using PreparedMeshSwBvhBuildVector = Vector<
    PreparedMeshSwBvhBuild,
    Core::Alloc::GlobalArena
>;


// The scene-level software traversal consumes one descriptor-table entry per distinct mesh. Retain owning buffer
// handles rather than the mutable raw tables rebuilt by the legacy recording path.
struct PreparedSceneSwBvhMesh{
    Name meshName = NAME_NONE;
    Core::BufferHandle nodeBuffer;
    Core::BufferHandle positionBuffer;
    Core::BufferHandle triangleIndexBuffer;
    Core::BufferHandle attributeBuffer;
    Core::GpuDescriptorHandle nodeHeapHandle;
    Core::GpuDescriptorHandle positionHeapHandle;
    Core::GpuDescriptorHandle triangleIndexHeapHandle;
    Core::GpuDescriptorHandle attributeHeapHandle;
    u64 runtimeMeshVersion = 0u;
    usize nodeByteSize = 0u;
    usize positionByteSize = 0u;
    usize triangleIndexByteSize = 0u;
    usize attributeByteSize = 0u;
    u32 primitiveCount = 0u;
    bool runtimeMesh = false;
};

using PreparedSceneSwBvhMeshVector = Vector<
    PreparedSceneSwBvhMesh,
    Core::Alloc::GlobalArena
>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

