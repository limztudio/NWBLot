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
        || (resource.type != GpuGraphResourceType::Texture && resource.type != GpuGraphResourceType::Buffer)
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
            || (resource.type == GpuGraphResourceType::Texture
                ? !source.range.textureSubresources.contains(barrier.range.textureSubresources)
                : !source.range.bufferRange.contains(barrier.range.bufferRange)
            )
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
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuTaskGraphExternalCompletionToken* const bindings,
    const usize bindingCount
){
    if(bindingCount != 0u && !bindings)
        return false;

    for(usize bindingIndex = 0u; bindingIndex < bindingCount; ++bindingIndex){
        const GpuTaskGraphExternalCompletionToken& binding = bindings[bindingIndex];
        if(!binding.validFallbackFor(graph, declarationAccess, compiledGraph, planAccess))
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

