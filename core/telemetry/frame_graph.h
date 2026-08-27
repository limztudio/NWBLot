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
inline constexpr u16 s_FrameGraphPayloadVersion = 4u;
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
// not inherit the host width of usize. The separately frozen v4 wire records below are packed and fixed-size, but use
// the telemetry codec's native byte order rather than defining a cross-endian interchange format. Durations are seconds.
struct FrameGraphCompileRuntimeStatistics{
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
    u16 version = s_FrameGraphPayloadVersion;
    u16 reserved = 0u;
    u64 frameIndex = 0u;
    u32 nodeCount = 0u;
    u32 edgeCount = 0u;
    u32 stringTableBytes = 0u;
    u32 queueAssignmentCount = 0u;
    u32 compiledTaskCount = 0u;
    u32 runtimeStatisticsCount = 0u;
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
    u64 frameIndex = 0u;
    Vector<FrameGraphNodePayload, TelemetryArena> nodes;
    Vector<FrameGraphEdgePayload, TelemetryArena> edges;

    explicit FrameGraphPayload(TelemetryArena& arena)
        : nodes(arena)
        , edges(arena)
    {}
};

using FrameGraphNodeDescs = Vector<FrameGraphNodeDesc, TelemetryArena>;
using FrameGraphEdgeDescs = Vector<FrameGraphEdgeDesc, TelemetryArena>;


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
[[nodiscard]] bool BuildFrameGraphPayload(
    TelemetryArena& arena,
    u64 frameIndex,
    const FrameGraphNodeDescs& nodes,
    const FrameGraphEdgeDescs& edges,
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

