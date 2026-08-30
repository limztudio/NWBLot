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
    enum Enum : u8{
        Unknown = 0u,
        Graphics = 1u,
        Compute = 2u,
        Transfer = 3u,

        kCount = 4u,
    };
};

namespace FrameGraphQueueAssignmentReason{
    enum Enum : u8{
        Unknown = 0u,
        RequiredGraphics = 1u,
        PreferredQueue = 2u,
        DedicatedCompute = 3u,
        DedicatedTransfer = 4u,
        Fallback = 5u,
        ConservativeAny = 6u,
        SameClassRouting = 7u,
        CompilerOverride = 8u,
        ScoredAny = 9u,

        kCount = 10u,
    };
};

namespace FrameGraphQueueAssignmentModifier{
    enum Mask : u8{
        None = 0u,
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
    enum Enum : u8{
        NotAccepted = 0u,
        First = 1u,
        Unchanged = 2u,
        Changed = 3u,

        kCount = 4u,
    };
};

namespace FrameGraphTaskPacketizationDecision{
    enum Enum : u8{
        Unknown = 0u,
        FirstTask = 1u,
        MergeNotRequested = 2u,
        TaskForcesBoundary = 3u,
        QueueChanged = 4u,
        PrecedingTaskForcesBoundary = 5u,
        ScoredMergeIneligible = 6u,
        MergeRequiresExplicitImmediateDependency = 7u,
        CrossQueueConsumerFrontier = 8u,
        MergedExplicit = 9u,
        MergedFrontierScored = 10u,
        ScoredMergeDomainMismatch = 11u,

