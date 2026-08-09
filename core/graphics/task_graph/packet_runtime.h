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
    // Native recording owns its newly-created lists through submission.
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
    // A serial predecessor on this packet's physical queue may provide its complete final snapshot as the native
    // packet's base.  It deliberately remains unfiltered so resources not touched by this packet survive for later
    // declaration-filtered consumers; independent producer branches must use externalStateSources instead.
    const CommandListResourceStateHandoff* serialStateSeed = nullptr;
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
    // Records one non-empty contiguous compiler-order range. Earlier producer packets needed by the range must
    // already be recorded, which keeps deliberate late tails separate from the ordinary graph prefix.
    [[nodiscard]] bool recordPacketRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        usize firstPacketIndex,
        const GpuNativePacketRecordDesc* recordDescs,
        usize recordDescCount,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;
    // Records every compiler packet in its stable dependency order.  This is the native path for a graph whose
    // complete packet chain is known before submission; callers retain recordPacket for intentional partial work.
    [[nodiscard]] bool recordPacketsInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecordDesc* recordDescs,
        usize recordDescCount,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr
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


// Associates one compiler-generated packet with the timing ticket that must submit its native command list.  A
// packet omitted from a compile-order submission uses the ordinary non-timing transport.
struct GpuTaskGraphPacketTimingTicket{
    GpuSubmissionPacketId packet;
    GpuTimingSubmissionTicket* timingTicket = nullptr;
};


// Runs immediately after a packet accepts its submission token. Returning false stops the current range traversal,
// but retains the accepted packet so the caller can submit an appropriate recovery or finalization tail.
struct GpuTaskGraphPacketAcceptedCallback{
    void* context = nullptr;
    [[nodiscard]] bool (*invoke)(
        void* context,
        const GpuSubmissionPacketId& packet,
        const QueueSubmissionToken& token
    ) = nullptr;
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
    // Submits one non-empty contiguous compiler-order range. Dependencies outside the range must already be
    // accepted in the transaction; this preserves graph-owned waits while allowing intentional late tails. An
    // accepted callback may stop the range after retaining its packet for recovery or finalization.
    [[nodiscard]] bool submitPacketRangeInCompileOrder(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        usize firstPacketIndex,
        usize packetCount,
        const GpuTaskGraphExternalCompletionToken* externalCompletionTokens,
        usize externalCompletionTokenCount,
        const GpuTaskGraphPacketTimingTicket* timingTickets,
        usize timingTicketCount,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        const GpuTaskGraphPacketAcceptedCallback* acceptedCallback = nullptr
    )const;
    // Submits every compiler packet in its stable dependency order.  Internal packet dependencies are resolved
    // from the shared transaction; external bindings and timing tickets are supplied once for the whole graph.
    [[nodiscard]] bool submitPacketsInCompileOrder(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        const GpuTaskGraphExternalCompletionToken* externalCompletionTokens,
        usize externalCompletionTokenCount,
        const GpuTaskGraphPacketTimingTicket* timingTickets,
        usize timingTicketCount,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;


private:
    Device& m_device;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
