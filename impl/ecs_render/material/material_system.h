// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/material_surface_lookup.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


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
    struct CsgGraphResourceSnapshot;
    struct MeshViewGpuData;
};

struct MaterialInstanceOverrideField{
    const MaterialTypedLayoutField& field;
    u32 blockByteBegin = 0u;
    b8 mutableBlock = false;
};

class CsgShapeRegistry;
template<typename OutputArena>
class MaterialSampledTextureCollector;
class RendererMaterialState;
class RendererShaderSystem;
class RendererMeshSystem;
class RendererCsgSystem;
class RendererOpticalVolumeSelection;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem final : public IMaterialSurfaceLookup, NoCopy{
public:
    [[nodiscard]] static Expected<MaterialInstanceOverrideField> FindMaterialInstanceOverrideField(Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceParameter& parameter);
    [[nodiscard]] static b8 ApplyMaterialInstanceOverrides(Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceComponent& materialInstance, Span<u8> inOutMutableTypedBytes);


private:
    static void SplitMaterialTypedBytesByClass(const Material& material, MaterialTypedByteVector& outConstantTypedBytes, MaterialTypedByteVector& outMutableDefaultTypedBytes);


public:
    RendererMaterialSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::GraphicsRuntime& graphics,
        Core::Assets::AssetManager& assetManager,
        CsgShapeRegistry& csgShapeRegistry,
        RendererMaterialState& materialState,
        RendererShaderSystem& shaderSystem,
        RendererMeshSystem& meshSystem,
        RendererCsgSystem& csgSystem,
        const RendererOpticalVolumeSelection& opticalVolumes
    )noexcept;