        kCount = 12u,
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

// Aggregate, graph-generation-scoped CPU runtime telemetry. Counts use fixed-width values so decoded telemetry does
// not inherit the host width of usize. The separately frozen runtime-statistics wire records below are packed and
// fixed-size, but use the telemetry codec's native byte order rather than defining a cross-endian interchange format.
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
    u16 deviceGeneration = 0u;
    FrameGraphCompileRuntimeStatistics compile;
    FrameGraphRecordingRuntimeStatistics recording;
    FrameGraphSubmissionRuntimeStatistics submission;
    bool present = false;
};

// Exact CPU telemetry for one physical queue in one immutable graph plan and recording attempt. A side-table row is
// independently exact; the generic payload codec accepts a validated partial set, and an absent queue row is unknown rather
// than zero. Live frame-graph producers must emit every queue in the compiled topology for a complete report. Durations are
// seconds.
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
    u16 deviceGeneration = 0u;
    FrameGraphPhysicalQueueId queue;
    FrameGraphQueueClass::Enum queueClass = FrameGraphQueueClass::Unknown;
    FrameGraphPhysicalQueueCompileRuntimeStatistics compile;
    FrameGraphPhysicalQueueRecordingRuntimeStatistics recording;
    FrameGraphPhysicalQueueSubmissionRuntimeStatistics submission;
};

struct FrameGraphPhysicalQueueRuntimeStatisticsRecord{
    u32 ownerNodeIndex = Limit<u32>::s_Max;
    FrameGraphPhysicalQueueRuntimeStatistics statistics;
};

// Exact native-submission telemetry for one compiler-generated packet. V7 payloads and V8 payloads whose table is
// marked present contain every native submission for every runtime-statistics owner, including an exact empty table
// when no owner submitted native work. Packet generation is the immutable plan generation. Wait counts exclude
// backend-internal waits outside the graph.
struct FrameGraphPacketSubmissionStatisticsRecord{
    u32 ownerNodeIndex = Limit<u32>::s_Max;
    u32 packetIndex = Limit<u32>::s_Max;
    u64 packetGeneration = 0u;
    FrameGraphPhysicalQueueId queue;
    FrameGraphQueueClass::Enum queueClass = FrameGraphQueueClass::Unknown;
    u64 taskCount = 0u;
    u64 commandListCount = 0u;
    u64 plannedWaitTokenCount = 0u;
    u64 sameQueueWaitElisionCount = 0u;
    u64 timelineWaitCount = 0u;
    u64 mergedTimelineWaitCount = 0u;
    bool joinsAcceptedQueueFrontier = false;
    bool recoverySubmission = false;
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

// V8 appends resource-version counters after the frozen V4-V7 compile-statistics prefix. Keeping the prefix intact
// lets older payload records retain their exact layout while the decoder defaults counters absent before V8 to zero.
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
static_assert(sizeof(EncodedFrameGraphPayloadHeader) == 28u, "EncodedFrameGraphPayloadHeader wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeader) == 1u, "EncodedFrameGraphPayloadHeader must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeader>, "EncodedFrameGraphPayloadHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeader>, "EncodedFrameGraphPayloadHeader must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV2) == 32u, "EncodedFrameGraphPayloadHeaderV2 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV2) == 1u, "EncodedFrameGraphPayloadHeaderV2 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV2>, "EncodedFrameGraphPayloadHeaderV2 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV2>, "EncodedFrameGraphPayloadHeaderV2 must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV3) == 36u, "EncodedFrameGraphPayloadHeaderV3 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV3) == 1u, "EncodedFrameGraphPayloadHeaderV3 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV3>, "EncodedFrameGraphPayloadHeaderV3 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV3>, "EncodedFrameGraphPayloadHeaderV3 must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV4) == 40u, "EncodedFrameGraphPayloadHeaderV4 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV4) == 1u, "EncodedFrameGraphPayloadHeaderV4 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV4>, "EncodedFrameGraphPayloadHeaderV4 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV4>, "EncodedFrameGraphPayloadHeaderV4 must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV5) == 44u, "EncodedFrameGraphPayloadHeaderV5 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV5) == 1u, "EncodedFrameGraphPayloadHeaderV5 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV5>, "EncodedFrameGraphPayloadHeaderV5 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV5>, "EncodedFrameGraphPayloadHeaderV5 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPayloadHeaderV5, magic) == 0u
    && offsetof(EncodedFrameGraphPayloadHeaderV5, version) == 4u
    && offsetof(EncodedFrameGraphPayloadHeaderV5, reserved) == 6u
    && offsetof(EncodedFrameGraphPayloadHeaderV5, frameIndex) == 8u
    && offsetof(EncodedFrameGraphPayloadHeaderV5, nodeCount) == 16u
    && offsetof(EncodedFrameGraphPayloadHeaderV5, edgeCount) == 20u
    && offsetof(EncodedFrameGraphPayloadHeaderV5, stringTableBytes) == 24u
    && offsetof(EncodedFrameGraphPayloadHeaderV5, queueAssignmentCount) == 28u
    && offsetof(EncodedFrameGraphPayloadHeaderV5, compiledTaskCount) == 32u
    && offsetof(EncodedFrameGraphPayloadHeaderV5, runtimeStatisticsCount) == 36u
    && offsetof(EncodedFrameGraphPayloadHeaderV5, physicalQueueRuntimeStatisticsCount) == 40u,
    "EncodedFrameGraphPayloadHeaderV5 field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV6) == 44u, "EncodedFrameGraphPayloadHeaderV6 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV6) == 1u, "EncodedFrameGraphPayloadHeaderV6 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV6>, "EncodedFrameGraphPayloadHeaderV6 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV6>, "EncodedFrameGraphPayloadHeaderV6 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPayloadHeaderV6, magic) == 0u
    && offsetof(EncodedFrameGraphPayloadHeaderV6, version) == 4u
    && offsetof(EncodedFrameGraphPayloadHeaderV6, reserved) == 6u
    && offsetof(EncodedFrameGraphPayloadHeaderV6, frameIndex) == 8u
    && offsetof(EncodedFrameGraphPayloadHeaderV6, nodeCount) == 16u
    && offsetof(EncodedFrameGraphPayloadHeaderV6, edgeCount) == 20u
    && offsetof(EncodedFrameGraphPayloadHeaderV6, stringTableBytes) == 24u
    && offsetof(EncodedFrameGraphPayloadHeaderV6, queueAssignmentCount) == 28u
    && offsetof(EncodedFrameGraphPayloadHeaderV6, compiledTaskCount) == 32u
    && offsetof(EncodedFrameGraphPayloadHeaderV6, runtimeStatisticsCount) == 36u
    && offsetof(EncodedFrameGraphPayloadHeaderV6, physicalQueueRuntimeStatisticsCount) == 40u,
    "EncodedFrameGraphPayloadHeaderV6 field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV7) == 48u, "EncodedFrameGraphPayloadHeaderV7 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV7) == 1u, "EncodedFrameGraphPayloadHeaderV7 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV7>, "EncodedFrameGraphPayloadHeaderV7 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV7>, "EncodedFrameGraphPayloadHeaderV7 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPayloadHeaderV7, magic) == 0u
    && offsetof(EncodedFrameGraphPayloadHeaderV7, version) == 4u
    && offsetof(EncodedFrameGraphPayloadHeaderV7, reserved) == 6u
    && offsetof(EncodedFrameGraphPayloadHeaderV7, frameIndex) == 8u
    && offsetof(EncodedFrameGraphPayloadHeaderV7, nodeCount) == 16u
    && offsetof(EncodedFrameGraphPayloadHeaderV7, edgeCount) == 20u
    && offsetof(EncodedFrameGraphPayloadHeaderV7, stringTableBytes) == 24u
    && offsetof(EncodedFrameGraphPayloadHeaderV7, queueAssignmentCount) == 28u
    && offsetof(EncodedFrameGraphPayloadHeaderV7, compiledTaskCount) == 32u
    && offsetof(EncodedFrameGraphPayloadHeaderV7, runtimeStatisticsCount) == 36u
    && offsetof(EncodedFrameGraphPayloadHeaderV7, physicalQueueRuntimeStatisticsCount) == 40u
    && offsetof(EncodedFrameGraphPayloadHeaderV7, packetSubmissionStatisticsCount) == 44u,
    "EncodedFrameGraphPayloadHeaderV7 field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPayloadHeaderV8) == 52u, "EncodedFrameGraphPayloadHeaderV8 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPayloadHeaderV8) == 1u, "EncodedFrameGraphPayloadHeaderV8 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPayloadHeaderV8>, "EncodedFrameGraphPayloadHeaderV8 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPayloadHeaderV8>, "EncodedFrameGraphPayloadHeaderV8 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPayloadHeaderV8, magic) == 0u
    && offsetof(EncodedFrameGraphPayloadHeaderV8, version) == 4u
    && offsetof(EncodedFrameGraphPayloadHeaderV8, reserved) == 6u
    && offsetof(EncodedFrameGraphPayloadHeaderV8, frameIndex) == 8u
    && offsetof(EncodedFrameGraphPayloadHeaderV8, nodeCount) == 16u
    && offsetof(EncodedFrameGraphPayloadHeaderV8, edgeCount) == 20u
    && offsetof(EncodedFrameGraphPayloadHeaderV8, stringTableBytes) == 24u
    && offsetof(EncodedFrameGraphPayloadHeaderV8, queueAssignmentCount) == 28u
    && offsetof(EncodedFrameGraphPayloadHeaderV8, compiledTaskCount) == 32u
    && offsetof(EncodedFrameGraphPayloadHeaderV8, runtimeStatisticsCount) == 36u
    && offsetof(EncodedFrameGraphPayloadHeaderV8, physicalQueueRuntimeStatisticsCount) == 40u
    && offsetof(EncodedFrameGraphPayloadHeaderV8, packetSubmissionStatisticsCount) == 44u
    && offsetof(EncodedFrameGraphPayloadHeaderV8, packetSubmissionStatisticsPresent) == 48u
    && offsetof(EncodedFrameGraphPayloadHeaderV8, reservedTail) == 49u,
    "EncodedFrameGraphPayloadHeaderV8 field order drifted"
);
static_assert(sizeof(EncodedFrameGraphNode) == 72u, "EncodedFrameGraphNode wire layout drifted");
static_assert(alignof(EncodedFrameGraphNode) == 1u, "EncodedFrameGraphNode must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphNode>, "EncodedFrameGraphNode must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphNode>, "EncodedFrameGraphNode must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphEdge) == 12u, "EncodedFrameGraphEdge wire layout drifted");
static_assert(alignof(EncodedFrameGraphEdge) == 1u, "EncodedFrameGraphEdge must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphEdge>, "EncodedFrameGraphEdge must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphEdge>, "EncodedFrameGraphEdge must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphPhysicalQueueId) == 4u, "EncodedFrameGraphPhysicalQueueId wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueId) == 1u, "EncodedFrameGraphPhysicalQueueId must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueId>, "EncodedFrameGraphPhysicalQueueId must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueId>, "EncodedFrameGraphPhysicalQueueId must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphQueueAssignment) == 56u, "EncodedFrameGraphQueueAssignment wire layout drifted");
static_assert(alignof(EncodedFrameGraphQueueAssignment) == 1u, "EncodedFrameGraphQueueAssignment must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphQueueAssignment>, "EncodedFrameGraphQueueAssignment must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphQueueAssignment>, "EncodedFrameGraphQueueAssignment must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphCompiledTask) == 20u, "EncodedFrameGraphCompiledTask wire layout drifted");
static_assert(alignof(EncodedFrameGraphCompiledTask) == 1u, "EncodedFrameGraphCompiledTask must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphCompiledTask>, "EncodedFrameGraphCompiledTask must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphCompiledTask>, "EncodedFrameGraphCompiledTask must stay binary-serializable");
static_assert(sizeof(EncodedFrameGraphCompileRuntimeStatistics) == 336u, "EncodedFrameGraphCompileRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphCompileRuntimeStatistics) == 1u, "EncodedFrameGraphCompileRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphCompileRuntimeStatistics>, "EncodedFrameGraphCompileRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphCompileRuntimeStatistics>, "EncodedFrameGraphCompileRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphCompileRuntimeStatistics, taskCount) == 0u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceCount) == 8u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceUseCount) == 16u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, explicitDependencyCount) == 24u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, inferredDependencyCount) == 32u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, packetCount) == 40u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, packetDependencyCount) == 48u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, mergedTaskCount) == 56u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, transitionBarrierCount) == 64u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, uavBarrierCount) == 72u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, ownershipReleaseBarrierCount) == 80u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, ownershipAcquireBarrierCount) == 88u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, stateExportBarrierCount) == 96u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferCount) == 104u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferSignatureCount) == 112u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, repeatedOwnershipTransferSignatureCount) == 120u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, concurrentSharingCouldAvoidTransferCount) == 128u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, concurrentSharingAdviceResourceCount) == 136u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferInternalCount) == 144u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferExternalImportCount) == 152u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, logicalOwnershipTransferExternalExportCount) == 160u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceSetCount) == 168u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceSetMemberCount) == 176u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, directResourceUseCount) == 184u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, declaredResourceSetUseCount) == 192u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, expandedResourceSetMemberUseCount) == 200u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, payloadObjectCount) == 208u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, payloadObjectBytes) == 216u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, uploadBlobCount) == 224u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, uploadBlobBytes) == 232u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, declarationSeconds) == 240u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, analysisSeconds) == 248u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, validationSeconds) == 256u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, dependencyAnalysisSeconds) == 264u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, hazardAnalysisSeconds) == 272u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, topologicalOrderSeconds) == 280u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, queueAssignmentSeconds) == 288u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, planningSeconds) == 296u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, packetizationSeconds) == 304u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, resourceStatePlanningSeconds) == 312u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, packetDependencyPlanningSeconds) == 320u
    && offsetof(EncodedFrameGraphCompileRuntimeStatistics, totalSeconds) == 328u,
    "EncodedFrameGraphCompileRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphRecordingRuntimeStatistics) == 112u, "EncodedFrameGraphRecordingRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphRecordingRuntimeStatistics) == 1u, "EncodedFrameGraphRecordingRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphRecordingRuntimeStatistics>, "EncodedFrameGraphRecordingRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphRecordingRuntimeStatistics>, "EncodedFrameGraphRecordingRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphRecordingRuntimeStatistics, packetCount) == 0u
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, taskCount) == 8u
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, commandListCount) == 16u
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, barrierCount) == 24u
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, workerRoutedPacketCount) == 32u
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, parallelPacketCount) == 40u
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, commandListAcquisitionSeconds) == 48u
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, graphBarrierRecordingSeconds) == 56u
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, taskRecordSeconds) == 64u
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, recordingSeconds) == 72u
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, recordingElapsedSeconds) == 80u
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, readyFrontierElapsedSeconds) == 88u
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, readyFrontierWorkerBusySeconds) == 96u
    && offsetof(EncodedFrameGraphRecordingRuntimeStatistics, readyFrontierWorkerCapacitySeconds) == 104u,
    "EncodedFrameGraphRecordingRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphSubmissionRuntimeStatistics) == 104u, "EncodedFrameGraphSubmissionRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphSubmissionRuntimeStatistics) == 1u, "EncodedFrameGraphSubmissionRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphSubmissionRuntimeStatistics>, "EncodedFrameGraphSubmissionRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphSubmissionRuntimeStatistics>, "EncodedFrameGraphSubmissionRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, acceptedPacketCount) == 0u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, acceptedTaskCount) == 8u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, rejectedPacketCount) == 16u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, rejectedTaskCount) == 24u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, nativeSubmissionCount) == 32u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, rejectedSubmissionCount) == 40u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, nativeCommandListCount) == 48u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, plannedWaitTokenCount) == 56u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, sameQueueWaitElisionCount) == 64u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, timelineWaitCount) == 72u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, mergedTimelineWaitCount) == 80u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, acceptedFrontierSubmissionCount) == 88u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatistics, submissionSeconds) == 96u,
    "EncodedFrameGraphSubmissionRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphRuntimeStatistics) == 584u, "EncodedFrameGraphRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphRuntimeStatistics) == 1u, "EncodedFrameGraphRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphRuntimeStatistics>, "EncodedFrameGraphRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphRuntimeStatistics>, "EncodedFrameGraphRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphRuntimeStatistics, nodeIndex) == 0u
    && offsetof(EncodedFrameGraphRuntimeStatistics, deviceGeneration) == 4u
    && offsetof(EncodedFrameGraphRuntimeStatistics, reserved) == 6u
    && offsetof(EncodedFrameGraphRuntimeStatistics, graphGeneration) == 8u
    && offsetof(EncodedFrameGraphRuntimeStatistics, planGeneration) == 16u
    && offsetof(EncodedFrameGraphRuntimeStatistics, recordingAttemptGeneration) == 24u
    && offsetof(EncodedFrameGraphRuntimeStatistics, compile) == 32u
    && offsetof(EncodedFrameGraphRuntimeStatistics, recording) == 368u
    && offsetof(EncodedFrameGraphRuntimeStatistics, submission) == 480u,
    "EncodedFrameGraphRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphSubmissionRuntimeStatisticsV6) == 112u, "EncodedFrameGraphSubmissionRuntimeStatisticsV6 wire layout drifted");
