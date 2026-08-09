// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"

#include <core/alloc/scratch.h>
#include <core/graphics/rhi/device.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingSubmissionTicket;
class GpuTaskGraph;
struct GpuExternalPacketStateSource;


struct GpuRecordedPacket{
    static constexpr usize s_MaxCommandLists = 12u;

    GpuSubmissionPacketId packet;
    // Native recording owns its newly-created lists. Imported packets borrow renderer-owned lists that remain alive
    // through submission, allowing a graph packet to take over established multi-list transport incrementally.
    CommandListHandle ownedCommandLists[s_MaxCommandLists];
    CommandList* commandLists[s_MaxCommandLists] = {};
    u8 commandListCount = 0u;
};


class GpuRecordedGraph final : NoCopy{
    friend class GpuNativePacketRecorder;

public:
    explicit GpuRecordedGraph(GraphicsArena& arena)
        : m_arena(arena)
        , m_packets(arena)
        , m_packetStateSeeds(arena)
        , m_initialStateSeed(arena)
        , m_stateSubsetScratch(arena)
        , m_stateMergeScratch(arena)
        , m_externalBaseStateSeed(arena)
        , m_externalMergedStateSeed(arena)
    {}


public:
    void reset(const GpuCompiledGraph& compiledGraph);

    [[nodiscard]] bool validFor(const GpuCompiledGraph& compiledGraph)const noexcept;
    [[nodiscard]] const GpuRecordedPacket* find(const GpuSubmissionPacketId& packet)const noexcept;
    // Read-only export for a later graph or cross-frame state cache that needs this packet's actual native final
    // state. Graph-internal consumers use compiler-produced state seeds instead.
    [[nodiscard]] const CommandListResourceStateHandoff* packetFinalStateSeed(
        const GpuSubmissionPacketId& packet
    )const noexcept;


private:
    [[nodiscard]] bool buildPacketInitialStateSeed(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketId& packet,
        const GpuExternalPacketStateSource* externalStateSources,
        usize externalStateSourceCount,
        const CommandListResourceStateHandoff*& outInitialStates
    );
    [[nodiscard]] CommandListResourceStateHandoff* packetStateSeed(const GpuSubmissionPacketId& packet)noexcept;
    [[nodiscard]] const CommandListResourceStateHandoff* packetStateSeed(const GpuSubmissionPacketId& packet)const noexcept;


private:
    GraphicsArena& m_arena;
    GraphicsVector<GpuRecordedPacket> m_packets;
    GraphicsVector<CommandListResourceStateHandoff> m_packetStateSeeds;
    CommandListResourceStateHandoff m_initialStateSeed;
    CommandListResourceStateHandoff m_stateSubsetScratch;
    CommandListResourceStateHandoff m_stateMergeScratch;
    CommandListResourceStateHandoff m_externalBaseStateSeed;
    CommandListResourceStateHandoff m_externalMergedStateSeed;
    u64 m_generation = 0u;
    u16 m_deviceGeneration = 0u;
    bool m_valid = false;
};


// An external producer's native final state. Packet recording filters every source through the consuming task's
// declared imported resources, including declared texture ranges, so renderer subsystems never hand-build state
// subsets or fan-ins for graph-owned packet boundaries.
struct GpuExternalPacketStateSource{
    const CommandListResourceStateHandoff* states = nullptr;
};


// Native graph packets always receive compiler-produced internal state seeds, declaration-selected external state
// seeds, and compiler-planned packet-boundary barriers. Task thunks retain only intra-task synchronization.
struct GpuNativePacketRecordDesc{
    GpuSubmissionPacketId packet;
    const GpuExternalPacketStateSource* externalStateSources = nullptr;
    usize externalStateSourceCount = 0u;
};


