// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/material/renderer_draw_types.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;


namespace AvboitGeometryPhase{ enum Enum : u8{
    Occupancy,
    Extinction,
    Accumulation,
}; };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared AVBOIT geometry preparation owns material geometry plus sampled-texture sets.
struct AvboitGeometryPreparationInputs{
    const MaterialPassDrawItems* const* drawItemSets = nullptr;
    usize drawItemSetCount = 0u;
    AvboitGeometryPhase::Enum phase = AvboitGeometryPhase::Occupancy;
};

struct AvboitGeometryPreparationResult{
    Core::GpuGraphResourceSetId materialGeometrySet;
    Core::GpuGraphResourceSetId materialSampledTextureSet;
    bool geometryOwned = false;
    bool sampledTexturesCollected = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitGeometryPreparationBuilder final : NoCopy{
public:
    AvboitGeometryPreparationBuilder(
        Core::GpuTaskGraph& graph,
        RendererMaterialSystem& materialSystem,
        Core::Alloc::ScratchArena& scratchArena
    );


public:
    [[nodiscard]] bool declare(
        const AvboitGeometryPreparationInputs& inputs,
        AvboitGeometryPreparationResult& outResult
    );


private:
    Core::GpuTaskGraph& m_graph;
    RendererMaterialSystem& m_materialSystem;
    Core::Alloc::ScratchArena& m_scratchArena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
