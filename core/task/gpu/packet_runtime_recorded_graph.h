// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"
#include "packet_runtime_artifact.h"
#include "task_graph.h"

#include <core/alloc/scratch.h>
#include <core/graphics/rhi/device.h>

#include <global/sync.h>
#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder;
class GpuTimingSubmissionTicket;
class GpuTaskScheduler;
class GpuNativePacketRecorder;
class GpuGraphSubmissionTransaction;
struct GpuTaskGraphRuntimeStatistics;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuPacketRuntimeDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_PacketStateFanInScratchArena("core/task/gpu/packet_state_fan_in");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuRecordedPacket{
    static constexpr usize s_MaxCommandLists = 12u;

    GpuSubmissionPacketId packet;
    // Native recording retains its acquired lists through submission.
    CommandListHandle ownedCommandLists[s_MaxCommandLists] = {};
    CommandList* commandLists[s_MaxCommandLists] = {};
    // Exact publication identities let reset/destruction revoke an unsubmitted list without touching a later recording that reuses the same retained CommandList object.
    u64 commandListRecordingLeaseSerials[s_MaxCommandLists] = {};
    // These fields are written before commandListCount publishes the slot.
    // They intentionally describe the packet after graph lowering, compile tooling can distinguish declared work from the native work that was recorded.
    u32 taskCount = 0u;
    u32 barrierCount = 0u;
    f64 commandListAcquisitionSeconds = 0.0;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    // Monotonic steady-clock endpoints make actual CPU recording overlap observable without exposing Timer in the public packet snapshot.
    // Both endpoints are published before commandListCount makes the slot visible.
    u64 recordingBeginNanoseconds = 0u;
    u64 recordingEndNanoseconds = 0u;
    f64 recordingSeconds = 0.0;
    // Worker zero is serial/default recording; ready-frontier workers retain a process-unique CpuTaskScheduler domain and scheduler-local index.
    u64 recordingWorkerDomain = 0u;
    u32 recordingWorkerIndex = 0u;
    u8 commandListCount = 0u;
};


// Snapshot of published packet slots (parallel recording). recordingSeconds sums per-packet spans (slot occupancy, not CPU). Phase counters split acquisition/barriers/callbacks.
struct GpuTaskGraphRecordingStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    u16 deviceGeneration = 0u;
    usize packetCount = 0u;
    usize taskCount = 0u;
    usize commandListCount = 0u;
    usize barrierCount = 0u;
    // workerRoutedPacketCount includes one-packet worker batches; parallelPacketCount requires overlapping intervals.
    usize workerRoutedPacketCount = 0u;
    usize parallelPacketCount = 0u;
    f64 commandListAcquisitionSeconds = 0.0;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    f64 recordingSeconds = 0.0;
    // Sum of successful recorder-operation wall spans; separate range calls exclude intervening caller work.
    f64 recordingElapsedSeconds = 0.0;
    f64 readyFrontierElapsedSeconds = 0.0;
    // Busy is summed packet-span occupancy across logical recording slots, not operating-system CPU time.
    // Capacity is readyFrontierElapsedSeconds weighted per call by every callable CpuTaskScheduler slot, including its caller.
    f64 readyFrontierWorkerBusySeconds = 0.0;
    f64 readyFrontierWorkerCapacitySeconds = 0.0;


public:
    // The utilization is splat across all lanes.
    [[nodiscard]] SIMDVectorDouble readyFrontierWorkerUtilization()const noexcept{
        if(!(readyFrontierWorkerCapacitySeconds > 0.0))
            return VectorDoubleZero();
        return VectorDoubleSaturate(VectorDoubleDivide(VectorDoubleReplicate(readyFrontierWorkerBusySeconds), VectorDoubleReplicate(readyFrontierWorkerCapacitySeconds)));
    }
    [[nodiscard]] bool valid()const noexcept{ return graphGeneration != 0u && planGeneration != 0u; }
};


// Immutable native recording telemetry for one physical queue in one recording attempt. Queue identity carries the device generation, so a recreated device cannot alias it.
struct GpuTaskGraphPhysicalQueueRecordingStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    GpuPhysicalQueueId queue;
    u16 deviceGeneration = 0u;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    usize packetCount = 0u;
    usize taskCount = 0u;
    usize commandListCount = 0u;
    usize barrierCount = 0u;
    usize workerRoutedPacketCount = 0u;
    usize parallelPacketCount = 0u;
    f64 commandListAcquisitionSeconds = 0.0;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    f64 recordingSeconds = 0.0;


public:
    [[nodiscard]] bool valid()const noexcept{
        return graphGeneration != 0u && planGeneration != 0u && deviceGeneration != 0u && queue.valid() && queue.deviceGeneration == deviceGeneration && queueClass < CommandQueue::kCount;
    }
};


