// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "recorder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_FrameGraphLegacyPayloadVersion = 1u;
inline constexpr u16 s_FrameGraphQueueAssignmentPayloadVersion = 2u;
inline constexpr u16 s_FrameGraphCompiledTaskPayloadVersion = 3u;
inline constexpr u16 s_FrameGraphRuntimeStatisticsPayloadVersion = 4u;
inline constexpr u16 s_FrameGraphPhysicalQueueRuntimeStatisticsPayloadVersion = 5u;
inline constexpr u16 s_FrameGraphRecoverySubmissionCountPayloadVersion = 6u;
inline constexpr u16 s_FrameGraphPacketSubmissionStatisticsPayloadVersion = 7u;
inline constexpr u16 s_FrameGraphResourceVersionStatisticsPayloadVersion = 8u;
inline constexpr u16 s_FrameGraphPayloadVersion = s_FrameGraphResourceVersionStatisticsPayloadVersion;
inline constexpr u32 s_FrameGraphPayloadMagic = 0x4E574647u; // NWFG

namespace FrameGraphNodeKind{
    enum Enum : u8{
        Unknown,
        Pass,
        Resource,
        External,
    };
};

namespace FrameGraphEdgeKind{
    enum Enum : u8{
        Unknown,
        Reads,
        Writes,
        DependsOn,
    };
};

namespace FrameGraphQueueClass{
    static constexpr u8 kFrameGraphQueueClassUnknownBase = 0u;
    static constexpr u8 kFrameGraphQueueClassCountValue = 4u;
    enum Enum : u8{
        Unknown = kFrameGraphQueueClassUnknownBase,
        Graphics,
        Compute,
        Transfer,

        kCount = kFrameGraphQueueClassCountValue,
    };
};

namespace FrameGraphQueueAssignmentReason{
    static constexpr u8 kFrameGraphQueueAssignmentReasonUnknownBase = 0u;
    static constexpr u8 kFrameGraphQueueAssignmentReasonCountValue = 10u;
    enum Enum : u8{
        Unknown = kFrameGraphQueueAssignmentReasonUnknownBase,
        RequiredGraphics,
        PreferredQueue,
        DedicatedCompute,
        DedicatedTransfer,
        Fallback,
        ConservativeAny,
        SameClassRouting,
        CompilerOverride,
        ScoredAny,

        kCount = kFrameGraphQueueAssignmentReasonCountValue,
    };
};

namespace FrameGraphQueueAssignmentModifier{
    static constexpr u8 kFrameGraphQueueAssignmentModifierNoneBase = 0u;
    enum Mask : u8{
        None = kFrameGraphQueueAssignmentModifierNoneBase,
        DirectDependencyAffinity = 1u << 0u,
        SameClassLoadBalance = 1u << 1u,
        NonPrimaryPreference = 1u << 2u,
        DebugTimingOverride = 1u << 3u,
        TimingCalibration = 1u << 4u,
        TimingFeedback = 1u << 5u,

        All = DirectDependencyAffinity
            | SameClassLoadBalance
            | NonPrimaryPreference
            | DebugTimingOverride
            | TimingCalibration
            | TimingFeedback,
    };
};

namespace FrameGraphQueueAssignmentAcceptance{
    static constexpr u8 kFrameGraphQueueAssignmentAcceptanceNotAcceptedBase = 0u;
    static constexpr u8 kFrameGraphQueueAssignmentAcceptanceCountValue = 4u;
    enum Enum : u8{
        NotAccepted = kFrameGraphQueueAssignmentAcceptanceNotAcceptedBase,
        First,
        Unchanged,
        Changed,

        kCount = kFrameGraphQueueAssignmentAcceptanceCountValue,
    };
};

namespace FrameGraphTaskPacketizationDecision{
    static constexpr u8 kFrameGraphTaskPacketizationDecisionUnknownBase = 0u;
    static constexpr u8 kFrameGraphTaskPacketizationDecisionCountValue = 12u;
    enum Enum : u8{
        Unknown = kFrameGraphTaskPacketizationDecisionUnknownBase,
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

        kCount = kFrameGraphTaskPacketizationDecisionCountValue,
    };
};

struct FrameGraphPhysicalQueueId{
    u16 index = Limit<u16>::s_Max;
    u16 deviceGeneration = 0u;

    [[nodiscard]] constexpr bool valid()const noexcept{
        return index != Limit<u16>::s_Max && deviceGeneration != 0u;
    }
};
inline constexpr bool operator==(const FrameGraphPhysicalQueueId& lhs, const FrameGraphPhysicalQueueId& rhs)noexcept{
    return lhs.index == rhs.index && lhs.deviceGeneration == rhs.deviceGeneration;
}
inline constexpr bool operator!=(const FrameGraphPhysicalQueueId& lhs, const FrameGraphPhysicalQueueId& rhs)noexcept{
    return !(lhs == rhs);
}

struct FrameGraphQueueAssignmentScore{
    i32 preference = 0;
    i32 overlap = 0;
    i32 queueLoad = 0;
    i32 incomingCrossings = 0;
    i32 outgoingCrossings = 0;
    i32 ownershipTransfers = 0;
    i32 total = 0;
};

struct FrameGraphQueueAssignment{
    FrameGraphPhysicalQueueId initialQueue;
    FrameGraphPhysicalQueueId plannedQueue;
    FrameGraphPhysicalQueueId acceptedQueue;
    FrameGraphPhysicalQueueId previousAcceptedQueue;
    FrameGraphQueueAssignmentScore score;
    FrameGraphQueueClass::Enum queueClass = FrameGraphQueueClass::Unknown;
    FrameGraphQueueAssignmentReason::Enum reason = FrameGraphQueueAssignmentReason::Unknown;
    FrameGraphQueueAssignmentModifier::Mask modifiers = FrameGraphQueueAssignmentModifier::None;
    FrameGraphQueueAssignmentAcceptance::Enum acceptance = FrameGraphQueueAssignmentAcceptance::NotAccepted;
    bool dedicated = false;
    bool present = false;
};

struct FrameGraphCompiledTask{
    u64 planGeneration = 0u;
    u32 packetIndex = Limit<u32>::s_Max;
    FrameGraphTaskPacketizationDecision::Enum packetizationDecision = FrameGraphTaskPacketizationDecision::Unknown;
    bool present = false;
};

