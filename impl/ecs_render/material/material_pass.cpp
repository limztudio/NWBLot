// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"
#include "material_pass_gatherer.h"

#include <impl/ecs_render/material/instance_typed_ranges.adhd.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_csg/shape_registry.h>
#include <impl/ecs_scene/module.h>

#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_pass{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Core::GpuTimingScopeDefinition s_NoneGpuTimingScope;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static const Core::GpuTimingScopeDefinition& MaterialPassGpuTimingScope(const MaterialPipelinePass::Enum pass)noexcept{
    switch(pass){
    case MaterialPipelinePass::AvboitOccupancy: return RendererGpuTimingScope::s_AvboitOccupancy;
    case MaterialPipelinePass::AvboitExtinction: return RendererGpuTimingScope::s_AvboitExtinction;
    case MaterialPipelinePass::AvboitAccumulate: return RendererGpuTimingScope::s_AvboitAccumulate;
    default: return s_NoneGpuTimingScope;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererMaterialSystem::renderPreparedMaterialPass(
    Core::CommandList& commandList,
    Core::Framebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const AvboitFrameTargets* const avboitTargets,
    const MaterialPassDrawItemPartitionsView& drawItems,
    const CsgFrameGpuDataView& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const usize instanceCount,
    const usize materialTypedByteCount,
    const b8 materialFrameStatesGraphOwned,
    const b8 materialGeometryStatesGraphOwned,
    const b8 emulationOutputEntryStateGraphOwned,
    const b8 csgEmulationOutputEntryStateGraphOwned,
    const b8 emulationOutputReused,
    Optional<Core::GpuTimingMeasure>* const emulationOutputTiming
){
    const auto discardEmulationOutputTiming = [emulationOutputTiming](){
        if(!emulationOutputTiming || !emulationOutputTiming->has_value())
            return;
        emulationOutputTiming->value().discardTiming();
        emulationOutputTiming->reset();
    };
    if(!framebuffer || drawItems.empty()){
        discardEmulationOutputTiming();
        return;
    }
    const b8 usesAvboit = MaterialPipelinePassUsesRendererAvboit(pass);
    if(usesAvboit && (!avboitTargets || !avboitTargets->valid())){
        discardEmulationOutputTiming();
        return;
    }

    commandList.endRenderPass();

    // Fresh producers open split timing. Reused regular output has a separate, already-validated graph producer.
    const b8 emulationOutputStatesGraphOwned = emulationOutputEntryStateGraphOwned || csgEmulationOutputEntryStateGraphOwned;
    if(emulationOutputReused && (!emulationOutputEntryStateGraphOwned || csgEmulationOutputEntryStateGraphOwned))
        return;
    const b8 splitEmulationTiming = emulationOutputStatesGraphOwned && !emulationOutputReused;
    if(splitEmulationTiming && (!emulationOutputTiming || !emulationOutputTiming->has_value()))
        return;

    // Declaration froze ordering and published bytes; keep this consumer side-effect free.
    if(!frameBindings.frameReady(instanceCount, materialTypedByteCount)){
        discardEmulationOutputTiming();
        return;
    }
    const b8 regularDrawResourcesReady = m_drawRecorder.materialPassDrawResourcesReady(drawItems.regular, frameBindings);
    // Active handoff covers every regular draw; reject on late resource disagreement.
    if(emulationOutputEntryStateGraphOwned && !regularDrawResourcesReady){
        discardEmulationOutputTiming();
        return;
    }
    const b8 csgResourcesReady = csgResources.frameReady(csgFrameData);
    const b8 csgDrawResourcesReady = csgResourcesReady && (drawItems.csg.empty() || m_drawRecorder.materialPassDrawResourcesReady(drawItems.csg, frameBindings));
    if(csgEmulationOutputEntryStateGraphOwned && !csgDrawResourcesReady){
        discardEmulationOutputTiming();
        return;
    }

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());
    const MaterialPassDrawContext regularDrawContext{ commandList, framebuffer, avboitTargets, viewportState, nullptr, frameBindings, pass, materialFrameStatesGraphOwned, materialGeometryStatesGraphOwned, emulationOutputEntryStateGraphOwned };
    // CSG opts in only via its own frozen producer; keep it separate from the regular flag.
    const MaterialPassDrawContext csgDrawContext{ commandList, framebuffer, avboitTargets, viewportState, &csgResources, frameBindings, pass, materialFrameStatesGraphOwned, materialGeometryStatesGraphOwned, csgEmulationOutputEntryStateGraphOwned };
    const auto recordPreparedDraws = [&](){
        if(regularDrawResourcesReady)
            m_drawRecorder.renderMaterialPassDrawItems(regularDrawContext, drawItems.regular);
        if(csgDrawResourcesReady)
            m_drawRecorder.renderMaterialPassDrawItems(csgDrawContext, drawItems.csg);
    };
    if(splitEmulationTiming){
        recordPreparedDraws();
        emulationOutputTiming->value().finishTiming(commandList);
        emulationOutputTiming->reset();
    }
    else{
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), __hidden_material_pass::MaterialPassGpuTimingScope(pass), m_graphics.getDevice(), commandList);

        recordPreparedDraws();
    }
}

