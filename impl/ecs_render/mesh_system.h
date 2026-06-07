// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "subsystem_base.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMeshSystem final : public RendererSystemSubsystemBase<RendererSystem>{
public:
    explicit RendererMeshSystem(RendererSystem& renderer);

public:
    [[nodiscard]] bool createMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset, MeshResources*& outMesh);
    [[nodiscard]] bool createRuntimeMeshResources(const RuntimeMeshDesc& desc, MeshResources*& outMesh);
    void pruneRuntimeMeshResources();
    [[nodiscard]] bool createMeshViewBuffer();
    [[nodiscard]] bool updateMeshViewBuffer(Core::CommandList& commandList, f32 fallbackAspectRatio);
    void destroyMeshBindingSets();
    [[nodiscard]] bool createMeshBindingSet(MeshResources& mesh);
    [[nodiscard]] bool createComputeBindingSet(MeshResources& mesh);
    [[nodiscard]] bool meshFrameBindingResourcesReady(const tchar* context)const;
    void addMeshSourceBindingItems(Core::BindingSetDesc& bindingSetDesc, const MeshResources& mesh)const;
    void addMeshFrameBindingItems(Core::BindingSetDesc& bindingSetDesc)const;
    void addMeshDrawBindingItems(Core::BindingSetDesc& bindingSetDesc, const MeshResources& mesh)const;
    static void addMeshSourceBindingLayoutItems(Core::BindingLayoutDesc& bindingLayoutDesc);
    static void addMeshFrameBindingLayoutItems(Core::BindingLayoutDesc& bindingLayoutDesc);
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
    static constexpr u32 s_MeshMaterialTypedBindingSlot = NWB_MESH_BINDING_MATERIAL_TYPED;
    static constexpr u32 s_MeshletBoundsBindingSlot = NWB_MESH_BINDING_MESHLET_BOUNDS;
    static constexpr u32 s_MeshletPositionRefBindingSlot = NWB_MESH_BINDING_MESHLET_POSITION_REFS;
    static constexpr u32 s_MeshletAttributeRefBindingSlot = NWB_MESH_BINDING_MESHLET_ATTRIBUTE_REFS;
    static constexpr u32 s_MeshletLocalVertexRefBindingSlot = NWB_MESH_BINDING_MESHLET_LOCAL_VERTEX_REFS;
    static constexpr u32 s_MeshletPrimitiveIndexBindingSlot = NWB_MESH_BINDING_MESHLET_PRIMITIVE_INDICES;
    static constexpr u32 s_MeshInstanceBindingSlot = NWB_MESH_BINDING_INSTANCE;
    static constexpr u32 s_MeshViewBindingSlot = NWB_MESH_BINDING_VIEW;
    static constexpr u32 s_MeshGeneratedVertexBindingSlot = NWB_MESH_BINDING_GENERATED_VERTEX;
    static_assert(s_MeshMaterialTypedBindingSlot == 6u, "Mesh material typed payload binding must stay at slot 6");
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

