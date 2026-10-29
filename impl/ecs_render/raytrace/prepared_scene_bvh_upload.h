// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>

#include <core/task/gpu/task_graph.h>

#include <global/simdmath.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingState;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// CPU mirror of the shader BVH node.
struct NwbBvhNodeGpu{
    Float3UInt aabbMinLeftChild;
    Float3UInt aabbMaxRightChild;
};
static constexpr usize s_NwbBvhNodeGpuByteSize = 32u;
static_assert(sizeof(NwbBvhNodeGpu) == s_NwbBvhNodeGpuByteSize, "NwbBvhNodeGpu must match the shader NwbBvhNode std430 layout");

// CPU mirror of the software scene-BVH instance ABI.
struct SceneSwBvhInstanceGpu{
    Float34 worldToObject{};
    u32 reservedMeshIndex = 0u;
    u32 primitiveCount = 0u;
    u32 pad0 = 0u;
    u32 pad1 = 0u;
};
static_assert(sizeof(SceneSwBvhInstanceGpu) == sizeof(Float34) + sizeof(u32) * 4u, "SceneSwBvhInstanceGpu must stay a tight 64-byte record");

// Scene hierarchy and leaf instances share topology/indices; preflight freezes one batch per storage generation.
// A fresh capture keeps node and instance bytes together for one graph upload; accepted static-cache reuse keeps only identity, counts, and hash.
class PreparedSceneBvhUpload final : NoCopy{
private:
    [[nodiscard]] static b8 StorageReady(const RendererRayTracingState& state, usize nodeCount, usize instanceCount)noexcept;


public:
    explicit PreparedSceneBvhUpload(Core::Alloc::GlobalArena& arena)noexcept
        : m_nodeBytes(arena)
        , m_instanceBytes(arena)
    {}


public:
    [[nodiscard]] b8 capture(const RendererRayTracingState& state, b8 staticScene, u64 staticSceneHash, Span<const NwbBvhNodeGpu> nodes, Span<const SceneSwBvhInstanceGpu> instances);
    [[nodiscard]] b8 captureCacheReuse(const RendererRayTracingState& state, u64 staticSceneHash, u32 instanceCount)noexcept;
    void clear()noexcept;

    [[nodiscard]] Expected<PreparedSceneBvhUploads> retainUploads(const RendererRayTracingState& state, Core::GpuTaskGraph& graph)const;
    // Publishes the accepted static-scene hash; false when no fresh upload was pending.
    [[nodiscard]] b8 publishAcceptedUpload(RendererRayTracingState& state)const noexcept;

    [[nodiscard]] b8 storageMatches(const RendererRayTracingState& state)const noexcept;
    [[nodiscard]] b8 covers(usize sceneInstanceCount)const noexcept{ return m_ready && m_instanceCount == sceneInstanceCount; }
    // A fresh upload stays valid; cache reuse still needs the accepted static-scene hash over the same instances.
    [[nodiscard]] b8 snapshotAccepted(const RendererRayTracingState& state)const noexcept;
    [[nodiscard]] b8 ready()const noexcept{ return m_ready; }
    [[nodiscard]] b8 uploadRequired()const noexcept{ return m_uploadRequired; }
    [[nodiscard]] const Core::BufferHandle& nodeBuffer()const noexcept{ return m_nodeBuffer; }
    [[nodiscard]] Core::GpuDescriptorHandle nodeHeapHandle()const noexcept{ return m_nodeHeapHandle; }


private:
    Vector<u8, Core::Alloc::GlobalArena> m_nodeBytes;
    Vector<u8, Core::Alloc::GlobalArena> m_instanceBytes;
    Core::BufferHandle m_nodeBuffer;
    Core::BufferHandle m_instanceBuffer;
    usize m_nodeCount = 0u;
    usize m_instanceCount = 0u;
    usize m_nodeCapacity = 0u;
    usize m_instanceCapacity = 0u;
    u64 m_staticSceneHash = 0u;
    Core::GpuDescriptorHandle m_nodeHeapHandle;
    Core::GpuDescriptorHandle m_instanceHeapHandle;
    b8 m_static = false;
    b8 m_ready = false;
    b8 m_uploadRequired = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