class GpuRecordedGraph final : NoCopy{
    friend GpuTaskGraphRuntimeStatistics CollectGpuTaskGraphRuntimeStatistics(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuRecordedGraph& recordedGraph, const GpuGraphSubmissionTransaction& transaction)noexcept;
    friend class GpuGraphSubmissionTransaction;
    friend class GpuNativePacketRecorder;
    friend class GpuTaskScheduler;


private:
    struct ArtifactStorage;
    enum class ArtifactOperationMode : u8{
        Read,
        WaitRead,
        Exclusive,
    };
    class ArtifactOperation final : NoCopy{
    private:
        static thread_local ArtifactOperation* s_ActiveOperation;


    public:
        [[nodiscard]] static bool Active()noexcept{ return s_ActiveOperation != nullptr; }
        [[nodiscard]] static bool ActiveFor(const GpuRecordedGraph& recordedGraph)noexcept;
        [[nodiscard]] static bool ActiveExclusiveFor(const GpuRecordedGraph& recordedGraph)noexcept;
        [[nodiscard]] static bool ActiveScopeIs(const ArtifactOperation& operation)noexcept{
            if(!operation.m_ownsAdmission || !operation.m_recordedGraph)
                return false;
            bool foundOperation = false;
            for(const ArtifactOperation* active = s_ActiveOperation; active; active = active->m_previousOperation){
                if(active->m_recordedGraph != operation.m_recordedGraph)
                    return false;
                if(active == &operation)
                    foundOperation = true;
            }
            return foundOperation;
        }


    public:
        ArtifactOperation(const GpuRecordedGraph& recordedGraph, ArtifactOperationMode mode)noexcept;
        ~ArtifactOperation()noexcept;


    public:
        [[nodiscard]] bool valid()const noexcept{ return m_recordedGraph != nullptr; }
        [[nodiscard]] bool validFor(const GpuRecordedGraph& recordedGraph)const noexcept{
            return m_recordedGraph == &recordedGraph;
        }
        [[nodiscard]] bool exclusiveFor(const GpuRecordedGraph& recordedGraph)const noexcept{
            return m_recordedGraph == &recordedGraph && m_exclusive;
        }


    private:
        const GpuRecordedGraph* m_recordedGraph = nullptr;
        ArtifactOperation* m_previousOperation = nullptr;
        bool m_exclusive = false;
        bool m_ownsAdmission = false;
    };
    // Every ready-frontier worker receives isolated state-handoff scratch.
    // This is separate from the per-packet final-state slots, are written only by the packet's own recording worker and read by later frontiers.
    struct PacketRecordingScratch final : NoCopy{
        struct CachedCommandList{
            CommandListParameters parameters;
            CommandListHandle commandList;
        };

        GlobalUniquePtr<Alloc::ScratchArena> stateFanInScratchArena;
        CommandListResourceStateHandoff initialStateSeed;
        CommandListResourceStateHandoff stateSubsetScratch;
        CommandListResourceStateHandoff stateMergeScratch;
        CommandListResourceStateHandoff externalBaseStateSeed;
        CommandListResourceStateHandoff externalMergedStateSeed;
        GraphicsVector<CachedCommandList> commandLists;


    public:
        explicit PacketRecordingScratch(GraphicsArena& arena)noexcept
            : initialStateSeed(arena)
            , stateSubsetScratch(arena)
            , stateMergeScratch(arena)
            , externalBaseStateSeed(arena)
            , externalMergedStateSeed(arena)
            , commandLists(arena)
        {}


