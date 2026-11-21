// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_desc.h"

#include <core/alloc/scratch.h>

#include <global/optional.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph;
class GpuTaskGraphDeclarationReadView;
class GpuTaskGraphCompiler;
class GpuCommandIrCapture;
class GpuGraphSubmissionBinding;
class GpuGraphSubmissionTransaction;
class GpuNativePacketRecorder;
class GpuRecordedGraph;
class GpuTaskScheduler;

struct GpuSubmissionPacket{
    u32 taskOffset = 0u;
    u32 taskCount = 0u;
    u32 dependencyOffset = 0u;
    u32 dependencyCount = 0u;
    u32 externalDependencyOffset = 0u;
    u32 externalDependencyCount = 0u;
    u32 recordingFrontier = 0u;
    GpuPhysicalQueueId queue;
    b8 joinsAcceptedQueueFrontier = false;
    b8 isRecoverySubmission = false;
    b8 recordsPacketEnvelopeTiming = false;
    b8 recordsTiming = false;
};

struct GpuPacketDependency{
    GpuSubmissionPacketId producer;
    GpuSubmissionPacketId consumer;
};

struct GpuCompiledPacketView{
    GpuSubmissionPacketId id;
    const GpuSubmissionPacket* plan = nullptr;
    const GpuTaskId* tasks = nullptr;
    const GpuPacketDependency* dependencies = nullptr;
    const GpuExternalCompletionId* externalDependencies = nullptr;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return id.valid() && plan && (plan->taskCount == 0u || tasks) && (plan->dependencyCount == 0u || dependencies) && (plan->externalDependencyCount == 0u || externalDependencies);
    }
    [[nodiscard]] Span<const GpuTaskId> taskSpan()const noexcept{
        if(!valid())
            return {};
        return { tasks, plan->taskCount };
    }
    [[nodiscard]] Optional<GpuSubmissionPacket> packetPlan()const noexcept{
        if(!plan)
            return Optional<GpuSubmissionPacket>();
        return Optional<GpuSubmissionPacket>(*plan);
    }
    [[nodiscard]] b8 hasTasks()const noexcept{ return tasks != nullptr; }
    [[nodiscard]] Span<const GpuTaskId> tasksSpan()const noexcept{
        return plan && tasks ? Span<const GpuTaskId>(tasks, plan->taskCount) : Span<const GpuTaskId>();
    }
    [[nodiscard]] b8 hasDependencies()const noexcept{ return dependencies != nullptr; }
    [[nodiscard]] Span<const GpuPacketDependency> dependenciesSpan()const noexcept{
        return plan && dependencies ? Span<const GpuPacketDependency>(dependencies, plan->dependencyCount) : Span<const GpuPacketDependency>();
    }
    [[nodiscard]] b8 hasExternalDependencies()const noexcept{ return externalDependencies != nullptr; }
    [[nodiscard]] Span<const GpuExternalCompletionId> externalDependenciesSpan()const noexcept{
        return plan && externalDependencies ? Span<const GpuExternalCompletionId>(externalDependencies, plan->externalDependencyCount) : Span<const GpuExternalCompletionId>();
    }
};

namespace GpuTaskPacketizationDecision{
    enum Enum : u8{
        Unknown,
        FirstTask,
        MergeNotRequested,
        TaskForcesBoundary,
        QueueChanged,
        PrecedingTaskForcesBoundary,
        ScoredMergeIneligible,
        MergeRequiresExplicitImmediateDependency,
        CrossQueueConsumerFrontier,
        MergedExplicit,
        MergedFrontierScored,
        ScoredMergeDomainMismatch,

        Count,
    };
};

