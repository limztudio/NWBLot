// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/material_surface_lookup.h>
#include <impl/ecs_render/material/material_typed_private.h>

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Graphics;


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
    struct MeshViewGpuData;

    struct MaterialPassBufferSnapshot{
        Core::BufferHandle instanceBuffer;
        Core::BufferHandle typedBuffer;
    };
};

struct MaterialInstanceOverrideField{
    const MaterialTypedLayoutField* field = nullptr;
    u32 blockByteBegin = 0u;
    bool mutableBlock = false;
};

class CsgShapeRegistry;
class RendererMaterialState;
class RendererDrawState;
class RendererShaderSystem;
class RendererMeshSystem;
class RendererCsgSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem final : public IMaterialSurfaceLookup, NoCopy{
public:
    RendererMaterialSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::Graphics& graphics,
        Core::Assets::AssetManager& assetManager,
        CsgShapeRegistry& csgShapeRegistry,
        RendererMaterialState& materialState,
        RendererDrawState& drawState,
        RendererShaderSystem& shaderSystem,
        RendererMeshSystem& meshSystem,
        RendererCsgSystem& csgSystem
    );

public:
    [[nodiscard]] static bool splitMaterialTypedBytesByClass(
        const Material& material,
        const Name& materialPath,
        MaterialTypedByteVector& outConstantTypedBytes,
        MaterialTypedByteVector& outMutableDefaultTypedBytes
    );

