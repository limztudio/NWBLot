// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_desc.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph;
class GpuTaskGraphCompiler;
class GpuCommandIrCapture;


// Tasks describe semantic work. Packets are compiler-generated native-recording and submission units; a packet
// contains one or more explicitly compatible tasks.
struct GpuSubmissionPacket{
    GpuPhysicalQueueId queue;
    u32 taskOffset = 0u;
    u32 taskCount = 0u;
    u32 dependencyOffset = 0u;
    u32 dependencyCount = 0u;
    u32 externalDependencyOffset = 0u;
    u32 externalDependencyCount = 0u;
    // Compiler-derived ready-frontier depth for native recording. Packets in one depth have no internal packet
    // producer relationship, so the runtime may record opted-in packets concurrently while retaining deterministic
    // compile-order submission.
    u32 recordingFrontier = 0u;
    // A late recovery/finalization packet receives waits for the latest accepted packet on every other physical
    // queue directly from GpuGraphSubmissionTransaction. It is a graph runtime policy, not a renderer token ladder.
    bool joinsAcceptedQueueFrontier = false;
    bool recordsTiming = true;
};

struct GpuPacketDependency{
    GpuSubmissionPacketId producer;
    GpuSubmissionPacketId consumer;
};

struct GpuCompiledTask{
    GpuTaskId task;
    GpuPhysicalQueueId queue;
    GpuSubmissionPacketId packet;
    u32 prologueStateSeedOffset = 0u;
    u32 prologueStateSeedCount = 0u;
    u32 prologueBarrierOffset = 0u;
    u32 prologueBarrierCount = 0u;
    u32 epilogueBarrierOffset = 0u;
    u32 epilogueBarrierCount = 0u;
};

// One terminal declared range that contributes to an explicit graph-to-external release.  Textures may have
// several disjoint terminal subresource ranges, potentially recorded by different packets and physical queues.
// The runtime turns every source into one acceptance-gated producer token and merges their exact exported state
// ranges before handing the texture to native code. Buffer and AS releases remain one-packet because their native
// tracking is whole-allocation.
struct GpuCompiledExternalResourceExportSource{
    GpuTaskId producerTask;
    GpuPhysicalQueueId sourceQueue;
    GpuTaskResourceRange range;
};

// Imported texture/buffer/AS graph-to-external release metadata.  `producerTask`/`sourceQueue` retain the
// original one-producer convenience contract whenever every terminal range resolves to the same packet.  Callers
// that need the complete contract must consume `sourceCount` sources through externalResourceExportSources().
struct GpuCompiledExternalResourceExport{
    GpuGraphResourceId resource;
    GpuTaskId producerTask;
    GpuPhysicalQueueId sourceQueue;
    u32 sourceOffset = 0u;
    u32 sourceCount = 0u;
    GpuPhysicalQueueId destinationQueue;
    ResourceStates::Mask finalState = ResourceStates::Unknown;
};


class GpuCompiledGraph final : NoCopy{
    friend class GpuTaskGraphCompiler;

public:
    explicit GpuCompiledGraph(GraphicsArena& arena);


public:
    void reset();

