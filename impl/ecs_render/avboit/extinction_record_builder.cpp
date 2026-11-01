// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "extinction_record_builder.h"

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 AvboitExtinctionRecordBuilder::declare(
    Core::Alloc::ScratchArena& scratchArena,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    ObjectGeometryCacheGraph& objectGeometry,
    AvboitExtinctionRecordInputs& inputs,
    RendererTaskGraphDetail::AvboitExtinctionGraphTask::Payload& extinctionPayload,
    RendererTaskGraphDetail::AvboitExtinctionComputeEmulationGraphTask::Payload& computeEmulationPayload
){
    using namespace RendererTaskGraphDetail;
    if(!inputs.depthWarpCompletionTask.valid() || !inputs.uploadTask.valid())
        return false;

    const Array<Core::GpuTaskResourceUse, 10u> baseResourceUses = {
        ReadUse(inputs.albedo),
        ReadUse(inputs.refractionInstance),
        ReadUse(inputs.normal, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.depth),
        ReadWriteUse(inputs.avboitLowRaster, Core::ResourceStates::RenderTarget),
        ReadUse(inputs.avboitDepthWarp),
        ReadUse(inputs.avboitControl),
        ReadWriteUse(inputs.avboitExtinction, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(inputs.avboitExtinctionOverflow, Core::ResourceStates::UnorderedAccess),
    };
    NWB_DEFINE_AVBOIT_SHARED_COMPUTE_EMULATION_PHASE_TABLES("render.avboit.extinction", "AVBOIT Extinction");

    RendererAvboitTaskGraphStageState& stage = m_avboitSystem.taskGraphStage();
    return DeclareAvboitRasterRecordPhase<AvboitExtinctionComputeEmulationGraphTask, AvboitExtinctionSharedComputeEmulationGraphTask, AvboitExtinctionGraphTask>(
        m_graph,
        m_graphics,
        m_materialSystem,
        scratchArena,
        frameBindings,
        objectGeometry,
        inputs,
        inputs.uploadTask,
        baseResourceUses,
        inputs.extinctionTimingTicket,
        inputs.extinctionComputeEmulationTiming,
        NWB_MAKE_AVBOIT_RASTER_RECORD_PHASE_DESC(
            "render.avboit.extinction",
            "AVBOIT Extinction",
            Name(RendererGpuPassText::s_AvboitExtinctionName),
            RendererGpuPassText::s_AvboitExtinctionLabel,
            AvboitRecordInputsBase::s_ResourceBaseCount
        ),
        extinctionPayload,
        computeEmulationPayload,
        AvboitRasterRecordStageOutputs{
            .reusedGeometryProducer = stage.m_extinctionReusedGeometryProducer,
            .streamTask = stage.m_extinctionStreamTask,
            .computeEmulationTask = stage.m_extinctionComputeEmulationTask,
            .sharedComputeEmulationTasks = stage.m_extinctionSharedComputeEmulationTasks,
            .sharedComputeEmulationTaskCount = stage.m_extinctionSharedComputeEmulationTaskCount,
            .rasterTask = stage.m_extinctionTask,
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

