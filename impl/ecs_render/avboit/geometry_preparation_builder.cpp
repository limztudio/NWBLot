// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "geometry_preparation_builder.h"

#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AvboitGeometryPreparationBuilder::AvboitGeometryPreparationBuilder(
    Core::GpuTaskGraph& graph,
    RendererMaterialSystem& materialSystem,
    Core::Alloc::ScratchArena& scratchArena
)
    : m_graph(graph)
    , m_materialSystem(materialSystem)
    , m_scratchArena(scratchArena){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_preparation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PhaseIdentities{
    AStringView geometryIdentity;
    AStringView geometryLabel;
    AStringView sampledIdentity;
    AStringView sampledLabel;
};


[[nodiscard]] PhaseIdentities IdentitiesForPhase(AvboitGeometryPhase::Enum phase)noexcept{
    switch(phase){
    case AvboitGeometryPhase::Extinction:
        return PhaseIdentities{
            .geometryIdentity = "render.avboit.extinction.material_geometry",
            .geometryLabel = "AVBOIT Extinction Material Geometry",
            .sampledIdentity = "render.avboit.extinction.material_sampled_textures",
            .sampledLabel = "AVBOIT Extinction Material Sampled Textures",
        };
    case AvboitGeometryPhase::Accumulation:
        return PhaseIdentities{
            .geometryIdentity = "render.avboit.accumulation.material_geometry",
            .geometryLabel = "AVBOIT Accumulation Material Geometry",
            .sampledIdentity = "render.avboit.accumulation.material_sampled_textures",
            .sampledLabel = "AVBOIT Accumulation Material Sampled Textures",
        };
    case AvboitGeometryPhase::Occupancy:
    default:
        return PhaseIdentities{
            .geometryIdentity = "render.avboit.occupancy.material_geometry",
            .geometryLabel = "AVBOIT Occupancy Material Geometry",
            .sampledIdentity = "render.avboit.occupancy.material_sampled_textures",
            .sampledLabel = "AVBOIT Occupancy Material Sampled Textures",
        };
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<AvboitGeometryPreparationResult> AvboitGeometryPreparationBuilder::declare(
    const AvboitGeometryPreparationInputs& inputs
){
    AvboitGeometryPreparationResult result{};
    if(!inputs.drawItemSets || inputs.drawItemSetCount == 0u)
        return MakeUnexpected(Failure{});
    const __hidden_geometry_preparation::PhaseIdentities identities =
        __hidden_geometry_preparation::IdentitiesForPhase(inputs.phase)
    ;
    const auto materialGeometrySetResult = RendererTaskGraphDetail::GatherPreparedMaterialGeometryResourceSet(
        m_graph,
        inputs.drawItemSets,
        inputs.drawItemSetCount,
        m_scratchArena,
        Name(identities.geometryIdentity),
        identities.geometryLabel
    );
    result.geometryOwned = materialGeometrySetResult.has_value();
    if(materialGeometrySetResult)
        result.materialGeometrySet = *materialGeometrySetResult;
    if(!result.geometryOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared AVBOIT material geometry states"));
        return MakeUnexpected(Failure{});
    }
    result.sampledTexturesCollected = false;
    if(result.geometryOwned){
        const auto materialSampledTextureSetResult = RendererTaskGraphDetail::GatherPreparedMaterialSampledTextureResourceSet(
                m_materialSystem,
                m_graph,
                inputs.drawItemSets,
                inputs.drawItemSetCount,
                m_scratchArena,
                Name(identities.sampledIdentity),
                identities.sampledLabel
            );
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

