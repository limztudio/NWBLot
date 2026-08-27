// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_stage.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererAvboitTaskGraphStageState::reset()noexcept{
    m_clearFirstTask = {};
    m_clearTask = {};
    m_transparentCsgIntervalClearFirstTask = {};
    m_transparentCsgIntervalClearTask = {};
    m_preTask = {};
    m_csgReceiverSpanTask = {};
    m_csgIntervalCombineTask = {};
    m_occupancyStreamTask = {};
    m_occupancyComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_occupancySharedComputeEmulationTasks)
        task = {};
    m_occupancySharedComputeEmulationTaskCount = 0u;
    m_occupancyTask = {};
    m_depthWarpTask = {};
    m_extinctionStreamTask = {};
    m_extinctionComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_extinctionSharedComputeEmulationTasks)
        task = {};
    m_extinctionSharedComputeEmulationTaskCount = 0u;
    m_extinctionTask = {};
    m_integrationTask = {};
    m_accumulationStreamTask = {};
    m_accumulationComputeEmulationTask = {};
    for(Core::GpuTaskId& task : m_accumulationSharedComputeEmulationTasks)
        task = {};
    m_accumulationSharedComputeEmulationTaskCount = 0u;
    m_accumulationTask = {};
    m_accumulationFinalizeTask = {};
}


RendererTaskGraphTransparencyStage RendererAvboitTaskGraphStageState::transparencyStage()const noexcept{
    return RendererTaskGraphTransparencyStage{
        .firstTask = m_preTask,
        .completionTask = m_accumulationFinalizeTask.valid() ? m_accumulationFinalizeTask : m_occupancyTask,
        .hasTransparentTasks = m_depthWarpTask.valid(),
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

