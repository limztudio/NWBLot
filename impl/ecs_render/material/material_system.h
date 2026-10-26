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


namespace RendererResourceLookupMode{
    enum Enum : u8{
        CreateMissing,
        PreparedOnly,
    };
};

namespace ECSRenderDetail{
    struct CsgGraphResourceSnapshot;
    struct MeshViewGpuData;
};

struct ShadowOccluderMaterialContext{
    InstanceGpuData instance;
    u32 constantByteOffset = 0u;
};

struct MaterialInstanceOverrideField{
    const MaterialTypedLayoutField& field;
    u32 blockByteBegin = 0u;
    bool mutableBlock = false;
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
    [[nodiscard]] static bool ApplyMaterialInstanceOverrides(Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceComponent& materialInstance, Span<u8> inOutMutableTypedBytes);


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
    [[nodiscard]] bool resolveMaterialResourceReferences(MaterialSurfaceInfo& materialInfo);
    [[nodiscard]] bool prepareVisibleMaterialSurfaceInfos();
    void prepareVisibleMaterialInstanceMutableCache();
    [[nodiscard]] Expected<Core::BindingLayoutHandle> prepareMaterialPassBindingLayout();
    [[nodiscard]] Expected<NotNull<MaterialPipelineResources*>> createRendererPipeline(const MaterialSurfaceInfo& materialInfo, const MaterialPipelineKey& pipelineKey, Core::Framebuffer& framebuffer);
    [[nodiscard]] Expected<NotNull<MaterialPipelineResources*>> findRendererPipeline(const MaterialPipelineKey& pipelineKey)noexcept;
    void invalidateRendererPipelines()noexcept;
    [[nodiscard]] bool hasTransparentRenderers(RendererResourceLookupMode::Enum lookupMode);
    void logMaterialRenderPathDecision(const Name& materialKey, RenderPath::Enum renderPath, bool meshSupported);
    [[nodiscard]] bool prepareMeshComputeBindingLayout();
    [[nodiscard]] bool createComputeEmulationResources();
    [[nodiscard]] bool createObjectGeometryPipelineResources(MaterialPipelineResources& resources);
    // Graph-owned draw streams are already gathered, patched, and uploaded before native recording. This consumer deliberately performs no mutable mesh-view or material/CSG buffer updates.
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
        bool materialFrameStatesGraphOwned = false,
        // The graph can also retain the per-mesh source buffer batch selected by the prepared draw stream.
        bool materialGeometryStatesGraphOwned = false,
        // An immediately preceding graph task generated every regular emulation output. Consume those buffers as Combined vertex/index inputs instead of replaying the local UAV bridge.
        bool emulationOutputEntryStateGraphOwned = false,
        // The producer opens this established pass measure before its dispatches; the prepared raster closes it.
        // A missing measure means the producer intentionally no-op'd, so this consumer must not raster stale data.
        // AVBOIT may instead freeze a CSG-only alias-free producer. Keep this separate from the regular handoff so a mixed CSG stream cannot suppress its local dispatch/raster interleaving.
        bool csgEmulationOutputEntryStateGraphOwned = false,
        // A matching earlier producer permits raster timing independent of this pass.
        bool emulationOutputReused = false,
        // The producer opens this established pass measure before its dispatches; the prepared raster closes it.
        // A missing measure means the producer intentionally no-op'd, so this consumer must not raster stale data.
        Optional<Core::GpuTimingMeasure>* emulationOutputTiming = nullptr
    );
    [[nodiscard]] bool prepareMaterialPassResources(Core::Alloc::ScratchArena& scratchArena, Core::Framebuffer* framebuffer, MaterialPipelinePass::Enum pass, bool transparent, const CsgFrameState& csgFrameState, const AvboitFrameTargets* avboitTargets);
    void gatherMaterialPassDrawItems(
        Core::Framebuffer* framebuffer,
        MaterialPipelinePass::Enum pass,
        bool transparent,
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
    [[nodiscard]] bool materialPassDrawResourcesReady(const MaterialPassMeshResourceSnapshot& mesh, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)const noexcept;
    [[nodiscard]] bool materialPassDrawResourcesReady(const MaterialPassDrawItemsView& drawItems, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)noexcept;
    [[nodiscard]] bool meshMaterialPassDrawResourcesReady(Span<const MaterialPassDrawItem> drawItems, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)noexcept;
    [[nodiscard]] bool indexedMaterialPassDrawResourcesReady(Span<const MaterialPassDrawItem> drawItems, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)noexcept;
    [[nodiscard]] bool computeMaterialPassDrawResourcesReady(Span<const MaterialPassDrawItem> drawItems, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)noexcept;
    // Resolves persistent sampled textures selected by frozen draws.
    // Missing cached resources reject collection; asset and descriptor creation belong to preparation.
    [[nodiscard]] bool appendPreparedMaterialSurfaceSampledTextures(const MaterialSurfaceInfo& materialInfo, MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& collector)const;
    [[nodiscard]] Expected<Vector<Core::TextureHandle, Core::Alloc::ScratchArena>> gatherPreparedMaterialPassSampledTextures(Span<const MaterialPassDrawItemsView> drawItemSets, Core::Alloc::ScratchArena& scratchArena)const;
    [[nodiscard]] bool prepareMaterialPassResourceBindings(const MaterialPassDrawItemsView& drawItems)noexcept;
    [[nodiscard]] bool prepareMeshMaterialPassResourceBindings(Span<const MaterialPassDrawItem> drawItems)noexcept;
    [[nodiscard]] bool prepareIndexedMaterialPassResourceBindings(Span<const MaterialPassDrawItem> drawItems)noexcept;
    [[nodiscard]] bool prepareComputeMaterialPassResourceBindings(Span<const MaterialPassDrawItem> drawItems)noexcept;
    [[nodiscard]] u32 meshDispatchFlags(const MaterialPassMeshResourceSnapshot& mesh, MaterialPipelinePass::Enum pass, bool twoSided, bool meshletConeCullScaleSafe)const noexcept;
    [[nodiscard]] u32 materialPassDrawDispatchFlags(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MaterialPassMeshResourceSnapshot& mesh)const noexcept;
    void setMaterialPassCommonBufferStates(const MaterialPassDrawContext& context, const MaterialPassMeshResourceSnapshot& mesh);
    [[nodiscard]] bool setMaterialPassDrawPushConstants(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MaterialPassMeshResourceSnapshot& mesh);
    void dispatchComputeMaterialPassDrawItem(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MaterialPassMeshResourceSnapshot& mesh, const MaterialPassPipelineResourceSnapshot& pipelineResources);
    void drawComputeMaterialPassDrawItem(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MaterialPassMeshResourceSnapshot& mesh, const MaterialPassPipelineResourceSnapshot& pipelineResources);
    void renderMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemsView& drawItems);
    void renderIndexedMaterialPassDrawItems(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    void renderMeshMaterialPassDrawItems(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    // Records the fixed object-space decoder; the graph owns source reads and the cache UAV-to-vertex/index handoff.
    [[nodiscard]] bool recordObjectGeometryDecode(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem);
    // Graph-only producer half. The graph owns generated output UAV states; this method records only the dispatch.
    void generateComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    // Graph-only raster half. The graph must provide each generated output in combined vertex/index state; this records graphics state, heap, push constants, and draws with no transition.
    void renderComputeMaterialPassDrawItemsRasterOnly(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    // Per-draw generation and rasterization retain the local UAV-to-vertex/index handoff when outputs alias.
    void renderComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    [[nodiscard]] bool reserveInstanceBufferCapacity(usize instanceCount);
    [[nodiscard]] bool reserveMaterialTypedBufferCapacity(usize byteCount);
    [[nodiscard]] bool prepareMaterialPassDrawBuffers(Span<const InstanceGpuData> instanceData, Span<const u8> materialTypedBytes);
    [[nodiscard]] bool materialPassDrawBuffersReady(Span<const InstanceGpuData> instanceData, Span<const u8> materialTypedBytes)const noexcept;
    [[nodiscard]] bool materialPassDrawBuffersReady(usize instanceCount, usize materialTypedByteCount)const noexcept;
    [[nodiscard]] ECSRenderDetail::MaterialPassBufferSnapshot materialPassBufferSnapshot()const noexcept;
    // The CSG context descriptor is selected through every instance's heap-slot lane. Graph declaration patches the upload copy before the packet is recorded so every phase keeps the same ABI.
    void prepareMaterialPassInstanceUploadData(const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources, Span<InstanceGpuData> instanceData)noexcept;
    [[nodiscard]] bool prepareMaterialPassResourceBindingsImpl(Span<const MaterialPassDrawItem> drawItems, bool computeEmulation)noexcept;


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

