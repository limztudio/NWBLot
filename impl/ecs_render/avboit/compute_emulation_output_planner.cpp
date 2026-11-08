// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compute_emulation_output_planner.h"

#include <impl/ecs_render/avboit/record_inputs_base.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitComputeEmulationOutputPlanner::Prepare(
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena,
    const AvboitRecordInputsBase& inputs,
    const AvboitRasterRecordPhaseDesc& phaseDesc,
    AvboitRasterPassTaskPayload& rasterPayload,
    const AvboitComputeEmulationTaskPayload& computeEmulationPayload,
    Optional<Core::GpuTimingMeasure>& computeEmulationTiming,
    AvboitComputeEmulationOutputs& result
){
    using namespace RendererTaskGraphDetail;
    const b8 generatedGeometryReused = inputs.generatedGeometryReused();

    auto& computeEmulationOutputSet = result.computeEmulationOutputSet;
    const b8 computeEmulationPlanCaptured = inputs.regularComputeEmulationPlanCaptured || inputs.csgComputeEmulationPlanCaptured;
    auto& computeEmulationOutputStatesGraphOwned = result.computeEmulationOutputStatesGraphOwned;
    if(inputs.regularComputeEmulationPlanCaptured){
        const auto outputSetResult = GatherImportedOutputBufferResourceSet(
            graph,
            computeEmulationPayload.plan,
            scratchArena,
            phaseDesc.regularOutputSetIdentity,
            phaseDesc.regularOutputSetLabel
        );
        computeEmulationOutputStatesGraphOwned = outputSetResult.has_value();
        if(outputSetResult)
            computeEmulationOutputSet = *outputSetResult;
    }
    else if(inputs.csgComputeEmulationPlanCaptured){
        const auto outputSetResult = GatherImportedOutputBufferResourceSet(
            graph,
            computeEmulationPayload.csgPlan,
            scratchArena,
            phaseDesc.csgOutputSetIdentity,
            phaseDesc.csgOutputSetLabel
        );
        computeEmulationOutputStatesGraphOwned = outputSetResult.has_value();
        if(outputSetResult)
            computeEmulationOutputSet = *outputSetResult;
    }
    if(computeEmulationPlanCaptured && !computeEmulationOutputStatesGraphOwned)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT compute-emulation output states"));
    if(generatedGeometryReused && !computeEmulationOutputStatesGraphOwned)
        return false;
    rasterPayload.computeEmulationOutputStatesGraphOwned = inputs.regularComputeEmulationPlanCaptured && computeEmulationOutputStatesGraphOwned;
    rasterPayload.csgComputeEmulationOutputStatesGraphOwned = inputs.csgComputeEmulationPlanCaptured && computeEmulationOutputStatesGraphOwned;
    rasterPayload.computeEmulationTiming = computeEmulationOutputStatesGraphOwned ? &computeEmulationTiming : nullptr;

    auto& sharedComputeEmulationOutput = result.sharedComputeEmulationOutput;
    auto& sharedComputeEmulationOutputStatesGraphOwned = result.sharedComputeEmulationOutputStatesGraphOwned;
    if(inputs.sharedComputeEmulationPlanCaptured){
        const auto sharedOutputResult = GatherRegularSharedComputeEmulationResource(graph, inputs.sharedComputeEmulationPlan, phaseDesc.sharedOutputLabel);
        sharedComputeEmulationOutputStatesGraphOwned = sharedOutputResult.has_value();
        if(sharedOutputResult)
            sharedComputeEmulationOutput = *sharedOutputResult;
    }
    if(inputs.sharedComputeEmulationPlanCaptured && !sharedComputeEmulationOutputStatesGraphOwned)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT shared compute-emulation output state"));

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

