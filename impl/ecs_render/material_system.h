// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "material_typed_private.h"
#include "subsystem_base.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererResourceLookupMode{
    enum Enum : u8{
        CreateMissing,
        PreparedOnly,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem final : public RendererSystemSubsystemBase<RendererSystem>{
public:
    explicit RendererMaterialSystem(RendererSystem& renderer);

public:
    [[nodiscard]] static bool splitMaterialTypedBytesByClass(
        const Material& material,
        const Name& materialPath,
        MaterialTypedByteVector& outConstantTypedBytes,
        MaterialTypedByteVector& outMutableDefaultTypedBytes
    );
    [[nodiscard]] bool createMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo);
    [[nodiscard]] bool findMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo);
    void prepareVisibleMaterialSurfaceInfos();
    [[nodiscard]] bool createRendererPipeline(const MaterialSurfaceInfo& materialInfo, const MaterialPipelineKey& pipelineKey, Core::Framebuffer* framebuffer, MaterialPipelineResources*& outResources);
    [[nodiscard]] bool findRendererPipeline(const MaterialPipelineKey& pipelineKey, MaterialPipelineResources*& outResources);
    [[nodiscard]] bool hasTransparentRenderers(RendererResourceLookupMode::Enum lookupMode);
    void logMaterialRenderPathDecision(const Name& materialKey, RenderPath::Enum renderPath, bool meshSupported);
    [[nodiscard]] bool createMeshShaderResources();
    [[nodiscard]] bool createComputeEmulationResources();
    [[nodiscard]] bool createEmulationViewResources();
    void renderMaterialPass(
        Core::CommandList& commandList,
        Core::Framebuffer* framebuffer,
        MaterialPipelinePass::Enum pass,
        bool transparent,
        const CsgFrameState& csgFrameState,
        Core::BindingSet* passBindingSet,
        const AvboitFrameTargets* avboitTargets
    );
    [[nodiscard]] bool prepareMaterialPassResources(
        Core::Framebuffer* framebuffer,
        MaterialPipelinePass::Enum pass,
        bool transparent,
        const CsgFrameState& csgFrameState,
        const AvboitFrameTargets* avboitTargets
    );
    void gatherMaterialPassDrawItems(
        Core::Framebuffer* framebuffer,
        MaterialPipelinePass::Enum pass,
        bool transparent,
        const CsgFrameState& csgFrameState,
        MaterialPassDrawItemPartitions& drawItems,
        InstanceGpuDataVector& instanceData,
        CsgFrameGpuData& csgFrameData,
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector& materialTypedRanges,
#endif
        MaterialTypedByteDataVector& materialTypedBytes,
        RendererResourceLookupMode::Enum lookupMode
    );
    [[nodiscard]] static bool findMaterialInstanceOverrideField(
        Core::ECS::EntityID entity,
        const MaterialSurfaceInfo& materialInfo,
        const MaterialInstanceParameter& parameter,
        RendererMaterialInstanceOverrideField& outField
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
    // Packs one shadow occluder's material-constants context into a shadow-OWNED combined typed buffer (the draw
    // passes' g_NwbMaterialTypedWords / g_NwbMeshInstances hold only one pass's transparency class at trace time,
    // so the trace cannot read them). Appends this occluder's constant block + its per-instance mutable block into
    // inOutMaterialTypedBytes (mutable blocks deduped through inOutMutableRanges), builds the matching
    // InstanceGpuData (the mutable byte offset packs into translation.w, exactly as the draw pass does), and
    // returns the constant block's byte offset for the instance record's materialConstantByteOffset. Mirrors the
    // draw pass's per-instance packing (gatherMaterialPassDrawItems) so the trace's surface hook reads the same
    // bytes it would in the rasterizer.
    [[nodiscard]] bool appendShadowOccluderMaterialContext(
        Core::ECS::EntityID entity,
        const MaterialSurfaceInfo& materialInfo,
        const NWB::Impl::Scene::TransformComponent* transform,
        MaterialTypedByteDataVector& inOutMaterialTypedBytes,
        ECSRenderDetail::MaterialTypedByteContentRangeMap& inOutMutableRanges,
        InstanceGpuData& outInstance,
        u32& outConstantByteOffset
    );
    void pruneMaterialInstanceMutableCache();
    [[nodiscard]] bool materialPassDrawResourcesReady(const MeshResources& mesh)const;
    [[nodiscard]] bool materialPassDrawResourcesReady(const MaterialPassDrawItems& drawItems);
    [[nodiscard]] bool meshMaterialPassDrawResourcesReady(const MaterialPassDrawItemVector& drawItems);
    [[nodiscard]] bool computeMaterialPassDrawResourcesReady(const MaterialPassDrawItemVector& drawItems);
    [[nodiscard]] bool prepareMaterialPassDrawResources(const MaterialPassDrawItems& drawItems);
    [[nodiscard]] bool prepareMeshMaterialPassDrawResources(const MaterialPassDrawItemVector& drawItems);
    [[nodiscard]] bool prepareComputeMaterialPassDrawResources(const MaterialPassDrawItemVector& drawItems);
    [[nodiscard]] u32 meshDispatchFlags(const MeshResources& mesh, MaterialPipelinePass::Enum pass, bool twoSided, bool meshletConeCullScaleSafe)const;
    [[nodiscard]] u32 materialPassDrawDispatchFlags(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MeshResources& mesh)const;
    void setMaterialPassCommonBufferStates(Core::CommandList& commandList, const MeshResources& mesh);
    void setMaterialPassDrawPushConstants(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MeshResources& mesh);
    void renderMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItems& drawItems);
    void renderMeshMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);
    void renderComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);
    [[nodiscard]] bool reserveInstanceBufferCapacity(usize instanceCount);
    [[nodiscard]] bool reserveMaterialTypedBufferCapacity(usize byteCount);
    [[nodiscard]] bool prepareMaterialPassDrawBuffers(
        const InstanceGpuDataVector& instanceData,
        const MaterialTypedByteDataVector& materialTypedBytes
    );
    [[nodiscard]] bool materialPassDrawBuffersReady(
        const InstanceGpuDataVector& instanceData,
        const MaterialTypedByteDataVector& materialTypedBytes
    )const;
    [[nodiscard]] bool uploadInstanceBuffer(Core::CommandList& commandList, const InstanceGpuDataVector& instanceData);
    [[nodiscard]] bool uploadMaterialTypedBuffer(Core::CommandList& commandList, const MaterialTypedByteDataVector& materialTypedBytes);
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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