static_assert(alignof(EncodedFrameGraphSubmissionRuntimeStatisticsV6) == 1u, "EncodedFrameGraphSubmissionRuntimeStatisticsV6 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphSubmissionRuntimeStatisticsV6>, "EncodedFrameGraphSubmissionRuntimeStatisticsV6 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphSubmissionRuntimeStatisticsV6>, "EncodedFrameGraphSubmissionRuntimeStatisticsV6 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, acceptedPacketCount) == 0u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, acceptedTaskCount) == 8u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, rejectedPacketCount) == 16u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, rejectedTaskCount) == 24u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, nativeSubmissionCount) == 32u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, rejectedSubmissionCount) == 40u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, nativeCommandListCount) == 48u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, plannedWaitTokenCount) == 56u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, sameQueueWaitElisionCount) == 64u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, timelineWaitCount) == 72u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, mergedTimelineWaitCount) == 80u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, acceptedFrontierSubmissionCount) == 88u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, submissionSeconds) == 96u
    && offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, recoverySubmissionCount) == 104u,
    "EncodedFrameGraphSubmissionRuntimeStatisticsV6 field order drifted"
);
static_assert(sizeof(EncodedFrameGraphRuntimeStatisticsV6) == 592u, "EncodedFrameGraphRuntimeStatisticsV6 wire layout drifted");
static_assert(alignof(EncodedFrameGraphRuntimeStatisticsV6) == 1u, "EncodedFrameGraphRuntimeStatisticsV6 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphRuntimeStatisticsV6>, "EncodedFrameGraphRuntimeStatisticsV6 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphRuntimeStatisticsV6>, "EncodedFrameGraphRuntimeStatisticsV6 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphRuntimeStatisticsV6, nodeIndex) == 0u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, deviceGeneration) == 4u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, reserved) == 6u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, graphGeneration) == 8u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, planGeneration) == 16u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, recordingAttemptGeneration) == 24u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, compile) == 32u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, recording) == 368u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, submission) == 480u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, submission)
        + offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, submissionSeconds) == 576u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV6, submission)
        + offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, recoverySubmissionCount) == 584u,
    "EncodedFrameGraphRuntimeStatisticsV6 field order drifted"
);
static_assert(sizeof(EncodedFrameGraphCompileRuntimeStatisticsV8) == 352u, "EncodedFrameGraphCompileRuntimeStatisticsV8 wire layout drifted");
static_assert(alignof(EncodedFrameGraphCompileRuntimeStatisticsV8) == 1u, "EncodedFrameGraphCompileRuntimeStatisticsV8 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphCompileRuntimeStatisticsV8>, "EncodedFrameGraphCompileRuntimeStatisticsV8 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphCompileRuntimeStatisticsV8>, "EncodedFrameGraphCompileRuntimeStatisticsV8 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphCompileRuntimeStatisticsV8, taskCount) == 0u
    && offsetof(EncodedFrameGraphCompileRuntimeStatisticsV8, totalSeconds) == 328u
    && offsetof(EncodedFrameGraphCompileRuntimeStatisticsV8, resourceVersionCount) == 336u
    && offsetof(EncodedFrameGraphCompileRuntimeStatisticsV8, resourceVersionEdgeCount) == 344u,
    "EncodedFrameGraphCompileRuntimeStatisticsV8 field order drifted"
);
static_assert(sizeof(EncodedFrameGraphRuntimeStatisticsV8) == 608u, "EncodedFrameGraphRuntimeStatisticsV8 wire layout drifted");
static_assert(alignof(EncodedFrameGraphRuntimeStatisticsV8) == 1u, "EncodedFrameGraphRuntimeStatisticsV8 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphRuntimeStatisticsV8>, "EncodedFrameGraphRuntimeStatisticsV8 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphRuntimeStatisticsV8>, "EncodedFrameGraphRuntimeStatisticsV8 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphRuntimeStatisticsV8, nodeIndex) == 0u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, deviceGeneration) == 4u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, reserved) == 6u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, graphGeneration) == 8u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, planGeneration) == 16u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, recordingAttemptGeneration) == 24u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, compile) == 32u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, recording) == 384u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, submission) == 496u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, submission)
        + offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, submissionSeconds) == 592u
    && offsetof(EncodedFrameGraphRuntimeStatisticsV8, submission)
        + offsetof(EncodedFrameGraphSubmissionRuntimeStatisticsV6, recoverySubmissionCount) == 600u,
    "EncodedFrameGraphRuntimeStatisticsV8 field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics) == 112u, "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics) == 1u, "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, taskCount) == 0u
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, packetCount) == 8u
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, mergedTaskCount) == 16u
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, prologueBarrierCount) == 24u
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, epilogueBarrierCount) == 32u
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, ownershipReleaseBarrierCount) == 40u
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, ownershipAcquireBarrierCount) == 48u
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, incomingLogicalOwnershipTransferCount) == 56u
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, outgoingLogicalOwnershipTransferCount) == 64u
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, incomingLogicalOwnershipTransferSignatureCount) == 72u
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, outgoingLogicalOwnershipTransferSignatureCount) == 80u
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, incomingRepeatedOwnershipTransferSignatureCount) == 88u
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, outgoingRepeatedOwnershipTransferSignatureCount) == 96u
    && offsetof(EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics, concurrentSharingAdviceResourceCount) == 104u,
    "EncodedFrameGraphPhysicalQueueCompileRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics) == 80u, "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics) == 1u, "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, packetCount) == 0u
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, taskCount) == 8u
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, commandListCount) == 16u
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, barrierCount) == 24u
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, workerRoutedPacketCount) == 32u
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, parallelPacketCount) == 40u
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, commandListAcquisitionSeconds) == 48u
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, graphBarrierRecordingSeconds) == 56u
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, taskRecordSeconds) == 64u
    && offsetof(EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics, recordingSeconds) == 72u,
    "EncodedFrameGraphPhysicalQueueRecordingRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics) == 104u, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics) == 1u, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, acceptedPacketCount) == 0u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, acceptedTaskCount) == 8u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, rejectedPacketCount) == 16u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, rejectedTaskCount) == 24u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, nativeSubmissionCount) == 32u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, rejectedSubmissionCount) == 40u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, nativeCommandListCount) == 48u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, plannedWaitTokenCount) == 56u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, sameQueueWaitElisionCount) == 64u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, timelineWaitCount) == 72u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, mergedTimelineWaitCount) == 80u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, acceptedFrontierSubmissionCount) == 88u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics, submissionSeconds) == 96u,
    "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatistics) == 312u, "EncodedFrameGraphPhysicalQueueRuntimeStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueRuntimeStatistics) == 1u, "EncodedFrameGraphPhysicalQueueRuntimeStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueRuntimeStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueRuntimeStatistics>, "EncodedFrameGraphPhysicalQueueRuntimeStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, ownerNodeIndex) == 0u
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, queue) == 4u
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, queueClass) == 8u
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, reserved) == 9u
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, compile) == 16u
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, recording) == 128u
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatistics, submission) == 208u,
    "EncodedFrameGraphPhysicalQueueRuntimeStatistics field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6) == 112u, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6) == 1u, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6>, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6>, "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, acceptedPacketCount) == 0u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, acceptedTaskCount) == 8u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, rejectedPacketCount) == 16u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, rejectedTaskCount) == 24u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, nativeSubmissionCount) == 32u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, rejectedSubmissionCount) == 40u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, nativeCommandListCount) == 48u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, plannedWaitTokenCount) == 56u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, sameQueueWaitElisionCount) == 64u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, timelineWaitCount) == 72u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, mergedTimelineWaitCount) == 80u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, acceptedFrontierSubmissionCount) == 88u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, submissionSeconds) == 96u
    && offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, recoverySubmissionCount) == 104u,
    "EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6 field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6) == 320u, "EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 wire layout drifted");
