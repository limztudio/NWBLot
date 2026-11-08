// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "opaque_gbuffer_material_resources.h"

#include <impl/ecs_render/material/task_graph_resource_sets.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<OpaqueGbufferMaterialResources> OpaqueGbufferMaterialResourceCollector::gather(const MaterialPassDrawItemPartitions& drawItems){
    using namespace RendererTaskGraphDetail;

    OpaqueGbufferMaterialResources resources;
    const Array<MaterialPassDrawItemsView, 2> drawSets = { drawItems.regular, drawItems.csgReceiverSurface, };
    const b8 usesMaterialGeometry = !drawItems.regular.empty() || !drawItems.csgReceiverSurface.empty();
    if(usesMaterialGeometry){
        const auto geometrySet = GatherPreparedMaterialGeometryResourceSet(drawSets, Name("render.graphics_prefix.gbuffer.material_geometry"), "Opaque Material Geometry", m_graph, m_scratchArena);
        resources.geometryStatesGraphOwned = geometrySet.has_value();
        if(geometrySet)
            resources.geometrySet = *geometrySet;
    }
    if(usesMaterialGeometry && !resources.geometryStatesGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque material geometry states"));
        // Frozen heap slots require a complete graph declaration before this material callback can record.
        return MakeUnexpected(Failure{});
    }
    if(usesMaterialGeometry){
        const auto sampledTextureSet = GatherPreparedMaterialSampledTextureResourceSet(
            m_materialSystem,
            drawSets,
            Name("render.graphics_prefix.gbuffer.material_sampled_textures"),
            "Opaque Material Sampled Textures",
            m_graph,
            m_scratchArena
        );
        resources.sampledTexturesCollected = sampledTextureSet.has_value();
        if(sampledTextureSet)
            resources.sampledTextureSet = *sampledTextureSet;
    }
    if(usesMaterialGeometry && !resources.sampledTexturesCollected){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque material sampled textures"));
        return MakeUnexpected(Failure{});
    }
    return resources;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

