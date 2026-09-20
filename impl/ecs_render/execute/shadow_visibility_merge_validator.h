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


// Prepared shadow callbacks share one acceptance endpoint. Fusion omits only the opaque upsample tail.
struct PreparedShadowVisibilityTasks{
    Core::GpuTaskId terminal;
    Core::GpuTaskId opaque;
    Core::GpuTaskId opaqueFirstWavelet;
    Core::GpuTaskId opaqueResolve;
    Core::GpuTaskId transparentTrace;
    Core::GpuTaskId transparentTemporalMerge;
    Core::GpuTaskId transparentFirstWavelet;
    bool combinedUpsample = false;
    bool combinedWavelet = false;
};

[[nodiscard]] bool PreparedShadowVisibilityTasksSharePacket(
    const Core::GpuCompiledGraph::ReadView& compiledPlan,
    const PreparedShadowVisibilityTasks& tasks
);


// Shadow-visibility merge validation owns packet-merge checks for visibility tasks.
struct ShadowVisibilityMergeValidationResult{
    bool preparedTasksMerged = false;
    bool allLitClearMerged = false;
    bool adaptivePrimitivesMerged = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShadowVisibilityMergeValidator final : NoCopy{
public:
    explicit ShadowVisibilityMergeValidator(NotNull<RendererFramePipeline*> pipeline);


public:
    void validate(
        const Core::GpuCompiledGraph::ReadView& compiledPlan,
        ShadowVisibilityMergeValidationResult& outResult
    )const;


private:
    NotNull<RendererFramePipeline*> m_pipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

