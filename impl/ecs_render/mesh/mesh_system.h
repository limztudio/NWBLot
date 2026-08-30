// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/mesh/renderer_mesh_types.h>

#include <core/assets/ref.h>
#include <core/ecs/entity_id.h>
#include <core/graphics/render_pass.h>
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
    struct MeshFrameHeapSlots;
    struct MeshViewGpuData;

    struct MeshViewBufferSnapshot{
        Core::BufferHandle buffer;
        Core::GpuDescriptorHandle heapHandle = Core::GpuDescriptorHandle::invalid();

        [[nodiscard]] bool valid()const noexcept{ return static_cast<bool>(buffer); }
        [[nodiscard]] bool bindingValid()const noexcept{
            return
                buffer
                && heapHandle.valid()
                && heapHandle.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer
            ;
        }
    };
    // Mesh owns the descriptor-registration transaction for all three frame bindings.  This retained snapshot keeps
    // each buffer paired with the exact heap handle published in that transaction while a graph packet is pending.
    struct MeshFrameBindingSnapshot{
        Core::BufferHandle instanceBuffer;
        Core::BufferHandle materialTypedBuffer;
        MeshViewBufferSnapshot meshView;
        Core::GpuDescriptorHandle instanceHeapHandle = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle materialTypedHeapHandle = Core::GpuDescriptorHandle::invalid();
        usize instanceBufferCapacity = 0u;
        usize materialTypedBufferCapacity = 0u;

        [[nodiscard]] bool bindingValid()const noexcept{
            return
                instanceBuffer
                && materialTypedBuffer
                && meshView.bindingValid()
                && instanceHeapHandle.valid()
                && instanceHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
                && materialTypedHeapHandle.valid()
                && materialTypedHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
            ;
        }
        [[nodiscard]] bool frameReady(const usize instanceCount, const usize materialTypedByteCount)const noexcept{
            if(
                materialTypedByteCount == 0u
                || (materialTypedByteCount & (sizeof(u32) - 1u)) != 0u
            )
                return false;

            return
                bindingValid()
                && instanceBufferCapacity >= instanceCount
                && materialTypedBufferCapacity >= Max<usize>(materialTypedByteCount, sizeof(u32))
            ;
        }
    };
    struct MeshSoftwareBvhParentBuildState{
        Core::BufferHandle buffer;
        Name identity = NAME_NONE;
    };
    struct MeshBlasGraphState{
        Name meshName = NAME_NONE;
        Core::RayTracingAccelStructHandle blas;
        bool backingFresh = false;
        bool nativeBuildsBlas = false;
    };
    // Owning cross-domain view of mesh-bound acceleration resources. Consumers may prepare changes against this
    // value, but only RendererMeshSystem may publish them into the live mesh registry.
    struct MeshRayTracingResourceSnapshot{
        Name meshName = NAME_NONE;
        Core::BufferHandle positionBuffer;
        Core::BufferHandle triangleIndexBuffer;
        Core::BufferHandle attributeBuffer;
        Core::RayTracingAccelStructHandle blas;
        Core::GpuDescriptorHandle swBvhPositionHeapHandle = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle swBvhTriangleIndexHeapHandle = Core::GpuDescriptorHandle::invalid();
        Core::BufferHandle swBvhNodeBuffer;
        Core::BufferHandle swBvhParentBuffer;
        Core::GpuDescriptorHandle swBvhNodeHeapHandle = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle swBvhParentHeapHandle = Core::GpuDescriptorHandle::invalid();
        u32 meshletPrimitiveIndexCount = 0u;
        u32 blasRefitsSinceRebuild = 0u;
        u32 swBvhRefitsSinceRebuild = 0u;
        bool runtimeMesh = false;
        bool blasBuildPending = false;
        bool blasBackingFresh = false;
        bool blasBackingStateHandoffPending = false;
        bool swBvhBuildPending = false;
        bool swBvhTopologyBuilt = false;
        u64 runtimeMeshVersion = 0u;
        CsgReceiverCpuBounds csgLocalBounds;
    };
    using MeshSoftwareBvhParentBuildStateVector = Vector<MeshSoftwareBvhParentBuildState, Core::Alloc::ScratchArena>;
    using MeshRetainedAccelerationStateBufferVector = Vector<Core::BufferHandle, Core::Alloc::ScratchArena>;
    using MeshBlasGraphStateVector = Vector<MeshBlasGraphState, Core::Alloc::ScratchArena>;
    using MeshRayTracingResourceSnapshotVector = Vector<MeshRayTracingResourceSnapshot, Core::Alloc::ScratchArena>;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMeshState;
