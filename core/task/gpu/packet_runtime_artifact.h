// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"

#include <core/graphics/rhi/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuGraphSubmissionTransaction;


// One accepted terminal packet feeding a graph-to-external handoff. Consumers wait on the token frontier before opening the merged state.
struct GpuTaskGraphExternalResourceHandoffProducer{
    GpuTaskId producerTask;
    GpuPhysicalQueueId sourceQueue;
    QueueSubmissionToken token;
};

// One exact terminal range in a graph-to-external handoff. A later graph may turn each range into an initial-owner source.
struct GpuTaskGraphExternalResourceHandoffRange{
    GpuTaskResourceRange range;
    GpuTaskId producerTask;
    GpuPhysicalQueueId sourceQueue;
    QueueSubmissionToken token;
};

// View into a caller-owned snapshot. Copies share the arrays; keep the snapshot alive and serialize reads against refill/destroy.
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


public:
    [[nodiscard]] b8 valid()const noexcept{
        return planGeneration != 0u && resource.valid() && destinationQueue.valid() && finalState != ResourceStates::Unknown && producerCount != 0u && producers && waitTokenCount != 0u && waitTokens && terminalRangeCount != 0u && terminalRanges && stateSource;
    }
};


// External handoff snapshot: builds into the inactive role, publishes via one non-throwing swap (failure keeps the prior snapshot).
// Per-object serialization; distinct objects concurrent.
class GpuTaskGraphExternalResourceHandoffSnapshot final : NoCopy{
    friend class GpuGraphSubmissionTransaction;


private:
    struct Storage;


public:
    explicit GpuTaskGraphExternalResourceHandoffSnapshot(GraphicsArena& arena)noexcept;
    ~GpuTaskGraphExternalResourceHandoffSnapshot()noexcept;


public:
    [[nodiscard]] const GpuTaskGraphExternalResourceHandoff* value()const noexcept;
    [[nodiscard]] b8 valid()const noexcept;
    [[nodiscard]] b8 validFor(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess)const noexcept;


private:
    GraphicsArena& m_arena;
    GlobalUniquePtr<Storage> m_activeStorage;
    GlobalUniquePtr<Storage> m_candidateStorage;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

