// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_texture_imports.h"
#include "graph_resource_import_builder.h"

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void DeferredCsgTextureImports::capture(Core::GpuTaskGraph& graph, const DeferredFrameTargets& deferredTargets){
    m_csgCapBackNormal = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgCapBackNormal, Name(s_DeferredImportNameRenderDeferredCsgCapBackNormal), "CSG Cap Back Normal");
    m_csgIntervalDepth = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgIntervalDepth, Name(s_DeferredImportNameRenderDeferredCsgIntervalDepth), "CSG Interval Depth");
    m_csgIntervalId = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgIntervalId, Name(s_DeferredImportNameRenderDeferredCsgIntervalId), "CSG Interval ID");
    m_csgReceiverEventData = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgReceiverEventData, Name(s_DeferredImportNameRenderDeferredCsgReceiverEventData), "CSG Receiver Event Data");
    m_csgReceiverEventCount = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgReceiverEventCount, Name(s_DeferredImportNameRenderDeferredCsgReceiverEventCount), "CSG Receiver Event Count");
    m_csgReceiverSpanData = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgReceiverSpanData, Name(s_DeferredImportNameRenderDeferredCsgReceiverSpanData), "CSG Receiver Span Data");
    m_csgReceiverSpanCount = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgReceiverSpanCount, Name(s_DeferredImportNameRenderDeferredCsgReceiverSpanCount), "CSG Receiver Span Count");
    m_csgRemovedIntervalDepth = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgRemovedIntervalDepth, Name(s_DeferredImportNameRenderDeferredCsgRemovedIntervalDepth), "CSG Removed Interval Depth");
    m_csgRemovedIntervalCapNormal = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgRemovedIntervalCapNormal, Name(s_DeferredImportNameRenderDeferredCsgRemovedIntervalCapNormal), "CSG Removed Interval Cap Normal");
    m_csgRemovedIntervalData = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgRemovedIntervalData, Name(s_DeferredImportNameRenderDeferredCsgRemovedIntervalData), "CSG Removed Interval Data");
    m_csgRemovedIntervalCount = RendererTaskGraphDetail::ImportExternalTexture(graph, deferredTargets.csgRemovedIntervalCount, Name(s_DeferredImportNameRenderDeferredCsgRemovedIntervalCount), "CSG Removed Interval Count");
}

b8 DeferredCsgTextureImports::valid()const noexcept{
    return
        m_csgCapBackNormal.has_value()
        && m_csgIntervalDepth.has_value()
        && m_csgIntervalId.has_value()
        && m_csgReceiverEventData.has_value()
        && m_csgReceiverEventCount.has_value()
        && m_csgReceiverSpanData.has_value()
        && m_csgReceiverSpanCount.has_value()
        && m_csgRemovedIntervalDepth.has_value()
        && m_csgRemovedIntervalCapNormal.has_value()
        && m_csgRemovedIntervalData.has_value()
        && m_csgRemovedIntervalCount.has_value()
    ;
}

void DeferredCsgTextureImports::publish(DeferredGraphResourceImportResult& result)const noexcept{
    result.csgCapBackNormal = *m_csgCapBackNormal;
    result.csgIntervalDepth = *m_csgIntervalDepth;
    result.csgIntervalId = *m_csgIntervalId;
    result.csgReceiverEventData = *m_csgReceiverEventData;
    result.csgReceiverEventCount = *m_csgReceiverEventCount;
    result.csgReceiverSpanData = *m_csgReceiverSpanData;
    result.csgReceiverSpanCount = *m_csgReceiverSpanCount;
    result.csgRemovedIntervalDepth = *m_csgRemovedIntervalDepth;
    result.csgRemovedIntervalCapNormal = *m_csgRemovedIntervalCapNormal;
    result.csgRemovedIntervalData = *m_csgRemovedIntervalData;
    result.csgRemovedIntervalCount = *m_csgRemovedIntervalCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