struct GpuCompiledTask{
    GpuTaskId task;
    GpuSubmissionPacketId packet;
    u32 prologueStateSeedOffset = 0u;
    u32 prologueStateSeedCount = 0u;
    u32 prologueBarrierOffset = 0u;
    u32 prologueBarrierCount = 0u;
    u32 epilogueBarrierOffset = 0u;
    u32 epilogueBarrierCount = 0u;
    GpuPhysicalQueueId queue;
    GpuTaskPacketizationDecision::Enum packetizationDecision = GpuTaskPacketizationDecision::Unknown;
    GpuTaskTimingPolicy::Enum timingPolicy = GpuTaskTimingPolicy::None;
    b8 recordsNonCommittingTimingSample = false;
};

struct GpuCompiledTaskView{
    const GpuCompiledTask* plan = nullptr;
    const GpuPacketStateSeed* prologueStateSeeds = nullptr;
    const GpuCompiledBarrier* prologueBarriers = nullptr;
    const GpuCompiledBarrier* epilogueBarriers = nullptr;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return plan && (plan->prologueStateSeedCount == 0u || prologueStateSeeds) && (plan->prologueBarrierCount == 0u || prologueBarriers) && (plan->epilogueBarrierCount == 0u || epilogueBarriers);
    }

    [[nodiscard]] b8 recordsNonCommittingTimingSample()const noexcept{ return plan && plan->recordsNonCommittingTimingSample; }
    [[nodiscard]] b8 hasPrologueStateSeeds()const noexcept{ return prologueStateSeeds != nullptr; }
    [[nodiscard]] Span<const GpuPacketStateSeed> prologueStateSeedsSpan()const noexcept{
        return plan && prologueStateSeeds ? Span<const GpuPacketStateSeed>(prologueStateSeeds, plan->prologueStateSeedCount) : Span<const GpuPacketStateSeed>();
    }
    [[nodiscard]] b8 hasPrologueBarriers()const noexcept{ return prologueBarriers != nullptr; }
    [[nodiscard]] Span<const GpuCompiledBarrier> prologueBarriersSpan()const noexcept{
        return plan && prologueBarriers ? Span<const GpuCompiledBarrier>(prologueBarriers, plan->prologueBarrierCount) : Span<const GpuCompiledBarrier>();
    }
    [[nodiscard]] b8 hasEpilogueBarriers()const noexcept{ return epilogueBarriers != nullptr; }
    [[nodiscard]] Span<const GpuCompiledBarrier> epilogueBarriersSpan()const noexcept{
        return plan && epilogueBarriers ? Span<const GpuCompiledBarrier>(epilogueBarriers, plan->epilogueBarrierCount) : Span<const GpuCompiledBarrier>();
    }
    [[nodiscard]] Optional<GpuCompiledTask> taskPlan()const noexcept{
        return plan ? Optional<GpuCompiledTask>(*plan) : Optional<GpuCompiledTask>();
    }
};

struct GpuCompiledExternalResourceExportSource{
    GpuTaskId producerTask;
    GpuPhysicalQueueId sourceQueue;
    GpuTaskResourceRange range;
};

struct GpuCompiledExternalResourceExport{
    GpuGraphResourceId resource;
    GpuTaskId producerTask;
    GpuPhysicalQueueId sourceQueue;
    u32 sourceOffset = 0u;
    u32 sourceCount = 0u;
    GpuPhysicalQueueId destinationQueue;
    ResourceStates::Mask finalState = ResourceStates::Unknown;
};

struct GpuCompiledExternalResourceExportView{
    const GpuCompiledExternalResourceExport* plan = nullptr;
    const GpuCompiledExternalResourceExportSource* sources = nullptr;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return plan && plan->sourceCount != 0u && sources;
    }
    [[nodiscard]] b8 hasSources()const noexcept{ return sources != nullptr; }
    [[nodiscard]] Span<const GpuCompiledExternalResourceExportSource> sourcesSpan()const noexcept{
        return plan && sources ? Span<const GpuCompiledExternalResourceExportSource>(sources, plan->sourceCount) : Span<const GpuCompiledExternalResourceExportSource>();
    }
    [[nodiscard]] Optional<GpuCompiledExternalResourceExport> exportPlan()const noexcept{
        return plan ? Optional<GpuCompiledExternalResourceExport>(*plan) : Optional<GpuCompiledExternalResourceExport>();
    }
};

