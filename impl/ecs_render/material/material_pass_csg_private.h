#pragma once


#include <impl/ecs_render/material/renderer_pipeline_types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialPassCsgBindingSets{
    const Core::BindingSetHandle& clip;
    const Core::BindingSetHandle& receiverSurface;
    const Core::BindingSetHandle& intervalSample;
};

[[nodiscard]] NWB_INLINE bool MaterialPassCsgResourcesReadyForPipelineKey(
    const MaterialPipelineKey& pipelineKey,
    const MaterialPipelinePass::Enum pass,
    const MaterialPassCsgBindingSets& bindingSets,
    const bool requireIntervalSample
){
    const MaterialPipelineCsgBindingUse csgBindingUse = MaterialPipelineResolveCsgBindingUse(pipelineKey, pass);
    if(csgBindingUse.clip && !bindingSets.clip)
        return false;
    if(csgBindingUse.receiverSurface && !bindingSets.receiverSurface)
        return false;
    if(requireIntervalSample && csgBindingUse.intervalSample && !bindingSets.intervalSample)
        return false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

