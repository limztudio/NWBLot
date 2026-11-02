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


// Surfel-GI plus caustics merge validation owns packet-merge checks for GI tasks.
struct SurfelCausticsMergeValidationResult{
    const Core::GpuPhysicalQueueInfo* hardwareCausticsQueue = nullptr;
    const Core::GpuPhysicalQueueInfo* surfelGiQueue = nullptr;
    const Core::GpuPhysicalQueueInfo* surfelGiPreparationQueue = nullptr;
    const Core::GpuPhysicalQueueInfo* surfelGiSnapshotCopyQueue = nullptr;
    const Core::GpuPhysicalQueueInfo* surfelGiCounterReadbackQueue = nullptr;
    b8 surfelGiOutputClearMergedIntoGiPacket = false;
    b8 surfelGiPreparedPrefixMergedIntoGiPacket = false;
    b8 surfelGiInitializationLifecycleMergedIntoPreparationPacket = false;
    b8 causticPhotonMergedIntoCausticsPacket = false;
    b8 causticGeometryMergedIntoCausticsPacket = false;
    b8 causticResolvePrepareMergedIntoCausticsPacket = false;
    b8 causticResolveWaveletsMergedIntoCausticsPacket = false;
    b8 causticResolveUpsampleMergedIntoCausticsPacket = false;
    b8 causticIrradianceClearMergedIntoCausticsPacket = false;
    b8 causticAccumulatorBootstrapClearMergedIntoCausticsPacket = false;
    b8 causticAccumulatorDecayMergedIntoCausticsPacket = false;
    b8 surfelGiSnapshotCopyAndTimingPacketsAreDistinct = false;
    b8 surfelCounterReadbackFollowsPresentation = false;
    b8 laggedLightingHistoryFollowsPresentation = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SurfelCausticsMergeValidator final : NoCopy{
public:
    explicit SurfelCausticsMergeValidator(const RendererFramePipeline& pipeline)noexcept
        : m_pipeline(pipeline)
    {}


public:
    void validate(const Core::GpuCompiledGraph::ReadView& compiledPlan, Core::GpuTaskId causticsTask, Core::GpuTaskId terminalPresentationTask, b8 captureLaggedLightingHistory, SurfelCausticsMergeValidationResult& outResult)const noexcept;


private:
    const RendererFramePipeline& m_pipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

