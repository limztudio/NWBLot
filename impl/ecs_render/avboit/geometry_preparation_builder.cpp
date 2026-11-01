// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "geometry_preparation_builder.h"

#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<AvboitGeometryPreparationResult> AvboitGeometryPreparationBuilder::declare(const AvboitGeometryPreparationInputs& inputs){
    AvboitGeometryPreparationResult result{};
    if(inputs.drawItemSets.empty())
        return MakeUnexpected(Failure{});
    const AvboitGeometryPhaseIdentities identities = ResolveAvboitGeometryPhaseIdentities(inputs.phase);
    const auto materialGeometrySetResult = RendererTaskGraphDetail::GatherPreparedMaterialGeometryResourceSet(inputs.drawItemSets, identities.geometryIdentity, identities.geometryLabel, m_graph, m_scratchArena);
    result.geometryOwned = materialGeometrySetResult.has_value();
    if(materialGeometrySetResult)
        result.materialGeometrySet = *materialGeometrySetResult;
    if(!result.geometryOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT material geometry states"));
        return MakeUnexpected(Failure{});
    }
    result.sampledTexturesCollected = false;
    if(result.geometryOwned){
        const auto materialSampledTextureSetResult = RendererTaskGraphDetail::GatherPreparedMaterialSampledTextureResourceSet(m_materialSystem, inputs.drawItemSets, identities.sampledIdentity, identities.sampledLabel, m_graph, m_scratchArena);
        result.sampledTexturesCollected = materialSampledTextureSetResult.has_value();
        if(materialSampledTextureSetResult)
            result.materialSampledTextureSet = *materialSampledTextureSetResult;
    }
    if(!result.sampledTexturesCollected){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT material sampled textures"));
        return MakeUnexpected(Failure{});
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