struct GpuCompiledPresentEndpoint{
    GpuTaskId producer;
    GpuGraphResourceId backBuffer;
    GpuSubmissionPacketId packet;
    GpuPhysicalQueueId queue;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return producer.valid() && backBuffer.valid() && packet.valid() && queue.valid();
    }
};

namespace GpuOwnershipTransferRoute{
    enum Enum : u8{
        Internal,
        ExternalImport,
        ExternalExport,

        Count,
    };
};

struct GpuCompiledOwnershipTransfer{
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    GpuTaskId sourceTask;
    GpuTaskId destinationTask;
    GpuSubmissionPacketId sourcePacket;
    GpuSubmissionPacketId destinationPacket;
    u32 sourceQueueFamilyIndex = Limit<u32>::s_Max;
    u32 destinationQueueFamilyIndex = Limit<u32>::s_Max;
    Name resourceIdentity = s_NameNone;
    GpuPhysicalQueueId sourceQueue;
    GpuPhysicalQueueId destinationQueue;
    ResourceQueueSharing::Mask declaredQueueSharing = ResourceQueueSharing::Exclusive;
    GpuGraphResourceType::Enum resourceType = GpuGraphResourceType::HazardDomain;
    GpuOwnershipTransferRoute::Enum route = GpuOwnershipTransferRoute::Count;
    b8 concurrentSharingCouldAvoid = false;


public:
    [[nodiscard]] b8 valid()const noexcept;
};

struct GpuTaskGraphCompileStatistics{
    static constexpr usize s_PacketizationDecisionCount = static_cast<usize>(GpuTaskPacketizationDecision::Count);
    static constexpr usize s_OwnershipTransferRouteCount = static_cast<usize>(GpuOwnershipTransferRoute::Count);

    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    usize taskCount = 0u;
    usize resourceCount = 0u;
    usize resourceUseCount = 0u;
    usize resourceVersionCount = 0u;
    usize resourceVersionEdgeCount = 0u;
    usize explicitDependencyCount = 0u;
    usize inferredDependencyCount = 0u;
    usize declaredExternalDependencyCount = 0u;
    usize initialOwnershipExternalDependencyCount = 0u;
    usize initialAvailabilityExternalDependencyCount = 0u;
    usize externalDependencyCount = 0u;
    usize packetCount = 0u;
    usize packetDependencyCount = 0u;
    usize packetExternalDependencyCount = 0u;
    usize crossQueuePacketDependencyCount = 0u;
    usize crossFamilyPacketDependencyCount = 0u;
    usize mergedTaskCount = 0u;
    usize prologueStateSeedCount = 0u;
    usize prologueBarrierCount = 0u;
    usize epilogueBarrierCount = 0u;
    usize transitionBarrierCount = 0u;
    usize uavBarrierCount = 0u;
    usize ownershipReleaseBarrierCount = 0u;
    usize ownershipAcquireBarrierCount = 0u;
    usize stateExportBarrierCount = 0u;
    usize logicalOwnershipTransferCount = 0u;
    usize logicalOwnershipTransferSignatureCount = 0u;
    usize repeatedOwnershipTransferSignatureCount = 0u;
    usize concurrentSharingCouldAvoidTransferCount = 0u;
    usize concurrentSharingAdviceResourceCount = 0u;
    Array<usize, s_OwnershipTransferRouteCount> logicalOwnershipTransferCountByRoute{};
    usize recordingFrontierCount = 0u;
    Array<usize, s_QueueClassCount> taskCountByQueueClass{};
    Array<usize, s_QueueClassCount> packetCountByQueueClass{};
    Array<usize, s_PacketizationDecisionCount> packetizationDecisionCounts{};
    f64 declarationSeconds = 0.0;
    f64 analysisSeconds = 0.0;
    f64 validationSeconds = 0.0;
    f64 dependencyAnalysisSeconds = 0.0;
    f64 hazardAnalysisSeconds = 0.0;
    f64 topologicalOrderSeconds = 0.0;
    f64 queueAssignmentSeconds = 0.0;
    f64 planningSeconds = 0.0;
    f64 totalSeconds = 0.0;
    usize resourceSetCount = 0u;
    usize resourceSetMemberCount = 0u;
    usize directResourceUseCount = 0u;
    usize declaredResourceSetUseCount = 0u;
    usize expandedResourceSetMemberUseCount = 0u;
    usize payloadObjectCount = 0u;
    usize payloadObjectBytes = 0u;
    usize uploadBlobCount = 0u;
    usize uploadBlobBytes = 0u;
    f64 packetizationSeconds = 0.0;
    f64 resourceStatePlanningSeconds = 0.0;
    f64 packetDependencyPlanningSeconds = 0.0;
    u16 deviceGeneration = 0u;


public:
    [[nodiscard]] b8 valid()const noexcept{ return graphGeneration != 0u && planGeneration != 0u; }
};