// Imported command lists have already been opened, closed, and state-handoffed by their renderer work. The graph
// becomes the authoritative packet/submission owner without forcing an all-at-once recording-body rewrite.  The
// imported packet captures its final seed once so following graph packets consume the graph-owned export instead
// of the renderer's temporary handoff.
struct GpuImportedPacketRecordDesc{
    GpuSubmissionPacketId packet;
    CommandList* const* commandLists = nullptr;
    usize commandListCount = 0u;
    const CommandListResourceStateHandoff* stateSeed = nullptr;
};


// One explicit merged packet may retain an imported metadata-only prefix while moving its ordered suffix tasks into
// native graph recording. The imported lists must already be recorded in execution order and export the exact state
// from which the native suffix begins. Every task accepts or discards together because they share one queue
// submission.
struct GpuImportedPacketNativeSuffixRecordDesc{
    GpuSubmissionPacketId packet;
    CommandList* const* importedCommandLists = nullptr;
    usize importedCommandListCount = 0u;
    const CommandListResourceStateHandoff* importedStateSeed = nullptr;
};


class GpuNativePacketRecorder final : NoCopy{
public:
    explicit GpuNativePacketRecorder(Device& device)
        : m_device(device)
    {}


public:
    [[nodiscard]] bool recordPacket(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecordDesc& desc,
        GpuRecordedGraph& outRecordedGraph
    )const;
    [[nodiscard]] bool recordImportedPacket(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuImportedPacketRecordDesc& desc,
        GpuRecordedGraph& outRecordedGraph
    )const;
    [[nodiscard]] bool recordImportedPacketNativeSuffix(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuImportedPacketNativeSuffixRecordDesc& desc,
        GpuRecordedGraph& outRecordedGraph
    )const;


private:
    Device& m_device;
};


namespace GpuPacketRuntimeState{
    enum Enum : u8{
        Declared,
        Recorded,
        Accepted,
        Rejected,
        Retired,
    };
};

struct GpuPacketRuntime{
    GpuPacketRuntimeState::Enum state = GpuPacketRuntimeState::Declared;
    QueueSubmissionToken token;
};

struct GpuTaskGraphExternalCompletionToken{
    GpuExternalCompletionId completion;
    QueueSubmissionToken token;
};


class GpuGraphSubmissionTransaction final : NoCopy{
public:
    explicit GpuGraphSubmissionTransaction(GraphicsArena& arena)
        : m_packets(arena)
        , m_latestAcceptedQueueTokens(arena)
    {}


public:
    void reset(const GpuCompiledGraph& compiledGraph);

    [[nodiscard]] bool validFor(const GpuCompiledGraph& compiledGraph)const noexcept;
    [[nodiscard]] bool markPacketRecorded(const GpuSubmissionPacketId& packet)noexcept;
    void acceptPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketId& packet,
        const QueueSubmissionToken& token
    )noexcept;
    void rejectPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketId& packet
    )noexcept;
    void discardUnaccepted(GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph)noexcept;

    [[nodiscard]] QueueSubmissionToken packetToken(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] const QueueSubmissionToken* latestAcceptedToken(const GpuPhysicalQueueId& queue)const noexcept;
    [[nodiscard]] const GpuPacketRuntime* packetRuntime(const GpuSubmissionPacketId& packet)const noexcept;


private:
    struct LatestAcceptedQueueToken{
        GpuPhysicalQueueId queue;
        QueueSubmissionToken token;
    };

    GraphicsVector<GpuPacketRuntime> m_packets;
    GraphicsVector<LatestAcceptedQueueToken> m_latestAcceptedQueueTokens;
    u64 m_generation = 0u;
    u16 m_deviceGeneration = 0u;
    bool m_valid = false;
};


class GpuTaskGraphSubmitter final : NoCopy{
public:
    explicit GpuTaskGraphSubmitter(Device& device)
        : m_device(device)
    {}


public:
    [[nodiscard]] bool submitPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        const GpuSubmissionPacketId& packet,
        const GpuTaskGraphExternalCompletionToken* externalCompletionTokens,
        usize externalCompletionTokenCount,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuTimingSubmissionTicket* timingTicket = nullptr
    )const;


private:
    Device& m_device;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
