// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "accumulation_record_builder.h"

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 AvboitAccumulationRecordBuilder::declare(
    Core::Alloc::ScratchArena& scratchArena,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    ObjectGeometryCacheGraph& objectGeometry,
    AvboitAccumulationRecordInputs& inputs,
    RendererTaskGraphDetail::AvboitAccumulationGraphTask::Payload& accumulationPayload,
    RendererTaskGraphDetail::AvboitAccumulationComputeEmulationGraphTask::Payload& computeEmulationPayload
){
    using namespace RendererTaskGraphDetail;
    if(!inputs.integrationTask.valid() || !inputs.uploadTask.valid())
        return false;

    const Array<Core::GpuTaskResourceUse, 13u> baseResourceUses = {
        ReadUse(inputs.albedo),
        ReadUse(inputs.normal, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.depth, Core::ResourceStates::DepthRead),
        ReadUse(inputs.avboitTransmittance),
        ReadUse(inputs.avboitDepthWarp),
        ReadUse(inputs.avboitControl),
        ReadUse(inputs.refractionInstance),
        ReadUse(inputs.refractionDepth),
        ReadWriteUse(inputs.avboitForegroundColor, Core::ResourceStates::RenderTarget),
        ReadWriteUse(inputs.avboitForegroundExtinction, Core::ResourceStates::RenderTarget),
        ReadWriteUse(inputs.avboitAccumColor, Core::ResourceStates::RenderTarget),
        ReadWriteUse(inputs.avboitAccumExtinction, Core::ResourceStates::RenderTarget),
    };
    NWB_DEFINE_AVBOIT_SHARED_COMPUTE_EMULATION_PHASE_TABLES("render.avboit.accumulation", "AVBOIT Accumulation");

    RendererAvboitTaskGraphStageState& stage = m_avboitSystem.taskGraphStage();
    if(!DeclareAvboitRasterRecordPhase<AvboitAccumulationComputeEmulationGraphTask, AvboitAccumulationSharedComputeEmulationGraphTask, AvboitAccumulationGraphTask>(
        m_graph,
        m_graphics,
        m_materialSystem,
        scratchArena,
        frameBindings,
        objectGeometry,
        inputs,
        inputs.uploadTask,
        baseResourceUses,
        inputs.accumulationTimingTicket,
        inputs.accumulationComputeEmulationTiming,
        NWB_MAKE_AVBOIT_RASTER_RECORD_PHASE_DESC(
            "render.avboit.accumulation",
            "AVBOIT Accumulation",
            Name("render.avboit.accumulation"),
            "AVBOIT Accumulation",
            AvboitRecordInputsBase::s_ResourceBaseCount
        ),
        accumulationPayload,
        computeEmulationPayload,
        AvboitRasterRecordStageOutputs{
            .reusedGeometryProducer = stage.m_accumulationReusedGeometryProducer,
            .streamTask = stage.m_accumulationStreamTask,
            .computeEmulationTask = stage.m_accumulationComputeEmulationTask,
            .sharedComputeEmulationTasks = stage.m_accumulationSharedComputeEmulationTasks,
            .sharedComputeEmulationTaskCount = stage.m_accumulationSharedComputeEmulationTaskCount,
            .rasterTask = stage.m_accumulationTask,
        }
    ))
        return false;

    const Array<Core::GpuTaskResourceUse, 5u> accumulationFinalizeResourceUses = {
        ReadUse(inputs.avboitAccumColor, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.avboitForegroundColor),
        ReadUse(inputs.avboitForegroundExtinction),
        ReadUse(inputs.avboitAccumExtinction, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.depth, Core::ResourceStates::ShaderResource),
    };
    Core::GpuTaskSchedulingHint accumulationFinalizeScheduling;
    accumulationFinalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
    accumulationFinalizeScheduling.forceSubmissionBoundary = false;
    accumulationFinalizeScheduling.allowPacketMerge = true;
    accumulationFinalizeScheduling.mergeWithPrevious = true;
    // Finalizer is Accumulation's tail; retain its timing/acceptance packet before Lighting.
    accumulationFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc accumulationFinalizeDesc;
    accumulationFinalizeDesc
        .setIdentity(Name("render.avboit.accumulation_finalize"))
        .setMarkerLabel("AVBOIT Accumulation Finalize")
        .setScheduling(accumulationFinalizeScheduling)
        .setDependencies({ &stage.m_accumulationTask, 1u })
        .setResourceUses(accumulationFinalizeResourceUses)
    ;
    const auto accumulationFinalizeTaskResult = m_graph.addTask<AvboitAccumulationFinalizeGraphTask>(accumulationFinalizeDesc, AvboitAccumulationFinalizeGraphTask::Payload{});
    if(!accumulationFinalizeTaskResult){
        stage.m_accumulationFinalizeTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation finalizer graph task"));
        return false;
    }
    stage.m_accumulationFinalizeTask = *accumulationFinalizeTaskResult;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