public:
    void invalidateResources();
    [[nodiscard]] bool createMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo);
    // Prepared-only lookup: creation and descriptor-backed resource resolution belong to preparation.
    [[nodiscard]] virtual bool findMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo)override;
    [[nodiscard]] bool resolveMaterialResourceReferences(MaterialSurfaceInfo& materialInfo);
    [[nodiscard]] bool prepareVisibleMaterialSurfaceInfos();
    void prepareVisibleMaterialInstanceMutableCache();
    [[nodiscard]] bool prepareMaterialPassBindingLayout(Core::BindingLayoutHandle& outBindingLayout);
    [[nodiscard]] bool createRendererPipeline(const MaterialSurfaceInfo& materialInfo, const MaterialPipelineKey& pipelineKey, Core::Framebuffer* framebuffer, MaterialPipelineResources*& outResources);
    [[nodiscard]] bool findRendererPipeline(const MaterialPipelineKey& pipelineKey, MaterialPipelineResources*& outResources);
    void invalidateRendererPipelines();
    [[nodiscard]] bool hasTransparentRenderers(RendererResourceLookupMode::Enum lookupMode);
    void logMaterialRenderPathDecision(const Name& materialKey, RenderPath::Enum renderPath, bool meshSupported);
    [[nodiscard]] bool createComputeEmulationResources();
    // Graph-owned draw streams are already gathered, patched, and uploaded before native recording. This consumer
    // deliberately performs no mutable mesh-view or material/CSG buffer updates.
    void renderPreparedMaterialPass(
        Core::CommandList& commandList,
        const DeferredFrameTargets& deferredTargets,
        Core::Framebuffer* framebuffer,
        MaterialPipelinePass::Enum pass,
        const AvboitFrameTargets* avboitTargets,
        const MaterialPassDrawItemPartitions& drawItems,
        const CsgFrameGpuData& csgFrameData,
        usize instanceCount,
        usize materialTypedByteCount,
        // The graph can declare the removed-interval StorageImage reads at a producer/consumer boundary. Direct
        // and other prepared compatibility phases retain their native UAV handoff by leaving this false.
        bool csgIntervalSampleImageStatesGraphOwned = false,
        // Prepared graph tasks also declare their heap-selected CSG clip buffers. Direct and unprepared callers
        // retain the native SRV/CBV state setup by leaving this false.
        bool csgClipBufferStatesGraphOwned = false,
        // The shared graph declares mesh-view/material entry states for immutable prepared streams. Direct and
        // compatibility consumers retain their historical native setup by leaving this false.
        bool materialFrameStatesGraphOwned = false,
        // The graph can also retain the per-mesh source buffer batch selected by the prepared draw stream.
        bool materialGeometryStatesGraphOwned = false,
        // An immediately preceding graph task generated every regular emulation output. Consume those buffers as
        // VertexBuffer inputs instead of replaying the local UAV-to-VertexBuffer bridge.
        bool emulationOutputEntryStateGraphOwned = false,
        // The producer opens this established pass measure before its dispatches; the prepared raster closes it.
        // A missing measure means the producer intentionally no-op'd, so this consumer must not raster stale data.
        Optional<Core::GpuTimingMeasure>* emulationOutputTiming = nullptr,
        // AVBOIT may instead freeze a CSG-only alias-free producer. Keep this separate from the regular handoff so
        // a mixed CSG stream cannot accidentally suppress its required local dispatch/raster interleaving.
        bool csgEmulationOutputEntryStateGraphOwned = false
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
        RendererResourceLookupMode::Enum lookupMode,
        // Graph declaration can supply the exact immutable view payload that will be uploaded before this CSG
        // work records. Compatibility paths retain the accepted CPU mirror fallback.
        const ECSRenderDetail::MeshViewGpuData* csgWorkRegionMeshViewState = nullptr
    );
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
    // Creation and override application belong to preparation; render paths only use the prepared lookup below.
    [[nodiscard]] bool prepareMaterialInstanceMutableTypedBytes(
        Core::ECS::EntityID entity,
        const MaterialSurfaceInfo& materialInfo,
        const MaterialInstanceComponent* materialInstance,
        const MaterialTypedByteVector*& outMutableTypedBytes
    );
    [[nodiscard]] bool findPreparedMaterialInstanceMutableTypedBytes(
        Core::ECS::EntityID entity,
        const MaterialSurfaceInfo& materialInfo,
        const MaterialInstanceComponent* materialInstance,
        const MaterialTypedByteVector*& outMutableTypedBytes
    )const;
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
    // Resolves the exact persistent sampled textures selected by frozen prepared draw streams. It never creates
    // assets or descriptors: a missing or unresolved cached resource makes the collection unavailable so callers
    // can retain their existing compatibility route.
    [[nodiscard]] bool appendPreparedMaterialSurfaceSampledTextures(
        const MaterialSurfaceInfo& materialInfo,
        Vector<Core::TextureHandle, Core::Alloc::ScratchArena>& inOutTextures
    );
    [[nodiscard]] bool gatherPreparedMaterialPassSampledTextures(
        const MaterialPassDrawItems* const* drawItemSets,
        usize drawItemSetCount,
        Vector<Core::TextureHandle, Core::Alloc::ScratchArena>& outTextures
    );
    [[nodiscard]] bool prepareMaterialPassResourceBindings(const MaterialPassDrawItems& drawItems);
    [[nodiscard]] bool prepareMeshMaterialPassResourceBindings(const MaterialPassDrawItemVector& drawItems);
    [[nodiscard]] bool prepareComputeMaterialPassResourceBindings(const MaterialPassDrawItemVector& drawItems);
    [[nodiscard]] u32 meshDispatchFlags(const MeshResources& mesh, MaterialPipelinePass::Enum pass, bool twoSided, bool meshletConeCullScaleSafe)const;
    [[nodiscard]] u32 materialPassDrawDispatchFlags(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MeshResources& mesh)const;
    void setMaterialPassCommonBufferStates(
        Core::CommandList& commandList,
        const MeshResources& mesh,
        bool materialFrameStatesGraphOwned,
        bool materialGeometryStatesGraphOwned
    );
    void setMaterialPassDrawItemResourceStates(
        const MaterialPassDrawContext& context,
        const MaterialPassDrawItem& drawItem,
        const MeshResources& mesh
    );
    void setMaterialPassDrawPushConstants(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MeshResources& mesh);
    void dispatchComputeMaterialPassDrawItem(
        const MaterialPassDrawContext& context,
        const MaterialPassDrawItem& drawItem,
        const MeshResources& mesh,
        MaterialPipelineResources& pipelineResources
    );
    void drawComputeMaterialPassDrawItem(
        const MaterialPassDrawContext& context,
        const MaterialPassDrawItem& drawItem,
        const MeshResources& mesh,
        MaterialPipelineResources& pipelineResources
    );
    void renderMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItems& drawItems);
    void renderMeshMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);
    // Graph-only producer half. The graph must provide each generated-vertex output in UAV state; this method
    // records the compute state, descriptor heap, push constants, and dispatches without an output transition.
    void generateComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);
    // Graph-only raster half. The graph must provide each generated-vertex output in VertexBuffer state; this
    // method records the graphics state, descriptor heap, push constants, and draws without an output transition.
    void renderComputeMaterialPassDrawItemsRasterOnly(const MaterialPassDrawContext& context, const MaterialPassDrawItemVector& drawItems);
    // Compatibility combined producer/raster path. It retains the native per-item UAV-to-VertexBuffer handoff.
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
    [[nodiscard]] bool materialPassDrawBuffersReady(
        usize instanceCount,
        usize materialTypedByteCount
    )const;
    [[nodiscard]] ECSRenderDetail::MaterialPassBufferSnapshot materialPassBufferSnapshot()const;
    // The CSG context descriptor is selected through every instance's retained heap-slot lane. Graph declaration
    // patches the immutable upload copy before the packet is recorded so every prepared phase keeps the same ABI.
    void prepareMaterialPassInstanceUploadData(InstanceGpuDataVector& instanceData);
    [[nodiscard]] bool findMaterialPassDrawItemResources(
        const MaterialPassDrawItem& drawItem,
        MeshResources*& outMesh,
        MaterialPipelineResources*& outPipelineResources
    );
    [[nodiscard]] bool prepareMaterialPassResourceBindingsImpl(
        const MaterialPassDrawItemVector& drawItems,
        bool computeEmulation
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
    void releaseMaterialResourceReferences();

private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    CsgShapeRegistry& m_csgShapeRegistry;
    RendererMaterialState& m_materialState;
    RendererDrawState& m_drawState;
    RendererShaderSystem& m_shaderSystem;
    RendererMeshSystem& m_meshSystem;
    RendererCsgSystem& m_csgSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