    [[nodiscard]] bool valid()const noexcept{ return m_valid && m_planGeneration != 0u; }
    [[nodiscard]] bool validFor(const GpuTaskGraph& graph)const noexcept;
    [[nodiscard]] u64 generation()const noexcept{ return m_generation; }
    // Graph tasks/resources retain generation(), while compiler-owned packet identities use this immutable-plan
    // generation so a same-graph recompile cannot alias old native recording or submission state.
    [[nodiscard]] u64 planGeneration()const noexcept{ return m_planGeneration; }
    [[nodiscard]] u16 deviceGeneration()const noexcept{ return m_deviceGeneration; }
    [[nodiscard]] usize taskCount()const noexcept{ return m_tasks.size(); }
    [[nodiscard]] usize packetCount()const noexcept{ return m_packets.size(); }
    [[nodiscard]] bool validPacket(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] bool validPacketRange(const GpuSubmissionPacketRange& range)const noexcept;
    // Compiler packet indices follow the stable topological task order.  Full-graph native record/submit traversal
    // consumes this order so each packet observes already-recorded and accepted internal producers.
    [[nodiscard]] GpuSubmissionPacketId packetIdAt(usize index)const noexcept;
    // Derives an inclusive contiguous compiler-order range from packet handles. This keeps callers independent from
    // the compiler's raw packet indices while still rejecting handles from another immutable compiled plan.
    [[nodiscard]] GpuSubmissionPacketRange packetRange(
        const GpuSubmissionPacketId& first,
        const GpuSubmissionPacketId& last
    )const noexcept;
    // Semantic companion to packetRange(). Resolves both declared task endpoints through this compiled generation,
    // keeping renderer record/submit spans independent from packet splitting and merging. The result remains an
    // inclusive contiguous compiler-order range, so deliberately late tails retain their existing traversal rules.
    [[nodiscard]] GpuSubmissionPacketRange packetRangeForTasks(
        const GpuTaskId& first,
        const GpuTaskId& last
    )const noexcept;
    [[nodiscard]] GpuSubmissionPacketRange allPacketRange()const noexcept;
    [[nodiscard]] const GpuCompiledTask* findTask(const GpuTaskId& task)const noexcept;
    [[nodiscard]] GpuSubmissionPacketId packetForTask(const GpuTaskId& task)const noexcept;
    // Semantic packet-topology queries.  Renderer policy can validate coalescing and routing without retaining
    // compiler packet IDs; packet handles remain available below for packet-local runtime compatibility only.
    [[nodiscard]] bool tasksSharePacket(const GpuTaskId& first, const GpuTaskId& second)const noexcept;
    [[nodiscard]] bool taskPrecedesOrSharesPacket(const GpuTaskId& first, const GpuTaskId& second)const noexcept;
    // Strict semantic compiler-order query for two tasks in one packet. This keeps renderer validation out of the
    // packet task array while retaining the difference between an in-packet ordering guarantee and an ordinary
    // cross-packet dependency.
    [[nodiscard]] bool taskPrecedesInSamePacket(const GpuTaskId& first, const GpuTaskId& second)const noexcept;
    // Returns true only when the declared tasks occur as one exact contiguous sequence in the same compiled
    // packet. The runtime owns the packet-local search, so renderer code need not retain compiler packet IDs just
    // to validate graph-owned producer/raster alternation.
    [[nodiscard]] bool tasksFormContiguousPacketSequence(
        const GpuTaskId* tasks,
        usize taskCount
    )const noexcept;
    [[nodiscard]] bool taskJoinsAcceptedQueueFrontier(const GpuTaskId& task)const noexcept;
    [[nodiscard]] const GpuPhysicalQueueInfo* queueInfoForTask(const GpuTaskId& task)const noexcept;
    [[nodiscard]] const GpuSubmissionPacket& packet(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] const GpuTaskId* packetTasks(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] const GpuPacketDependency* packetDependencies(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] const GpuExternalCompletionId* packetExternalDependencies(
        const GpuSubmissionPacketId& packet
    )const noexcept;
    [[nodiscard]] const GpuPacketStateSeed* taskPrologueStateSeeds(const GpuTaskId& task)const noexcept;
    [[nodiscard]] const GpuCompiledBarrier* taskPrologueBarriers(const GpuTaskId& task)const noexcept;
    [[nodiscard]] const GpuCompiledBarrier* taskEpilogueBarriers(const GpuTaskId& task)const noexcept;
    // Resolves the terminal graph-to-external release declaration for this imported resource. No result means the
    // resource did not request a graph-to-external handoff in this compiled generation.
    [[nodiscard]] const GpuCompiledExternalResourceExport* externalResourceExport(
        const GpuGraphResourceId& resource
    )const noexcept;
    [[nodiscard]] usize externalResourceExportCount()const noexcept{ return m_externalResourceExports.size(); }
    [[nodiscard]] const GpuCompiledExternalResourceExport* externalResourceExportAt(usize index)const noexcept;
    [[nodiscard]] const GpuCompiledExternalResourceExportSource* externalResourceExportSources(
        const GpuCompiledExternalResourceExport& exportInfo
    )const noexcept;
    [[nodiscard]] const GpuPhysicalQueueInfo* queueInfo(const GpuPhysicalQueueId& queue)const noexcept;


private:
    GraphicsVector<GpuCompiledTask> m_tasks;
    GraphicsVector<GpuSubmissionPacket> m_packets;
    GraphicsVector<GpuTaskId> m_packetTasks;
    GraphicsVector<GpuPacketDependency> m_packetDependencies;
    GraphicsVector<GpuExternalCompletionId> m_packetExternalDependencies;
    GraphicsVector<GpuPacketStateSeed> m_prologueStateSeeds;
    GraphicsVector<GpuCompiledBarrier> m_prologueBarriers;
    GraphicsVector<GpuCompiledBarrier> m_epilogueBarriers;
    GraphicsVector<GpuCompiledExternalResourceExport> m_externalResourceExports;
    GraphicsVector<GpuCompiledExternalResourceExportSource> m_externalResourceExportSources;
    GraphicsVector<GpuPhysicalQueueInfo> m_queueTopology;
    u64 m_generation = 0u;
    u64 m_planGeneration = 0u;
    u16 m_deviceGeneration = 0u;
    usize m_graphTaskCount = 0u;
    bool m_valid = false;
};


// Recording happens only after the compiler has selected a concrete physical queue and packet.  Task record thunks
// receive immutable compiled metadata rather than a nullable renderer-specific context.
struct GpuTaskRecordContext{
    const GpuTaskGraph& taskGraph;
    const GpuCompiledGraph& graph;
    GpuTaskId task;
    GpuSubmissionPacketId packet;
    GpuPhysicalQueueId queue;
    // Optional Phase 11 tooling sink. Null keeps direct native task recording on the ordinary runtime path.
    GpuCommandIrCapture* commandIrCapture = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