// Aggregate, graph-generation-scoped CPU runtime telemetry. Counts use fixed-width values so decoded telemetry does not inherit the host width of usize. The separately frozen runtime-statistics wire records below are packed and fixed-size, but use the telemetry codec's native byte order rather than defining a cross-endian interchange format.
// Durations are seconds.
struct FrameGraphCompileRuntimeStatistics{
    u64 taskCount = 0u;
    u64 resourceCount = 0u;
    u64 resourceVersionCount = 0u;
    u64 resourceVersionEdgeCount = 0u;
    u64 resourceUseCount = 0u;
    u64 explicitDependencyCount = 0u;
    u64 inferredDependencyCount = 0u;
    u64 packetCount = 0u;
    u64 packetDependencyCount = 0u;
    u64 mergedTaskCount = 0u;
    u64 transitionBarrierCount = 0u;
    u64 uavBarrierCount = 0u;
    u64 ownershipReleaseBarrierCount = 0u;
    u64 ownershipAcquireBarrierCount = 0u;
    u64 stateExportBarrierCount = 0u;
    u64 logicalOwnershipTransferCount = 0u;
    u64 logicalOwnershipTransferSignatureCount = 0u;
    u64 repeatedOwnershipTransferSignatureCount = 0u;
    u64 concurrentSharingCouldAvoidTransferCount = 0u;
    u64 concurrentSharingAdviceResourceCount = 0u;
    u64 logicalOwnershipTransferInternalCount = 0u;
    u64 logicalOwnershipTransferExternalImportCount = 0u;
    u64 logicalOwnershipTransferExternalExportCount = 0u;
    u64 resourceSetCount = 0u;
    u64 resourceSetMemberCount = 0u;
    u64 directResourceUseCount = 0u;
    u64 declaredResourceSetUseCount = 0u;
    u64 expandedResourceSetMemberUseCount = 0u;
    u64 payloadObjectCount = 0u;
    u64 payloadObjectBytes = 0u;
    u64 uploadBlobCount = 0u;
    u64 uploadBlobBytes = 0u;
    f64 declarationSeconds = 0.0;
    f64 analysisSeconds = 0.0;
    f64 validationSeconds = 0.0;
    f64 dependencyAnalysisSeconds = 0.0;
    f64 hazardAnalysisSeconds = 0.0;
    f64 topologicalOrderSeconds = 0.0;
    f64 queueAssignmentSeconds = 0.0;
    f64 planningSeconds = 0.0;
    f64 packetizationSeconds = 0.0;
    f64 resourceStatePlanningSeconds = 0.0;
    f64 packetDependencyPlanningSeconds = 0.0;
    f64 totalSeconds = 0.0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameGraphStatisticsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool FrameGraphCompileBarrierCount(
    const FrameGraphCompileRuntimeStatistics& statistics,
    u64& outBarrierCount
)noexcept{
    outBarrierCount = statistics.transitionBarrierCount;
    const u64 remainingBarrierCounts[] = {
        statistics.uavBarrierCount,
        statistics.ownershipReleaseBarrierCount,
        statistics.ownershipAcquireBarrierCount,
        statistics.stateExportBarrierCount,
    };
    for(const u64 barrierCount : remainingBarrierCounts){
        if(barrierCount > Limit<u64>::s_Max - outBarrierCount)
            return false;
        outBarrierCount += barrierCount;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FrameGraphRecordingRuntimeStatistics{
    u64 packetCount = 0u;
    u64 taskCount = 0u;
    u64 commandListCount = 0u;
    u64 barrierCount = 0u;
    u64 workerRoutedPacketCount = 0u;
    u64 parallelPacketCount = 0u;
    f64 commandListAcquisitionSeconds = 0.0;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    f64 recordingSeconds = 0.0;
    f64 recordingElapsedSeconds = 0.0;
    f64 readyFrontierElapsedSeconds = 0.0;
    f64 readyFrontierWorkerBusySeconds = 0.0;
    f64 readyFrontierWorkerCapacitySeconds = 0.0;
};

struct FrameGraphSubmissionRuntimeStatistics{
    u64 acceptedPacketCount = 0u;
    u64 acceptedTaskCount = 0u;
    u64 rejectedPacketCount = 0u;
    u64 rejectedTaskCount = 0u;
    u64 nativeSubmissionCount = 0u;
    u64 rejectedSubmissionCount = 0u;
    u64 nativeCommandListCount = 0u;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    u64 acceptedFrontierSubmissionCount = 0u;
    u64 recoverySubmissionCount = 0u;
    f64 submissionSeconds = 0.0;
};

struct FrameGraphRuntimeStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    FrameGraphCompileRuntimeStatistics compile;
    FrameGraphRecordingRuntimeStatistics recording;
    FrameGraphSubmissionRuntimeStatistics submission;
    u16 deviceGeneration = 0u;
    bool present = false;
};

// Exact CPU telemetry per physical queue; absent rows are unknown, not zero.
struct FrameGraphPhysicalQueueCompileRuntimeStatistics{
    u64 taskCount = 0u;
    u64 packetCount = 0u;
    u64 mergedTaskCount = 0u;
    u64 prologueBarrierCount = 0u;
    u64 epilogueBarrierCount = 0u;
    u64 ownershipReleaseBarrierCount = 0u;
    u64 ownershipAcquireBarrierCount = 0u;
    u64 incomingLogicalOwnershipTransferCount = 0u;
    u64 outgoingLogicalOwnershipTransferCount = 0u;
    u64 incomingLogicalOwnershipTransferSignatureCount = 0u;
    u64 outgoingLogicalOwnershipTransferSignatureCount = 0u;
    u64 incomingRepeatedOwnershipTransferSignatureCount = 0u;
    u64 outgoingRepeatedOwnershipTransferSignatureCount = 0u;
    u64 concurrentSharingAdviceResourceCount = 0u;
};

struct FrameGraphPhysicalQueueRecordingRuntimeStatistics{
    u64 packetCount = 0u;
    u64 taskCount = 0u;
    u64 commandListCount = 0u;
    u64 barrierCount = 0u;
    u64 workerRoutedPacketCount = 0u;
    u64 parallelPacketCount = 0u;
    f64 commandListAcquisitionSeconds = 0.0;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    f64 recordingSeconds = 0.0;
};

struct FrameGraphPhysicalQueueSubmissionRuntimeStatistics{
    u64 acceptedPacketCount = 0u;
    u64 acceptedTaskCount = 0u;
    u64 rejectedPacketCount = 0u;
    u64 rejectedTaskCount = 0u;
    u64 nativeSubmissionCount = 0u;
    u64 rejectedSubmissionCount = 0u;
    u64 nativeCommandListCount = 0u;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    u64 acceptedFrontierSubmissionCount = 0u;
    u64 recoverySubmissionCount = 0u;
    f64 submissionSeconds = 0.0;
};

struct FrameGraphPhysicalQueueRuntimeStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    FrameGraphPhysicalQueueId queue;
    u16 deviceGeneration = 0u;
    FrameGraphQueueClass::Enum queueClass = FrameGraphQueueClass::Unknown;
    FrameGraphPhysicalQueueCompileRuntimeStatistics compile;
    FrameGraphPhysicalQueueRecordingRuntimeStatistics recording;
    FrameGraphPhysicalQueueSubmissionRuntimeStatistics submission;
};

struct FrameGraphPhysicalQueueRuntimeStatisticsRecord{
    u32 ownerNodeIndex = Limit<u32>::s_Max;
    FrameGraphPhysicalQueueRuntimeStatistics statistics;
};

// Exact native-submission telemetry for one compiler-generated packet. V7 payloads and V8 payloads whose table is marked present contain every native submission for every runtime-statistics owner, including an exact empty table when no owner submitted native work.
// Packet generation is the immutable plan generation. Wait counts exclude backend-internal waits outside the graph.
struct FrameGraphPacketSubmissionStatisticsRecord{
    u64 packetGeneration = 0u;
    u64 taskCount = 0u;
    u64 commandListCount = 0u;
    u32 ownerNodeIndex = Limit<u32>::s_Max;
    u32 packetIndex = Limit<u32>::s_Max;
    FrameGraphPhysicalQueueId queue;
    FrameGraphQueueClass::Enum queueClass = FrameGraphQueueClass::Unknown;
    bool joinsAcceptedQueueFrontier = false;
    bool recoverySubmission = false;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    f64 submissionSeconds = 0.0;
};

#pragma pack(push, 1)
struct EncodedFrameGraphPayloadHeader{
    u32 magic = s_FrameGraphPayloadMagic;
    u16 version = s_FrameGraphLegacyPayloadVersion;
    u16 reserved = 0u;
    u64 frameIndex = 0u;
    u32 nodeCount = 0u;
    u32 edgeCount = 0u;
    u32 stringTableBytes = 0u;
};

struct EncodedFrameGraphPayloadHeaderV2{
    u32 magic = s_FrameGraphPayloadMagic;
    u16 version = s_FrameGraphQueueAssignmentPayloadVersion;
    u16 reserved = 0u;
    u64 frameIndex = 0u;
    u32 nodeCount = 0u;
    u32 edgeCount = 0u;
    u32 stringTableBytes = 0u;
    u32 queueAssignmentCount = 0u;
};

struct EncodedFrameGraphPayloadHeaderV3{
    u32 magic = s_FrameGraphPayloadMagic;
    u16 version = s_FrameGraphCompiledTaskPayloadVersion;
    u16 reserved = 0u;
    u64 frameIndex = 0u;
    u32 nodeCount = 0u;
    u32 edgeCount = 0u;
    u32 stringTableBytes = 0u;
    u32 queueAssignmentCount = 0u;
    u32 compiledTaskCount = 0u;
};

struct EncodedFrameGraphPayloadHeaderV4{
    u32 magic = s_FrameGraphPayloadMagic;
    u16 version = s_FrameGraphRuntimeStatisticsPayloadVersion;
    u16 reserved = 0u;
    u64 frameIndex = 0u;
    u32 nodeCount = 0u;
    u32 edgeCount = 0u;
    u32 stringTableBytes = 0u;
    u32 queueAssignmentCount = 0u;
    u32 compiledTaskCount = 0u;
    u32 runtimeStatisticsCount = 0u;
};

struct EncodedFrameGraphPayloadHeaderV5{
    u32 magic = s_FrameGraphPayloadMagic;
    u16 version = s_FrameGraphPhysicalQueueRuntimeStatisticsPayloadVersion;
    u16 reserved = 0u;
    u64 frameIndex = 0u;
    u32 nodeCount = 0u;
    u32 edgeCount = 0u;
    u32 stringTableBytes = 0u;
    u32 queueAssignmentCount = 0u;
    u32 compiledTaskCount = 0u;
    u32 runtimeStatisticsCount = 0u;
    u32 physicalQueueRuntimeStatisticsCount = 0u;
};

struct EncodedFrameGraphPayloadHeaderV6{
    u32 magic = s_FrameGraphPayloadMagic;
    u16 version = s_FrameGraphRecoverySubmissionCountPayloadVersion;
    u16 reserved = 0u;
    u64 frameIndex = 0u;
    u32 nodeCount = 0u;
    u32 edgeCount = 0u;
    u32 stringTableBytes = 0u;
    u32 queueAssignmentCount = 0u;
    u32 compiledTaskCount = 0u;
    u32 runtimeStatisticsCount = 0u;
    u32 physicalQueueRuntimeStatisticsCount = 0u;
};

struct EncodedFrameGraphPayloadHeaderV7{
    u32 magic = s_FrameGraphPayloadMagic;
    u16 version = s_FrameGraphPacketSubmissionStatisticsPayloadVersion;
    u16 reserved = 0u;
    u64 frameIndex = 0u;
    u32 nodeCount = 0u;
    u32 edgeCount = 0u;
    u32 stringTableBytes = 0u;
    u32 queueAssignmentCount = 0u;
    u32 compiledTaskCount = 0u;
    u32 runtimeStatisticsCount = 0u;
    u32 physicalQueueRuntimeStatisticsCount = 0u;
    u32 packetSubmissionStatisticsCount = 0u;
};

struct EncodedFrameGraphPayloadHeaderV8{
    u32 magic = s_FrameGraphPayloadMagic;
    u16 version = s_FrameGraphResourceVersionStatisticsPayloadVersion;
    u16 reserved = 0u;
    u64 frameIndex = 0u;
    u32 nodeCount = 0u;
    u32 edgeCount = 0u;
    u32 stringTableBytes = 0u;
    u32 queueAssignmentCount = 0u;
    u32 compiledTaskCount = 0u;
    u32 runtimeStatisticsCount = 0u;
    u32 physicalQueueRuntimeStatisticsCount = 0u;
    u32 packetSubmissionStatisticsCount = 0u;
    u8 packetSubmissionStatisticsPresent = 0u;
    u8 reservedTail[3u] = {};
};

struct EncodedFrameGraphNode{
    NameHash nameHash = {};
    u32 labelOffset = 0u;
    u8 kind = FrameGraphNodeKind::Unknown;
    u8 flags = 0u;
    u16 reserved = 0u;
};

struct EncodedFrameGraphEdge{
    u32 fromNodeIndex = 0u;
    u32 toNodeIndex = 0u;
    u8 kind = FrameGraphEdgeKind::Unknown;
    u8 flags = 0u;
    u16 reserved = 0u;
};

struct EncodedFrameGraphPhysicalQueueId{
    u16 index = Limit<u16>::s_Max;
    u16 deviceGeneration = 0u;
};

struct EncodedFrameGraphQueueAssignment{
    u32 nodeIndex = 0u;
    EncodedFrameGraphPhysicalQueueId initialQueue;
    EncodedFrameGraphPhysicalQueueId plannedQueue;
    EncodedFrameGraphPhysicalQueueId acceptedQueue;
    EncodedFrameGraphPhysicalQueueId previousAcceptedQueue;
    i32 scorePreference = 0;
    i32 scoreOverlap = 0;
    i32 scoreQueueLoad = 0;
    i32 scoreIncomingCrossings = 0;
    i32 scoreOutgoingCrossings = 0;
    i32 scoreOwnershipTransfers = 0;
    i32 scoreTotal = 0;
    u8 queueClass = FrameGraphQueueClass::Unknown;
    u8 reason = FrameGraphQueueAssignmentReason::Unknown;
    u8 modifiers = FrameGraphQueueAssignmentModifier::None;
    u8 acceptance = FrameGraphQueueAssignmentAcceptance::NotAccepted;
    u8 dedicated = 0u;
    u8 reserved[3u] = {};
};

struct EncodedFrameGraphCompiledTask{
    u32 nodeIndex = 0u;
    u32 packetIndex = Limit<u32>::s_Max;
    u64 planGeneration = 0u;
    u8 packetizationDecision = FrameGraphTaskPacketizationDecision::Unknown;
    u8 reserved[3u] = {};
};

struct EncodedFrameGraphCompileRuntimeStatistics{
    u64 taskCount = 0u;
    u64 resourceCount = 0u;
    u64 resourceUseCount = 0u;
    u64 explicitDependencyCount = 0u;
    u64 inferredDependencyCount = 0u;
    u64 packetCount = 0u;
    u64 packetDependencyCount = 0u;
    u64 mergedTaskCount = 0u;
    u64 transitionBarrierCount = 0u;
    u64 uavBarrierCount = 0u;
    u64 ownershipReleaseBarrierCount = 0u;
    u64 ownershipAcquireBarrierCount = 0u;
    u64 stateExportBarrierCount = 0u;
    u64 logicalOwnershipTransferCount = 0u;
    u64 logicalOwnershipTransferSignatureCount = 0u;
    u64 repeatedOwnershipTransferSignatureCount = 0u;
    u64 concurrentSharingCouldAvoidTransferCount = 0u;
    u64 concurrentSharingAdviceResourceCount = 0u;
    u64 logicalOwnershipTransferInternalCount = 0u;
    u64 logicalOwnershipTransferExternalImportCount = 0u;
    u64 logicalOwnershipTransferExternalExportCount = 0u;
    u64 resourceSetCount = 0u;
    u64 resourceSetMemberCount = 0u;
    u64 directResourceUseCount = 0u;
    u64 declaredResourceSetUseCount = 0u;
    u64 expandedResourceSetMemberUseCount = 0u;
    u64 payloadObjectCount = 0u;
    u64 payloadObjectBytes = 0u;
    u64 uploadBlobCount = 0u;
    u64 uploadBlobBytes = 0u;
    f64 declarationSeconds = 0.0;
    f64 analysisSeconds = 0.0;
    f64 validationSeconds = 0.0;
    f64 dependencyAnalysisSeconds = 0.0;
    f64 hazardAnalysisSeconds = 0.0;
    f64 topologicalOrderSeconds = 0.0;
    f64 queueAssignmentSeconds = 0.0;
    f64 planningSeconds = 0.0;
    f64 packetizationSeconds = 0.0;
    f64 resourceStatePlanningSeconds = 0.0;
    f64 packetDependencyPlanningSeconds = 0.0;
    f64 totalSeconds = 0.0;
};

struct EncodedFrameGraphRecordingRuntimeStatistics{
    u64 packetCount = 0u;
    u64 taskCount = 0u;
    u64 commandListCount = 0u;
    u64 barrierCount = 0u;
    u64 workerRoutedPacketCount = 0u;
    u64 parallelPacketCount = 0u;
    f64 commandListAcquisitionSeconds = 0.0;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    f64 recordingSeconds = 0.0;
    f64 recordingElapsedSeconds = 0.0;
    f64 readyFrontierElapsedSeconds = 0.0;
    f64 readyFrontierWorkerBusySeconds = 0.0;
    f64 readyFrontierWorkerCapacitySeconds = 0.0;
};

struct EncodedFrameGraphSubmissionRuntimeStatistics{
    u64 acceptedPacketCount = 0u;
    u64 acceptedTaskCount = 0u;
    u64 rejectedPacketCount = 0u;
    u64 rejectedTaskCount = 0u;
    u64 nativeSubmissionCount = 0u;
    u64 rejectedSubmissionCount = 0u;
    u64 nativeCommandListCount = 0u;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    u64 acceptedFrontierSubmissionCount = 0u;
    f64 submissionSeconds = 0.0;
};

struct EncodedFrameGraphRuntimeStatistics{
    u32 nodeIndex = 0u;
    u16 deviceGeneration = 0u;
    u16 reserved = 0u;
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    EncodedFrameGraphCompileRuntimeStatistics compile;
    EncodedFrameGraphRecordingRuntimeStatistics recording;
    EncodedFrameGraphSubmissionRuntimeStatistics submission;
};

struct EncodedFrameGraphSubmissionRuntimeStatisticsV6{
    u64 acceptedPacketCount = 0u;
    u64 acceptedTaskCount = 0u;
    u64 rejectedPacketCount = 0u;
    u64 rejectedTaskCount = 0u;
    u64 nativeSubmissionCount = 0u;
    u64 rejectedSubmissionCount = 0u;
    u64 nativeCommandListCount = 0u;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    u64 acceptedFrontierSubmissionCount = 0u;
    f64 submissionSeconds = 0.0;
    u64 recoverySubmissionCount = 0u;
};

struct EncodedFrameGraphRuntimeStatisticsV6{
    u32 nodeIndex = 0u;
    u16 deviceGeneration = 0u;
    u16 reserved = 0u;
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    EncodedFrameGraphCompileRuntimeStatistics compile;
    EncodedFrameGraphRecordingRuntimeStatistics recording;
    EncodedFrameGraphSubmissionRuntimeStatisticsV6 submission;
};

// V8 appends resource-version counters after the frozen V4-V7 compile-statistics prefix. Keeping the prefix intact lets older payload records retain their exact layout while the decoder defaults counters absent before V8 to zero.
struct EncodedFrameGraphCompileRuntimeStatisticsV8{
    u64 taskCount = 0u;
    u64 resourceCount = 0u;
    u64 resourceUseCount = 0u;
    u64 explicitDependencyCount = 0u;
    u64 inferredDependencyCount = 0u;
    u64 packetCount = 0u;
    u64 packetDependencyCount = 0u;
    u64 mergedTaskCount = 0u;
    u64 transitionBarrierCount = 0u;
    u64 uavBarrierCount = 0u;
    u64 ownershipReleaseBarrierCount = 0u;
    u64 ownershipAcquireBarrierCount = 0u;
    u64 stateExportBarrierCount = 0u;
    u64 logicalOwnershipTransferCount = 0u;
    u64 logicalOwnershipTransferSignatureCount = 0u;
    u64 repeatedOwnershipTransferSignatureCount = 0u;
    u64 concurrentSharingCouldAvoidTransferCount = 0u;
    u64 concurrentSharingAdviceResourceCount = 0u;
    u64 logicalOwnershipTransferInternalCount = 0u;
    u64 logicalOwnershipTransferExternalImportCount = 0u;
    u64 logicalOwnershipTransferExternalExportCount = 0u;
    u64 resourceSetCount = 0u;
    u64 resourceSetMemberCount = 0u;
    u64 directResourceUseCount = 0u;
    u64 declaredResourceSetUseCount = 0u;
    u64 expandedResourceSetMemberUseCount = 0u;
    u64 payloadObjectCount = 0u;
    u64 payloadObjectBytes = 0u;
    u64 uploadBlobCount = 0u;
    u64 uploadBlobBytes = 0u;
    f64 declarationSeconds = 0.0;
    f64 analysisSeconds = 0.0;
    f64 validationSeconds = 0.0;
    f64 dependencyAnalysisSeconds = 0.0;
    f64 hazardAnalysisSeconds = 0.0;
    f64 topologicalOrderSeconds = 0.0;
    f64 queueAssignmentSeconds = 0.0;
    f64 planningSeconds = 0.0;
    f64 packetizationSeconds = 0.0;
    f64 resourceStatePlanningSeconds = 0.0;
    f64 packetDependencyPlanningSeconds = 0.0;
    f64 totalSeconds = 0.0;
    u64 resourceVersionCount = 0u;
    u64 resourceVersionEdgeCount = 0u;
};

struct EncodedFrameGraphRuntimeStatisticsV8{
    u32 nodeIndex = 0u;
    u16 deviceGeneration = 0u;
    u16 reserved = 0u;
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    EncodedFrameGraphCompileRuntimeStatisticsV8 compile;
    EncodedFrameGraphRecordingRuntimeStatistics recording;
    EncodedFrameGraphSubmissionRuntimeStatisticsV6 submission;
};

struct EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics{
    u64 taskCount = 0u;
    u64 packetCount = 0u;
    u64 mergedTaskCount = 0u;
    u64 prologueBarrierCount = 0u;
    u64 epilogueBarrierCount = 0u;
    u64 ownershipReleaseBarrierCount = 0u;
    u64 ownershipAcquireBarrierCount = 0u;
    u64 incomingLogicalOwnershipTransferCount = 0u;
    u64 outgoingLogicalOwnershipTransferCount = 0u;
    u64 incomingLogicalOwnershipTransferSignatureCount = 0u;
    u64 outgoingLogicalOwnershipTransferSignatureCount = 0u;
    u64 incomingRepeatedOwnershipTransferSignatureCount = 0u;
    u64 outgoingRepeatedOwnershipTransferSignatureCount = 0u;
    u64 concurrentSharingAdviceResourceCount = 0u;
};

struct EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics{
    u64 packetCount = 0u;
    u64 taskCount = 0u;
    u64 commandListCount = 0u;
    u64 barrierCount = 0u;
    u64 workerRoutedPacketCount = 0u;
    u64 parallelPacketCount = 0u;
    f64 commandListAcquisitionSeconds = 0.0;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    f64 recordingSeconds = 0.0;
};

struct EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics{
    u64 acceptedPacketCount = 0u;
    u64 acceptedTaskCount = 0u;
    u64 rejectedPacketCount = 0u;
    u64 rejectedTaskCount = 0u;
    u64 nativeSubmissionCount = 0u;
    u64 rejectedSubmissionCount = 0u;
    u64 nativeCommandListCount = 0u;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    u64 acceptedFrontierSubmissionCount = 0u;
    f64 submissionSeconds = 0.0;
};

struct EncodedFrameGraphPhysicalQueueRuntimeStatistics{
    u32 ownerNodeIndex = 0u;
    EncodedFrameGraphPhysicalQueueId queue;
    u8 queueClass = FrameGraphQueueClass::Unknown;
    u8 reserved[7u] = {};
    EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics compile;
    EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics recording;
    EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics submission;
};

struct EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6{
    u64 acceptedPacketCount = 0u;
    u64 acceptedTaskCount = 0u;
    u64 rejectedPacketCount = 0u;
    u64 rejectedTaskCount = 0u;
    u64 nativeSubmissionCount = 0u;
    u64 rejectedSubmissionCount = 0u;
    u64 nativeCommandListCount = 0u;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    u64 acceptedFrontierSubmissionCount = 0u;
    f64 submissionSeconds = 0.0;
    u64 recoverySubmissionCount = 0u;
};

struct EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6{
    u32 ownerNodeIndex = 0u;
    EncodedFrameGraphPhysicalQueueId queue;
    u8 queueClass = FrameGraphQueueClass::Unknown;
    u8 reserved[7u] = {};
    EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics compile;
    EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics recording;
    EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6 submission;
};

struct EncodedFrameGraphPacketSubmissionStatistics{
    u32 ownerNodeIndex = Limit<u32>::s_Max;
    u32 packetIndex = Limit<u32>::s_Max;
    u64 packetGeneration = 0u;
    EncodedFrameGraphPhysicalQueueId queue;
    u8 queueClass = FrameGraphQueueClass::Unknown;
    u8 joinsAcceptedQueueFrontier = 0u;
    u8 recoverySubmission = 0u;
    u8 reserved = 0u;
    u64 taskCount = 0u;
    u64 commandListCount = 0u;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    f64 submissionSeconds = 0.0;
};
#pragma pack(pop)
static constexpr usize s_EncodedFrameGraphPayloadHeaderByteSize = 28u;
static_assert(sizeof(EncodedFrameGraphPayloadHeader) == s_EncodedFrameGraphPayloadHeaderByteSize, "EncodedFrameGraphPayloadHeader wire layout drifted");
static constexpr usize s_FrameGraphPackedAlignBytes = 1u;
static_assert(alignof(EncodedFrameGraphPayloadHeader) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPayloadHeader must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeader>, "EncodedFrameGraphPayloadHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeader>, "EncodedFrameGraphPayloadHeader must stay binary-serializable");
static constexpr usize s_EncodedFrameGraphPayloadHeaderV2ByteSize = 32u;
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV2) == s_EncodedFrameGraphPayloadHeaderV2ByteSize, "EncodedFrameGraphPayloadHeaderV2 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV2) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPayloadHeaderV2 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV2>, "EncodedFrameGraphPayloadHeaderV2 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV2>, "EncodedFrameGraphPayloadHeaderV2 must stay binary-serializable");
static constexpr usize s_EncodedFrameGraphPayloadHeaderV3ByteSize = 36u;
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV3) == s_EncodedFrameGraphPayloadHeaderV3ByteSize, "EncodedFrameGraphPayloadHeaderV3 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV3) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPayloadHeaderV3 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV3>, "EncodedFrameGraphPayloadHeaderV3 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV3>, "EncodedFrameGraphPayloadHeaderV3 must stay binary-serializable");
static constexpr usize s_EncodedFrameGraphPayloadHeaderV4ByteSize = 40u;
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV4) == s_EncodedFrameGraphPayloadHeaderV4ByteSize, "EncodedFrameGraphPayloadHeaderV4 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV4) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPayloadHeaderV4 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV4>, "EncodedFrameGraphPayloadHeaderV4 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV4>, "EncodedFrameGraphPayloadHeaderV4 must stay binary-serializable");
static constexpr usize s_EncodedFrameGraphPayloadHeaderV5ByteSize = 44u;
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV5) == s_EncodedFrameGraphPayloadHeaderV5ByteSize, "EncodedFrameGraphPayloadHeaderV5 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV5) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPayloadHeaderV5 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV5>, "EncodedFrameGraphPayloadHeaderV5 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV5>, "EncodedFrameGraphPayloadHeaderV5 must stay binary-serializable");
static constexpr usize s_HeaderV5MagicOffset = 0u;
static constexpr usize s_HeaderV5VersionOffset = 4u;
static constexpr usize s_HeaderV5ReservedOffset = 6u;
static constexpr usize s_HeaderV5FrameIndexOffset = 8u;
static constexpr usize s_HeaderV5NodeCountOffset = 16u;
static constexpr usize s_HeaderV5EdgeCountOffset = 20u;
static constexpr usize s_HeaderV5StringTableBytesOffset = 24u;
static constexpr usize s_HeaderV5QueueAssignmentCountOffset = 28u;
static constexpr usize s_HeaderV5RuntimeStatisticsCountOffset = 36u;
static constexpr usize s_HeaderV5PhysicalQueueCountOffset = 40u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV5CompiledTaskCountOffset = 32u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV6MagicOffset = 0u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV6VersionOffset = 4u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV6ReservedOffset = 6u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV6FrameIndexOffset = 8u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV6NodeCountOffset = 16u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV6EdgeCountOffset = 20u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV6StringTableBytesOffset = 24u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV6QueueAssignmentCountOffset = 28u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV6CompiledTaskCountOffset = 32u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV6RuntimeStatisticsCountOffset = 36u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV6PhysicalQueueRuntimeStatisticsCountOffset = 40u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV7MagicOffset = 0u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV7VersionOffset = 4u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV7ReservedOffset = 6u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV7FrameIndexOffset = 8u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV7NodeCountOffset = 16u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV7EdgeCountOffset = 20u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV7StringTableBytesOffset = 24u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV7QueueAssignmentCountOffset = 28u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV7CompiledTaskCountOffset = 32u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV7RuntimeStatisticsCountOffset = 36u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV7PhysicalQueueRuntimeStatisticsCountOffset = 40u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV7PacketSubmissionStatisticsCountOffset = 44u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8MagicOffset = 0u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8VersionOffset = 4u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8ReservedOffset = 6u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8FrameIndexOffset = 8u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8NodeCountOffset = 16u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8EdgeCountOffset = 20u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8StringTableBytesOffset = 24u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8QueueAssignmentCountOffset = 28u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8CompiledTaskCountOffset = 32u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8RuntimeStatisticsCountOffset = 36u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8PhysicalQueueRuntimeStatisticsCountOffset = 40u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8PacketSubmissionStatisticsCountOffset = 44u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8PacketSubmissionStatisticsPresentOffset = 48u;
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8ReservedTailOffset = 49u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsTaskCountOffset = 0u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsResourceCountOffset = 8u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsResourceUseCountOffset = 16u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsExplicitDependencyCountOffset = 24u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsInferredDependencyCountOffset = 32u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsPacketCountOffset = 40u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsPacketDependencyCountOffset = 48u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsMergedTaskCountOffset = 56u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsTransitionBarrierCountOffset = 64u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsUavBarrierCountOffset = 72u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsOwnershipReleaseBarrierCountOffset = 80u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsOwnershipAcquireBarrierCountOffset = 88u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsStateExportBarrierCountOffset = 96u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferCountOffset = 104u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferSignatureCountOffset = 112u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsRepeatedOwnershipTransferSignatureCountOffset = 120u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsConcurrentSharingCouldAvoidTransferCountOffset = 128u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsConcurrentSharingAdviceResourceCountOffset = 136u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferInternalCountOffset = 144u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferExternalImportCountOffset = 152u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferExternalExportCountOffset = 160u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsResourceSetCountOffset = 168u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsResourceSetMemberCountOffset = 176u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsDirectResourceUseCountOffset = 184u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsDeclaredResourceSetUseCountOffset = 192u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsExpandedResourceSetMemberUseCountOffset = 200u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsPayloadObjectCountOffset = 208u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsPayloadObjectBytesOffset = 216u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsUploadBlobCountOffset = 224u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsUploadBlobBytesOffset = 232u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsDeclarationSecondsOffset = 240u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsAnalysisSecondsOffset = 248u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsValidationSecondsOffset = 256u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsDependencyAnalysisSecondsOffset = 264u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsHazardAnalysisSecondsOffset = 272u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsTopologicalOrderSecondsOffset = 280u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsQueueAssignmentSecondsOffset = 288u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsPlanningSecondsOffset = 296u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsPacketizationSecondsOffset = 304u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsResourceStatePlanningSecondsOffset = 312u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsPacketDependencyPlanningSecondsOffset = 320u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsTotalSecondsOffset = 328u;
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsPacketCountOffset = 0u;
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsTaskCountOffset = 8u;
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsCommandListCountOffset = 16u;
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsBarrierCountOffset = 24u;
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsWorkerRoutedPacketCountOffset = 32u;
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsParallelPacketCountOffset = 40u;
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsCommandListAcquisitionSecondsOffset = 48u;
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsGraphBarrierRecordingSecondsOffset = 56u;
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsTaskRecordSecondsOffset = 64u;
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsRecordingSecondsOffset = 72u;
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsRecordingElapsedSecondsOffset = 80u;
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsReadyFrontierElapsedSecondsOffset = 88u;
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsReadyFrontierWorkerBusySecondsOffset = 96u;
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsReadyFrontierWorkerCapacitySecondsOffset = 104u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsAcceptedPacketCountOffset = 0u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsAcceptedTaskCountOffset = 8u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsRejectedPacketCountOffset = 16u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsRejectedTaskCountOffset = 24u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsNativeSubmissionCountOffset = 32u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsRejectedSubmissionCountOffset = 40u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsNativeCommandListCountOffset = 48u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsPlannedWaitTokenCountOffset = 56u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsSameQueueWaitElisionCountOffset = 64u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsTimelineWaitCountOffset = 72u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsMergedTimelineWaitCountOffset = 80u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsAcceptedFrontierSubmissionCountOffset = 88u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsSubmissionSecondsOffset = 96u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsNodeIndexOffset = 0u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsDeviceGenerationOffset = 4u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsReservedOffset = 6u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsGraphGenerationOffset = 8u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsPlanGenerationOffset = 16u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsRecordingAttemptGenerationOffset = 24u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsCompileOffset = 32u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsRecordingOffset = 368u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsSubmissionOffset = 480u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6AcceptedPacketCountOffset = 0u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6AcceptedTaskCountOffset = 8u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6RejectedPacketCountOffset = 16u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6RejectedTaskCountOffset = 24u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6NativeSubmissionCountOffset = 32u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6RejectedSubmissionCountOffset = 40u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6NativeCommandListCountOffset = 48u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6PlannedWaitTokenCountOffset = 56u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6SameQueueWaitElisionCountOffset = 64u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6TimelineWaitCountOffset = 72u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6MergedTimelineWaitCountOffset = 80u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6AcceptedFrontierSubmissionCountOffset = 88u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6SubmissionSecondsOffset = 96u;
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6RecoverySubmissionCountOffset = 104u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV6NodeIndexOffset = 0u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV6DeviceGenerationOffset = 4u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV6ReservedOffset = 6u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV6GraphGenerationOffset = 8u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV6PlanGenerationOffset = 16u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV6RecordingAttemptGenerationOffset = 24u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV6CompileOffset = 32u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV6RecordingOffset = 368u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV6SubmissionOffset = 480u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV6NestedSubmissionSecondsOffset = 576u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV6NestedRecoverySubmissionCountOffset = 584u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsV8TaskCountOffset = 0u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsV8TotalSecondsOffset = 328u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsV8ResourceVersionCountOffset = 336u;
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsV8ResourceVersionEdgeCountOffset = 344u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV8NodeIndexOffset = 0u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV8DeviceGenerationOffset = 4u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV8ReservedOffset = 6u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV8GraphGenerationOffset = 8u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV8PlanGenerationOffset = 16u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV8RecordingAttemptGenerationOffset = 24u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV8CompileOffset = 32u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV8RecordingOffset = 384u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV8SubmissionOffset = 496u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV8NestedSubmissionSecondsOffset = 592u;
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV8NestedRecoverySubmissionCountOffset = 600u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsTaskCountOffset = 0u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsPacketCountOffset = 8u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsMergedTaskCountOffset = 16u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsPrologueBarrierCountOffset = 24u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsEpilogueBarrierCountOffset = 32u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOwnershipReleaseBarrierCountOffset = 40u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOwnershipAcquireBarrierCountOffset = 48u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsIncomingLogicalOwnershipTransferCountOffset = 56u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOutgoingLogicalOwnershipTransferCountOffset = 64u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsIncomingLogicalOwnershipTransferSignatureCountOffset = 72u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOutgoingLogicalOwnershipTransferSignatureCountOffset = 80u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsIncomingRepeatedOwnershipTransferSignatureCountOffset = 88u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOutgoingRepeatedOwnershipTransferSignatureCountOffset = 96u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsConcurrentSharingAdviceResourceCountOffset = 104u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsPacketCountOffset = 0u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsTaskCountOffset = 8u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsCommandListCountOffset = 16u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsBarrierCountOffset = 24u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsWorkerRoutedPacketCountOffset = 32u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsParallelPacketCountOffset = 40u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsCommandListAcquisitionSecondsOffset = 48u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsGraphBarrierRecordingSecondsOffset = 56u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsTaskRecordSecondsOffset = 64u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsRecordingSecondsOffset = 72u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsAcceptedPacketCountOffset = 0u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsAcceptedTaskCountOffset = 8u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsRejectedPacketCountOffset = 16u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsRejectedTaskCountOffset = 24u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsNativeSubmissionCountOffset = 32u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsRejectedSubmissionCountOffset = 40u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsNativeCommandListCountOffset = 48u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsPlannedWaitTokenCountOffset = 56u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsSameQueueWaitElisionCountOffset = 64u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsTimelineWaitCountOffset = 72u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsMergedTimelineWaitCountOffset = 80u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsAcceptedFrontierSubmissionCountOffset = 88u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsSubmissionSecondsOffset = 96u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsOwnerNodeIndexOffset = 0u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsQueueOffset = 4u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsQueueClassOffset = 8u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsReservedOffset = 9u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsCompileOffset = 16u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsRecordingOffset = 128u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsSubmissionOffset = 208u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6AcceptedPacketCountOffset = 0u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6AcceptedTaskCountOffset = 8u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6RejectedPacketCountOffset = 16u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6RejectedTaskCountOffset = 24u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6NativeSubmissionCountOffset = 32u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6RejectedSubmissionCountOffset = 40u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6NativeCommandListCountOffset = 48u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6PlannedWaitTokenCountOffset = 56u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6SameQueueWaitElisionCountOffset = 64u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6TimelineWaitCountOffset = 72u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6MergedTimelineWaitCountOffset = 80u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6AcceptedFrontierSubmissionCountOffset = 88u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6SubmissionSecondsOffset = 96u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6RecoverySubmissionCountOffset = 104u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6OwnerNodeIndexOffset = 0u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6QueueOffset = 4u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6QueueClassOffset = 8u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6ReservedOffset = 9u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6CompileOffset = 16u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6RecordingOffset = 128u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6SubmissionOffset = 208u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6NestedSubmissionSecondsOffset = 304u;
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6NestedRecoverySubmissionCountOffset = 312u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsOwnerNodeIndexOffset = 0u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsPacketIndexOffset = 4u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsPacketGenerationOffset = 8u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsQueueOffset = 16u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsQueueClassOffset = 20u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsJoinsAcceptedQueueFrontierOffset = 21u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsRecoverySubmissionOffset = 22u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsReservedOffset = 23u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsTaskCountOffset = 24u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsCommandListCountOffset = 32u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsPlannedWaitTokenCountOffset = 40u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsSameQueueWaitElisionCountOffset = 48u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsTimelineWaitCountOffset = 56u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsMergedTimelineWaitCountOffset = 64u;
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsSubmissionSecondsOffset = 72u;
static_assert(
    offsetof(EncodedFrameGraphPayloadHeaderV5, magic) == s_HeaderV5MagicOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV5, version) == s_HeaderV5VersionOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV5, reserved) == s_HeaderV5ReservedOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV5, frameIndex) == s_HeaderV5FrameIndexOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV5, nodeCount) == s_HeaderV5NodeCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV5, edgeCount) == s_HeaderV5EdgeCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV5, stringTableBytes) == s_HeaderV5StringTableBytesOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV5, queueAssignmentCount) == s_HeaderV5QueueAssignmentCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV5, compiledTaskCount) == s_EncodedFrameGraphPayloadHeaderV5CompiledTaskCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV5, runtimeStatisticsCount) == s_HeaderV5RuntimeStatisticsCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV5, physicalQueueRuntimeStatisticsCount) == s_HeaderV5PhysicalQueueCountOffset,
    "EncodedFrameGraphPayloadHeaderV5 field order drifted"
);
static constexpr usize s_EncodedFrameGraphPayloadHeaderV6ByteSize = 44u;
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV6) == s_EncodedFrameGraphPayloadHeaderV6ByteSize, "EncodedFrameGraphPayloadHeaderV6 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV6) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPayloadHeaderV6 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV6>, "EncodedFrameGraphPayloadHeaderV6 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV6>, "EncodedFrameGraphPayloadHeaderV6 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPayloadHeaderV6, magic) == s_EncodedFrameGraphPayloadHeaderV6MagicOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV6, version) == s_EncodedFrameGraphPayloadHeaderV6VersionOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV6, reserved) == s_EncodedFrameGraphPayloadHeaderV6ReservedOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV6, frameIndex) == s_EncodedFrameGraphPayloadHeaderV6FrameIndexOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV6, nodeCount) == s_EncodedFrameGraphPayloadHeaderV6NodeCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV6, edgeCount) == s_EncodedFrameGraphPayloadHeaderV6EdgeCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV6, stringTableBytes) == s_EncodedFrameGraphPayloadHeaderV6StringTableBytesOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV6, queueAssignmentCount) == s_EncodedFrameGraphPayloadHeaderV6QueueAssignmentCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV6, compiledTaskCount) == s_EncodedFrameGraphPayloadHeaderV6CompiledTaskCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV6, runtimeStatisticsCount) == s_EncodedFrameGraphPayloadHeaderV6RuntimeStatisticsCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV6, physicalQueueRuntimeStatisticsCount) == s_EncodedFrameGraphPayloadHeaderV6PhysicalQueueRuntimeStatisticsCountOffset,
    "EncodedFrameGraphPayloadHeaderV6 field order drifted"
);
static constexpr usize s_EncodedFrameGraphPayloadHeaderV7ByteSize = 48u;
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV7) == s_EncodedFrameGraphPayloadHeaderV7ByteSize, "EncodedFrameGraphPayloadHeaderV7 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV7) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPayloadHeaderV7 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV7>, "EncodedFrameGraphPayloadHeaderV7 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV7>, "EncodedFrameGraphPayloadHeaderV7 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPayloadHeaderV7, magic) == s_EncodedFrameGraphPayloadHeaderV7MagicOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV7, version) == s_EncodedFrameGraphPayloadHeaderV7VersionOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV7, reserved) == s_EncodedFrameGraphPayloadHeaderV7ReservedOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV7, frameIndex) == s_EncodedFrameGraphPayloadHeaderV7FrameIndexOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV7, nodeCount) == s_EncodedFrameGraphPayloadHeaderV7NodeCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV7, edgeCount) == s_EncodedFrameGraphPayloadHeaderV7EdgeCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV7, stringTableBytes) == s_EncodedFrameGraphPayloadHeaderV7StringTableBytesOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV7, queueAssignmentCount) == s_EncodedFrameGraphPayloadHeaderV7QueueAssignmentCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV7, compiledTaskCount) == s_EncodedFrameGraphPayloadHeaderV7CompiledTaskCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV7, runtimeStatisticsCount) == s_EncodedFrameGraphPayloadHeaderV7RuntimeStatisticsCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV7, physicalQueueRuntimeStatisticsCount) == s_EncodedFrameGraphPayloadHeaderV7PhysicalQueueRuntimeStatisticsCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV7, packetSubmissionStatisticsCount) == s_EncodedFrameGraphPayloadHeaderV7PacketSubmissionStatisticsCountOffset,
    "EncodedFrameGraphPayloadHeaderV7 field order drifted"
);
static constexpr usize s_EncodedFrameGraphPayloadHeaderV8ByteSize = 52u;
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV8) == s_EncodedFrameGraphPayloadHeaderV8ByteSize, "EncodedFrameGraphPayloadHeaderV8 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV8) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPayloadHeaderV8 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV8>, "EncodedFrameGraphPayloadHeaderV8 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV8>, "EncodedFrameGraphPayloadHeaderV8 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPayloadHeaderV8, magic) == s_EncodedFrameGraphPayloadHeaderV8MagicOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV8, version) == s_EncodedFrameGraphPayloadHeaderV8VersionOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV8, reserved) == s_EncodedFrameGraphPayloadHeaderV8ReservedOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV8, frameIndex) == s_EncodedFrameGraphPayloadHeaderV8FrameIndexOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV8, nodeCount) == s_EncodedFrameGraphPayloadHeaderV8NodeCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV8, edgeCount) == s_EncodedFrameGraphPayloadHeaderV8EdgeCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV8, stringTableBytes) == s_EncodedFrameGraphPayloadHeaderV8StringTableBytesOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV8, queueAssignmentCount) == s_EncodedFrameGraphPayloadHeaderV8QueueAssignmentCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV8, compiledTaskCount) == s_EncodedFrameGraphPayloadHeaderV8CompiledTaskCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV8, runtimeStatisticsCount) == s_EncodedFrameGraphPayloadHeaderV8RuntimeStatisticsCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV8, physicalQueueRuntimeStatisticsCount) == s_EncodedFrameGraphPayloadHeaderV8PhysicalQueueRuntimeStatisticsCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV8, packetSubmissionStatisticsCount) == s_EncodedFrameGraphPayloadHeaderV8PacketSubmissionStatisticsCountOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV8, packetSubmissionStatisticsPresent) == s_EncodedFrameGraphPayloadHeaderV8PacketSubmissionStatisticsPresentOffset
    && offsetof(EncodedFrameGraphPayloadHeaderV8, reservedTail) == s_EncodedFrameGraphPayloadHeaderV8ReservedTailOffset,
    "EncodedFrameGraphPayloadHeaderV8 field order drifted"
);
static constexpr usize s_EncodedFrameGraphNodeByteSize = 72u;
static_assert(sizeof(EncodedFrameGraphNode) == s_EncodedFrameGraphNodeByteSize, "EncodedFrameGraphNode wire layout drifted");
static_assert(alignof(EncodedFrameGraphNode) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphNode must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphNode>, "EncodedFrameGraphNode must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphNode>, "EncodedFrameGraphNode must stay binary-serializable");
static constexpr usize s_EncodedFrameGraphEdgeByteSize = 12u;
static_assert(sizeof(EncodedFrameGraphEdge) == s_EncodedFrameGraphEdgeByteSize, "EncodedFrameGraphEdge wire layout drifted");
static_assert(alignof(EncodedFrameGraphEdge) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphEdge must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphEdge>, "EncodedFrameGraphEdge must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphEdge>, "EncodedFrameGraphEdge must stay binary-serializable");
static constexpr usize s_EncodedFrameGraphPhysicalQueueIdByteSize = 4u;
static_assert(sizeof(EncodedFrameGraphPhysicalQueueId) == s_EncodedFrameGraphPhysicalQueueIdByteSize, "EncodedFrameGraphPhysicalQueueId wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueId) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPhysicalQueueId must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueId>, "EncodedFrameGraphPhysicalQueueId must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueId>, "EncodedFrameGraphPhysicalQueueId must stay binary-serializable");
static constexpr usize s_EncodedFrameGraphQueueAssignmentByteSize = 56u;
static_assert(sizeof(EncodedFrameGraphQueueAssignment) == s_EncodedFrameGraphQueueAssignmentByteSize, "EncodedFrameGraphQueueAssignment wire layout drifted");
static_assert(alignof(EncodedFrameGraphQueueAssignment) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphQueueAssignment must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphQueueAssignment>, "EncodedFrameGraphQueueAssignment must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphQueueAssignment>, "EncodedFrameGraphQueueAssignment must stay binary-serializable");
static constexpr usize s_EncodedFrameGraphCompiledTaskByteSize = 20u;
static_assert(sizeof(EncodedFrameGraphCompiledTask) == s_EncodedFrameGraphCompiledTaskByteSize, "EncodedFrameGraphCompiledTask wire layout drifted");
static_assert(alignof(EncodedFrameGraphCompiledTask) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphCompiledTask must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphCompiledTask>, "EncodedFrameGraphCompiledTask must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphCompiledTask>, "EncodedFrameGraphCompiledTask must stay binary-serializable");
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsByteSize = 336u;
static_assert(sizeof(EncodedFrameGraphCompileRuntimeStatistics) == s_EncodedFrameGraphCompileRuntimeStatisticsByteSize, "EncodedFrameGraphCompileRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphCompileRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphCompileRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphCompileRuntimeStatistics>, "EncodedFrameGraphCompileRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphCompileRuntimeStatistics>, "EncodedFrameGraphCompileRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphCompileRuntimeStatistics, taskCount) == s_EncodedFrameGraphCompileRuntimeStatisticsTaskCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceCount) == s_EncodedFrameGraphCompileRuntimeStatisticsResourceCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceUseCount) == s_EncodedFrameGraphCompileRuntimeStatisticsResourceUseCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, explicitDependencyCount) == s_EncodedFrameGraphCompileRuntimeStatisticsExplicitDependencyCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, inferredDependencyCount) == s_EncodedFrameGraphCompileRuntimeStatisticsInferredDependencyCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, packetCount) == s_EncodedFrameGraphCompileRuntimeStatisticsPacketCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, packetDependencyCount) == s_EncodedFrameGraphCompileRuntimeStatisticsPacketDependencyCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, mergedTaskCount) == s_EncodedFrameGraphCompileRuntimeStatisticsMergedTaskCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, transitionBarrierCount) == s_EncodedFrameGraphCompileRuntimeStatisticsTransitionBarrierCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, uavBarrierCount) == s_EncodedFrameGraphCompileRuntimeStatisticsUavBarrierCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, ownershipReleaseBarrierCount) == s_EncodedFrameGraphCompileRuntimeStatisticsOwnershipReleaseBarrierCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, ownershipAcquireBarrierCount) == s_EncodedFrameGraphCompileRuntimeStatisticsOwnershipAcquireBarrierCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, stateExportBarrierCount) == s_EncodedFrameGraphCompileRuntimeStatisticsStateExportBarrierCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferCount) == s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferSignatureCount) == s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferSignatureCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, repeatedOwnershipTransferSignatureCount) == s_EncodedFrameGraphCompileRuntimeStatisticsRepeatedOwnershipTransferSignatureCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, concurrentSharingCouldAvoidTransferCount) == s_EncodedFrameGraphCompileRuntimeStatisticsConcurrentSharingCouldAvoidTransferCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, concurrentSharingAdviceResourceCount) == s_EncodedFrameGraphCompileRuntimeStatisticsConcurrentSharingAdviceResourceCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferInternalCount) == s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferInternalCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferExternalImportCount) == s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferExternalImportCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferExternalExportCount) == s_EncodedFrameGraphCompileRuntimeStatisticsLogicalOwnershipTransferExternalExportCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceSetCount) == s_EncodedFrameGraphCompileRuntimeStatisticsResourceSetCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceSetMemberCount) == s_EncodedFrameGraphCompileRuntimeStatisticsResourceSetMemberCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, directResourceUseCount) == s_EncodedFrameGraphCompileRuntimeStatisticsDirectResourceUseCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, declaredResourceSetUseCount) == s_EncodedFrameGraphCompileRuntimeStatisticsDeclaredResourceSetUseCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, expandedResourceSetMemberUseCount) == s_EncodedFrameGraphCompileRuntimeStatisticsExpandedResourceSetMemberUseCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, payloadObjectCount) == s_EncodedFrameGraphCompileRuntimeStatisticsPayloadObjectCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, payloadObjectBytes) == s_EncodedFrameGraphCompileRuntimeStatisticsPayloadObjectBytesOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, uploadBlobCount) == s_EncodedFrameGraphCompileRuntimeStatisticsUploadBlobCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, uploadBlobBytes) == s_EncodedFrameGraphCompileRuntimeStatisticsUploadBlobBytesOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, declarationSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsDeclarationSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, analysisSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsAnalysisSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, validationSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsValidationSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, dependencyAnalysisSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsDependencyAnalysisSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, hazardAnalysisSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsHazardAnalysisSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, topologicalOrderSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsTopologicalOrderSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, queueAssignmentSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsQueueAssignmentSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, planningSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsPlanningSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, packetizationSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsPacketizationSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceStatePlanningSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsResourceStatePlanningSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, packetDependencyPlanningSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsPacketDependencyPlanningSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, totalSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsTotalSecondsOffset,
    "EncodedFrameGraphCompileRuntimeStatistics field order drifted"
);
static constexpr usize s_EncodedFrameGraphRecordingRuntimeStatisticsByteSize = 112u;
static_assert(sizeof(EncodedFrameGraphRecordingRuntimeStatistics) == s_EncodedFrameGraphRecordingRuntimeStatisticsByteSize, "EncodedFrameGraphRecordingRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphRecordingRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphRecordingRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphRecordingRuntimeStatistics>, "EncodedFrameGraphRecordingRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphRecordingRuntimeStatistics>, "EncodedFrameGraphRecordingRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphRecordingRuntimeStatistics, packetCount) == s_EncodedFrameGraphRecordingRuntimeStatisticsPacketCountOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, taskCount) == s_EncodedFrameGraphRecordingRuntimeStatisticsTaskCountOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, commandListCount) == s_EncodedFrameGraphRecordingRuntimeStatisticsCommandListCountOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, barrierCount) == s_EncodedFrameGraphRecordingRuntimeStatisticsBarrierCountOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, workerRoutedPacketCount) == s_EncodedFrameGraphRecordingRuntimeStatisticsWorkerRoutedPacketCountOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, parallelPacketCount) == s_EncodedFrameGraphRecordingRuntimeStatisticsParallelPacketCountOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, commandListAcquisitionSeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsCommandListAcquisitionSecondsOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, graphBarrierRecordingSeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsGraphBarrierRecordingSecondsOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, taskRecordSeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsTaskRecordSecondsOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, recordingSeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsRecordingSecondsOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, recordingElapsedSeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsRecordingElapsedSecondsOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, readyFrontierElapsedSeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsReadyFrontierElapsedSecondsOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, readyFrontierWorkerBusySeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsReadyFrontierWorkerBusySecondsOffset
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, readyFrontierWorkerCapacitySeconds) == s_EncodedFrameGraphRecordingRuntimeStatisticsReadyFrontierWorkerCapacitySecondsOffset,
    "EncodedFrameGraphRecordingRuntimeStatistics field order drifted"
);
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsByteSize = 104u;
static_assert(sizeof(EncodedFrameGraphSubmissionRuntimeStatistics) == s_EncodedFrameGraphSubmissionRuntimeStatisticsByteSize, "EncodedFrameGraphSubmissionRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphSubmissionRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphSubmissionRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphSubmissionRuntimeStatistics>, "EncodedFrameGraphSubmissionRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphSubmissionRuntimeStatistics>, "EncodedFrameGraphSubmissionRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, acceptedPacketCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsAcceptedPacketCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, acceptedTaskCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsAcceptedTaskCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, rejectedPacketCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsRejectedPacketCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, rejectedTaskCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsRejectedTaskCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, nativeSubmissionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsNativeSubmissionCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, rejectedSubmissionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsRejectedSubmissionCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, nativeCommandListCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsNativeCommandListCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, plannedWaitTokenCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsPlannedWaitTokenCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, sameQueueWaitElisionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsSameQueueWaitElisionCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, timelineWaitCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsTimelineWaitCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, mergedTimelineWaitCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsMergedTimelineWaitCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, acceptedFrontierSubmissionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsAcceptedFrontierSubmissionCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, submissionSeconds) == s_EncodedFrameGraphSubmissionRuntimeStatisticsSubmissionSecondsOffset,
    "EncodedFrameGraphSubmissionRuntimeStatistics field order drifted"
);
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsByteSize = 584u;
static_assert(sizeof(EncodedFrameGraphRuntimeStatistics) == s_EncodedFrameGraphRuntimeStatisticsByteSize, "EncodedFrameGraphRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphRuntimeStatistics>, "EncodedFrameGraphRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphRuntimeStatistics>, "EncodedFrameGraphRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphRuntimeStatistics, nodeIndex) == s_EncodedFrameGraphRuntimeStatisticsNodeIndexOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, deviceGeneration) == s_EncodedFrameGraphRuntimeStatisticsDeviceGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, reserved) == s_EncodedFrameGraphRuntimeStatisticsReservedOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, graphGeneration) == s_EncodedFrameGraphRuntimeStatisticsGraphGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, planGeneration) == s_EncodedFrameGraphRuntimeStatisticsPlanGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, recordingAttemptGeneration) == s_EncodedFrameGraphRuntimeStatisticsRecordingAttemptGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, compile) == s_EncodedFrameGraphRuntimeStatisticsCompileOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, recording) == s_EncodedFrameGraphRuntimeStatisticsRecordingOffset
    && offsetof(EncodedFrameGraphRuntimeStatistics, submission) == s_EncodedFrameGraphRuntimeStatisticsSubmissionOffset,
    "EncodedFrameGraphRuntimeStatistics field order drifted"
);
static constexpr usize s_EncodedFrameGraphSubmissionRuntimeStatisticsV6ByteSize = 112u;
static_assert(sizeof(EncodedFrameGraphSubmissionRuntimeStatisticsV6) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6ByteSize, "EncodedFrameGraphSubmissionRuntimeStatisticsV6 wire layout drifted");
static_assert(alignof(EncodedFrameGraphSubmissionRuntimeStatisticsV6) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphSubmissionRuntimeStatisticsV6 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphSubmissionRuntimeStatisticsV6>, "EncodedFrameGraphSubmissionRuntimeStatisticsV6 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphSubmissionRuntimeStatisticsV6>, "EncodedFrameGraphSubmissionRuntimeStatisticsV6 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, acceptedPacketCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6AcceptedPacketCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, acceptedTaskCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6AcceptedTaskCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, rejectedPacketCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6RejectedPacketCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, rejectedTaskCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6RejectedTaskCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, nativeSubmissionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6NativeSubmissionCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, rejectedSubmissionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6RejectedSubmissionCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, nativeCommandListCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6NativeCommandListCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, plannedWaitTokenCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6PlannedWaitTokenCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, sameQueueWaitElisionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6SameQueueWaitElisionCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, timelineWaitCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6TimelineWaitCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, mergedTimelineWaitCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6MergedTimelineWaitCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, acceptedFrontierSubmissionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6AcceptedFrontierSubmissionCountOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, submissionSeconds) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6SubmissionSecondsOffset
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, recoverySubmissionCount) == s_EncodedFrameGraphSubmissionRuntimeStatisticsV6RecoverySubmissionCountOffset,
    "EncodedFrameGraphSubmissionRuntimeStatisticsV6 field order drifted"
);
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV6ByteSize = 592u;
static_assert(sizeof(EncodedFrameGraphRuntimeStatisticsV6) == s_EncodedFrameGraphRuntimeStatisticsV6ByteSize, "EncodedFrameGraphRuntimeStatisticsV6 wire layout drifted");
static_assert(alignof(EncodedFrameGraphRuntimeStatisticsV6) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphRuntimeStatisticsV6 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphRuntimeStatisticsV6>, "EncodedFrameGraphRuntimeStatisticsV6 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphRuntimeStatisticsV6>, "EncodedFrameGraphRuntimeStatisticsV6 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphRuntimeStatisticsV6, nodeIndex) == s_EncodedFrameGraphRuntimeStatisticsV6NodeIndexOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, deviceGeneration) == s_EncodedFrameGraphRuntimeStatisticsV6DeviceGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, reserved) == s_EncodedFrameGraphRuntimeStatisticsV6ReservedOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, graphGeneration) == s_EncodedFrameGraphRuntimeStatisticsV6GraphGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, planGeneration) == s_EncodedFrameGraphRuntimeStatisticsV6PlanGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, recordingAttemptGeneration) == s_EncodedFrameGraphRuntimeStatisticsV6RecordingAttemptGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, compile) == s_EncodedFrameGraphRuntimeStatisticsV6CompileOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, recording) == s_EncodedFrameGraphRuntimeStatisticsV6RecordingOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, submission) == s_EncodedFrameGraphRuntimeStatisticsV6SubmissionOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, submission)
        + offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, submissionSeconds) == s_EncodedFrameGraphRuntimeStatisticsV6NestedSubmissionSecondsOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, submission)
        + offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, recoverySubmissionCount) == s_EncodedFrameGraphRuntimeStatisticsV6NestedRecoverySubmissionCountOffset,
    "EncodedFrameGraphRuntimeStatisticsV6 field order drifted"
);
static constexpr usize s_EncodedFrameGraphCompileRuntimeStatisticsV8ByteSize = 352u;
static_assert(sizeof(EncodedFrameGraphCompileRuntimeStatisticsV8) == s_EncodedFrameGraphCompileRuntimeStatisticsV8ByteSize, "EncodedFrameGraphCompileRuntimeStatisticsV8 wire layout drifted");
static_assert(alignof(EncodedFrameGraphCompileRuntimeStatisticsV8) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphCompileRuntimeStatisticsV8 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphCompileRuntimeStatisticsV8>, "EncodedFrameGraphCompileRuntimeStatisticsV8 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphCompileRuntimeStatisticsV8>, "EncodedFrameGraphCompileRuntimeStatisticsV8 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphCompileRuntimeStatisticsV8, taskCount) == s_EncodedFrameGraphCompileRuntimeStatisticsV8TaskCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatisticsV8, totalSeconds) == s_EncodedFrameGraphCompileRuntimeStatisticsV8TotalSecondsOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatisticsV8, resourceVersionCount) == s_EncodedFrameGraphCompileRuntimeStatisticsV8ResourceVersionCountOffset
    && offsetof(EncodedFrameGraphCompileRuntimeStatisticsV8, resourceVersionEdgeCount) == s_EncodedFrameGraphCompileRuntimeStatisticsV8ResourceVersionEdgeCountOffset,
    "EncodedFrameGraphCompileRuntimeStatisticsV8 field order drifted"
);
static constexpr usize s_EncodedFrameGraphRuntimeStatisticsV8ByteSize = 608u;
static_assert(sizeof(EncodedFrameGraphRuntimeStatisticsV8) == s_EncodedFrameGraphRuntimeStatisticsV8ByteSize, "EncodedFrameGraphRuntimeStatisticsV8 wire layout drifted");
static_assert(alignof(EncodedFrameGraphRuntimeStatisticsV8) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphRuntimeStatisticsV8 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphRuntimeStatisticsV8>, "EncodedFrameGraphRuntimeStatisticsV8 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphRuntimeStatisticsV8>, "EncodedFrameGraphRuntimeStatisticsV8 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphRuntimeStatisticsV8, nodeIndex) == s_EncodedFrameGraphRuntimeStatisticsV8NodeIndexOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, deviceGeneration) == s_EncodedFrameGraphRuntimeStatisticsV8DeviceGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, reserved) == s_EncodedFrameGraphRuntimeStatisticsV8ReservedOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, graphGeneration) == s_EncodedFrameGraphRuntimeStatisticsV8GraphGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, planGeneration) == s_EncodedFrameGraphRuntimeStatisticsV8PlanGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, recordingAttemptGeneration) == s_EncodedFrameGraphRuntimeStatisticsV8RecordingAttemptGenerationOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, compile) == s_EncodedFrameGraphRuntimeStatisticsV8CompileOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, recording) == s_EncodedFrameGraphRuntimeStatisticsV8RecordingOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, submission) == s_EncodedFrameGraphRuntimeStatisticsV8SubmissionOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, submission)
        + offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, submissionSeconds) == s_EncodedFrameGraphRuntimeStatisticsV8NestedSubmissionSecondsOffset
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, submission)
        + offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, recoverySubmissionCount) == s_EncodedFrameGraphRuntimeStatisticsV8NestedRecoverySubmissionCountOffset,
    "EncodedFrameGraphRuntimeStatisticsV8 field order drifted"
);
static constexpr usize s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsByteSize = 112u;
static_assert(sizeof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsByteSize, "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, taskCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsTaskCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, packetCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsPacketCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, mergedTaskCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsMergedTaskCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, prologueBarrierCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsPrologueBarrierCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, epilogueBarrierCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsEpilogueBarrierCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, ownershipReleaseBarrierCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOwnershipReleaseBarrierCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, ownershipAcquireBarrierCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOwnershipAcquireBarrierCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, incomingLogicalOwnershipTransferCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsIncomingLogicalOwnershipTransferCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, outgoingLogicalOwnershipTransferCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOutgoingLogicalOwnershipTransferCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, incomingLogicalOwnershipTransferSignatureCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsIncomingLogicalOwnershipTransferSignatureCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, outgoingLogicalOwnershipTransferSignatureCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOutgoingLogicalOwnershipTransferSignatureCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, incomingRepeatedOwnershipTransferSignatureCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsIncomingRepeatedOwnershipTransferSignatureCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, outgoingRepeatedOwnershipTransferSignatureCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsOutgoingRepeatedOwnershipTransferSignatureCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, concurrentSharingAdviceResourceCount) == s_EncodedFrameGraphPhysicalQueueCompileRuntimeStatisticsConcurrentSharingAdviceResourceCountOffset,
    "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics field order drifted"
);
static constexpr usize s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsByteSize = 80u;
static_assert(sizeof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsByteSize, "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, packetCount) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsPacketCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, taskCount) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsTaskCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, commandListCount) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsCommandListCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, barrierCount) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsBarrierCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, workerRoutedPacketCount) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsWorkerRoutedPacketCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, parallelPacketCount) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsParallelPacketCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, commandListAcquisitionSeconds) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsCommandListAcquisitionSecondsOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, graphBarrierRecordingSeconds) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsGraphBarrierRecordingSecondsOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, taskRecordSeconds) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsTaskRecordSecondsOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, recordingSeconds) == s_EncodedFrameGraphPhysicalQueueRecordingRuntimeStatisticsRecordingSecondsOffset,
    "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics field order drifted"
);
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsByteSize = 104u;
static_assert(sizeof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsByteSize, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, acceptedPacketCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsAcceptedPacketCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, acceptedTaskCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsAcceptedTaskCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, rejectedPacketCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsRejectedPacketCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, rejectedTaskCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsRejectedTaskCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, nativeSubmissionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsNativeSubmissionCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, rejectedSubmissionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsRejectedSubmissionCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, nativeCommandListCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsNativeCommandListCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, plannedWaitTokenCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsPlannedWaitTokenCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, sameQueueWaitElisionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsSameQueueWaitElisionCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, timelineWaitCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsTimelineWaitCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, mergedTimelineWaitCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsMergedTimelineWaitCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, acceptedFrontierSubmissionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsAcceptedFrontierSubmissionCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, submissionSeconds) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsSubmissionSecondsOffset,
    "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics field order drifted"
);
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsByteSize = 312u;
static_assert(sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatistics) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsByteSize, "EncodedFrameGraphPhysicalQueueRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueRuntimeStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPhysicalQueueRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, ownerNodeIndex) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsOwnerNodeIndexOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, queue) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsQueueOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, queueClass) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsQueueClassOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, reserved) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsReservedOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, compile) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsCompileOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, recording) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsRecordingOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, submission) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsSubmissionOffset,
    "EncodedFrameGraphPhysicalQueueRuntimeStatistics field order drifted"
);
static constexpr usize s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6ByteSize = 112u;
static_assert(sizeof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6ByteSize, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6>, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6>, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, acceptedPacketCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6AcceptedPacketCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, acceptedTaskCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6AcceptedTaskCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, rejectedPacketCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6RejectedPacketCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, rejectedTaskCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6RejectedTaskCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, nativeSubmissionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6NativeSubmissionCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, rejectedSubmissionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6RejectedSubmissionCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, nativeCommandListCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6NativeCommandListCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, plannedWaitTokenCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6PlannedWaitTokenCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, sameQueueWaitElisionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6SameQueueWaitElisionCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, timelineWaitCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6TimelineWaitCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, mergedTimelineWaitCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6MergedTimelineWaitCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, acceptedFrontierSubmissionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6AcceptedFrontierSubmissionCountOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, submissionSeconds) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6SubmissionSecondsOffset
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, recoverySubmissionCount) == s_EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6RecoverySubmissionCountOffset,
    "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6 field order drifted"
);
static constexpr usize s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6ByteSize = 320u;
static_assert(sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6ByteSize, "EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6>, "EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6>, "EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, ownerNodeIndex) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6OwnerNodeIndexOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, queue) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6QueueOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, queueClass) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6QueueClassOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, reserved) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6ReservedOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, compile) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6CompileOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, recording) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6RecordingOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, submission) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6SubmissionOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, submission)
        + offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, submissionSeconds) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6NestedSubmissionSecondsOffset
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, submission)
        + offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, recoverySubmissionCount) == s_EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6NestedRecoverySubmissionCountOffset,
    "EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 field order drifted"
);
static constexpr usize s_EncodedFrameGraphPacketSubmissionStatisticsByteSize = 80u;
static_assert(sizeof(EncodedFrameGraphPacketSubmissionStatistics) == s_EncodedFrameGraphPacketSubmissionStatisticsByteSize, "EncodedFrameGraphPacketSubmissionStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPacketSubmissionStatistics) == s_FrameGraphPackedAlignBytes, "EncodedFrameGraphPacketSubmissionStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPacketSubmissionStatistics>, "EncodedFrameGraphPacketSubmissionStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPacketSubmissionStatistics>, "EncodedFrameGraphPacketSubmissionStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPacketSubmissionStatistics, ownerNodeIndex) == s_EncodedFrameGraphPacketSubmissionStatisticsOwnerNodeIndexOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, packetIndex) == s_EncodedFrameGraphPacketSubmissionStatisticsPacketIndexOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, packetGeneration) == s_EncodedFrameGraphPacketSubmissionStatisticsPacketGenerationOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, queue) == s_EncodedFrameGraphPacketSubmissionStatisticsQueueOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, queueClass) == s_EncodedFrameGraphPacketSubmissionStatisticsQueueClassOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, joinsAcceptedQueueFrontier) == s_EncodedFrameGraphPacketSubmissionStatisticsJoinsAcceptedQueueFrontierOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, recoverySubmission) == s_EncodedFrameGraphPacketSubmissionStatisticsRecoverySubmissionOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, reserved) == s_EncodedFrameGraphPacketSubmissionStatisticsReservedOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, taskCount) == s_EncodedFrameGraphPacketSubmissionStatisticsTaskCountOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, commandListCount) == s_EncodedFrameGraphPacketSubmissionStatisticsCommandListCountOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, plannedWaitTokenCount) == s_EncodedFrameGraphPacketSubmissionStatisticsPlannedWaitTokenCountOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, sameQueueWaitElisionCount) == s_EncodedFrameGraphPacketSubmissionStatisticsSameQueueWaitElisionCountOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, timelineWaitCount) == s_EncodedFrameGraphPacketSubmissionStatisticsTimelineWaitCountOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, mergedTimelineWaitCount) == s_EncodedFrameGraphPacketSubmissionStatisticsMergedTimelineWaitCountOffset
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, submissionSeconds) == s_EncodedFrameGraphPacketSubmissionStatisticsSubmissionSecondsOffset,
    "EncodedFrameGraphPacketSubmissionStatistics field order drifted"
);