public:
    void invalidateResources();
    [[nodiscard]] Expected<NotNull<MaterialSurfaceInfo*>> createMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset);
    // Prepared-only lookup: creation and descriptor-backed resource resolution belong to preparation.
    [[nodiscard]] virtual Expected<NotNull<MaterialSurfaceInfo*>> findMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset)noexcept override;
    [[nodiscard]] b8 resolveMaterialResourceReferences(MaterialSurfaceInfo& materialInfo);
    [[nodiscard]] b8 prepareVisibleMaterialSurfaceInfos();
    void prepareVisibleMaterialInstanceMutableCache();
    [[nodiscard]] Expected<Core::BindingLayoutHandle> prepareMaterialPassBindingLayout();
    [[nodiscard]] Expected<NotNull<MaterialPipelineResources*>> createRendererPipeline(const MaterialSurfaceInfo& materialInfo, const MaterialPipelineKey& pipelineKey, Core::Framebuffer& framebuffer);
    [[nodiscard]] Expected<NotNull<MaterialPipelineResources*>> findRendererPipeline(const MaterialPipelineKey& pipelineKey)noexcept;
    void invalidateRendererPipelines()noexcept;
    [[nodiscard]] b8 hasTransparentRenderers(RendererResourceLookupMode::Enum lookupMode);
    void logMaterialRenderPathDecision(const Name& materialKey, RenderPath::Enum renderPath, b8 meshSupported);
    [[nodiscard]] b8 prepareMeshComputeBindingLayout();
    [[nodiscard]] b8 createComputeEmulationResources();
    [[nodiscard]] b8 createObjectGeometryPipelineResources(MaterialPipelineResources& resources);
    // Draw streams are gathered and uploaded before recording. This consumer performs no mesh/material updates.
    void renderPreparedMaterialPass(
        Core::CommandList& commandList,
        Core::Framebuffer* framebuffer,
        MaterialPipelinePass::Enum pass,
        const AvboitFrameTargets* avboitTargets,
        const MaterialPassDrawItemPartitionsView& drawItems,
        const CsgFrameGpuDataView& csgFrameData,
        const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        usize instanceCount,
        usize materialTypedByteCount,
        // The shared graph declares immutable mesh-view/material inputs; local phases establish these states.
        b8 materialFrameStatesGraphOwned = false,
        // The graph can also retain the per-mesh source buffer batch selected by the prepared draw stream.
        b8 materialGeometryStatesGraphOwned = false,
        // A preceding task generated every regular emulation output. Consume them as Combined inputs, not via the local bridge.
        b8 emulationOutputEntryStateGraphOwned = false,
        // The producer opens this established pass measure before its dispatches; the prepared raster closes it.
        // A missing measure means the producer intentionally no-op'd, so this consumer must not raster stale data.
        // AVBOIT may instead freeze a CSG-only alias-free producer. Keep it separate so mixed CSG keeps its interleaving.
        b8 csgEmulationOutputEntryStateGraphOwned = false,
        // A matching earlier producer permits raster timing independent of this pass.
        b8 emulationOutputReused = false,
        // The producer opens this established pass measure before its dispatches; the prepared raster closes it.
        // A missing measure means the producer intentionally no-op'd, so this consumer must not raster stale data.
        Optional<Core::GpuTimingMeasure>* emulationOutputTiming = nullptr
    );
    [[nodiscard]] b8 prepareMaterialPassResources(Core::Alloc::ScratchArena& scratchArena, Core::Framebuffer* framebuffer, MaterialPipelinePass::Enum pass, b8 transparent, const CsgFrameState& csgFrameState, const AvboitFrameTargets* avboitTargets);
    void gatherMaterialPassDrawItems(
        Core::Framebuffer* framebuffer,
        MaterialPipelinePass::Enum pass,
        b8 transparent,
        const CsgFrameState& csgFrameState,
        RendererResourceLookupMode::Enum lookupMode,
        // CSG graph preparation uses the immutable view payload; other gathers use the accepted CPU view.
        const ECSRenderDetail::MeshViewGpuData* csgWorkRegionMeshViewState,
        MaterialPassDrawItemPartitions& drawItems,
        InstanceGpuDataVector& instanceData,
        CsgFrameGpuData& csgFrameData,
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector& materialTypedRanges,
#endif
        MaterialTypedByteDataVector& materialTypedBytes
    );
    // Creation and override application belong to preparation; render paths only use the prepared lookup below.
    [[nodiscard]] Expected<Span<const u8>> prepareMaterialInstanceMutableTypedBytes(
        Core::ECS::EntityID entity,
        const MaterialSurfaceInfo& materialInfo,
        const MaterialInstanceComponent* materialInstance
    );
    [[nodiscard]] Expected<Span<const u8>> findPreparedMaterialInstanceMutableTypedBytes(Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceComponent* materialInstance)const noexcept;
    // Shadow-owned copy of one occluder's constants context (draw-pass buffers hold one transparency class at trace time).
    // Mirrors gatherMaterialPassDrawItems packing so the trace hook reads rasterizer-identical bytes.
    [[nodiscard]] Expected<ShadowOccluderMaterialContext> appendShadowOccluderMaterialContext(
        Core::ECS::EntityID entity,
        const MaterialSurfaceInfo& materialInfo,
        const NWB::Impl::Scene::TransformComponent* transform,
        MaterialTypedByteDataVector& inOutMaterialTypedBytes,
        ECSRenderDetail::MaterialTypedConstantRangeMap& inOutConstantRanges,
        ECSRenderDetail::MaterialTypedByteContentRangeMap& inOutMutableRanges
    );
    void pruneMaterialInstanceMutableCache()noexcept;
    [[nodiscard]] b8 materialPassDrawResourcesReady(const MaterialPassMeshResourceSnapshot& mesh, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)const noexcept;
    [[nodiscard]] b8 materialPassDrawResourcesReady(const MaterialPassDrawItemsView& drawItems, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)noexcept;
    [[nodiscard]] b8 meshMaterialPassDrawResourcesReady(Span<const MaterialPassDrawItem> drawItems, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)noexcept;
    [[nodiscard]] b8 indexedMaterialPassDrawResourcesReady(Span<const MaterialPassDrawItem> drawItems, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)noexcept;
    [[nodiscard]] b8 computeMaterialPassDrawResourcesReady(Span<const MaterialPassDrawItem> drawItems, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)noexcept;
    // Resolves persistent sampled textures selected by frozen draws.
    // Missing cached resources reject collection; asset and descriptor creation belong to preparation.
    [[nodiscard]] b8 appendPreparedMaterialSurfaceSampledTextures(const MaterialSurfaceInfo& materialInfo, MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& collector)const;
    [[nodiscard]] Expected<Vector<Core::TextureHandle, Core::Alloc::ScratchArena>> gatherPreparedMaterialPassSampledTextures(Span<const MaterialPassDrawItemsView> drawItemSets, Core::Alloc::ScratchArena& scratchArena)const;
    [[nodiscard]] b8 prepareMaterialPassResourceBindings(const MaterialPassDrawItemsView& drawItems)noexcept;
    [[nodiscard]] b8 prepareMeshMaterialPassResourceBindings(Span<const MaterialPassDrawItem> drawItems)noexcept;
    [[nodiscard]] b8 prepareIndexedMaterialPassResourceBindings(Span<const MaterialPassDrawItem> drawItems)noexcept;
    [[nodiscard]] b8 prepareComputeMaterialPassResourceBindings(Span<const MaterialPassDrawItem> drawItems)noexcept;
    [[nodiscard]] u32 meshDispatchFlags(const MaterialPassMeshResourceSnapshot& mesh, MaterialPipelinePass::Enum pass, b8 twoSided, b8 meshletConeCullScaleSafe)const noexcept;
    [[nodiscard]] u32 materialPassDrawDispatchFlags(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MaterialPassMeshResourceSnapshot& mesh)const noexcept;
    void setMaterialPassCommonBufferStates(const MaterialPassDrawContext& context, const MaterialPassMeshResourceSnapshot& mesh);
    [[nodiscard]] b8 setMaterialPassDrawPushConstants(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MaterialPassMeshResourceSnapshot& mesh);
    void dispatchComputeMaterialPassDrawItem(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MaterialPassMeshResourceSnapshot& mesh, const MaterialPassPipelineResourceSnapshot& pipelineResources);
    void drawComputeMaterialPassDrawItem(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MaterialPassMeshResourceSnapshot& mesh, const MaterialPassPipelineResourceSnapshot& pipelineResources);
    void renderMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemsView& drawItems);
    void renderIndexedMaterialPassDrawItems(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    void renderMeshMaterialPassDrawItems(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    // Records the fixed object-space decoder; the graph owns source reads and the cache UAV-to-vertex/index handoff.
    [[nodiscard]] b8 recordObjectGeometryDecode(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem);
    // Graph-only producer half. The graph owns generated output UAV states; this method records only the dispatch.
    void generateComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    // Graph-only raster half. The graph provides each output Combined; this method records state, heap, push constants, draws.
    void renderComputeMaterialPassDrawItemsRasterOnly(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    // Per-draw generation and rasterization retain the local UAV-to-vertex/index handoff when outputs alias.
    void renderComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    [[nodiscard]] b8 reserveInstanceBufferCapacity(usize instanceCount);
    [[nodiscard]] b8 reserveMaterialTypedBufferCapacity(usize byteCount);
    [[nodiscard]] b8 prepareMaterialPassDrawBuffers(Span<const InstanceGpuData> instanceData, Span<const u8> materialTypedBytes);
    [[nodiscard]] b8 materialPassDrawBuffersReady(Span<const InstanceGpuData> instanceData, Span<const u8> materialTypedBytes)const noexcept;
    [[nodiscard]] b8 materialPassDrawBuffersReady(usize instanceCount, usize materialTypedByteCount)const noexcept;
    [[nodiscard]] ECSRenderDetail::MaterialPassBufferSnapshot materialPassBufferSnapshot()const noexcept;
    // The CSG context descriptor is selected per instance heap-slot lane. Declaration patches the copy before recording.
    void prepareMaterialPassInstanceUploadData(const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources, Span<InstanceGpuData> instanceData)noexcept;
    [[nodiscard]] b8 prepareMaterialPassResourceBindingsImpl(Span<const MaterialPassDrawItem> drawItems, b8 computeEmulation)noexcept;


private:
    void releaseMaterialResourceReferences();


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::GraphicsRuntime& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    CsgShapeRegistry& m_csgShapeRegistry;
    RendererMaterialState& m_materialState;
    RendererShaderSystem& m_shaderSystem;
    RendererMeshSystem& m_meshSystem;
    RendererCsgSystem& m_csgSystem;
    const RendererOpticalVolumeSelection& m_opticalVolumes;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