struct GpuTaskGraphPhysicalQueueCompileStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    GpuPhysicalQueueId queue;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    u16 deviceGeneration = 0u;
    usize taskCount = 0u;
    usize packetCount = 0u;
    usize mergedTaskCount = 0u;
    usize prologueBarrierCount = 0u;
    usize epilogueBarrierCount = 0u;
    usize ownershipReleaseBarrierCount = 0u;
    usize ownershipAcquireBarrierCount = 0u;
    usize incomingLogicalOwnershipTransferCount = 0u;
    usize outgoingLogicalOwnershipTransferCount = 0u;
    usize incomingLogicalOwnershipTransferSignatureCount = 0u;
    usize outgoingLogicalOwnershipTransferSignatureCount = 0u;
    usize incomingRepeatedOwnershipTransferSignatureCount = 0u;
    usize outgoingRepeatedOwnershipTransferSignatureCount = 0u;
    usize concurrentSharingAdviceResourceCount = 0u;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return graphGeneration != 0u && planGeneration != 0u && deviceGeneration != 0u && queue.valid() && queue.deviceGeneration == deviceGeneration && queueClass < CommandQueue::kCount;
    }
};


class GpuCompiledGraph final : NoCopy{
    friend class GpuTaskGraph;
    friend class GpuTaskGraphCompiler;
    friend class GpuGraphSubmissionTransaction;
    friend class GpuNativePacketRecorder;
    friend class GpuRecordedGraph;
    friend class GpuTaskGraphQueueAssignmentTelemetryTracker;
    friend class GpuTaskScheduler;


public:
    class ReadView final : NoCopy{
        friend class GpuCompiledGraph;


    private:
        friend class GpuRecordedGraph;
        friend class GpuNativePacketRecorder;
        friend class GpuTaskScheduler;
        static thread_local ReadView* s_ActiveView;


    public:
        explicit ReadView(const GpuCompiledGraph& graph)noexcept;
        ~ReadView()noexcept;


    public:
        [[nodiscard]] b8 valid()const noexcept{ return m_graph != nullptr; }

