// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"

#include <core/graphics/rhi/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuGraphSubmissionTransaction;


// One accepted terminal packet that contributes to a graph-to-external resource handoff. Several disjoint texture
// subresource ranges may be published by different packets; a consumer waits on the returned compact token frontier
// before opening the handoff's merged native state source.
struct GpuTaskGraphExternalResourceHandoffProducer{
    GpuTaskId producerTask;
    GpuPhysicalQueueId sourceQueue;
    QueueSubmissionToken token;
};

// One exact terminal range that contributes to a graph-to-external handoff. A later graph can turn each range into
// an immutable initial-owner source with the corresponding graph-local completion token; direct native consumers
// normally use the compact `waitTokens` frontier instead.
struct GpuTaskGraphExternalResourceHandoffRange{
    GpuTaskResourceRange range;
    GpuTaskId producerTask;
    GpuPhysicalQueueId sourceQueue;
    QueueSubmissionToken token;
};

// Immutable view into one caller-owned GpuTaskGraphExternalResourceHandoffSnapshot. Copying this view does not copy
// its pointed-to arrays; keep the snapshot alive and externally serialize every read against refill or destruction
// of that same snapshot.
struct GpuTaskGraphExternalResourceHandoff{
    u64 planGeneration = 0u;
    GpuGraphResourceId resource;
    GpuTaskId producerTask;
    GpuPhysicalQueueId sourceQueue;
    GpuPhysicalQueueId destinationQueue;
    ResourceStates::Mask finalState = ResourceStates::Unknown;
    QueueSubmissionToken token;
    const GpuTaskGraphExternalResourceHandoffProducer* producers = nullptr;
    usize producerCount = 0u;
    const QueueSubmissionToken* waitTokens = nullptr;
    usize waitTokenCount = 0u;
    usize terminalRangeCount = 0u;
    const GpuTaskGraphExternalResourceHandoffRange* terminalRanges = nullptr;
    const CommandListResourceStateHandoff* stateSource = nullptr;


    [[nodiscard]] bool valid()const noexcept{
        return planGeneration != 0u
            && resource.valid()
            && destinationQueue.valid()
            && finalState != ResourceStates::Unknown
            && producerCount != 0u
            && producers
            && waitTokenCount != 0u
            && waitTokens
            && terminalRangeCount != 0u
            && terminalRanges
            && stateSource
        ;
    }
};


// Owns every producer, range, wait token, and native state snapshot returned by an external-resource handoff query.
// Queries build into the inactive same-arena role and publish with one non-throwing owner swap, so a false return or
// allocation exception preserves the previously published snapshot exactly. Refill, inspection, and destruction of
// one snapshot are externally serialized; independent snapshot objects may be queried concurrently.
class GpuTaskGraphExternalResourceHandoffSnapshot final : NoCopy{
    friend class GpuGraphSubmissionTransaction;

private:
    struct Storage;


public:
    explicit GpuTaskGraphExternalResourceHandoffSnapshot(GraphicsArena& arena);
    ~GpuTaskGraphExternalResourceHandoffSnapshot();


public:
    [[nodiscard]] const GpuTaskGraphExternalResourceHandoff* value()const noexcept;
    [[nodiscard]] bool valid()const noexcept;
    [[nodiscard]] bool validFor(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess
    )const noexcept;


private:
    GraphicsArena& m_arena;
    GlobalUniquePtr<Storage> m_activeStorage;
    GlobalUniquePtr<Storage> m_candidateStorage;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

