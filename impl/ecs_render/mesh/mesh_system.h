// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/mesh/renderer_mesh_types.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>

#include <core/assets/ref.h>
#include <core/ecs/entity_id.h>
#include <core/graphics/runtime/render_pass.h>
#include <impl/assets/graphics/mesh/binding_slots.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{
    struct MeshViewGpuData;
    struct MeshViewBufferUpload;
    struct MeshSoftwareBvhParentBuildState{
        Core::BufferHandle buffer;
        Name identity = s_NameNone;
    };
    struct MeshBlasGraphState{
        Name meshName = s_NameNone;
        Core::RayTracingAccelStructHandle blas;
        bool backingFresh = false;
        bool nativeBuildsBlas = false;
    };
    // Cross-domain view of accel resources; only RendererMeshSystem publishes changes.
    struct MeshRayTracingResourceSnapshot{
        Name meshName = s_NameNone;
        u64 runtimeMeshVersion = 0u;
        u64 runtimeGeometryContentRevision = 0u;
        u64 blasGeometryContentRevision = 0u;
        u64 swBvhGeometryContentRevision = 0u;
        Core::BufferHandle positionBuffer;
        Core::BufferHandle triangleIndexBuffer;
        Core::BufferHandle attributeBuffer;
        Core::BufferHandle runtimeLocalBoundsBuffer;
        // Complete accepted runtime tuple, or zero meshlets for the whole-caster path.
        Core::BufferHandle meshletDescBuffer;
        Core::BufferHandle meshletLocalBoundsBuffer;
        Core::RayTracingAccelStructHandle blas;
        Core::BufferHandle swBvhNodeBuffer;
        Core::BufferHandle swBvhParentBuffer;
        CsgReceiverCpuBounds csgLocalBounds;
        Core::GpuDescriptorHandle runtimeLocalBoundsHeapHandle = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle meshletDescHeapHandle = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle meshletLocalBoundsHeapHandle = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle swBvhPositionHeapHandle = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle swBvhTriangleIndexHeapHandle = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle swBvhNodeHeapHandle = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle swBvhParentHeapHandle = Core::GpuDescriptorHandle::Invalid();
        u32 meshletCount = 0u;
        u32 meshletPrimitiveIndexCount = 0u;
        u32 blasRefitsSinceRebuild = 0u;
        u32 swBvhRefitsSinceRebuild = 0u;
        bool runtimeMesh = false;
        bool blasBuildPending = false;
        bool blasBackingFresh = false;
        bool swBvhBuildPending = false;
        bool swBvhTopologyBuilt = false;
        bool blasBuildAccepted = false;
        bool swBvhBuildAccepted = false;
    };
    using MeshSoftwareBvhParentBuildStateVector = Vector<MeshSoftwareBvhParentBuildState, Core::Alloc::ScratchArena>;
    using MeshRetainedAccelerationStateBufferVector = Vector<Core::BufferHandle, Core::Alloc::ScratchArena>;
    using MeshBlasGraphStateVector = Vector<MeshBlasGraphState, Core::Alloc::ScratchArena>;
    using MeshRayTracingResourceSnapshotVector = Vector<MeshRayTracingResourceSnapshot, Core::Alloc::ScratchArena>;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMeshState;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMeshSystem final : NoCopy{
private:
    static constexpr u32 s_MeshPositionBindingSlot = NWB_MESH_BINDING_POSITION;
    static constexpr u32 s_MeshNormalBindingSlot = NWB_MESH_BINDING_NORMAL;
    static constexpr u32 s_MeshTangentBindingSlot = NWB_MESH_BINDING_TANGENT;
    static constexpr u32 s_MeshUv0BindingSlot = NWB_MESH_BINDING_UV0;
    static constexpr u32 s_MeshColorBindingSlot = NWB_MESH_BINDING_COLOR;
    static constexpr u32 s_MeshletDescBindingSlot = NWB_MESH_BINDING_MESHLET_DESC;
    static constexpr u32 s_MeshletBoundsBindingSlot = NWB_MESH_BINDING_MESHLET_BOUNDS;
    static constexpr u32 s_MeshletPositionRefBindingSlot = NWB_MESH_BINDING_MESHLET_POSITION_REFS;
    static constexpr u32 s_MeshletAttributeRefBindingSlot = NWB_MESH_BINDING_MESHLET_ATTRIBUTE_REFS;
    static constexpr u32 s_MeshletLocalVertexRefBindingSlot = NWB_MESH_BINDING_MESHLET_LOCAL_VERTEX_REFS;
    static constexpr u32 s_MeshletPrimitiveIndexBindingSlot = NWB_MESH_BINDING_MESHLET_PRIMITIVE_INDICES;


public:
    template<typename BindingHandler>
    static void ForEachMeshSourceBindingSlot(BindingHandler&& handler)noexcept(noexcept(handler(s_MeshPositionBindingSlot, false))){
        handler(s_MeshPositionBindingSlot, false);
        handler(s_MeshNormalBindingSlot, false);
        handler(s_MeshTangentBindingSlot, false);
        handler(s_MeshUv0BindingSlot, false);
        handler(s_MeshColorBindingSlot, false);
        handler(s_MeshletDescBindingSlot, false);
        handler(s_MeshletBoundsBindingSlot, true);
        handler(s_MeshletPositionRefBindingSlot, true);
        handler(s_MeshletAttributeRefBindingSlot, true);
        handler(s_MeshletLocalVertexRefBindingSlot, false);
        handler(s_MeshletPrimitiveIndexBindingSlot, true);
    }
    [[nodiscard]] static const Core::BufferHandle& MeshSourceBuffer(const MeshResources& mesh, u32 bindingSlot){
        switch(bindingSlot){
        case s_MeshPositionBindingSlot: return mesh.positionBuffer;
        case s_MeshNormalBindingSlot: return mesh.normalBuffer;
        case s_MeshTangentBindingSlot: return mesh.tangentBuffer;
        case s_MeshUv0BindingSlot: return mesh.uv0Buffer;
        case s_MeshColorBindingSlot: return mesh.colorBuffer;
        case s_MeshletDescBindingSlot: return mesh.meshletDescBuffer;
        case s_MeshletBoundsBindingSlot: return mesh.meshletBoundsBuffer;
        case s_MeshletPositionRefBindingSlot: return mesh.meshletPositionRefDeltaBuffer;
        case s_MeshletAttributeRefBindingSlot: return mesh.meshletAttributeRefDeltaBuffer;
        case s_MeshletLocalVertexRefBindingSlot: return mesh.meshletLocalVertexRefBuffer;
        case s_MeshletPrimitiveIndexBindingSlot: return mesh.meshletPrimitiveIndexBuffer;
        default:
            NWB_ASSERT(false);
            return mesh.positionBuffer;
        }
    }
    template<typename BufferHandler>
    static void ForEachMeshSourceBuffer(const MeshResources& mesh, BufferHandler&& handler){
        ForEachMeshSourceBindingSlot([&](const u32 bindingSlot, const bool rawView){
            handler(bindingSlot, MeshSourceBuffer(mesh, bindingSlot), rawView);
        });
    }


public:
    RendererMeshSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::GraphicsRuntime& graphics,
        Core::Assets::AssetManager& assetManager,
        RendererMeshState& meshState
    );