class RendererDrawState;


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
    static void forEachMeshSourceBindingSlot(BindingHandler&& handler){
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
    [[nodiscard]] static const Core::BufferHandle& meshSourceBuffer(const MeshResources& mesh, u32 bindingSlot){
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
    static void forEachMeshSourceBuffer(const MeshResources& mesh, BufferHandler&& handler){
        forEachMeshSourceBindingSlot([&](const u32 bindingSlot, const bool rawView){
            handler(bindingSlot, meshSourceBuffer(mesh, bindingSlot), rawView);
        });
    }

public:
    RendererMeshSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::Graphics& graphics,
        Core::Assets::AssetManager& assetManager,
        RendererMeshState& meshState,
        RendererDrawState& drawState
    );

public:
    void invalidateResources();
    [[nodiscard]] bool createMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset, MeshResources*& outMesh);
    [[nodiscard]] bool findMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset, MeshResources*& outMesh);
    // Graph declaration resolves already-prepared draw-item keys without touching assets or mutating mesh state.
    [[nodiscard]] bool findMeshResources(const Name& meshKey, MeshResources*& outMesh);
    [[nodiscard]] bool createRuntimeMeshResources(const RuntimeMeshDesc& desc, MeshResources*& outMesh);
    [[nodiscard]] bool findRuntimeMeshResources(const RuntimeMeshDesc& desc, MeshResources*& outMesh);
    void pruneRuntimeMeshResources();
    void collectRayTracingResourceSnapshots(ECSRenderDetail::MeshRayTracingResourceSnapshotVector& outSnapshots)const;
    [[nodiscard]] bool findRayTracingResourceSnapshot(
        const Name& meshName,
        ECSRenderDetail::MeshRayTracingResourceSnapshot& outSnapshot
    )const;
    [[nodiscard]] bool findRenderableRayTracingResourceSnapshot(
        const RenderableMeshDesc& mesh,
        ECSRenderDetail::MeshRayTracingResourceSnapshot& outSnapshot
    )const;
    [[nodiscard]] bool commitRayTracingResourceSnapshot(
        const ECSRenderDetail::MeshRayTracingResourceSnapshot& expected,
        ECSRenderDetail::MeshRayTracingResourceSnapshot& desired
    );
    [[nodiscard]] bool ensureRayTracingInputHeapHandles(
        const ECSRenderDetail::MeshRayTracingResourceSnapshot& expected,
        ECSRenderDetail::MeshRayTracingResourceSnapshot& outSnapshot
    );
    void confirmAcceptedRayTracingStateHandoffs()noexcept;
    void discardRayTracingBuildState()noexcept;
    [[nodiscard]] bool collectSoftwareBvhParentBuildStates(ECSRenderDetail::MeshSoftwareBvhParentBuildStateVector& outStates)const;
    void collectRetainedAccelerationStateBuffers(ECSRenderDetail::MeshRetainedAccelerationStateBufferVector& outBuffers)const;
    void collectBlasGraphStates(ECSRenderDetail::MeshBlasGraphStateVector& outStates)const;
    [[nodiscard]] bool createMeshViewBuffer();
    [[nodiscard]] ECSRenderDetail::MeshViewBufferSnapshot meshViewBufferSnapshot()const;
    [[nodiscard]] bool snapshotAcceptedMeshViewWorldToClip(Float44& outWorldToClip)const noexcept;
    // Resolves the immutable per-frame view payload before graph declaration.  The caller publishes it through a
    // graph-owned upload task, then confirms the CPU mirror only after that packet accepts.
    [[nodiscard]] bool prepareMeshViewBufferUpload(
        f32 fallbackAspectRatio,
        ECSRenderDetail::MeshViewGpuData& outViewState,
        bool& outUploadRequired
    )const;
    void confirmMeshViewBufferUpload(const ECSRenderDetail::MeshViewGpuData& viewState);
    void invalidateMeshViewBufferUploadMirror();
    [[nodiscard]] bool createMeshFrameHeapHandles();
    [[nodiscard]] bool meshFrameHeapHandlesReady()const;
    [[nodiscard]] ECSRenderDetail::MeshFrameBindingSnapshot meshFrameBindingSnapshot()const;
    void populateMeshFrameHeapSlots(ECSRenderDetail::MeshFrameHeapSlots& outSlots)const;
    void releaseMeshFrameHeapHandles();
    [[nodiscard]] bool meshGeometryHeapHandlesReady(const MeshResources& mesh)const;
    void populateMeshGeometryHeapSlots(InstanceGpuData& outInstance, const MeshResources& mesh)const;
    void releaseMeshGeometryHeapHandles(MeshResources& mesh);

private:
    // Persistent mesh descriptors are established while the resource is created.  Material preparation and draw
    // paths may only consume the ready handles so neither can allocate descriptor-heap entries mid-frame.
    [[nodiscard]] bool createMeshRenderBindings(MeshResources& mesh);
    [[nodiscard]] bool meshRenderBindingsReady(const MeshResources& mesh)const;
    [[nodiscard]] bool createComputeEmulationHeapHandle(MeshResources& mesh);
    [[nodiscard]] bool createMeshGeometryHeapHandles(MeshResources& mesh);
    [[nodiscard]] bool ensureMeshSwBvhInputHeapHandles(MeshResources& mesh);

private:
    void releaseAllMeshGeometryHeapHandles();

private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    RendererMeshState& m_meshState;
    RendererDrawState& m_drawState;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

