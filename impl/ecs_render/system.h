// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"
#include "material_instance.h"
#include "renderer_state.h"

#include <core/ecs/system.h>
#include <core/graphics/render_pass.h>
#include <impl/assets/graphics/mesh/binding_slots.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_csg/frame_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader;
class Mesh;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{
#if defined(NWB_DEBUG)
    struct MaterialTypedInstanceRangeVector;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystem final : public Core::ECS::ISystem, public Core::IRenderPass{
public:
    using ShaderPathResolveCallback = Function<bool(const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath)>;


public:
    RendererSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::Graphics& graphics,
        Core::Assets::AssetManager& assetManager,
        ShaderPathResolveCallback shaderPathResolver
    );
    virtual ~RendererSystem()override;


public:
    virtual void update(Core::ECS::World& world, f32 delta)override;

    virtual bool validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual void invalidateResources()override;
    virtual void render(Core::Framebuffer* framebuffer)override;

private:
    [[nodiscard]] bool createMeshResources(const Core::Assets::AssetRef<Mesh>& meshAsset, MeshResources*& outMesh);
    [[nodiscard]] bool createRuntimeMeshResources(const RuntimeMeshDesc& desc, MeshResources*& outMesh);
    void pruneRuntimeMeshResources();
    void destroyMeshBindingSets();
    [[nodiscard]] bool createMeshBindingSet(MeshResources& mesh);
    [[nodiscard]] bool createComputeBindingSet(MeshResources& mesh);
    [[nodiscard]] bool meshFrameBindingResourcesReady(const tchar* context)const;
    [[nodiscard]] bool materialPassDrawResourcesReady(const MeshResources& mesh)const;
    [[nodiscard]] u32 meshDispatchFlags(
        const MeshResources& mesh,
        MaterialPipelinePass::Enum pass,
        bool twoSided,
        bool meshletConeCullScaleSafe
    )const;
    [[nodiscard]] u32 materialPassDrawDispatchFlags(
        const MaterialPassDrawContext& context,
        const MaterialPassDrawItem& drawItem,
        const MeshResources& mesh
    )const;
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

private:
    [[nodiscard]] static bool splitMaterialTypedBytesByClass(
        const Material& material,
        const Name& materialPath,
        MaterialTypedByteVector& outConstantTypedBytes,
        MaterialTypedByteVector& outMutableDefaultTypedBytes
    );
    [[nodiscard]] bool createMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo);
    [[nodiscard]] bool createRendererPipeline(const MaterialSurfaceInfo& materialInfo, const MaterialPipelineKey& pipelineKey, Core::Framebuffer* framebuffer, MaterialPipelineResources*& outResources);
    [[nodiscard]] bool hasTransparentRenderers();
    void logMaterialRenderPathDecision(const Name& materialKey, RenderPath::Enum renderPath, bool meshSupported);

private:
    [[nodiscard]] bool createMeshShaderResources();
    [[nodiscard]] bool createComputeEmulationResources();
    [[nodiscard]] bool createEmulationViewResources();
    [[nodiscard]] bool updateMeshViewBuffer(Core::CommandList& commandList, f32 fallbackAspectRatio);
    void renderMaterialPass(
        Core::CommandList& commandList,
        Core::Framebuffer* framebuffer,
        MaterialPipelinePass::Enum pass,
        bool transparent,
        Core::BindingSet* passBindingSet,
        const AvboitFrameTargets* avboitTargets
    );
    void gatherMaterialPassDrawItems(
        Core::Framebuffer* framebuffer,
        MaterialPipelinePass::Enum pass,
        bool transparent,
        MaterialPassDrawItemPartitions& drawItems,
        InstanceGpuDataVector& instanceData,
        CsgFrameGpuData& csgFrameData,
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector& materialTypedRanges,
#endif
        MaterialTypedByteDataVector& materialTypedBytes
    );
    struct MaterialInstanceOverrideField{
        const MaterialTypedLayoutField* field = nullptr;
        u32 blockByteBegin = 0u;
        bool mutableBlock = false;
    };
    [[nodiscard]] static bool findMaterialInstanceOverrideField(
        Core::ECS::EntityID entity,
        const MaterialSurfaceInfo& materialInfo,
        const MaterialInstanceParameter& parameter,
        MaterialInstanceOverrideField& outField
    );
    [[nodiscard]] static bool applyMaterialInstanceOverrides(
        Core::ECS::EntityID entity,
        const MaterialSurfaceInfo& materialInfo,
        const MaterialInstanceComponent& materialInstance,
        MaterialTypedByteDataVector& inOutMutableTypedBytes
    );
    [[nodiscard]] bool resolveMaterialInstanceMutableTypedBytes(
        Core::ECS::EntityID entity,
        const MaterialSurfaceInfo& materialInfo,
        const MaterialInstanceComponent* materialInstance,
        const MaterialTypedByteVector*& outMutableTypedBytes
    );
    void pruneMaterialInstanceMutableCache();
    void setMaterialPassCommonBufferStates(Core::CommandList& commandList, const MeshResources& mesh);
    void setMaterialPassDrawPushConstants(
        const MaterialPassDrawContext& context,
        const MaterialPassDrawItem& drawItem,
        const MeshResources& mesh
    );
    void renderMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItems& drawItems);
    void renderMeshMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);
    void renderComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);