public:
    void invalidateResources();
    [[nodiscard]] Expected<MeshResources*> createMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset);
    [[nodiscard]] Expected<MeshResources*> findMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset);
    // Graph declaration resolves prepared keys without touching assets or mesh state.
    [[nodiscard]] Expected<MeshResources*> findMeshResources(const Name& meshKey);
    [[nodiscard]] Expected<MeshResources*> createRuntimeMeshResources(const RuntimeMeshDesc& desc);
    [[nodiscard]] Expected<MeshResources*> findRuntimeMeshResources(const RuntimeMeshDesc& desc);
    [[nodiscard]] bool prepareComputeEmulationResources(MeshResources& mesh);
    [[nodiscard]] bool prepareObjectGeometryCache(MeshResources& mesh, const Core::ComputePipelineHandle& decoderPipeline);
    [[nodiscard]] static ECSRenderDetail::ObjectGeometryCacheSnapshot ObjectGeometryCacheSnapshot(const MeshResources& mesh)noexcept;
    [[nodiscard]] bool confirmObjectGeometryCache(
        const Name& meshKey,
        const RuntimeMeshBuffers& sourceBuffers,
        const ECSRenderDetail::ObjectGeometryCacheSnapshot& expected,
        bool runtimeMesh
    );
    void pruneRuntimeMeshResources();
    void collectRayTracingResourceSnapshots(ECSRenderDetail::MeshRayTracingResourceSnapshotVector& outSnapshots)const;
    [[nodiscard]] Expected<ECSRenderDetail::MeshRayTracingResourceSnapshot> findRayTracingResourceSnapshot(
        const Name& meshName
    )const;
    [[nodiscard]] Expected<ECSRenderDetail::MeshRayTracingResourceSnapshot> findRenderableRayTracingResourceSnapshot(
        const RenderableMeshDesc& mesh
    )const;
    [[nodiscard]] bool commitRayTracingResourceSnapshot(
        const ECSRenderDetail::MeshRayTracingResourceSnapshot& expected,
        ECSRenderDetail::MeshRayTracingResourceSnapshot& desired
    );
    [[nodiscard]] Expected<ECSRenderDetail::MeshRayTracingResourceSnapshot> ensureRayTracingInputHeapHandles(
        const ECSRenderDetail::MeshRayTracingResourceSnapshot& expected
    );
    void discardRayTracingBuildState()noexcept;
    [[nodiscard]] Expected<ECSRenderDetail::MeshSoftwareBvhParentBuildStateVector> collectSoftwareBvhParentBuildStates(Core::Alloc::ScratchArena& arena)const;
    void collectRetainedAccelerationStateBuffers(ECSRenderDetail::MeshRetainedAccelerationStateBufferVector& outBuffers)const;
    void collectBlasGraphStates(ECSRenderDetail::MeshBlasGraphStateVector& outStates)const;
    [[nodiscard]] bool createMeshViewBuffer();
    [[nodiscard]] ECSRenderDetail::MeshViewBufferSnapshot meshViewBufferSnapshot()const noexcept;
    [[nodiscard]] Expected<Float44> snapshotAcceptedMeshViewWorldToClip()const noexcept;
    // Resolve the per-frame view payload; confirm the CPU mirror after packet accepts.
    [[nodiscard]] ECSRenderDetail::MeshViewBufferUpload prepareMeshViewBufferUpload(
        f32 fallbackAspectRatio
    )const;
    void confirmMeshViewBufferUpload(const ECSRenderDetail::MeshViewGpuData& viewState);
    void invalidateMeshViewBufferUploadMirror()noexcept;
    [[nodiscard]] bool prepareMeshFrameBindings(const ECSRenderDetail::MaterialPassBufferSnapshot& materialBuffers);
    [[nodiscard]] ECSRenderDetail::MeshFrameBindingSnapshot meshFrameBindingSnapshot()const;
    [[nodiscard]] bool meshGeometryHeapHandlesReady(const MeshResources& mesh)const;
    void populateMeshGeometryHeapSlots(InstanceGpuData& outInstance, const MeshResources& mesh)const;
    void releaseMeshGeometryHeapHandles(MeshResources& mesh);


private:
    void releaseMeshFrameHeapHandles();


private:
    // Mesh descriptors are established at creation; draw paths only consume ready handles.
    [[nodiscard]] bool createMeshRenderBindings(MeshResources& mesh);
    [[nodiscard]] bool meshRenderBindingsReady(const MeshResources& mesh)const;
    [[nodiscard]] bool createMeshGeometryHeapHandles(MeshResources& mesh);
    [[nodiscard]] bool ensureMeshSwBvhInputHeapHandles(MeshResources& mesh);

    void releaseAllMeshGeometryHeapHandles();


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::GraphicsRuntime& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    RendererMeshState& m_meshState;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

