// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "packet_runtime.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuPacketRuntimeDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline const GpuTaskGraphInitialOwnerHandoffSourceView* FindInitialOwnerHandoffSource(
    const GpuTaskGraphResourceView& resource,
    const GpuCompiledBarrier& barrier
)noexcept{
    if(
        resource.initialOwnerHandoffSourceCount == 0u
        || !resource.initialOwnerHandoffSources
        || resource.type != GpuGraphResourceType::Texture
    )
        return nullptr;

    const GpuTaskGraphInitialOwnerHandoffSourceView* result = nullptr;
    for(usize sourceIndex = 0u;
        sourceIndex < resource.initialOwnerHandoffSourceCount;
        ++sourceIndex
    ){
        const GpuTaskGraphInitialOwnerHandoffSourceView& source = resource.initialOwnerHandoffSources[sourceIndex];
        if(
            source.sourceQueue != barrier.sourceQueue
            || source.destinationQueue != barrier.destinationQueue
            || !source.range.textureSubresources.contains(barrier.range.textureSubresources)
        )
            continue;
        if(result)
            return nullptr;
        result = &source;
    }
    return result;
}

[[nodiscard]] inline bool ValidateExternalCompletionBindings(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskGraphExternalCompletionToken* const bindings,
    const usize bindingCount
){
    if(bindingCount != 0u && !bindings)
        return false;

    for(usize bindingIndex = 0u; bindingIndex < bindingCount; ++bindingIndex){
        const GpuTaskGraphExternalCompletionToken& binding = bindings[bindingIndex];
        if(!binding.validFallbackFor(graph, compiledGraph))
            return false;
        for(usize previousIndex = 0u; previousIndex < bindingIndex; ++previousIndex){
            if(bindings[previousIndex].completion == binding.completion)
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