struct FrameGraphNodeDesc{
    Name name = NAME_NONE;
    AStringView label;
    FrameGraphNodeKind::Enum kind = FrameGraphNodeKind::Unknown;
    u8 flags = 0u;
    FrameGraphQueueAssignment queueAssignment;
    FrameGraphCompiledTask compiledTask;
    FrameGraphRuntimeStatistics runtimeStatistics;
};

struct FrameGraphEdgeDesc{
    u32 fromNodeIndex = 0u;
    u32 toNodeIndex = 0u;
    FrameGraphEdgeKind::Enum kind = FrameGraphEdgeKind::Unknown;
    u8 flags = 0u;
};

struct FrameGraphNodePayload{
    Name name = NAME_NONE;
    AString<TelemetryArena> label;
    FrameGraphNodeKind::Enum kind = FrameGraphNodeKind::Unknown;
    u8 flags = 0u;
    FrameGraphQueueAssignment queueAssignment;
    FrameGraphCompiledTask compiledTask;
    FrameGraphRuntimeStatistics runtimeStatistics;

    explicit FrameGraphNodePayload(TelemetryArena& arena)
        : label(arena)
    {}
};

struct FrameGraphEdgePayload{
    u32 fromNodeIndex = 0u;
    u32 toNodeIndex = 0u;
    FrameGraphEdgeKind::Enum kind = FrameGraphEdgeKind::Unknown;
    u8 flags = 0u;
};