static_assert(alignof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6) == 1u, "EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6>, "EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6>, "EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, ownerNodeIndex) == 0u
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, queue) == 4u
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, queueClass) == 8u
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, reserved) == 9u
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, compile) == 16u
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, recording) == 128u
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, submission) == 208u
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, submission)
        + offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, submissionSeconds) == 304u
    && offsetof(EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6, submission)
        + offsetof(EncodedFrameGraphPhysicalQueueSubmissionRuntimeStatisticsV6, recoverySubmissionCount) == 312u,
    "EncodedFrameGraphPhysicalQueueRuntimeStatisticsV6 field order drifted"
);
static_assert(sizeof(EncodedFrameGraphPacketSubmissionStatistics) == 80u, "EncodedFrameGraphPacketSubmissionStatistics wire layout drifted");
static_assert(alignof(EncodedFrameGraphPacketSubmissionStatistics) == 1u, "EncodedFrameGraphPacketSubmissionStatistics must stay packed");
static_assert(IsStandardLayout_V<EncodedFrameGraphPacketSubmissionStatistics>, "EncodedFrameGraphPacketSubmissionStatistics must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedFrameGraphPacketSubmissionStatistics>, "EncodedFrameGraphPacketSubmissionStatistics must stay binary-serializable");
static_assert(
    offsetof(EncodedFrameGraphPacketSubmissionStatistics, ownerNodeIndex) == 0u
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, packetIndex) == 4u
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, packetGeneration) == 8u
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, queue) == 16u
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, queueClass) == 20u
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, joinsAcceptedQueueFrontier) == 21u
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, recoverySubmission) == 22u
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, reserved) == 23u
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, taskCount) == 24u
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, commandListCount) == 32u
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, plannedWaitTokenCount) == 40u
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, sameQueueWaitElisionCount) == 48u
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, timelineWaitCount) == 56u
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, mergedTimelineWaitCount) == 64u
    && offsetof(EncodedFrameGraphPacketSubmissionStatistics, submissionSeconds) == 72u,
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
    u16 wireVersion = 0u;
    u64 frameIndex = 0u;
    Vector<FrameGraphNodePayload, TelemetryArena> nodes;
    Vector<FrameGraphEdgePayload, TelemetryArena> edges;
    Vector<FrameGraphPhysicalQueueRuntimeStatisticsRecord, TelemetryArena> physicalQueueRuntimeStatistics;
    Vector<FrameGraphPacketSubmissionStatisticsRecord, TelemetryArena> packetSubmissionStatistics;
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