    public:
        [[nodiscard]] bool ensureValid(GraphicsArena& arena);
        [[nodiscard]] Expected<CommandListHandle> acquireCommandList(Device& device, const CommandListParameters& parameters);
        void reset()noexcept;
        [[nodiscard]] bool valid()const noexcept{ return stateFanInScratchArena != nullptr; }
    };


private:
    static constexpr u32 s_ArtifactOperationWriterBit = 1u << 31u;
    static constexpr u32 s_ArtifactOperationReaderMask = s_ArtifactOperationWriterBit - 1u;


private:
    static void RetireStorageWithoutCallbacks(ArtifactStorage& storage)noexcept;


public:
    explicit GpuRecordedGraph(GraphicsArena& arena)noexcept;
    ~GpuRecordedGraph()noexcept;


public:
    // A live record/submit operation leases every packet, timing ticket, state seed, and command-list handle in this artifact.
    // Failed reset leaves all storage and exact publication identities unchanged so the operation can finish or unwind safely
    [[nodiscard]] bool tryReset(const GpuCompiledGraph& compiledGraph);
    void reset(const GpuCompiledGraph& compiledGraph);
    // Join graph/native work before releasing device-bound cached recording objects.
    [[nodiscard]] bool releaseCommandListCache()noexcept;


private:
    void revokeCommandListPublicationsWithoutCallbacks(ArtifactStorage& storage)noexcept;
    [[nodiscard]] bool prepareStorageCandidateLayout(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess);
    [[nodiscard]] bool prepareResetStorageCandidate(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess);
    [[nodiscard]] bool prepareRecordingStorageCandidate(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuTimingRecorder* timingRecorder);
    void publishStorageCandidate(const GpuTaskGraph* graphIdentity, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, u64 recordingAttemptGeneration, const ArtifactOperation& artifactAccess)noexcept;
    [[nodiscard]] bool validForWithinArtifactOperation(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const ArtifactOperation& artifactAccess)const noexcept;
    [[nodiscard]] bool validForWithinArtifactOperation(
        const GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const ArtifactOperation& artifactAccess
    )const noexcept;


public:
    [[nodiscard]] bool validFor(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess)const noexcept;
    [[nodiscard]] bool validFor(const GpuTaskGraph& graph, const GpuTaskGraph::DeclarationReadView& declarationAccess, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess)const noexcept;
    [[nodiscard]] u64 recordingAttemptGeneration()const noexcept;
    // Returns an owned aggregate from packet slots published under artifact admission. The exact plan proof keeps packet metadata alive and rejects an artifact from another compiled object.
    [[nodiscard]] GpuTaskGraphRecordingStatistics recordingStatistics(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess)const noexcept;
    // Returns an owned physical-queue aggregate. Invalid/stale queue IDs and a recorded artifact from another exact compiled object return an empty result.
    [[nodiscard]] GpuTaskGraphPhysicalQueueRecordingStatistics physicalQueueRecordingStatistics(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuPhysicalQueueId& queue)const noexcept;
    [[nodiscard]] Optional<GpuRecordedPacket> packetSnapshot(const GpuSubmissionPacketId& packet)const noexcept;
    // Validates this recorded graph against current compiler output and resolves the declared task's packet. The result is that packet's native final state.
    [[nodiscard]] bool hasTaskFinalStateSeed(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuTaskId task)const noexcept;
    [[nodiscard]] bool copyTaskFinalStateSeed(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuTaskId task, CommandListResourceStateHandoff& outStateSeed)const;


private:
    void discardPacketTimingTicket(const GpuSubmissionPacketId& packet, const ArtifactOperation& artifactAccess);
    void abandonPacketTimingTicketWithoutCallbacks(const GpuSubmissionPacketId& packet, const ArtifactOperation& artifactAccess)noexcept;
    [[nodiscard]] GpuTimingSubmissionTicket* packetTimingTicket(const GpuSubmissionPacketId& packet, const ArtifactOperation& artifactAccess)const noexcept;
    [[nodiscard]] Expected<const CommandListResourceStateHandoff*> buildPacketInitialStateSeed(
        PacketRecordingScratch& scratch,
        Alloc::ScratchArena& stateFanInScratchArena,
        const GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const ArtifactOperation& artifactAccess,
        const GpuSubmissionPacketId& packet
    );
    [[nodiscard]] CommandListResourceStateHandoff* packetStateSeed(const GpuSubmissionPacketId& packet, const ArtifactOperation& artifactAccess)noexcept;
    [[nodiscard]] const CommandListResourceStateHandoff* packetStateSeed(const GpuSubmissionPacketId& packet, const ArtifactOperation& artifactAccess)const noexcept;
    [[nodiscard]] PacketRecordingScratch* packetRecordingScratch(const GpuSubmissionPacketId& packet, const ArtifactOperation& artifactAccess)noexcept;
    [[nodiscard]] PacketRecordingScratch* serialRecordingScratch(const ArtifactOperation& artifactAccess)noexcept;
    [[nodiscard]] GpuRecordedPacket* packetStorage(const GpuSubmissionPacketId& packet, const ArtifactOperation& artifactAccess)noexcept;
    void clearPacketPublicationWithoutCallbacks(const GpuSubmissionPacketId& packet, const ArtifactOperation& artifactAccess)noexcept;
    [[nodiscard]] const GpuRecordedPacket* findWithinArtifactOperation(const GpuSubmissionPacketId& packet, const ArtifactOperation& artifactAccess)const noexcept;
    [[nodiscard]] GpuTimingRecorder* timingRecorderWithinArtifactOperation(const ArtifactOperation& artifactAccess)const noexcept;
    [[nodiscard]] u64 recordingAttemptGenerationWithinArtifactOperation(const ArtifactOperation& artifactAccess)const noexcept;
    void addRecordingElapsedSeconds(f64 elapsedSeconds, const ArtifactOperation& artifactAccess)noexcept;
    void addReadyFrontierStatistics(f64 elapsedSeconds, f64 workerBusySeconds, f64 workerCapacitySeconds, const ArtifactOperation& artifactAccess)noexcept;
    void cachePacketRecordingOverlaps(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const Vector<u32, Alloc::ScratchArena>& packetIndices, Alloc::ScratchArena& scratchArena, const ArtifactOperation& artifactAccess);


private:
    GraphicsArena& m_arena;
    GlobalUniquePtr<ArtifactStorage> m_activeStorage;
    GlobalUniquePtr<ArtifactStorage> m_candidateStorage;
    // Submission readers may overlap on independent queues. Conflicting record/submit/reset admission is nonblocking, while destruction alone joins operations that own the artifact.
    mutable Atomic<u32> m_operationState{ 0u };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

