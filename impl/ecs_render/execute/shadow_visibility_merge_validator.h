// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/task/gpu/compiled_graph.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererFramePipeline;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Prepared shadow callbacks share one acceptance endpoint; combined stages omit their independent predecessors.
struct PreparedShadowVisibilityTasks{
    Core::GpuTaskId terminal;
    Core::GpuTaskId opaque;
    Core::GpuTaskId opaqueFirstWavelet;
    Core::GpuTaskId opaqueResolve;
    Core::GpuTaskId transparentTrace;
    Core::GpuTaskId transparentTemporalMerge;
    Core::GpuTaskId transparentFirstWavelet;
    Core::GpuTaskId diagnostics;
    b8 combinedUpsample = false;
    b8 combinedWavelet = false;
    b8 combinedTemporal = false;
};

[[nodiscard]] b8 PreparedShadowVisibilityTasksSharePacket(const Core::GpuCompiledGraph::ReadView& compiledPlan, const PreparedShadowVisibilityTasks& tasks)noexcept;


// Shadow-visibility merge validation owns packet-merge checks for visibility tasks.
struct ShadowVisibilityMergeValidationResult{
    b8 preparedTasksMerged = false;
    b8 allLitClearMerged = false;
    b8 adaptivePrimitivesMerged = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShadowVisibilityMergeValidator final : NoCopy{
public:
    explicit ShadowVisibilityMergeValidator(NotNull<RendererFramePipeline*> pipeline)noexcept;


public:
    void validate(const Core::GpuCompiledGraph::ReadView& compiledPlan, ShadowVisibilityMergeValidationResult& outResult)const noexcept;


private:
    NotNull<RendererFramePipeline*> m_pipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