        [[nodiscard]] b8 validFor(const GpuCompiledGraph& graph)const noexcept{ return m_graph == &graph; }
        [[nodiscard]] b8 validFor(const GpuTaskGraphDeclarationReadView& graph)const noexcept;
        [[nodiscard]] u64 generation()const noexcept;
        [[nodiscard]] u64 planGeneration()const noexcept;
        [[nodiscard]] u16 deviceGeneration()const noexcept;
        [[nodiscard]] u64 objectIdentity()const noexcept{ return m_graph ? m_graph->m_objectIdentity : 0u; }
        [[nodiscard]] usize taskCount()const noexcept;
        [[nodiscard]] usize packetCount()const noexcept;
        [[nodiscard]] b8 validPacket(const GpuSubmissionPacketId& packetID)const noexcept;
        [[nodiscard]] b8 validPacketRange(const GpuSubmissionPacketRange& range)const noexcept;
        [[nodiscard]] GpuSubmissionPacketId packetIdAt(usize index)const noexcept;
        [[nodiscard]] GpuSubmissionPacketRange packetRange(const GpuSubmissionPacketId& first, const GpuSubmissionPacketId& last)const noexcept;
        [[nodiscard]] GpuSubmissionPacketRange packetRangeForTasks(const GpuTaskId& first, const GpuTaskId& last)const noexcept;
        [[nodiscard]] GpuSubmissionPacketRange allPacketRange()const noexcept;
        [[nodiscard]] GpuSubmissionPacketRange packetTimingEnvelopeRange()const noexcept;
        [[nodiscard]] GpuCompiledTaskView findTask(const GpuTaskId& task)const & noexcept;
        GpuCompiledTaskView findTask(const GpuTaskId& task)const && = delete;
        [[nodiscard]] GpuSubmissionPacketId packetForTask(const GpuTaskId& task)const noexcept;
        [[nodiscard]] GpuTaskPacketizationDecision::Enum packetizationDecisionForTask(const GpuTaskId& task)const noexcept;
        [[nodiscard]] b8 tasksSharePacket(const GpuTaskId& first, const GpuTaskId& second)const noexcept;
        [[nodiscard]] b8 taskPrecedesOrSharesPacket(const GpuTaskId& first, const GpuTaskId& second)const noexcept;
        [[nodiscard]] b8 taskPrecedesInSamePacket(const GpuTaskId& first, const GpuTaskId& second)const noexcept;
        [[nodiscard]] b8 tasksFormContiguousPacketSequence(Span<const GpuTaskId> tasks)const noexcept;
        [[nodiscard]] b8 taskJoinsAcceptedQueueFrontier(const GpuTaskId& task)const noexcept;
        [[nodiscard]] const GpuPhysicalQueueInfo* queueInfoForTask(const GpuTaskId& task)const & noexcept;
        const GpuPhysicalQueueInfo* queueInfoForTask(const GpuTaskId& task)const && = delete;
        [[nodiscard]] Optional<GpuPhysicalQueueInfo> findQueueInfo(const GpuPhysicalQueueId& queue)const noexcept;
        [[nodiscard]] Optional<GpuPhysicalQueueInfo> findQueueInfoForTask(const GpuTaskId& task)const noexcept;
        [[nodiscard]] GpuCompiledPacketView packet(const GpuSubmissionPacketId& packet)const & noexcept;
        GpuCompiledPacketView packet(const GpuSubmissionPacketId& packet)const && = delete;
        [[nodiscard]] GpuCompiledPacketView packetWithTasks(const GpuSubmissionPacketId& packet)const & noexcept;
        GpuCompiledPacketView packetWithTasks(const GpuSubmissionPacketId& packet)const && = delete;
        [[nodiscard]] usize logicalOwnershipTransferCount()const noexcept;
        [[nodiscard]] const GpuCompiledOwnershipTransfer* logicalOwnershipTransfers()const & noexcept;
        const GpuCompiledOwnershipTransfer* logicalOwnershipTransfers()const && = delete;
        [[nodiscard]] Span<const GpuCompiledOwnershipTransfer> logicalOwnershipTransferSpan()const & noexcept;
        Span<const GpuCompiledOwnershipTransfer> logicalOwnershipTransferSpan()const && = delete;
        [[nodiscard]] const GpuCompiledOwnershipTransfer* logicalOwnershipTransferAt(usize index)const & noexcept;
        const GpuCompiledOwnershipTransfer* logicalOwnershipTransferAt(usize index)const && = delete;
        [[nodiscard]] GpuCompiledExternalResourceExportView externalResourceExport(const GpuGraphResourceId& resource)const & noexcept;
        GpuCompiledExternalResourceExportView externalResourceExport(const GpuGraphResourceId& resource)const && = delete;
        [[nodiscard]] const GpuCompiledPresentEndpoint* presentEndpoint()const & noexcept;
        const GpuCompiledPresentEndpoint* presentEndpoint()const && = delete;
        [[nodiscard]] Optional<GpuCompiledPresentEndpoint> findPresentEndpoint()const noexcept{
            if(const auto* endpoint = presentEndpoint())
                return *endpoint;
            return {};
        }
        [[nodiscard]] GpuTaskGraphCompileStatistics compileStatistics()const noexcept;
        [[nodiscard]] GpuTaskGraphPhysicalQueueCompileStatistics physicalQueueCompileStatistics(const GpuPhysicalQueueId& queue)const noexcept;
        [[nodiscard]] const GpuPhysicalQueueInfo* queueInfo(const GpuPhysicalQueueId& queue)const & noexcept;
        const GpuPhysicalQueueInfo* queueInfo(const GpuPhysicalQueueId& queue)const && = delete;
        [[nodiscard]] GpuPhysicalQueueTopology queueTopology()const & noexcept;
        GpuPhysicalQueueTopology queueTopology()const && = delete;


