// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/subsystem_base.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{
    struct MeshFrameHeapSlots;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMeshSystem final : public RendererSystemSubsystemBase<RendererSystem>{
public:
    explicit RendererMeshSystem(RendererSystem& renderer);

public:
    [[nodiscard]] bool createMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset, MeshResources*& outMesh);
    [[nodiscard]] bool findMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset, MeshResources*& outMesh);
    [[nodiscard]] bool createRuntimeMeshResources(const RuntimeMeshDesc& desc, MeshResources*& outMesh);
    [[nodiscard]] bool findRuntimeMeshResources(const RuntimeMeshDesc& desc, MeshResources*& outMesh);
    void pruneRuntimeMeshResources();
    [[nodiscard]] bool createMeshViewBuffer();
    [[nodiscard]] bool updateMeshViewBuffer(Core::CommandList& commandList, f32 fallbackAspectRatio);
    [[nodiscard]] bool createComputeEmulationHeapHandle(MeshResources& mesh);
    [[nodiscard]] bool createMeshFrameHeapHandles();
    [[nodiscard]] bool meshFrameHeapHandlesReady()const;
    void populateMeshFrameHeapSlots(ECSRenderDetail::MeshFrameHeapSlots& outSlots)const;
    void releaseMeshFrameHeapHandles();
    [[nodiscard]] bool createMeshGeometryHeapHandles(MeshResources& mesh);
    [[nodiscard]] bool meshGeometryHeapHandlesReady(const MeshResources& mesh)const;
    void populateMeshGeometryHeapSlots(InstanceGpuData& outInstance, const MeshResources& mesh)const;
    [[nodiscard]] bool ensureMeshSwBvhInputHeapHandles(MeshResources& mesh);
    void releaseMeshGeometryHeapHandles(MeshResources& mesh);
    void releaseAllMeshGeometryHeapHandles();
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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