struct FrameGraphPayload{
    Vector<FrameGraphNodePayload, TelemetryArena> nodes;
    Vector<FrameGraphEdgePayload, TelemetryArena> edges;
    Vector<FrameGraphPhysicalQueueRuntimeStatisticsRecord, TelemetryArena> physicalQueueRuntimeStatistics;
    Vector<FrameGraphPacketSubmissionStatisticsRecord, TelemetryArena> packetSubmissionStatistics;
    u64 frameIndex = 0u;
    u16 wireVersion = 0u;
    bool physicalQueueRuntimeStatisticsPresent = false;
    bool packetSubmissionStatisticsPresent = false;

    explicit FrameGraphPayload(TelemetryArena& arena)
        : nodes(arena)
        , edges(arena)
        , physicalQueueRuntimeStatistics(arena)
        , packetSubmissionStatistics(arena)
    {}
};

using FrameGraphNodeDescs = Vector<FrameGraphNodeDesc, TelemetryArena>;
using FrameGraphEdgeDescs = Vector<FrameGraphEdgeDesc, TelemetryArena>;
using FrameGraphPhysicalQueueRuntimeStatisticsRecords =
    Vector<FrameGraphPhysicalQueueRuntimeStatisticsRecord, TelemetryArena>
;
using FrameGraphPacketSubmissionStatisticsRecords =
    Vector<FrameGraphPacketSubmissionStatisticsRecord, TelemetryArena>
