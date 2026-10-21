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


namespace PreparedShadowMaterialContextRoute{
    enum Enum : u8{
        None,
        Hardware,
        Software,
    };
};

// The material table, instance stream, and typed bytes share indices and offsets, so preflight freezes them against one storage generation.
// A fresh context retains all three ABI-coupled byte streams until Shadow Preparation accepts.
// Accepted software-cache reuse retains only the same immutable storage identity, counts, and hash.
class PreparedShadowMaterialContext final : NoCopy{
private:
    [[nodiscard]] static bool StorageReady(const RendererRayTracingState& state, usize instanceMaterialCount, usize instanceCount, usize materialTypedByteCount)noexcept;


public:
    explicit PreparedShadowMaterialContext(Core::Alloc::GlobalArena& arena)noexcept
        : m_instanceMaterialBytes(arena)
        , m_instanceBytes(arena)
        , m_materialTypedBytes(arena)
    {}


public:
    [[nodiscard]] bool capture(
        const RendererRayTracingState& state,
        PreparedShadowMaterialContextRoute::Enum route,
        bool staticScene,
        u64 hash,
        const void* instanceMaterialData,
        usize instanceMaterialCount,
        usize instanceMaterialByteCount,
        const void* instanceData,
        usize instanceCount,
        usize instanceByteCount,
        const void* materialTypedData,
        usize materialTypedByteCount
    );
    [[nodiscard]] bool captureCacheReuse(const RendererRayTracingState& state, u64 hash, usize instanceMaterialCount, usize instanceCount, usize materialTypedByteCount)noexcept;
    void clear()noexcept;

    [[nodiscard]] Expected<PreparedShadowMaterialContextUploads> retainUploads(const RendererRayTracingState& state, Core::GpuTaskGraph& graph)const;
    // Publishes the accepted route hash; false when no fresh upload was pending.
    [[nodiscard]] bool publishAcceptedUpload(RendererRayTracingState& state)const noexcept;

    [[nodiscard]] bool storageMatches(const RendererRayTracingState& state)const noexcept;
    [[nodiscard]] bool softwareSnapshotCovers(usize sceneInstanceCount)const noexcept;
    // A fresh upload stays valid; cache reuse still needs the accepted software hash over the same scene instances.
    [[nodiscard]] bool softwareSnapshotAccepted(const RendererRayTracingState& state)const noexcept;
    [[nodiscard]] bool ready()const noexcept{ return m_ready; }
    [[nodiscard]] bool uploadRequired()const noexcept{ return m_uploadRequired; }
    [[nodiscard]] PreparedShadowMaterialContextRoute::Enum route()const noexcept{ return m_route; }


private:
    void captureStorage(const RendererRayTracingState& state, u64 hash, usize instanceMaterialCount, usize instanceCount, usize materialTypedByteCount)noexcept;


private:
    Vector<u8, Core::Alloc::GlobalArena> m_instanceMaterialBytes;
    Vector<u8, Core::Alloc::GlobalArena> m_instanceBytes;
    Vector<u8, Core::Alloc::GlobalArena> m_materialTypedBytes;
    Core::BufferHandle m_instanceMaterialBuffer;
    Core::BufferHandle m_instanceBuffer;
    Core::BufferHandle m_materialTypedBuffer;
    usize m_instanceMaterialCount = 0u;
    usize m_instanceCount = 0u;
    usize m_materialTypedUploadBytes = 0u;
    usize m_instanceMaterialCapacity = 0u;
    usize m_instanceCapacity = 0u;
    usize m_materialTypedCapacity = 0u;
    u64 m_hash = 0u;
    Core::GpuDescriptorHandle m_instanceMaterialHeapHandle;
    Core::GpuDescriptorHandle m_instanceHeapHandle;
    Core::GpuDescriptorHandle m_materialTypedHeapHandle;
    PreparedShadowMaterialContextRoute::Enum m_route = PreparedShadowMaterialContextRoute::None;
    bool m_static = false;
    bool m_ready = false;
    bool m_uploadRequired = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