    private:
        [[nodiscard]] u8 packetRuntimeFlags(usize packetIndex)const noexcept;


    private:
        void release()noexcept;


    private:
        const GpuCompiledGraph* m_graph = nullptr;
        ReadView* m_previousView = nullptr;
        b8 m_ownsAdmission = false;
    };


private:
    struct PacketRuntimeFlag{
        enum Mask : u8{
            None = 0u,
            AllowsParallelRecording = 1u << 0u,
            HasInitialOwnerHandoff = 1u << 1u,
            HasExternalStateSource = 1u << 2u,
            HasPrologueStateSeeds = 1u << 3u,
            HasRetainedInitialStateResource = 1u << 4u,
            HasCrossQueueConsumer = 1u << 5u,
        };
    };

    struct AttemptBindingState{
        enum Enum : u8{
            None,
            Recording,
            Submitting,
            Resolved,
        };
    };

    class CompilationScope final : NoCopy{
    public:
        explicit CompilationScope(GpuCompiledGraph& graph)noexcept;
        ~CompilationScope()noexcept;


    public:
        [[nodiscard]] b8 valid()const noexcept{ return m_graph != nullptr; }
        void publish()noexcept;


    private:
        GpuCompiledGraph* m_graph = nullptr;
        b8 m_published = false;
    };


private:
    static constexpr u32 s_PlanAccessWriterBit = 1u << 31u;
    static constexpr u32 s_PlanAccessReaderMask = s_PlanAccessWriterBit - 1u;


public:
    explicit GpuCompiledGraph(GraphicsArena& arena)noexcept;
    ~GpuCompiledGraph()noexcept;


public:
    [[nodiscard]] b8 tryReset()noexcept;
    void reset()noexcept;


private:
    void clearAttemptBindingWithinLock()noexcept;
    void resetPlanStorageWithinPlanWriteScope()noexcept;
    void buildPlanStatistics(const GpuTaskGraphDeclarationReadView& graph, Alloc::ScratchArena& scratchArena);

    [[nodiscard]] b8 valid()const noexcept{ return m_valid && m_planGeneration != 0u; }

    [[nodiscard]] u64 generation()const noexcept{ return m_generation; }

    [[nodiscard]] u64 planGeneration()const noexcept{ return m_planGeneration; }

    [[nodiscard]] u16 deviceGeneration()const noexcept{ return m_deviceGeneration; }

    [[nodiscard]] usize taskCount()const noexcept{ return m_tasks.size(); }

