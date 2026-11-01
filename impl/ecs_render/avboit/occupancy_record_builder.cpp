// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "occupancy_record_builder.h"

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 AvboitOccupancyRecordBuilder::declare(
    Core::Alloc::ScratchArena& scratchArena,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    ObjectGeometryCacheGraph& objectGeometry,
    AvboitOccupancyRecordInputs& inputs,
    RendererTaskGraphDetail::AvboitOccupancyGraphTask::Payload& occupancyPayload,
    RendererTaskGraphDetail::AvboitOccupancyComputeEmulationGraphTask::Payload& computeEmulationPayload
){
    using namespace RendererTaskGraphDetail;
    if(!inputs.clearTask.valid() || !inputs.uploadTask.valid())
        return false;

    const Array<Core::GpuTaskResourceUse, 7u> baseResourceUses = {
        ReadUse(inputs.albedo),
        ReadUse(inputs.normal, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.depth),
        ReadUse(inputs.refractionInstance),
        ReadWriteUse(inputs.avboitLowRaster, Core::ResourceStates::RenderTarget),
        ReadWriteUse(inputs.avboitCoverage, Core::ResourceStates::UnorderedAccess),
    };
    NWB_DEFINE_AVBOIT_SHARED_COMPUTE_EMULATION_PHASE_TABLES("render.avboit.occupancy", "AVBOIT Occupancy");

    RendererAvboitTaskGraphStageState& stage = m_avboitSystem.taskGraphStage();
    return DeclareAvboitRasterRecordPhase<AvboitOccupancyComputeEmulationGraphTask, AvboitOccupancySharedComputeEmulationGraphTask, AvboitOccupancyGraphTask>(
        m_graph,
        m_graphics,
        m_materialSystem,
        scratchArena,
        frameBindings,
        objectGeometry,
        inputs,
        inputs.clearTask,
        baseResourceUses,
        inputs.preTimingTicket,
        inputs.occupancyComputeEmulationTiming,
        NWB_MAKE_AVBOIT_RASTER_RECORD_PHASE_DESC(
            "render.avboit.occupancy",
            "AVBOIT Occupancy",
            Name("render.avboit.pre"),
            "AVBOIT Pre",
            s_AvboitOccupancyResourceBaseCount
        ),
        occupancyPayload,
        computeEmulationPayload,
        AvboitRasterRecordStageOutputs{
            .reusedGeometryProducer = stage.m_occupancyReusedGeometryProducer,
            .streamTask = stage.m_occupancyStreamTask,
            .computeEmulationTask = stage.m_occupancyComputeEmulationTask,
            .sharedComputeEmulationTasks = stage.m_occupancySharedComputeEmulationTasks,
            .sharedComputeEmulationTaskCount = stage.m_occupancySharedComputeEmulationTaskCount,
            .rasterTask = stage.m_occupancyTask,
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

