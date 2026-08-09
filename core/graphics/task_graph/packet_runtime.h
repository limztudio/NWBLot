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
    // Native recording owns its one newly-created list. Imported packets borrow renderer-owned lists that remain
    // alive through submission, allowing a graph packet to take over established multi-list transport incrementally.
    CommandListHandle ownedCommandList;
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
    {}


public:
    void reset(const GpuCompiledGraph& compiledGraph);

    [[nodiscard]] bool validFor(const GpuCompiledGraph& compiledGraph)const noexcept;
    [[nodiscard]] const GpuRecordedPacket* find(const GpuSubmissionPacketId& packet)const noexcept;
    // Transitional read-only export for an external graph that still needs a packet's actual native final state.
    // Graph-internal consumers use compiler-produced state seeds instead.
    [[nodiscard]] const CommandListResourceStateHandoff* packetFinalStateSeed(
        const GpuSubmissionPacketId& packet
    )const noexcept;


private:
    [[nodiscard]] bool buildPacketInitialStateSeed(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketId& packet,
        const CommandListResourceStateHandoff* externalInitialStates,
        const GpuExternalPacketStateSource* externalStateSources,
        usize externalStateSourceCount,
        bool includeCompiledStateSeeds,
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
    u64 m_generation = 0u;
    u16 m_deviceGeneration = 0u;
    bool m_valid = false;
};


// Selects the precise resources a packet needs from a completed external producer's native final state. This is the
// transitional cross-graph/cross-frame counterpart to compiler-produced packet state seeds.
struct GpuExternalPacketStateSource{
    const CommandListResourceStateHandoff* states = nullptr;
    Texture* const* textures = nullptr;
    usize textureCount = 0u;
    Buffer* const* buffers = nullptr;
    usize bufferCount = 0u;
};


// Transitional packet state handoffs seed imported/existing work at open and export final state at close. Graph
// barriers already lower through this recorder; the remaining bridge disappears once compiler-produced packet
// state seeds cover cross-graph and cross-frame imports.
struct GpuNativePacketRecordDesc{
    GpuSubmissionPacketId packet;
    const CommandListResourceStateHandoff* initialStates = nullptr;
    CommandListResourceStateHandoff* finalStates = nullptr;
    // Resource-filtered final states from completed external producers. They merge with initialStates before native
    // recording and remain distinct from graph-internal compiler state seeds.
    const GpuExternalPacketStateSource* externalStateSources = nullptr;
    usize externalStateSourceCount = 0u;
    // Uses compiler-selected producer snapshots for graph-internal resource state.  External/cross-frame inputs
    // remain in initialStates until their producers join the same graph.
    bool useCompiledStateSeeds = false;
    // Transitional opt-in while renderer tasks shed their local boundary transitions.  New graph-owned tasks set
    // this true; imported or not-yet-migrated record thunks retain their established state bridge.
    bool applyCompiledBarriers = false;
};


// Imported command lists have already been opened, closed, and state-handoffed by their renderer work. The graph
// becomes the authoritative packet/submission owner without forcing an all-at-once recording-body rewrite.
struct GpuImportedPacketRecordDesc{
    GpuSubmissionPacketId packet;
    CommandList* const* commandLists = nullptr;
    usize commandListCount = 0u;
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