b8 RendererMaterialSystem::prepareMaterialPassResources(
    Core::Alloc::ScratchArena& scratchArena,
    Core::Framebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const b8 transparent,
    const CsgFrameState& csgFrameState,
    const AvboitFrameTargets* avboitTargets
){
    if(!framebuffer)
        return false;

    const b8 usesAvboit = MaterialPipelinePassUsesRendererAvboit(pass);
    if(usesAvboit && (!avboitTargets || !avboitTargets->valid()))
        return false;

    MaterialPassDrawItemPartitions drawItems{scratchArena};
    InstanceGpuDataVector instanceData{scratchArena};
    CsgFrameGpuData csgFrameData{scratchArena};
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector materialTypedRanges{scratchArena};
#endif
    MaterialTypedByteDataVector materialTypedBytes{scratchArena};

    gatherMaterialPassDrawItems(
        framebuffer,
        pass,
        transparent,
        csgFrameState,
        RendererResourceLookupMode::CreateMissing,
        nullptr,
        drawItems,
        instanceData,
        csgFrameData,
#if defined(NWB_DEBUG)
        materialTypedRanges,
#endif
        materialTypedBytes
    );
    if(drawItems.empty())
        return true;

    const b8 drawBuffersReady = prepareMaterialPassDrawBuffers(instanceData, materialTypedBytes);
    const b8 regularDrawResourcesReady = m_drawRecorder.prepareMaterialPassResourceBindings(drawItems.regular);
    const b8 csgDrawResourcesReady = drawItems.csg.empty() || m_drawRecorder.prepareMaterialPassResourceBindings(drawItems.csg);
    const b8 csgReceiverSurfaceDrawResourcesReady = drawItems.csgReceiverSurface.empty() || m_drawRecorder.prepareMaterialPassResourceBindings(drawItems.csgReceiverSurface);

    return drawBuffersReady && regularDrawResourcesReady && csgDrawResourcesReady && csgReceiverSurfaceDrawResourcesReady;
}

void RendererMaterialSystem::gatherMaterialPassDrawItems(
    Core::Framebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const b8 transparent,
    const CsgFrameState& csgFrameState,
    const RendererResourceLookupMode::Enum lookupMode,
    const MeshViewGpuData* const csgWorkRegionMeshViewState,
    MaterialPassDrawItemPartitions& drawItems,
    InstanceGpuDataVector& instanceData,
    CsgFrameGpuData& csgFrameData,
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector& materialTypedRanges,
#endif
    MaterialTypedByteDataVector& materialTypedBytes
){
    MaterialPassGatherer(*this, m_world, m_meshSystem, m_csgSystem, m_csgShapeRegistry, m_opticalVolumes).gather(
        framebuffer, pass, transparent, csgFrameState, lookupMode, csgWorkRegionMeshViewState, drawItems, instanceData, csgFrameData,
#if defined(NWB_DEBUG)
        materialTypedRanges,
#endif
        materialTypedBytes
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

