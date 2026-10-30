// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>
#include <impl/ecs_render/raytrace/types.adhd.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/raytrace/instance_material.h>

#include <core/task/gpu/task_graph.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingState;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The material table, instance stream, and typed bytes share indices and offsets, so preflight freezes them against one storage generation.
// A fresh context retains all three ABI-coupled byte streams until Shadow Preparation accepts.
// Accepted software-cache reuse retains only the same immutable storage identity, counts, and hash.
class PreparedShadowMaterialContext final : NoCopy{
private:
    [[nodiscard]] static b8 StorageReady(const RendererRayTracingState& state, usize instanceMaterialCount, usize instanceCount, usize materialTypedByteCount)noexcept;


public:
    explicit PreparedShadowMaterialContext(Core::Alloc::GlobalArena& arena)noexcept
        : m_instanceMaterialBytes(arena)
        , m_instanceBytes(arena)
        , m_materialTypedBytes(arena)
    {}


public:
    [[nodiscard]] b8 capture(
        const RendererRayTracingState& state,
        PreparedShadowMaterialContextRoute::Enum route,
        b8 staticScene,
        u64 hash,
        Span<const NwbRtInstanceMaterialGpu> instanceMaterials,
        Span<const InstanceGpuData> instances,
        Span<const u8> materialTypedBytes
    );
    [[nodiscard]] b8 captureCacheReuse(const RendererRayTracingState& state, u64 hash, usize instanceMaterialCount, usize instanceCount, usize materialTypedByteCount)noexcept;
    void clear()noexcept;

    [[nodiscard]] Expected<PreparedShadowMaterialContextUploads> retainUploads(const RendererRayTracingState& state, Core::GpuTaskGraph& graph)const;
    // Publishes the accepted route hash; false when no fresh upload was pending.
    [[nodiscard]] b8 publishAcceptedUpload(RendererRayTracingState& state)const noexcept;

    [[nodiscard]] b8 storageMatches(const RendererRayTracingState& state)const noexcept;
    [[nodiscard]] b8 softwareSnapshotCovers(usize sceneInstanceCount)const noexcept;
    // A fresh upload stays valid; cache reuse still needs the accepted software hash over the same scene instances.
    [[nodiscard]] b8 softwareSnapshotAccepted(const RendererRayTracingState& state)const noexcept;
    [[nodiscard]] b8 ready()const noexcept{ return m_ready; }
    [[nodiscard]] b8 uploadRequired()const noexcept{ return m_uploadRequired; }
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
    b8 m_static = false;
    b8 m_ready = false;
    b8 m_uploadRequired = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
