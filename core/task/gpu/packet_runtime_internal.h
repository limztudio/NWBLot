// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "packet_runtime.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuNativePacketRecorder final : NoCopy{
    friend class GpuTaskScheduler;

private:
    class PacketRecordingExceptionScope;
    class PacketArtifactPublicationScope;
    class ReadyFrontierRecordingUnwindScope;


private:
    explicit GpuNativePacketRecorder(Device& device)
        : m_device(device)
    {}
    GpuNativePacketRecorder(Device& device, GpuTimingRecorder& timingRecorder)
        : m_device(device)
        , m_timingRecorder(&timingRecorder)
    {}


private:
    // Records one compiler-derived non-empty contiguous range. Earlier producer packets needed by the range must already be recorded, which keeps deliberate late tails separate from the ordinary graph prefix.
    [[nodiscard]] bool recordPacketRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* commandIrCapture = nullptr
    )const;
    // Semantic companion to the packet-range recorder. Task endpoints resolve only after compilation, keeping renderer record spans independent from packet splitting and merging while preserving intentional late tails.
    [[nodiscard]] bool recordTaskRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId firstTask,
        GpuTaskId lastTask,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* commandIrCapture = nullptr
    )const;
    // Records compiler-ready frontiers with `cpuScheduler`. Only packets whose tasks all set GpuTaskSchedulingHint::allowParallelRecording may share a worker frontier; every other packet remains serial. Command-IR capture deliberately keeps the established serial order. The method is synchronous: callers may submit or destroy the recorded graph once it returns.
    [[nodiscard]] bool recordPacketRangeInReadyFrontiers(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        GpuRecordedGraph& outRecordedGraph,
        CpuTaskScheduler& cpuScheduler,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* commandIrCapture = nullptr
    )const;


private:
    // Caller must own artifactAccess and complete prepareRecordingAttempt().
    [[nodiscard]] bool recordPreparedPacketRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        const GpuSubmissionPacketRange& range,
        GpuRecordedGraph& outRecordedGraph,
        GpuRecordedGraph::PacketRecordingScratch& scratch,
        Alloc::ScratchArena& stateFanInScratchArena,
        GpuCommandIrCapture* commandIrCapture,
        GpuSubmissionPacketId* outFailedPacket
    )const;
    [[nodiscard]] bool recordPacket(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        GpuSubmissionPacketId packet,
        GpuRecordedGraph& outRecordedGraph,
        GpuRecordedGraph::PacketRecordingScratch& scratch,
        Alloc::ScratchArena& stateFanInScratchArena,
        GpuCommandIrCapture* commandIrCapture,
        u64 recordingWorkerDomain = 0u,
        u32 recordingWorkerIndex = 0u,
        GpuTaskGraph::PacketRecordingAbort* deferredAbort = nullptr
    )const;
    [[nodiscard]] bool prepareRecordingAttempt(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        GpuRecordedGraph& outRecordedGraph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess
    )const;
    [[nodiscard]] bool preflightPacketResources(
        const GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuTaskGraph::PacketRecordingAccess& recordingAccess,
        GpuSubmissionPacketId packet,
        const CommandListResourceStateHandoff* initialStates
    )const noexcept;


private:
    Device& m_device;
    // Optional because all-None graphs retain the existing recorder path. A timing-aware recorder must outlive every GpuRecordedGraph ticket created through this instance.
    GpuTimingRecorder* m_timingRecorder = nullptr;
};


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

