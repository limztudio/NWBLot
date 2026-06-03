// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "subsystem_base.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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
    [[nodiscard]] bool createRendererPipeline(const MaterialSurfaceInfo& materialInfo, const MaterialPipelineKey& pipelineKey, Core::Framebuffer* framebuffer, MaterialPipelineResources*& outResources);
    [[nodiscard]] bool hasTransparentRenderers();
    void logMaterialRenderPathDecision(const Name& materialKey, RenderPath::Enum renderPath, bool meshSupported);
    [[nodiscard]] bool createMeshShaderResources();
    [[nodiscard]] bool createComputeEmulationResources();
    [[nodiscard]] bool createEmulationViewResources();
    [[nodiscard]] bool updateMeshViewBuffer(Core::CommandList& commandList, f32 fallbackAspectRatio);
    void renderMaterialPass(
        Core::CommandList& commandList,
        Core::Framebuffer* framebuffer,
        MaterialPipelinePass::Enum pass,
        bool transparent,
        const CsgFrameState& csgFrameState,
        Core::BindingSet* passBindingSet,
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
        MaterialTypedByteDataVector& materialTypedBytes
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
    void pruneMaterialInstanceMutableCache();
    [[nodiscard]] bool materialPassDrawResourcesReady(const MeshResources& mesh)const;
    [[nodiscard]] u32 meshDispatchFlags(const MeshResources& mesh, MaterialPipelinePass::Enum pass, bool twoSided, bool meshletConeCullScaleSafe)const;
    [[nodiscard]] u32 materialPassDrawDispatchFlags(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MeshResources& mesh)const;
    void setMaterialPassCommonBufferStates(Core::CommandList& commandList, const MeshResources& mesh);
    void setMaterialPassDrawPushConstants(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MeshResources& mesh);
    void renderMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItems& drawItems);
    void renderMeshMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);
    void renderComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);
    [[nodiscard]] bool reserveInstanceBufferCapacity(usize instanceCount);
    [[nodiscard]] bool reserveMaterialTypedBufferCapacity(usize byteCount);
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