    [[nodiscard]] usize packetCount()const noexcept{ return m_packets.size(); }
    [[nodiscard]] b8 validPacket(const GpuSubmissionPacketId& packetID)const noexcept;
    [[nodiscard]] b8 validPacketRange(const GpuSubmissionPacketRange& range)const noexcept;
    [[nodiscard]] GpuSubmissionPacketId packetIdAt(usize index)const noexcept;
    [[nodiscard]] GpuSubmissionPacketRange packetRange(const GpuSubmissionPacketId& first, const GpuSubmissionPacketId& last)const noexcept;
    [[nodiscard]] GpuSubmissionPacketRange packetRangeForTasks(const GpuTaskId& first, const GpuTaskId& last)const noexcept;
    [[nodiscard]] GpuSubmissionPacketRange allPacketRange()const noexcept;
    [[nodiscard]] GpuSubmissionPacketRange packetTimingEnvelopeRange()const noexcept{
        return validPacketRange(m_packetTimingEnvelopeRange) ? m_packetTimingEnvelopeRange : GpuSubmissionPacketRange{};
    }
    [[nodiscard]] const GpuCompiledTask* findTask(const GpuTaskId& task)const noexcept;
    [[nodiscard]] GpuSubmissionPacketId packetForTask(const GpuTaskId& task)const noexcept;
    [[nodiscard]] GpuTaskPacketizationDecision::Enum packetizationDecisionForTask(const GpuTaskId& task)const noexcept;
    [[nodiscard]] b8 tasksSharePacket(const GpuTaskId& first, const GpuTaskId& second)const noexcept;
    [[nodiscard]] b8 taskPrecedesOrSharesPacket(const GpuTaskId& first, const GpuTaskId& second)const noexcept;
    [[nodiscard]] b8 taskPrecedesInSamePacket(const GpuTaskId& first, const GpuTaskId& second)const noexcept;
    [[nodiscard]] b8 tasksFormContiguousPacketSequence(Span<const GpuTaskId> tasks)const noexcept;
    [[nodiscard]] b8 taskJoinsAcceptedQueueFrontier(const GpuTaskId& task)const noexcept;
    [[nodiscard]] const GpuPhysicalQueueInfo* queueInfoForTask(const GpuTaskId& task)const noexcept;
    [[nodiscard]] const GpuCompiledOwnershipTransfer* logicalOwnershipTransfers()const noexcept;
    [[nodiscard]] const GpuCompiledOwnershipTransfer* logicalOwnershipTransferAt(usize index)const noexcept;
    [[nodiscard]] const GpuCompiledExternalResourceExport* externalResourceExport(const GpuGraphResourceId& resource)const noexcept;
    [[nodiscard]] const GpuCompiledExternalResourceExportSource* externalResourceExportSources(const GpuCompiledExternalResourceExport& exportInfo)const noexcept;
    [[nodiscard]] GpuTaskGraphPhysicalQueueCompileStatistics physicalQueueCompileStatistics(const GpuPhysicalQueueId& queue)const noexcept;
    [[nodiscard]] const GpuPhysicalQueueInfo* queueInfo(const GpuPhysicalQueueId& queue)const noexcept;
    [[nodiscard]] GpuPhysicalQueueTopology queueTopology()const noexcept;


private:
    [[nodiscard]] b8 beginRecordingAttempt(
        const GpuTaskGraph& graph,
        const GpuTaskGraphDeclarationReadView& declarations,
        GpuSubmissionPacketId packet,
        u64 expectedPlanGeneration,
        u64 recordingAttemptGeneration,
        u64 previousRecordingAttemptGeneration,
        const ReadView& planAccess
    )const noexcept;
    [[nodiscard]] b8 matchesRecordingAttempt(const GpuTaskGraph& graph, u64 expectedPlanGeneration, u64 recordingAttemptGeneration)const noexcept;
    [[nodiscard]] b8 resolveRecordingAttempt(const GpuTaskGraph& graph, u64 expectedPlanGeneration, u64 recordingAttemptGeneration)const noexcept;
    [[nodiscard]] b8 bindSubmissionTransaction(const GpuTaskGraph& graph, u64 expectedPlanGeneration, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;
    [[nodiscard]] b8 matchesSubmissionTransaction(const GpuTaskGraph& graph, u64 expectedPlanGeneration, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;
    [[nodiscard]] b8 resolveSubmissionTransaction(const GpuTaskGraph& graph, u64 expectedPlanGeneration, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;
    [[nodiscard]] b8 matchesActiveAttemptIdentity(const GpuTaskGraph* graphIdentity, u64 compiledObjectIdentity, u64 expectedPlanGeneration, u64 recordingAttemptGeneration, const ReadView& planAccess)const noexcept;


private:
    GraphicsVector<GpuCompiledTask> m_tasks;
    GraphicsVector<u32> m_compiledTaskIndexByTask;
    GraphicsVector<GpuSubmissionPacket> m_packets;
    GraphicsVector<u8> m_packetRuntimeFlags;
    GraphicsVector<GpuTaskId> m_packetTasks;
    GraphicsVector<GpuPacketDependency> m_packetDependencies;
    GraphicsVector<GpuExternalCompletionId> m_packetExternalDependencies;
    GraphicsVector<GpuPacketStateSeed> m_prologueStateSeeds;
    GraphicsVector<GpuCompiledBarrier> m_prologueBarriers;
    GraphicsVector<GpuCompiledBarrier> m_epilogueBarriers;
    GraphicsVector<GpuCompiledOwnershipTransfer> m_ownershipTransfers;
    GraphicsVector<GpuCompiledExternalResourceExport> m_externalResourceExports;
    GraphicsVector<GpuCompiledExternalResourceExportSource> m_externalResourceExportSources;
    GraphicsVector<GpuPhysicalQueueInfo> m_queueTopology;
    GraphicsVector<GpuTaskGraphPhysicalQueueCompileStatistics> m_physicalQueueCompileStatistics;
    GpuCompiledPresentEndpoint m_presentEndpoint;
    GpuSubmissionPacketRange m_packetTimingEnvelopeRange;
    GpuTaskGraphCompileStatistics m_compileStatistics;
    u64 m_generation = 0u;
    u64 m_declarationRevision = 0u;
    u64 m_planGeneration = 0u;
    usize m_graphTaskCount = 0u;
    const u64 m_objectIdentity;
    mutable const GpuTaskGraph* m_attemptGraph = nullptr;
    mutable u64 m_attemptPlanGeneration = 0u;
    mutable u64 m_attemptRecordingGeneration = 0u;
    mutable u64 m_attemptTransactionIdentity = 0u;
    mutable u64 m_attemptTransactionResetGeneration = 0u;
    mutable Atomic<u32> m_planAccessState = 0u;
    mutable Futex m_attemptBindingMutex;
    u16 m_deviceGeneration = 0u;
    mutable AttemptBindingState::Enum m_attemptBindingState = AttemptBindingState::None;
    b8 m_hasPresentEndpoint = false;
    b8 m_valid = false;
};

struct GpuTaskRecordContext{
    const GpuTaskGraphDeclarationReadView& declarations;
    const GpuCompiledGraph::ReadView& compiledPlan;
    u64 recordingAttemptGeneration = 0u;
    GpuTaskId task;
    GpuSubmissionPacketId packet;
    GpuPhysicalQueueId queue;
    GpuCommandIrCapture* commandIrCapture = nullptr;

    [[nodiscard]] b8 hasCommandIrCapture()const noexcept{ return commandIrCapture != nullptr; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Capture>
[[nodiscard]] b8 CaptureGpuTaskCommandsIfEnabled(const GpuTaskRecordContext& context, Capture&& capture){
    return !context.commandIrCapture || capture(*context.commandIrCapture);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