private:
    [[nodiscard]] bool createCsgClipResources();
    void destroyCsgClipBindingSet();
    [[nodiscard]] bool reserveCsgReceiverRangeBufferCapacity(usize rangeCount);
    [[nodiscard]] bool reserveCsgCutterBufferCapacity(usize cutterCount);
    [[nodiscard]] bool reserveCsgParameterByteBufferCapacity(usize byteCount);
    [[nodiscard]] bool uploadCsgFrameBuffers(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData);
    void setCsgClipBufferStates(Core::CommandList& commandList);
    [[nodiscard]] u32 countCsgReceiverClipCutters(const CsgFrameReceiverLookup& receiverLookup, Core::ECS::EntityID entity)const;
    [[nodiscard]] bool appendCsgReceiverClipData(
        const CsgFrameReceiverLookup& receiverLookup,
        Core::ECS::EntityID entity,
        CsgFrameGpuData& csgFrameData,
        CsgReceiverRangeGpuData& outRange
    )const;

private:
    [[nodiscard]] bool reserveInstanceBufferCapacity(usize instanceCount);
    [[nodiscard]] bool reserveMaterialTypedBufferCapacity(usize byteCount);
    [[nodiscard]] bool uploadInstanceBuffer(Core::CommandList& commandList, const InstanceGpuDataVector& instanceData);
    [[nodiscard]] bool uploadMaterialTypedBuffer(
        Core::CommandList& commandList,
        const MaterialTypedByteDataVector& materialTypedBytes
    );
    [[nodiscard]] bool uploadMaterialPassDrawBuffers(
        Core::CommandList& commandList,
        const InstanceGpuDataVector& instanceData,
#if defined(NWB_DEBUG)
        const ECSRenderDetail::MaterialTypedInstanceRangeVector& materialTypedRanges,
#endif
        const MaterialTypedByteDataVector& materialTypedBytes
    );
    [[nodiscard]] bool findMaterialPassDrawItemResources(
        const MaterialPassDrawItem& drawItem,
        MeshResources*& outMesh,
        MaterialPipelineResources*& outPipelineResources
    );
    template<typename DrawItemHandler>
    void forEachMaterialPassDrawItemResources(const MaterialPassDrawItemVector& drawItems, DrawItemHandler&& handler){
        for(const MaterialPassDrawItem& drawItem : drawItems){
            MeshResources* mesh = nullptr;
            MaterialPipelineResources* pipelineResources = nullptr;
            if(!findMaterialPassDrawItemResources(drawItem, mesh, pipelineResources))
                continue;

            handler(drawItem, *mesh, *pipelineResources);
        }
    }

private:
    [[nodiscard]] bool updateSceneShadingBuffer(Core::CommandList& commandList, f32 fallbackAspectRatio);
    [[nodiscard]] bool createDeferredLightingResources();
    [[nodiscard]] bool createDeferredLightingPipeline(DeferredFrameTargets& targets);
    [[nodiscard]] bool renderDeferredLighting(Core::CommandList& commandList, DeferredFrameTargets& targets);

private:
    [[nodiscard]] bool createDeferredFrameTargets(u32 width, u32 height);
    [[nodiscard]] bool createDeferredCompositeResources();
    [[nodiscard]] bool createDeferredCompositePipeline(Core::Framebuffer* presentationFramebuffer);
    void resetAvboitFrameTargets(AvboitFrameTargets& targets);
    void resetDeferredFrameTargets();
    void clearDeferredTargets(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool renderDeferredComposite(Core::CommandList& commandList, DeferredFrameTargets& targets, Core::Framebuffer* presentationFramebuffer);

private:
    [[nodiscard]] bool createAvboitResources();
    [[nodiscard]] bool createAvboitPipelines();
    [[nodiscard]] bool createAvboitFrameTargets(
        DeferredFrameTargets& createdTargets,
        Core::Format::Enum lowRasterFormat,
        Core::Format::Enum accumColorFormat,
        Core::Format::Enum accumExtinctionFormat,
        Core::Format::Enum transmittanceFormat
    );
    void clearAvboitTargets(Core::CommandList& commandList, AvboitFrameTargets& targets);
    void renderAvboitPasses(Core::CommandList& commandList, DeferredFrameTargets& targets);
    void dispatchAvboitDepthWarp(Core::CommandList& commandList, AvboitFrameTargets& targets);
    void dispatchAvboitIntegration(Core::CommandList& commandList, AvboitFrameTargets& targets);


private:
    [[nodiscard]] bool loadDeferredCompositeVertexShader();
    [[nodiscard]] bool loadShader(
        Core::ShaderHandle& outShader,
        const Name& shaderName,
        AStringView variantName,
        Core::ShaderType::Mask shaderType,
        const Name& debugName,
        const Name* archiveStageName = nullptr
    );


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    ShaderPathResolveCallback m_shaderPathResolver;

private:
    RendererMeshState m_meshState;
    RendererMaterialState m_materialState;
    RendererDrawState m_drawState;
    RendererCsgState m_csgState;
    RendererDeferredState m_deferredState;
    RendererAvboitState m_avboitState;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

