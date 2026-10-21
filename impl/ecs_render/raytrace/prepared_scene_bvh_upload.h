// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingState;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The software scene hierarchy and its leaf instances share topology and leaf indices, so preflight freezes them as one batch against the selected storage generation.
// A fresh capture keeps node and instance bytes together for one graph upload; accepted static-cache reuse keeps only identity, counts, and hash.
class PreparedSceneBvhUpload final : NoCopy{
private:
    [[nodiscard]] static bool StorageReady(const RendererRayTracingState& state, usize nodeCount, usize instanceCount)noexcept;


public:
    explicit PreparedSceneBvhUpload(Core::Alloc::GlobalArena& arena)noexcept
        : m_nodeBytes(arena)
        , m_instanceBytes(arena)
    {}


public:
    [[nodiscard]] bool capture(const RendererRayTracingState& state, bool staticScene, u64 staticSceneHash, const void* nodeData, usize nodeCount, usize nodeByteCount, const void* instanceData, usize instanceCount, usize instanceByteCount);
    [[nodiscard]] bool captureCacheReuse(const RendererRayTracingState& state, u64 staticSceneHash, u32 instanceCount)noexcept;
    void clear()noexcept;

    [[nodiscard]] Expected<PreparedSceneBvhUploads> retainUploads(const RendererRayTracingState& state, Core::GpuTaskGraph& graph)const;
    // Publishes the accepted static-scene hash; false when no fresh upload was pending.
    [[nodiscard]] bool publishAcceptedUpload(RendererRayTracingState& state)const noexcept;

    [[nodiscard]] bool storageMatches(const RendererRayTracingState& state)const noexcept;
    [[nodiscard]] bool covers(usize sceneInstanceCount)const noexcept{ return m_ready && m_instanceCount == sceneInstanceCount; }
    // A fresh upload stays valid; cache reuse still needs the accepted static-scene hash over the same instances.
    [[nodiscard]] bool snapshotAccepted(const RendererRayTracingState& state)const noexcept;
    [[nodiscard]] bool ready()const noexcept{ return m_ready; }
    [[nodiscard]] bool uploadRequired()const noexcept{ return m_uploadRequired; }
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
    bool m_static = false;
    bool m_ready = false;
    bool m_uploadRequired = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

