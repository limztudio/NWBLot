// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/renderer_draw_types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MaterialPassDrawRecorder final : NoCopy{
public:
    explicit MaterialPassDrawRecorder(Core::GraphicsRuntime& graphics)noexcept
        : m_graphics(graphics)
    {}


public:
    [[nodiscard]] b8 materialPassDrawResourcesReady(const MaterialPassMeshResourceSnapshot& mesh, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)const noexcept{
        return mesh.valid() && frameBindings.bindingValid();
    }
    [[nodiscard]] b8 materialPassDrawResourcesReady(const MaterialPassDrawItemsView& drawItems, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)noexcept{
        return meshMaterialPassDrawResourcesReady(drawItems.meshDrawItems, frameBindings) && indexedMaterialPassDrawResourcesReady(drawItems.indexedDrawItems, frameBindings) && computeMaterialPassDrawResourcesReady(drawItems.computeDrawItems, frameBindings);
    }
    [[nodiscard]] b8 meshMaterialPassDrawResourcesReady(Span<const MaterialPassDrawItem> drawItems, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)noexcept;
    [[nodiscard]] b8 indexedMaterialPassDrawResourcesReady(Span<const MaterialPassDrawItem> drawItems, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)noexcept{
        return (drawItems.empty() || frameBindings.bindingValid()) && prepareIndexedMaterialPassResourceBindings(drawItems);
    }
    [[nodiscard]] b8 computeMaterialPassDrawResourcesReady(Span<const MaterialPassDrawItem> drawItems, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings)noexcept;
    [[nodiscard]] b8 prepareMaterialPassResourceBindings(const MaterialPassDrawItemsView& drawItems)noexcept{
        return prepareMeshMaterialPassResourceBindings(drawItems.meshDrawItems) && prepareIndexedMaterialPassResourceBindings(drawItems.indexedDrawItems) && prepareComputeMaterialPassResourceBindings(drawItems.computeDrawItems);
    }
    [[nodiscard]] b8 prepareMeshMaterialPassResourceBindings(Span<const MaterialPassDrawItem> drawItems)noexcept{
        return prepareMaterialPassResourceBindingsImpl(drawItems, false);
    }
    [[nodiscard]] b8 prepareIndexedMaterialPassResourceBindings(Span<const MaterialPassDrawItem> drawItems)noexcept;
    [[nodiscard]] b8 prepareComputeMaterialPassResourceBindings(Span<const MaterialPassDrawItem> drawItems)noexcept{
        return prepareMaterialPassResourceBindingsImpl(drawItems, true);
    }
    void setMaterialPassCommonBufferStates(const MaterialPassDrawContext& context, const MaterialPassMeshResourceSnapshot& mesh);
    [[nodiscard]] b8 setMaterialPassDrawPushConstants(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MaterialPassMeshResourceSnapshot& mesh);
    void dispatchComputeMaterialPassDrawItem(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MaterialPassMeshResourceSnapshot& mesh, const MaterialPassPipelineResourceSnapshot& pipelineResources);
    void drawComputeMaterialPassDrawItem(const MaterialPassDrawContext& context, const MaterialPassDrawItem& drawItem, const MaterialPassMeshResourceSnapshot& mesh, const MaterialPassPipelineResourceSnapshot& pipelineResources);
    void renderMaterialPassDrawItems(const MaterialPassDrawContext& context, const MaterialPassDrawItemsView& drawItems);
    void renderIndexedMaterialPassDrawItems(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    void renderMeshMaterialPassDrawItems(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    // Graph-only producer half. The graph owns generated output UAV states; this method records only the dispatch.
    void generateComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    // Graph-only raster half. The graph provides each output Combined; this method records state, heap, push constants, draws.
    void renderComputeMaterialPassDrawItemsRasterOnly(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    // Per-draw generation and rasterization retain the local UAV-to-vertex/index handoff when outputs alias.
    void renderComputeMaterialPassDrawItems(const MaterialPassDrawContext& context, Span<const MaterialPassDrawItem> drawItems);
    [[nodiscard]] b8 prepareMaterialPassResourceBindingsImpl(Span<const MaterialPassDrawItem> drawItems, b8 computeEmulation)noexcept;


private:
    Core::GraphicsRuntime& m_graphics;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

