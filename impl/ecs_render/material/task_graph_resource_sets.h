// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/pipeline_types.adhd.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The result preserves texture order; imports already published to the graph remain on failure.
[[nodiscard]] Expected<Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>, SampledTextureImportFailure::Enum> ImportMaterialSampledTextureResources(
    Span<const Core::TextureHandle> textures,
    AStringView markerLabel,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>> GatherPreparedMaterialGeometryUses(Span<const MaterialPassDrawItemsView> drawItemSets, Core::GpuTaskGraph& graph, Core::Alloc::ScratchArena& scratchArena);

// Material geometry enumerates from the frozen snapshot; give the graph one immutable collection.
[[nodiscard]] Expected<Core::GpuGraphResourceSetId> GatherPreparedMaterialGeometryResourceSet(
    Span<const MaterialPassDrawItemsView> drawItemSets,
    const Name& identity,
    AStringView label,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
);

[[nodiscard]] Expected<Core::GpuGraphResourceId> GatherRegularSharedComputeEmulationResource(Core::GpuTaskGraph& graph, const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan& plan, AStringView label);

[[nodiscard]] Expected<Core::GpuGraphResourceSetId> GatherPreparedMaterialSampledTextureResourceSet(
    const RendererMaterialSystem& materialSystem,
    Span<const MaterialPassDrawItemsView> drawItemSets,
    const Name& identity,
    AStringView label,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

