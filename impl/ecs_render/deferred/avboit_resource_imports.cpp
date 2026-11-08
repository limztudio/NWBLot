// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_resource_imports.h"
#include "graph_resource_import_builder.h"

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void DeferredAvboitResourceImports::capture(Core::GpuTaskGraph& graph, const DeferredFrameTargets& deferredTargets, const b8 clearTargets){
    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return clearTargets
            ? RendererTaskGraphDetail::ImportExternalTexture(graph, texture, identity, label, Core::ResourceStates::Unknown)
            : RendererTaskGraphDetail::ImportExternalTexture(graph, texture, identity, label)
        ;
    };
    m_avboitLowRaster = importTexture(deferredTargets.avboit.lowRasterTarget, Name("render.avboit.low_raster"), "AVBOIT Low Raster");
    m_avboitAccumColor = importTexture(deferredTargets.avboit.accumColor, Name("render.avboit.accum_color"), "AVBOIT Accumulated Color");
    m_avboitAccumExtinction = importTexture(deferredTargets.avboit.accumExtinction, Name("render.avboit.accum_extinction"), "AVBOIT Accumulated Extinction");
    m_refractionDepth = importTexture(deferredTargets.avboit.refractionDepth, Name("render.avboit.refractionDepth"), "AVBOIT refractionDepth");
    m_refractionNormalIor = importTexture(deferredTargets.avboit.refractionNormalIor, Name("render.avboit.refractionNormalIor"), "AVBOIT refractionNormalIor");
    m_refractionTintCoverage = importTexture(deferredTargets.avboit.refractionTintCoverage, Name("render.avboit.refractionTintCoverage"), "AVBOIT refractionTintCoverage");
    m_refractionInstance = importTexture(deferredTargets.avboit.refractionInstance, Name("render.avboit.refractionInstance"), "AVBOIT refractionInstance");
    m_refractionSpecularRoughness = importTexture(deferredTargets.avboit.refractionSpecularRoughness, Name("render.avboit.refractionSpecularRoughness"), "AVBOIT Refraction Specular Roughness");
    m_refractionResolve = importTexture(deferredTargets.avboit.refractionResolve, Name("render.avboit.refractionResolve"), "AVBOIT refractionResolve");
    m_avboitForegroundColor = importTexture(deferredTargets.avboit.foregroundAccumColor, Name("render.avboit.avboitForegroundColor"), "AVBOIT avboitForegroundColor");
    m_avboitForegroundExtinction = importTexture(deferredTargets.avboit.foregroundAccumExtinction, Name("render.avboit.avboitForegroundExtinction"), "AVBOIT avboitForegroundExtinction");
    m_avboitTransmittance = importTexture(deferredTargets.avboit.transmittanceTexture, Name("render.avboit.transmittance"), "AVBOIT Transmittance");
    m_avboitCoverage = RendererTaskGraphDetail::ImportExternalBuffer(graph, deferredTargets.avboit.coverageBuffer, Name("render.avboit.coverage"), "AVBOIT Coverage");
    m_avboitDepthWarp = RendererTaskGraphDetail::ImportExternalBuffer(graph, deferredTargets.avboit.depthWarpBuffer, Name(RendererGpuPassText::s_AvboitDepthWarpName), RendererGpuPassText::s_AvboitDepthWarpLabel);
    m_avboitControl = RendererTaskGraphDetail::ImportExternalBuffer(graph, deferredTargets.avboit.controlBuffer, Name("render.avboit.control"), "AVBOIT Control");
    m_avboitExtinction = RendererTaskGraphDetail::ImportExternalBuffer(graph, deferredTargets.avboit.extinctionBuffer, Name(RendererGpuPassText::s_AvboitExtinctionName), RendererGpuPassText::s_AvboitExtinctionLabel);
    m_avboitExtinctionOverflow = RendererTaskGraphDetail::ImportExternalBuffer(graph, deferredTargets.avboit.extinctionOverflowBuffer, Name("render.avboit.extinction_overflow"), "AVBOIT Extinction Overflow");
    m_avboitMaterialDomain = graph.importHazardDomain(RendererTaskGraphDetail::HazardDomainDesc(Name("render.avboit.material_domain"), "Transparent Materials and Geometry"));
    m_avboitCsgDomain = graph.importHazardDomain(RendererTaskGraphDetail::HazardDomainDesc(Name("render.avboit.csg_domain"), RendererGpuPassText::s_TransparentCsgIntervalsLabel));
}

b8 DeferredAvboitResourceImports::validRefractionInputs()const noexcept{
    return
        m_refractionDepth.has_value()
        && m_refractionNormalIor.has_value()
        && m_refractionTintCoverage.has_value()
        && m_refractionInstance.has_value()
        && m_refractionResolve.has_value()
        && m_avboitForegroundColor.has_value()
        && m_avboitForegroundExtinction.has_value()
    ;
}

b8 DeferredAvboitResourceImports::validRefractionSpecular()const noexcept{
    return m_refractionSpecularRoughness.has_value();
}

b8 DeferredAvboitResourceImports::validAvboitInputs()const noexcept{
    return
        m_avboitLowRaster.has_value()
        && m_avboitAccumColor.has_value()
        && m_avboitAccumExtinction.has_value()
        && m_avboitTransmittance.has_value()
        && m_avboitCoverage.has_value()
        && m_avboitDepthWarp.has_value()
        && m_avboitControl.has_value()
        && m_avboitExtinction.has_value()
        && m_avboitExtinctionOverflow.has_value()
        && m_avboitMaterialDomain.has_value()
        && m_avboitCsgDomain.has_value()
    ;
}

void DeferredAvboitResourceImports::publish(DeferredGraphResourceImportResult& result)const noexcept{
    result.avboitLowRaster = *m_avboitLowRaster;
    result.avboitAccumColor = *m_avboitAccumColor;
    result.avboitAccumExtinction = *m_avboitAccumExtinction;
    result.refractionDepth = *m_refractionDepth;
    result.refractionNormalIor = *m_refractionNormalIor;
    result.refractionTintCoverage = *m_refractionTintCoverage;
    result.refractionInstance = *m_refractionInstance;
    result.refractionSpecularRoughness = *m_refractionSpecularRoughness;
    result.refractionResolve = *m_refractionResolve;
    result.avboitForegroundColor = *m_avboitForegroundColor;
    result.avboitForegroundExtinction = *m_avboitForegroundExtinction;
    result.avboitTransmittance = *m_avboitTransmittance;
    result.avboitCoverage = *m_avboitCoverage;
    result.avboitDepthWarp = *m_avboitDepthWarp;
    result.avboitControl = *m_avboitControl;
    result.avboitExtinction = *m_avboitExtinction;
    result.avboitExtinctionOverflow = *m_avboitExtinctionOverflow;
    result.avboitMaterialDomain = *m_avboitMaterialDomain;
    result.avboitCsgDomain = *m_avboitCsgDomain;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

