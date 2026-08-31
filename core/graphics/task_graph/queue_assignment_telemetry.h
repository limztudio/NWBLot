// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiler.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuGraphSubmissionTransaction;

namespace GpuTaskQueueAssignmentAcceptance{
    enum Enum : u8{
        NotAccepted,
        First,
        Unchanged,
        Changed,

        kCount,
    };
};

struct GpuTaskQueueAssignmentTelemetry{
    GpuTaskQueueAssignment assignment;
    GpuPhysicalQueueId acceptedQueue;
    GpuPhysicalQueueId previousAcceptedQueue;
    GpuTaskQueueAssignmentAcceptance::Enum acceptance = GpuTaskQueueAssignmentAcceptance::NotAccepted;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraphQueueAssignmentTelemetryTracker final : NoCopy{
private:
    using AcceptedQueueHistory = HashMap<Name, GpuPhysicalQueueId, Hasher<Name>, EqualTo<Name>, GraphicsArena>;


public:
    explicit GpuTaskGraphQueueAssignmentTelemetryTracker(GraphicsArena& arena)
        : m_current(arena)
        , m_history(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}


public:
    void reset();
    [[nodiscard]] bool update(
        const GpuTaskGraph& graph,
        const GpuTaskGraphQueueAssignments& assignments,
        const GpuCompiledGraph& compiledGraph,
        const GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena
    );

    [[nodiscard]] bool validFor(
        const GpuTaskGraph& graph,
        const GpuTaskGraphQueueAssignments& assignments,
        const GpuCompiledGraph& compiledGraph
    )const noexcept;
    [[nodiscard]] const GpuTaskQueueAssignmentTelemetry* find(GpuTaskId task)const noexcept;


private:
    GraphicsVector<GpuTaskQueueAssignmentTelemetry> m_current;
    AcceptedQueueHistory m_history;
    u64 m_generation = 0u;
    u64 m_declarationRevision = 0u;
    u64 m_planGeneration = 0u;
    u64 m_recordingAttemptGeneration = 0u;
    u64 m_acceptanceRevision = 0u;
    u16 m_deviceGeneration = 0u;
    bool m_valid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