;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsValidFrameGraphNodeKind(FrameGraphNodeKind::Enum kind)noexcept;
[[nodiscard]] bool IsValidFrameGraphEdgeKind(FrameGraphEdgeKind::Enum kind)noexcept;
[[nodiscard]] bool IsValidFrameGraphQueueClass(FrameGraphQueueClass::Enum queueClass)noexcept;
[[nodiscard]] bool IsValidFrameGraphQueueAssignmentReason(FrameGraphQueueAssignmentReason::Enum reason)noexcept;
[[nodiscard]] bool IsValidFrameGraphQueueAssignmentAcceptance(FrameGraphQueueAssignmentAcceptance::Enum acceptance)noexcept;
[[nodiscard]] bool IsValidFrameGraphTaskPacketizationDecision(FrameGraphTaskPacketizationDecision::Enum decision)noexcept;
[[nodiscard]] bool IsValidFrameGraphQueueAssignment(const FrameGraphQueueAssignment& assignment)noexcept;
[[nodiscard]] bool IsValidFrameGraphCompiledTask(const FrameGraphCompiledTask& compiledTask)noexcept;
[[nodiscard]] bool IsValidFrameGraphRuntimeStatistics(const FrameGraphRuntimeStatistics& statistics)noexcept;
[[nodiscard]] bool IsValidFrameGraphPhysicalQueueRuntimeStatistics(
    const FrameGraphPhysicalQueueRuntimeStatistics& statistics
)noexcept;
[[nodiscard]] bool IsValidFrameGraphPhysicalQueueRuntimeStatisticsForOwner(
    const FrameGraphPhysicalQueueRuntimeStatistics& statistics,
    const FrameGraphRuntimeStatistics& ownerStatistics
)noexcept;
[[nodiscard]] bool IsValidFrameGraphPacketSubmissionStatistics(
    const FrameGraphPacketSubmissionStatisticsRecord& statistics
)noexcept;
[[nodiscard]] bool BuildFrameGraphPayload(
    TelemetryArena& arena,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    TelemetryBytes& outPayload
);
[[nodiscard]] bool BuildFrameGraphPayload(
    TelemetryArena& arena,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    TelemetryBytes& outPayload
);
[[nodiscard]] bool BuildFrameGraphPayload(
    TelemetryArena& arena,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics,
    TelemetryBytes& outPayload
);
[[nodiscard]] bool ParseFrameGraphPayload(TelemetryArena& arena, const void* payload, usize payloadBytes, FrameGraphPayload& outPayload);
[[nodiscard]] bool RecordFrameGraph(
    Recorder& recorder,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    u32 streamId = 0u
);
[[nodiscard]] bool RecordFrameGraph(
    Recorder& recorder,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    u32 streamId = 0u
);
[[nodiscard]] bool RecordFrameGraph(
    Recorder& recorder,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
    const FrameGraphPhysicalQueueRuntimeStatisticsRecords& physicalQueueRuntimeStatistics,
    const FrameGraphPacketSubmissionStatisticsRecords& packetSubmissionStatistics,
    u32 streamId = 0u
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

